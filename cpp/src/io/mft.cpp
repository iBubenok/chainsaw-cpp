// ==============================================================================
// mft.cpp - Implementation of MFT Parser (NTFS Master File Table)
// ==============================================================================
//
// MOD-0009 io::mft
// SLICE-018: MFT Parser Implementation
// SPEC-SLICE-018: micro-spec поведения
// ADR-0009: custom MFT parser
//
// NTFS MFT Format:
// - Entry header: 48 bytes (42 bytes standard + 6 bytes padding)
// - Attributes follow header
// - $STANDARD_INFORMATION (0x10): timestamps, flags
// - $FILE_NAME (0x30): name, parent reference, timestamps
// - $DATA (0x80): file content
//
// References:
// - https://flatcap.github.io/linux-ntfs/ntfs/concepts/file_record.html
// - https://github.com/libyal/libfsntfs/blob/main/documentation/
// - https://github.com/omerbenamram/mft (Rust reference implementation)
//
// ==============================================================================

#include <algorithm>
#include <chainsaw/mft.hpp>
#include <chainsaw/platform.hpp>
#include <chainsaw/reader.hpp>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace chainsaw::io::mft {

// ============================================================================
// MFT Format Constants
// ============================================================================

// Entry signatures
constexpr char FILE_SIGNATURE[4] = {'F', 'I', 'L', 'E'};
constexpr char BAAD_SIGNATURE[4] = {'B', 'A', 'A', 'D'};

// Standard entry size
constexpr std::uint32_t DEFAULT_ENTRY_SIZE = 1024;

// Attribute type codes (some reserved for future use)
constexpr std::uint32_t ATTR_STANDARD_INFORMATION = 0x10;
[[maybe_unused]] constexpr std::uint32_t ATTR_ATTRIBUTE_LIST = 0x20;
constexpr std::uint32_t ATTR_FILE_NAME = 0x30;
[[maybe_unused]] constexpr std::uint32_t ATTR_OBJECT_ID = 0x40;
[[maybe_unused]] constexpr std::uint32_t ATTR_SECURITY_DESCRIPTOR = 0x50;
[[maybe_unused]] constexpr std::uint32_t ATTR_VOLUME_NAME = 0x60;
[[maybe_unused]] constexpr std::uint32_t ATTR_VOLUME_INFORMATION = 0x70;
constexpr std::uint32_t ATTR_DATA = 0x80;
[[maybe_unused]] constexpr std::uint32_t ATTR_INDEX_ROOT = 0x90;
[[maybe_unused]] constexpr std::uint32_t ATTR_INDEX_ALLOCATION = 0xA0;
[[maybe_unused]] constexpr std::uint32_t ATTR_BITMAP = 0xB0;
[[maybe_unused]] constexpr std::uint32_t ATTR_REPARSE_POINT = 0xC0;
constexpr std::uint32_t ATTR_END = 0xFFFFFFFF;

// File name namespace (some reserved for future use)
[[maybe_unused]] constexpr std::uint8_t FILE_NAME_POSIX = 0;
constexpr std::uint8_t FILE_NAME_WIN32 = 1;
[[maybe_unused]] constexpr std::uint8_t FILE_NAME_DOS = 2;
constexpr std::uint8_t FILE_NAME_WIN32_DOS = 3;

// ============================================================================
// Helper Functions
// ============================================================================

namespace {

/// Read little-endian uint16
inline std::uint16_t read_u16_le(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}

/// Read little-endian uint32
inline std::uint32_t read_u32_le(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

/// Read little-endian uint64
inline std::uint64_t read_u64_le(const std::uint8_t* data) {
    return static_cast<std::uint64_t>(read_u32_le(data)) |
           (static_cast<std::uint64_t>(read_u32_le(data + 4)) << 32);
}

/// Convert FILETIME to time_point
/// FILETIME: 100-nanosecond intervals since January 1, 1601
std::chrono::system_clock::time_point filetime_to_timepoint(std::uint64_t filetime) {
    // FILETIME epoch: January 1, 1601
    // Unix epoch: January 1, 1970
    // Difference: 116444736000000000 * 100ns intervals
    constexpr std::uint64_t EPOCH_DIFF = 116444736000000000ULL;

    if (filetime < EPOCH_DIFF) {
        return std::chrono::system_clock::time_point{};
    }

    auto unix_100ns = filetime - EPOCH_DIFF;
    auto unix_seconds = unix_100ns / 10000000ULL;
    auto unix_micros = (unix_100ns % 10000000ULL) / 10;

    return std::chrono::system_clock::time_point{std::chrono::seconds{unix_seconds} +
                                                 std::chrono::microseconds{unix_micros}};
}

/// Convert UTF-16LE to UTF-8
std::string utf16le_to_utf8(const std::uint8_t* data, std::size_t byte_len) {
    if (byte_len == 0 || byte_len % 2 != 0) {
        return {};
    }

    std::string result;
    result.reserve(byte_len / 2);

    for (std::size_t i = 0; i < byte_len; i += 2) {
        std::uint16_t wchar = read_u16_le(data + i);

        if (wchar == 0) {
            break;  // Null terminator
        }

        if (wchar < 0x80) {
            // ASCII
            result.push_back(static_cast<char>(wchar));
        } else if (wchar < 0x800) {
            // 2-byte UTF-8
            result.push_back(static_cast<char>(0xC0 | (wchar >> 6)));
            result.push_back(static_cast<char>(0x80 | (wchar & 0x3F)));
        } else {
            // 3-byte UTF-8 (BMP)
            result.push_back(static_cast<char>(0xE0 | (wchar >> 12)));
            result.push_back(static_cast<char>(0x80 | ((wchar >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (wchar & 0x3F)));
        }
    }

    return result;
}

/// Apply fixup array to entry data
/// Returns true if fixup was successful
bool apply_fixup(std::uint8_t* entry_data, std::uint32_t entry_size, std::uint16_t fixup_offset,
                 std::uint16_t fixup_count) {
    if (fixup_count < 1 ||
        static_cast<std::uint32_t>(fixup_offset) + static_cast<std::uint32_t>(fixup_count) * 2 >
            entry_size) {
        return false;
    }

    // Get fixup signature and array
    const std::uint8_t* fixup_ptr = entry_data + fixup_offset;
    std::uint16_t fixup_signature = read_u16_le(fixup_ptr);
    fixup_ptr += 2;

    // Apply fixup to each sector (512 bytes)
    constexpr std::uint32_t SECTOR_SIZE = 512;
    for (std::uint16_t i = 1; i < fixup_count; ++i) {
        std::uint32_t sector_end = i * SECTOR_SIZE;
        if (sector_end > entry_size) {
            break;
        }

        // Check that sector ends with fixup signature
        std::uint16_t sector_sig = read_u16_le(entry_data + sector_end - 2);
        if (sector_sig != fixup_signature) {
            // Fixup mismatch - entry may be corrupted
            return false;
        }

        // Replace with original bytes
        std::uint16_t original = read_u16_le(fixup_ptr);
        entry_data[sector_end - 2] = static_cast<std::uint8_t>(original & 0xFF);
        entry_data[sector_end - 1] = static_cast<std::uint8_t>(original >> 8);

        fixup_ptr += 2;
    }

    return true;
}

}  // anonymous namespace

// ============================================================================
// MftTimestamp implementation
// ============================================================================

std::chrono::system_clock::time_point MftTimestamp::to_time_point() const {
    return filetime_to_timepoint(filetime);
}

std::string MftTimestamp::to_iso8601() const {
    if (filetime == 0) {
        return "";
    }

    auto tp = to_time_point();
    auto time = std::chrono::system_clock::to_time_t(tp);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&time));
    return std::string(buf);
}

// ============================================================================
// Flag conversion functions
// ============================================================================

std::string mft_entry_flags_to_string(std::uint16_t flags) {
    std::string result;

    if (flags & static_cast<std::uint16_t>(MftEntryFlags::Allocated)) {
        result = "ALLOCATED";
    }

    if (flags & static_cast<std::uint16_t>(MftEntryFlags::IndexPresent)) {
        if (!result.empty())
            result += " | ";
        result += "INDEX_PRESENT";
    }

    if (result.empty()) {
        result = "NONE";
    }

    return result;
}

std::string file_attribute_flags_to_string(std::uint32_t flags) {
    if (flags == 0) {
        return "NONE";
    }

    std::vector<std::string> parts;

    if (flags & static_cast<std::uint32_t>(FileAttributeFlags::ReadOnly))
        parts.push_back("FILE_ATTRIBUTE_READONLY");
    if (flags & static_cast<std::uint32_t>(FileAttributeFlags::Hidden))
        parts.push_back("FILE_ATTRIBUTE_HIDDEN");
    if (flags & static_cast<std::uint32_t>(FileAttributeFlags::System))
        parts.push_back("FILE_ATTRIBUTE_SYSTEM");
    if (flags & static_cast<std::uint32_t>(FileAttributeFlags::Archive))
        parts.push_back("FILE_ATTRIBUTE_ARCHIVE");
    if (flags & static_cast<std::uint32_t>(FileAttributeFlags::Device))
        parts.push_back("FILE_ATTRIBUTE_DEVICE");
    if (flags & static_cast<std::uint32_t>(FileAttributeFlags::Normal))
        parts.push_back("FILE_ATTRIBUTE_NORMAL");
    if (flags & static_cast<std::uint32_t>(FileAttributeFlags::Temporary))
        parts.push_back("FILE_ATTRIBUTE_TEMPORARY");
    if (flags & static_cast<std::uint32_t>(FileAttributeFlags::SparseFile))
        parts.push_back("FILE_ATTRIBUTE_SPARSE_FILE");
    if (flags & static_cast<std::uint32_t>(FileAttributeFlags::ReparsePoint))
        parts.push_back("FILE_ATTRIBUTE_REPARSE_POINT");
    if (flags & static_cast<std::uint32_t>(FileAttributeFlags::Compressed))
        parts.push_back("FILE_ATTRIBUTE_COMPRESSED");
    if (flags & static_cast<std::uint32_t>(FileAttributeFlags::Offline))
        parts.push_back("FILE_ATTRIBUTE_OFFLINE");
    if (flags & static_cast<std::uint32_t>(FileAttributeFlags::NotContentIndexed))
        parts.push_back("FILE_ATTRIBUTE_NOT_CONTENT_INDEXED");
    if (flags & static_cast<std::uint32_t>(FileAttributeFlags::Encrypted))
        parts.push_back("FILE_ATTRIBUTE_ENCRYPTED");
    if (flags & static_cast<std::uint32_t>(FileAttributeFlags::Directory))
        parts.push_back("FILE_ATTRIBUTE_DIRECTORY");

    if (parts.empty()) {
        return "NONE";
    }

    std::string result;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0)
            result += " | ";
        result += parts[i];
    }
    return result;
}

// ============================================================================
// MftError implementation
// ============================================================================

std::string MftError::format() const {
    return message;
}

// ============================================================================
// MftParser::Impl - Internal implementation
// ============================================================================

struct MftParser::Impl {
    // File data in memory
    std::vector<std::uint8_t> data;

    // Entry size (from first entry or default)
    std::uint32_t entry_size = DEFAULT_ENTRY_SIZE;

    // Number of entries
    std::size_t entry_count = 0;

    // File name cache for path reconstruction (entry_id -> name)
    std::map<std::uint64_t, std::string> name_cache;

    // Parent reference cache (entry_id -> parent_entry_id)
    std::map<std::uint64_t, std::uint64_t> parent_cache;

    /// Parse entry header
    bool parse_entry_header(const std::uint8_t* entry_data, MftEntry& entry) {
        // Check signature (offset 0, 4 bytes)
        if (std::memcmp(entry_data, FILE_SIGNATURE, 4) == 0) {
            entry.signature = "FILE";
        } else if (std::memcmp(entry_data, BAAD_SIGNATURE, 4) == 0) {
            entry.signature = "BAAD";
            return true;  // BAAD entries are valid but corrupted
        } else {
            entry.signature = "????";
            return false;
        }

        // Fixup offset/count читаются в parse_entry_at, здесь не нужны
        // std::uint16_t fixup_offset = read_u16_le(entry_data + 4);
        // std::uint16_t fixup_count = read_u16_le(entry_data + 6);

        // Sequence number (offset 16, 2 bytes)
        entry.sequence = read_u16_le(entry_data + 16);

        // Hard link count (offset 18, 2 bytes)
        entry.hard_link_count = read_u16_le(entry_data + 18);

        // First attribute offset (offset 20, 2 bytes)
        // std::uint16_t first_attr_offset = read_u16_le(entry_data + 20);

        // Flags (offset 22, 2 bytes)
        entry.flags = read_u16_le(entry_data + 22);

        // Used entry size (offset 24, 4 bytes)
        entry.used_entry_size = read_u32_le(entry_data + 24);

        // Total entry size (offset 28, 4 bytes)
        entry.total_entry_size = read_u32_le(entry_data + 28);

        // Base entry reference (offset 32, 8 bytes)
        // Lower 48 bits = entry ID, upper 16 bits = sequence
        std::uint64_t base_ref = read_u64_le(entry_data + 32);
        entry.base_entry_id = base_ref & 0x0000FFFFFFFFFFFFULL;
        entry.base_entry_sequence = static_cast<std::uint16_t>(base_ref >> 48);

        return true;
    }

    /// Parse $STANDARD_INFORMATION attribute
    void parse_standard_info(const std::uint8_t* attr_data, std::uint32_t content_size,
                             MftEntry& entry) {
        if (content_size < 48)
            return;

        // Created (offset 0, 8 bytes)
        entry.standard_info_created.filetime = read_u64_le(attr_data);

        // Last modified (offset 8, 8 bytes)
        entry.standard_info_last_modified.filetime = read_u64_le(attr_data + 8);

        // MFT modified (offset 16, 8 bytes)
        entry.standard_info_mft_modified.filetime = read_u64_le(attr_data + 16);

        // Last access (offset 24, 8 bytes)
        entry.standard_info_last_access.filetime = read_u64_le(attr_data + 24);

        // File attributes (offset 32, 4 bytes)
        entry.standard_info_flags = read_u32_le(attr_data + 32);
    }

    /// Parse $FILE_NAME attribute
    void parse_file_name(const std::uint8_t* attr_data, std::uint32_t content_size, MftEntry& entry,
                         bool& is_win32_name) {
        if (content_size < 66)
            return;

        // Parent directory reference (offset 0, 8 bytes)
        std::uint64_t parent_ref = read_u64_le(attr_data);
        std::uint64_t parent_entry_id = parent_ref & 0x0000FFFFFFFFFFFFULL;

        // Created (offset 8, 8 bytes)
        MftTimestamp created;
        created.filetime = read_u64_le(attr_data + 8);

        // Last modified (offset 16, 8 bytes)
        MftTimestamp modified;
        modified.filetime = read_u64_le(attr_data + 16);

        // MFT modified (offset 24, 8 bytes)
        MftTimestamp mft_modified;
        mft_modified.filetime = read_u64_le(attr_data + 24);

        // Last access (offset 32, 8 bytes)
        MftTimestamp accessed;
        accessed.filetime = read_u64_le(attr_data + 32);

        // Allocated size (offset 40, 8 bytes)
        std::uint64_t allocated_size = read_u64_le(attr_data + 40);

        // Real size (offset 48, 8 bytes)
        std::uint64_t real_size = read_u64_le(attr_data + 48);

        // File attributes (offset 56, 4 bytes)
        std::uint32_t file_flags = read_u32_le(attr_data + 56);

        // Name length in characters (offset 64, 1 byte)
        std::uint8_t name_len = attr_data[64];

        // Name namespace (offset 65, 1 byte)
        std::uint8_t name_space = attr_data[65];

        // Check if this is a Win32 name (preferred)
        // We prefer Win32 or Win32+DOS names over POSIX or DOS-only
        bool is_preferred = (name_space == FILE_NAME_WIN32 || name_space == FILE_NAME_WIN32_DOS);

        if (is_preferred || !is_win32_name) {
            // File name (offset 66, name_len * 2 bytes)
            std::size_t name_bytes = static_cast<std::size_t>(name_len) * 2;
            if (66 + name_bytes <= content_size) {
                entry.file_name = utf16le_to_utf8(attr_data + 66, name_bytes);
            }

            entry.parent_entry_id = parent_entry_id;
            entry.file_name_created = created;
            entry.file_name_last_modified = modified;
            entry.file_name_mft_modified = mft_modified;
            entry.file_name_last_access = accessed;
            entry.file_name_allocated_size = allocated_size;
            entry.file_name_real_size = real_size;
            entry.file_name_flags = file_flags;

            is_win32_name = is_preferred;
        }
    }

    /// Parse $DATA attribute
    void parse_data(const std::uint8_t* attr_header, std::uint32_t attr_length, MftEntry& entry,
                    bool decode_data, bool& found_unnamed_data) {
        if (attr_length < 24)
            return;

        // Non-resident flag (offset 8, 1 byte)
        std::uint8_t non_resident = attr_header[8];

        // Name length (offset 9, 1 byte)
        std::uint8_t name_length = attr_header[9];

        // Check if this is the unnamed (default) $DATA attribute
        bool is_unnamed = (name_length == 0);

        if (is_unnamed) {
            if (non_resident) {
                // Non-resident: size at offset 48 (allocated), 56 (real)
                if (attr_length >= 64) {
                    // Allocated size at offset 40
                    // Real size at offset 48
                    entry.file_size = read_u64_le(attr_header + 48);
                }
            } else {
                // Resident: content size at offset 16
                std::uint32_t content_size = read_u32_le(attr_header + 16);
                entry.file_size = content_size;

                // Content offset at offset 20
                std::uint16_t content_offset = read_u16_le(attr_header + 20);

                // Decode resident data if requested
                if (decode_data && content_size > 0 &&
                    content_offset + content_size <= attr_length) {
                    entry.resident_data = std::vector<std::uint8_t>(
                        attr_header + content_offset, attr_header + content_offset + content_size);
                }
            }
            found_unnamed_data = true;
        } else {
            // Named $DATA attribute = alternate data stream
            entry.has_alternate_data_streams = true;
        }
    }

    /// Parse all attributes in entry
    void parse_attributes(std::uint8_t* entry_data, std::uint32_t entry_size_local, MftEntry& entry,
                          bool decode_data) {
        // First attribute offset (offset 20, 2 bytes)
        // Используем size_t для безопасных сравнений с entry_size_local
        std::size_t attr_offset = read_u16_le(entry_data + 20);

        if (attr_offset < 24 || attr_offset >= entry_size_local) {
            return;
        }

        bool is_win32_name = false;
        bool found_unnamed_data = false;

        while (attr_offset + 4 <= entry_size_local) {
            const std::uint8_t* attr = entry_data + attr_offset;

            // Attribute type (offset 0, 4 bytes)
            std::uint32_t attr_type = read_u32_le(attr);

            // End marker
            if (attr_type == ATTR_END || attr_type == 0) {
                break;
            }

            // Attribute length (offset 4, 4 bytes)
            std::uint32_t attr_length = read_u32_le(attr + 4);

            if (attr_length < 16 || attr_offset + attr_length > entry_size_local) {
                break;  // Invalid attribute
            }

            // Non-resident flag (offset 8, 1 byte)
            std::uint8_t non_resident = attr[8];

            // Parse based on attribute type
            switch (attr_type) {
            case ATTR_STANDARD_INFORMATION: {
                if (!non_resident) {
                    std::uint32_t content_size = read_u32_le(attr + 16);
                    std::uint16_t content_offset = read_u16_le(attr + 20);
                    if (content_offset + content_size <= attr_length) {
                        parse_standard_info(attr + content_offset, content_size, entry);
                    }
                }
                break;
            }

            case ATTR_FILE_NAME: {
                if (!non_resident) {
                    std::uint32_t content_size = read_u32_le(attr + 16);
                    std::uint16_t content_offset = read_u16_le(attr + 20);
                    if (content_offset + content_size <= attr_length) {
                        parse_file_name(attr + content_offset, content_size, entry, is_win32_name);
                    }
                }
                break;
            }

            case ATTR_DATA: {
                parse_data(attr, attr_length, entry, decode_data, found_unnamed_data);
                break;
            }

            default:
                break;
            }

            attr_offset += attr_length;
        }
    }

    /// Parse a single MFT entry at given offset
    bool parse_entry_at(std::size_t offset, std::uint64_t entry_id, MftEntry& entry,
                        bool decode_data) {
        if (offset + entry_size > data.size()) {
            return false;
        }

        // Copy entry data for fixup application
        auto data_offset = static_cast<std::ptrdiff_t>(offset);
        auto data_end = static_cast<std::ptrdiff_t>(offset + entry_size);
        std::vector<std::uint8_t> entry_data(data.begin() + data_offset, data.begin() + data_end);

        entry.entry_id = entry_id;

        // Parse header
        if (!parse_entry_header(entry_data.data(), entry)) {
            return false;
        }

        if (entry.signature != "FILE") {
            return true;  // BAAD or invalid entries are technically parsed
        }

        // Apply fixup
        std::uint16_t fixup_offset = read_u16_le(entry_data.data() + 4);
        std::uint16_t fixup_count = read_u16_le(entry_data.data() + 6);
        apply_fixup(entry_data.data(), entry_size, fixup_offset, fixup_count);

        // Parse attributes
        parse_attributes(entry_data.data(), entry_size, entry, decode_data);

        // Cache name and parent for path reconstruction
        if (!entry.file_name.empty()) {
            name_cache[entry_id] = entry.file_name;
            parent_cache[entry_id] = entry.parent_entry_id;
        }

        return true;
    }
};

// ============================================================================
// MftParser implementation
// ============================================================================

MftParser::MftParser() : impl_(std::make_unique<Impl>()) {}

MftParser::~MftParser() = default;

MftParser::MftParser(MftParser&&) noexcept = default;
MftParser& MftParser::operator=(MftParser&&) noexcept = default;

bool MftParser::load(const std::filesystem::path& path, const Options& options) {
    path_ = path;
    options_ = options;
    loaded_ = false;
    error_.reset();

    // Check file exists
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        error_ =
            MftError{MftErrorKind::FileNotFound, "file not found: " + platform::path_to_utf8(path)};
        return false;
    }

    // Open and read file
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        error_ =
            MftError{MftErrorKind::IoError, "could not open file: " + platform::path_to_utf8(path)};
        return false;
    }

    auto size = file.tellg();
    if (size < static_cast<std::streampos>(DEFAULT_ENTRY_SIZE)) {
        error_ = MftError{MftErrorKind::InvalidSignature, "file too small for MFT entry"};
        return false;
    }

    file.seekg(0, std::ios::beg);
    impl_->data.resize(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(impl_->data.data()),
                   static_cast<std::streamsize>(size))) {
        error_ = MftError{MftErrorKind::IoError, "failed to read file"};
        return false;
    }

    // Check first entry signature
    if (std::memcmp(impl_->data.data(), FILE_SIGNATURE, 4) != 0 &&
        std::memcmp(impl_->data.data(), BAAD_SIGNATURE, 4) != 0) {
        error_ = MftError{MftErrorKind::InvalidSignature,
                          "invalid MFT signature (expected FILE or BAAD)"};
        return false;
    }

    // Detect entry size from first entry
    // Total entry size at offset 28
    if (impl_->data.size() >= 32) {
        impl_->entry_size = read_u32_le(impl_->data.data() + 28);
        if (impl_->entry_size < 512 || impl_->entry_size > 4096) {
            impl_->entry_size = DEFAULT_ENTRY_SIZE;
        }
    }

    // Calculate entry count
    impl_->entry_count = impl_->data.size() / impl_->entry_size;

    loaded_ = true;
    return true;
}

std::size_t MftParser::entry_count() const {
    return impl_ ? impl_->entry_count : 0;
}

std::uint32_t MftParser::entry_size() const {
    return impl_ ? impl_->entry_size : DEFAULT_ENTRY_SIZE;
}

std::optional<MftEntry> MftParser::get_entry(std::uint64_t entry_id) {
    if (!loaded_ || entry_id >= impl_->entry_count) {
        return std::nullopt;
    }

    MftEntry entry;
    std::size_t offset = static_cast<std::size_t>(entry_id) * impl_->entry_size;

    if (impl_->parse_entry_at(offset, entry_id, entry, options_.decode_data_streams)) {
        entry.full_path = reconstruct_path(entry);
        return entry;
    }

    return std::nullopt;
}

std::string MftParser::reconstruct_path(const MftEntry& entry) {
    if (entry.file_name.empty()) {
        return "";
    }

    // Special case: root directory (entry 5, parent points to self)
    if (entry.entry_id == 5 || entry.parent_entry_id == entry.entry_id) {
        return entry.file_name.empty() ? "." : entry.file_name;
    }

    // Build path by walking parent chain
    std::vector<std::string> parts;
    parts.push_back(entry.file_name);

    std::uint64_t current_parent = entry.parent_entry_id;
    constexpr int MAX_DEPTH = 256;  // Prevent infinite loops

    for (int depth = 0; depth < MAX_DEPTH && current_parent != 0; ++depth) {
        // Check if parent points to root (entry 5)
        if (current_parent == 5) {
            break;
        }

        // Look up parent in cache
        auto name_it = impl_->name_cache.find(current_parent);
        if (name_it != impl_->name_cache.end()) {
            parts.push_back(name_it->second);
        } else {
            // Try to parse parent entry
            if (current_parent < impl_->entry_count) {
                MftEntry parent_entry;
                std::size_t offset = static_cast<std::size_t>(current_parent) * impl_->entry_size;
                if (impl_->parse_entry_at(offset, current_parent, parent_entry, false)) {
                    if (!parent_entry.file_name.empty()) {
                        parts.push_back(parent_entry.file_name);
                    }
                }
            }
        }

        // Get next parent
        auto parent_it = impl_->parent_cache.find(current_parent);
        if (parent_it != impl_->parent_cache.end()) {
            if (parent_it->second == current_parent) {
                break;  // Self-reference, stop
            }
            current_parent = parent_it->second;
        } else {
            break;  // No parent info
        }
    }

    // Build path from parts (reverse order)
    std::string path;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!path.empty() && *it != ".") {
            path += "\\";
        }
        path += *it;
    }

    return path.empty() ? entry.file_name : path;
}

// ============================================================================
// MftParser::Iterator implementation
// ============================================================================

MftParser::Iterator::Iterator(MftParser* parser) : parser_(parser) {}

bool MftParser::Iterator::next(MftEntry& out) {
    if (!parser_ || !parser_->loaded_)
        return false;

    while (current_index_ < parser_->impl_->entry_count) {
        std::size_t offset = current_index_ * parser_->impl_->entry_size;
        std::uint64_t entry_id = current_index_;
        ++current_index_;

        if (parser_->impl_->parse_entry_at(offset, entry_id, out,
                                           parser_->options_.decode_data_streams)) {
            out.full_path = parser_->reconstruct_path(out);
            return true;
        }

        // Skip invalid entries if skip_errors is true
        if (!parser_->options_.skip_errors) {
            return false;
        }
    }

    return false;
}

bool MftParser::Iterator::has_next() const {
    if (!parser_ || !parser_->loaded_)
        return false;
    return current_index_ < parser_->impl_->entry_count;
}

MftParser::Iterator MftParser::iter() {
    return Iterator(this);
}

}  // namespace chainsaw::io::mft

// ============================================================================
// MftReader - Reader for Reader framework
// ============================================================================

namespace chainsaw::io {

class MftReader : public Reader {
public:
    explicit MftReader(std::filesystem::path path, bool decode_data_streams)
        : path_(std::move(path)), decode_data_streams_(decode_data_streams), iterator_(nullptr) {}

    bool load() {
        mft::MftParser::Options opts;
        opts.decode_data_streams = decode_data_streams_;
        opts.skip_errors = true;

        if (!parser_.load(path_, opts)) {
            const auto& err = parser_.last_error();
            error_ = ReaderError{ReaderErrorKind::ParseError, err ? err->message : "unknown error",
                                 platform::path_to_utf8(path_)};
            return false;
        }
        iterator_ = parser_.iter();
        loaded_ = true;
        return true;
    }

    bool next(Document& out) override {
        if (!loaded_)
            return false;

        mft::MftEntry entry;
        if (!iterator_.next(entry)) {
            return false;
        }

        // Convert MftEntry to Value (JSON-like)
        Value::Object obj;

        obj["Signature"] = Value(entry.signature);
        obj["EntryId"] = Value(static_cast<std::int64_t>(entry.entry_id));
        obj["Sequence"] = Value(static_cast<std::int64_t>(entry.sequence));
        obj["BaseEntryId"] = Value(static_cast<std::int64_t>(entry.base_entry_id));
        obj["BaseEntrySequence"] = Value(static_cast<std::int64_t>(entry.base_entry_sequence));
        obj["HardLinkCount"] = Value(static_cast<std::int64_t>(entry.hard_link_count));
        obj["Flags"] = Value(mft::mft_entry_flags_to_string(entry.flags));
        obj["UsedEntrySize"] = Value(static_cast<std::int64_t>(entry.used_entry_size));
        obj["TotalEntrySize"] = Value(static_cast<std::int64_t>(entry.total_entry_size));
        obj["FileSize"] = Value(static_cast<std::int64_t>(entry.file_size));
        obj["IsADirectory"] = Value(entry.is_directory());
        obj["IsDeleted"] = Value(entry.is_deleted());
        obj["HasAlternateDataStreams"] = Value(entry.has_alternate_data_streams);

        // $STANDARD_INFORMATION timestamps
        obj["StandardInfoFlags"] =
            Value(mft::file_attribute_flags_to_string(entry.standard_info_flags));
        obj["StandardInfoLastModified"] = Value(entry.standard_info_last_modified.to_iso8601());
        obj["StandardInfoLastAccess"] = Value(entry.standard_info_last_access.to_iso8601());
        obj["StandardInfoCreated"] = Value(entry.standard_info_created.to_iso8601());

        // $FILE_NAME timestamps
        obj["FileNameFlags"] = Value(mft::file_attribute_flags_to_string(entry.file_name_flags));
        obj["FileNameLastModified"] = Value(entry.file_name_last_modified.to_iso8601());
        obj["FileNameLastAccess"] = Value(entry.file_name_last_access.to_iso8601());
        obj["FileNameCreated"] = Value(entry.file_name_created.to_iso8601());

        // Full path
        obj["FullPath"] = Value(entry.full_path);

        // Resident data (if decoded)
        if (entry.resident_data.has_value()) {
            // Convert to base64 or hex string
            std::string hex;
            hex.reserve(entry.resident_data->size() * 2);
            for (auto b : *entry.resident_data) {
                static const char digits[] = "0123456789abcdef";
                hex.push_back(digits[(b >> 4) & 0xF]);
                hex.push_back(digits[b & 0xF]);
            }
            obj["ResidentData"] = Value(hex);
        }

        out.kind = DocumentKind::Mft;
        out.data = Value(std::move(obj));
        out.source = platform::path_to_utf8(path_);
        out.record_id = entry.entry_id;

        return true;
    }

    bool has_next() const override { return loaded_ && iterator_.has_next(); }

    DocumentKind kind() const override { return DocumentKind::Mft; }
    const std::filesystem::path& path() const override { return path_; }
    const std::optional<ReaderError>& last_error() const override { return error_; }

private:
    std::filesystem::path path_;
    bool decode_data_streams_;
    mft::MftParser parser_;
    mft::MftParser::Iterator iterator_;
    std::optional<ReaderError> error_;
    bool loaded_ = false;
};

}  // namespace chainsaw::io

namespace chainsaw::io::mft {

std::unique_ptr<chainsaw::io::Reader> create_mft_reader(const std::filesystem::path& path,
                                                        bool skip_errors,
                                                        bool decode_data_streams) {
    auto reader = std::make_unique<chainsaw::io::MftReader>(path, decode_data_streams);
    if (!reader->load()) {
        if (skip_errors) {
            return chainsaw::io::create_empty_reader(path, chainsaw::io::DocumentKind::Mft);
        }
    }
    return reader;
}

}  // namespace chainsaw::io::mft
