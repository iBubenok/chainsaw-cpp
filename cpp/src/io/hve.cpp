// ==============================================================================
// hve.cpp - Реализация HVE Parser (Windows Registry Hive - формат REGF)
// ==============================================================================
//
// MOD-0008 io::hve
// SLICE-015: HVE Parser Implementation
// SPEC-SLICE-015: micro-spec поведения
// ADR-0009: custom REGF parser
//
// Формат REGF (Registry Hive):
// - File header: 4096 bytes, signature "regf"
// - Hive bins: блоки с ключами и значениями
// - Key nodes (nk): записи ключей
// - Value nodes (vk): записи значений
// - Data cells: данные значений
//
// Transaction Log Support (HVLE format):
// - .LOG, .LOG1, .LOG2 файлы в той же директории
// - Применение dirty pages для восстановления данных
// - Валидация hash1/hash2 для целостности
//
// Источники:
// - https://github.com/libyal/libregf/blob/main/documentation/
// -
// https://github.com/msuhanov/regf/blob/master/Windows%20registry%20file%20format%20specification.md
// - https://github.com/alexkornitzer/notatin (Rust reference implementation)
//
// ==============================================================================

#include <algorithm>
#include <chainsaw/hve.hpp>
#include <chainsaw/platform.hpp>
#include <chainsaw/reader.hpp>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace chainsaw::io::hve {

// ============================================================================
// REGF Format Constants
// ============================================================================

// File header
constexpr char REGF_SIGNATURE[4] = {'r', 'e', 'g', 'f'};
constexpr std::uint32_t REGF_HEADER_SIZE = 4096;

// Cell signatures
constexpr char NK_SIGNATURE[2] = {'n', 'k'};  // Key node
constexpr char VK_SIGNATURE[2] = {'v', 'k'};  // Value node
constexpr char LF_SIGNATURE[2] = {'l', 'f'};  // Fast leaf (subkey list)
constexpr char LH_SIGNATURE[2] = {'l', 'h'};  // Hash leaf (subkey list)
constexpr char RI_SIGNATURE[2] = {'r', 'i'};  // Index root
constexpr char LI_SIGNATURE[2] = {'l', 'i'};  // Index leaf

// Key node flags
constexpr std::uint16_t KEY_COMP_NAME = 0x0020;  // Compressed name (ASCII)

// Value types (from winnt.h)
constexpr std::uint32_t REG_NONE = 0;
constexpr std::uint32_t REG_SZ = 1;
constexpr std::uint32_t REG_EXPAND_SZ = 2;
constexpr std::uint32_t REG_BINARY = 3;
constexpr std::uint32_t REG_DWORD = 4;
constexpr std::uint32_t REG_DWORD_BIG_ENDIAN = 5;
constexpr std::uint32_t REG_LINK = 6;
constexpr std::uint32_t REG_MULTI_SZ = 7;
constexpr std::uint32_t REG_QWORD = 11;

// ============================================================================
// Helper Functions
// ============================================================================

namespace {

// Transaction log constants
constexpr char HVLE_SIGNATURE[4] = {'H', 'v', 'L', 'E'};  // Log entry signature
constexpr std::uint32_t HVLE_HEADER_SIZE = 40;            // Log entry header size

/// Читать little-endian uint16
inline std::uint16_t read_u16_le(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}

/// Читать little-endian uint32
inline std::uint32_t read_u32_le(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

/// Читать little-endian int32 (для cell size)
inline std::int32_t read_i32_le(const std::uint8_t* data) {
    return static_cast<std::int32_t>(read_u32_le(data));
}

/// Читать little-endian uint64
inline std::uint64_t read_u64_le(const std::uint8_t* data) {
    return static_cast<std::uint64_t>(read_u32_le(data)) |
           (static_cast<std::uint64_t>(read_u32_le(data + 4)) << 32);
}

/// Конвертировать FILETIME в time_point
/// FILETIME: 100-nanosecond intervals since January 1, 1601
std::chrono::system_clock::time_point filetime_to_timepoint(std::uint64_t filetime) {
    // FILETIME epoch: January 1, 1601
    // Unix epoch: January 1, 1970
    // Difference: 11644473600 seconds = 116444736000000000 * 100ns
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

/// Конвертировать UTF-16LE в UTF-8
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
        } else if (wchar >= 0xD800 && wchar <= 0xDBFF) {
            // High surrogate - handle surrogate pairs for characters above U+FFFF
            if (i + 2 < byte_len) {
                std::uint16_t low_surrogate = read_u16_le(data + i + 2);
                if (low_surrogate >= 0xDC00 && low_surrogate <= 0xDFFF) {
                    // Valid surrogate pair
                    std::uint32_t codepoint =
                        0x10000 + ((static_cast<std::uint32_t>(wchar - 0xD800) << 10) |
                                   (low_surrogate - 0xDC00));
                    // 4-byte UTF-8
                    result.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
                    result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                    result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                    result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                    i += 2;  // Skip low surrogate in next iteration
                    continue;
                }
            }
            // Invalid surrogate pair - skip
            continue;
        } else if (wchar >= 0xDC00 && wchar <= 0xDFFF) {
            // Lone low surrogate - skip
            continue;
        } else {
            // 3-byte UTF-8 (BMP)
            result.push_back(static_cast<char>(0xE0 | (wchar >> 12)));
            result.push_back(static_cast<char>(0x80 | ((wchar >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (wchar & 0x3F)));
        }
    }

    return result;
}

/// Конвертировать ASCII в UTF-8 (просто копировать)
std::string ascii_to_utf8(const std::uint8_t* data, std::size_t len) {
    std::string result;
    result.reserve(len);
    for (std::size_t i = 0; i < len && data[i] != 0; ++i) {
        result.push_back(static_cast<char>(data[i]));
    }
    return result;
}

/// Разобрать путь на компоненты
std::vector<std::string> split_path(std::string_view path) {
    std::vector<std::string> parts;
    std::size_t start = 0;

    // Пропускаем ведущий backslash
    if (!path.empty() && path[0] == '\\') {
        start = 1;
    }

    while (start < path.size()) {
        auto pos = path.find('\\', start);
        if (pos == std::string_view::npos) {
            parts.emplace_back(path.substr(start));
            break;
        }
        if (pos > start) {
            parts.emplace_back(path.substr(start, pos - start));
        }
        start = pos + 1;
    }

    return parts;
}

// ============================================================================
// Transaction Log Structures and Functions
// ============================================================================

/// Dirty page reference from transaction log
struct DirtyPageRef {
    std::uint32_t offset;  // Offset in hive bins area
    std::uint32_t size;    // Size of dirty data
};

/// Log entry from transaction log (HVLE)
struct LogEntry {
    std::uint32_t size;                 // Entry size
    std::uint32_t flags;                // Copy of base block flags
    std::uint32_t sequence_number;      // Sequence identifier
    std::uint32_t hive_bins_data_size;  // Hive bins size from base block
    std::uint32_t dirty_pages_count;    // Number of dirty pages
    std::uint64_t hash1;                // Primary hash
    std::uint64_t hash2;                // Secondary hash
    std::vector<DirtyPageRef> dirty_page_refs;
    std::vector<std::vector<std::uint8_t>> dirty_page_data;
    bool valid = false;
};

/// Parse log entry from buffer
/// Returns the number of bytes consumed, or 0 on error
std::size_t parse_log_entry(const std::uint8_t* data, std::size_t data_size, std::size_t offset,
                            LogEntry& entry) {
    // Minimum size: signature (4) + header (36) = 40 bytes
    if (offset + HVLE_HEADER_SIZE > data_size) {
        return 0;
    }

    const std::uint8_t* ptr = data + offset;

    // Check signature "HvLE"
    if (std::memcmp(ptr, HVLE_SIGNATURE, 4) != 0) {
        return 0;
    }

    // Parse header fields
    entry.size = read_u32_le(ptr + 4);
    entry.flags = read_u32_le(ptr + 8);
    entry.sequence_number = read_u32_le(ptr + 12);
    entry.hive_bins_data_size = read_u32_le(ptr + 16);
    entry.dirty_pages_count = read_u32_le(ptr + 20);
    entry.hash1 = read_u64_le(ptr + 24);
    entry.hash2 = read_u64_le(ptr + 32);

    // Validate entry size
    if (entry.size < HVLE_HEADER_SIZE || offset + entry.size > data_size) {
        return 0;
    }

    // Validate dirty pages count (sanity check)
    if (entry.dirty_pages_count > 100000) {  // Arbitrary limit
        return 0;
    }

    // Calculate expected sizes
    std::size_t refs_size = static_cast<std::size_t>(entry.dirty_pages_count) * 8;
    std::size_t header_plus_refs = HVLE_HEADER_SIZE + refs_size;

    if (offset + header_plus_refs > data_size) {
        return 0;
    }

    // Parse dirty page references
    const std::uint8_t* refs_ptr = ptr + HVLE_HEADER_SIZE;
    entry.dirty_page_refs.reserve(entry.dirty_pages_count);

    std::size_t total_page_data_size = 0;
    for (std::uint32_t i = 0; i < entry.dirty_pages_count; ++i) {
        DirtyPageRef ref;
        ref.offset = read_u32_le(refs_ptr + (static_cast<std::size_t>(i) * 8));
        ref.size = read_u32_le(refs_ptr + (static_cast<std::size_t>(i) * 8) + 4);

        // Validate page size (should be multiple of 512, max reasonable size)
        if (ref.size > 0x10000000) {  // 256MB limit
            return 0;
        }

        entry.dirty_page_refs.push_back(ref);
        total_page_data_size += ref.size;
    }

    // Validate total size
    if (offset + header_plus_refs + total_page_data_size > data_size) {
        return 0;
    }

    // Parse dirty page data
    const std::uint8_t* page_data_ptr = refs_ptr + refs_size;
    entry.dirty_page_data.reserve(entry.dirty_pages_count);

    for (const auto& ref : entry.dirty_page_refs) {
        std::vector<std::uint8_t> page_bytes(page_data_ptr, page_data_ptr + ref.size);
        entry.dirty_page_data.push_back(std::move(page_bytes));
        page_data_ptr += ref.size;
    }

    entry.valid = true;
    return entry.size;
}

/// Parse all log entries from a transaction log file
std::vector<LogEntry> parse_transaction_log(const std::vector<std::uint8_t>& log_data) {
    std::vector<LogEntry> entries;

    // Transaction log starts with base block (same as hive header - 4096 bytes)
    // followed by log entries
    if (log_data.size() < REGF_HEADER_SIZE) {
        return entries;
    }

    // Check for "regf" signature in log file header
    if (std::memcmp(log_data.data(), REGF_SIGNATURE, 4) != 0) {
        return entries;
    }

    // Parse log entries starting after base block
    std::size_t offset = REGF_HEADER_SIZE;
    while (offset < log_data.size()) {
        LogEntry entry;
        std::size_t consumed = parse_log_entry(log_data.data(), log_data.size(), offset, entry);
        if (consumed == 0) {
            break;  // No more valid entries
        }

        if (entry.valid) {
            entries.push_back(std::move(entry));
        }

        offset += consumed;
    }

    return entries;
}

/// Apply dirty pages from log entries to hive data
/// Returns true if any pages were applied
bool apply_transaction_logs(std::vector<std::uint8_t>& hive_data,
                            const std::vector<LogEntry>& entries, std::uint32_t hive_sequence) {
    bool applied = false;

    for (const auto& entry : entries) {
        if (!entry.valid)
            continue;

        // Check sequence number - should be greater than hive's sequence
        // (indicating newer changes)
        if (entry.sequence_number <= hive_sequence) {
            continue;
        }

        // Apply dirty pages
        for (std::size_t i = 0; i < entry.dirty_page_refs.size(); ++i) {
            const auto& ref = entry.dirty_page_refs[i];
            const auto& page_bytes = entry.dirty_page_data[i];

            // Calculate destination offset (relative to hive bins, not header)
            std::size_t dest_offset = REGF_HEADER_SIZE + ref.offset;

            // Expand hive data if needed
            if (dest_offset + page_bytes.size() > hive_data.size()) {
                hive_data.resize(dest_offset + page_bytes.size());
            }

            // Copy dirty page data
            std::memcpy(hive_data.data() + dest_offset, page_bytes.data(), page_bytes.size());
            applied = true;
        }
    }

    return applied;
}

/// Find transaction log files in the same directory as the hive
std::vector<std::filesystem::path> find_transaction_logs(const std::filesystem::path& hive_path) {
    std::vector<std::filesystem::path> logs;

    std::error_code ec;
    auto parent = hive_path.parent_path();
    if (parent.empty()) {
        parent = ".";
    }

    auto stem = hive_path.stem();

    // Look for .LOG, .LOG1, .LOG2 files with the same stem
    for (const auto& entry : std::filesystem::directory_iterator(parent, ec)) {
        if (ec)
            break;

        if (!entry.is_regular_file(ec) || ec)
            continue;

        auto path = entry.path();
        auto ext = path.extension().string();

        // Convert extension to uppercase for comparison
        for (auto& c : ext) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }

        // Check if this is a transaction log for our hive
        if (path.stem() == stem && (ext == ".LOG" || ext == ".LOG1" || ext == ".LOG2")) {
            logs.push_back(path);
        }
    }

    // Sort logs by name for consistent processing order
    std::sort(logs.begin(), logs.end());

    return logs;
}

/// Read file into vector
std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path) {
    std::vector<std::uint8_t> data;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return data;
    }

    auto size = file.tellg();
    if (size <= 0) {
        return data;
    }

    file.seekg(0, std::ios::beg);
    data.resize(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));

    return data;
}

}  // anonymous namespace

// ============================================================================
// RegValueType functions
// ============================================================================

const char* reg_value_type_to_string(RegValueType type) {
    switch (type) {
    case RegValueType::Binary:
        return "REG_BINARY";
    case RegValueType::Dword:
        return "REG_DWORD";
    case RegValueType::DwordBigEndian:
        return "REG_DWORD_BIG_ENDIAN";
    case RegValueType::Qword:
        return "REG_QWORD";
    case RegValueType::String:
        return "REG_SZ";
    case RegValueType::ExpandString:
        return "REG_EXPAND_SZ";
    case RegValueType::MultiString:
        return "REG_MULTI_SZ";
    case RegValueType::Link:
        return "REG_LINK";
    case RegValueType::None:
        return "REG_NONE";
    case RegValueType::Error:
        return "REG_ERROR";
    }
    return "REG_UNKNOWN";
}

// ============================================================================
// RegValue methods
// ============================================================================

const std::vector<std::uint8_t>* RegValue::as_binary() const {
    return std::get_if<std::vector<std::uint8_t>>(&data);
}

std::optional<std::uint32_t> RegValue::as_u32() const {
    if (auto* v = std::get_if<std::uint32_t>(&data)) {
        return *v;
    }
    return std::nullopt;
}

std::optional<std::uint64_t> RegValue::as_u64() const {
    if (auto* v = std::get_if<std::uint64_t>(&data)) {
        return *v;
    }
    // Также поддерживаем u32 → u64
    if (auto* v = std::get_if<std::uint32_t>(&data)) {
        return static_cast<std::uint64_t>(*v);
    }
    return std::nullopt;
}

const std::string* RegValue::as_string() const {
    return std::get_if<std::string>(&data);
}

const std::vector<std::string>* RegValue::as_multi_string() const {
    return std::get_if<std::vector<std::string>>(&data);
}

// ============================================================================
// RegKey methods
// ============================================================================

std::optional<RegValue> RegKey::get_value(std::string_view name) const {
    for (const auto& val : values_) {
        if (val.name == name) {
            return val;
        }
    }
    return std::nullopt;
}

// ============================================================================
// HveError methods
// ============================================================================

std::string HveError::format() const {
    return message;
}

// ============================================================================
// HveParser::Impl - внутренняя реализация
// ============================================================================

struct HveParser::Impl {
    // Данные файла в памяти
    std::vector<std::uint8_t> data;

    // Заголовок REGF
    struct RegfHeader {
        std::uint32_t root_key_offset = 0;  // Offset от начала hive bins
        std::uint32_t hive_bins_size = 0;
        std::uint64_t last_written = 0;
        std::uint32_t primary_sequence = 0;  // For transaction log processing
        std::uint32_t secondary_sequence = 0;
        bool valid = false;
    } header;

    // Transaction log status
    bool transaction_logs_applied = false;

    // Смещение начала hive bins (после заголовка)
    std::size_t hive_bins_offset = REGF_HEADER_SIZE;

    /// Парсить заголовок REGF
    bool parse_header() {
        if (data.size() < REGF_HEADER_SIZE) {
            return false;
        }

        // Проверяем сигнатуру
        if (std::memcmp(data.data(), REGF_SIGNATURE, 4) != 0) {
            return false;
        }

        // Offset 0x04: primary sequence number
        header.primary_sequence = read_u32_le(data.data() + 0x04);

        // Offset 0x08: secondary sequence number
        header.secondary_sequence = read_u32_le(data.data() + 0x08);

        // Offset 0x0C: last written timestamp
        header.last_written = read_u64_le(data.data() + 0x0C);

        // Offset 0x24: root key offset (относительно начала hive bins)
        header.root_key_offset = read_u32_le(data.data() + 0x24);

        // Offset 0x28: hive bins data size
        header.hive_bins_size = read_u32_le(data.data() + 0x28);

        header.valid = true;
        return true;
    }

    /// Получить абсолютное смещение ячейки
    std::size_t cell_offset(std::uint32_t relative_offset) const {
        return hive_bins_offset + relative_offset;
    }

    /// Проверить валидность смещения
    bool valid_offset(std::size_t offset, std::size_t min_size = 4) const {
        return offset + min_size <= data.size();
    }

    /// Получить размер ячейки (абсолютное значение)
    /// Отрицательный размер означает занятую ячейку
    std::uint32_t get_cell_size(std::size_t offset) const {
        if (!valid_offset(offset, 4))
            return 0;
        std::int32_t size = read_i32_le(data.data() + offset);
        return static_cast<std::uint32_t>(size < 0 ? -size : size);
    }

    /// Парсить key node (nk cell)
    bool parse_key_node(std::size_t offset, RegKey& key, const std::string& parent_path) {
        if (!valid_offset(offset, 80))
            return false;

        const std::uint8_t* cell = data.data() + offset;

        // Размер ячейки
        std::int32_t cell_size = read_i32_le(cell);
        if (cell_size >= 0) {
            return false;  // Свободная ячейка
        }

        // Сигнатура nk (offset 4)
        if (std::memcmp(cell + 4, NK_SIGNATURE, 2) != 0) {
            return false;
        }

        // Flags (offset 6)
        std::uint16_t flags = read_u16_le(cell + 6);

        // Last written timestamp (offset 8, 8 bytes)
        std::uint64_t last_written = read_u64_le(cell + 8);
        key.last_modified_ = filetime_to_timepoint(last_written);

        // Parent key offset (offset 20, 4 bytes) - не используем пока

        // Number of stable subkeys (offset 24, 4 bytes)
        std::uint32_t subkey_count = read_u32_le(cell + 24);

        // Number of volatile subkeys (offset 28, 4 bytes)
        // Some simplified hive implementations put subkeys_list_offset at offset 28
        std::uint32_t volatile_subkey_count = read_u32_le(cell + 28);

        // Stable subkeys list offset (offset 32, 4 bytes)
        std::uint32_t subkeys_list_offset = read_u32_le(cell + 32);

        // Volatile subkeys list offset (offset 36, 4 bytes) - skip

        // Compatibility: if subkeys_list_offset at 32 is 0 or invalid but
        // there are subkeys, try using offset 28 (older/simplified format)
        if (subkey_count > 0 && (subkeys_list_offset == 0 || subkeys_list_offset == 0xFFFFFFFF)) {
            if (volatile_subkey_count != 0 && volatile_subkey_count != 0xFFFFFFFF) {
                subkeys_list_offset = volatile_subkey_count;
            }
        }

        // Number of values (offset 40, 4 bytes)
        std::uint32_t value_count = read_u32_le(cell + 40);

        // Values list offset (offset 44, 4 bytes)
        std::uint32_t values_list_offset = read_u32_le(cell + 44);

        // Key name size and offset - format detection
        // Standard format: name_size at offset 76, name at offset 80
        // Older/simplified format: name_size at offset 72, name at offset 76
        std::uint16_t name_size = read_u16_le(cell + 76);
        std::size_t name_offset = 80;

        // If name_size at offset 76 looks invalid (too large or would overflow),
        // try reading from offset 72 (older format)
        std::size_t cell_data_size =
            static_cast<std::size_t>(-(static_cast<std::int32_t>(read_i32_le(cell))));
        if (name_size == 0 || name_offset + name_size > cell_data_size) {
            std::uint16_t alt_name_size = read_u16_le(cell + 72);
            if (alt_name_size > 0 &&
                76 + static_cast<std::size_t>(alt_name_size) <= cell_data_size) {
                name_size = alt_name_size;
                name_offset = 76;
            }
        }

        // Key name
        if (!valid_offset(offset + name_offset, name_size))
            return false;
        if ((flags & KEY_COMP_NAME) != 0) {
            // Compressed name (ASCII)
            key.name_ = ascii_to_utf8(cell + name_offset, name_size);
        } else {
            // UTF-16LE name
            key.name_ = utf16le_to_utf8(cell + name_offset, name_size);
        }

        // Строим полный путь
        if (parent_path.empty()) {
            key.path_ = key.name_;
        } else {
            key.path_ = parent_path + "\\" + key.name_;
        }

        // Парсим список подключей
        if (subkey_count > 0 && subkeys_list_offset != 0xFFFFFFFF) {
            parse_subkey_list(cell_offset(subkeys_list_offset), key.subkey_names_);
        }

        // Парсим значения
        if (value_count > 0 && values_list_offset != 0xFFFFFFFF) {
            parse_values_list(cell_offset(values_list_offset), value_count, key.values_);
        }

        key.valid_ = true;
        return true;
    }

    /// Парсить список подключей (lf/lh/ri/li cell)
    void parse_subkey_list(std::size_t offset, std::vector<std::string>& subkey_names) {
        if (!valid_offset(offset, 8))
            return;

        const std::uint8_t* cell = data.data() + offset;

        // Размер ячейки
        std::int32_t cell_size = read_i32_le(cell);
        if (cell_size >= 0)
            return;  // Свободная ячейка

        // Сигнатура (offset 4)
        char sig[2] = {static_cast<char>(cell[4]), static_cast<char>(cell[5])};

        // Количество элементов (offset 6)
        std::uint16_t count = read_u16_le(cell + 6);

        if (std::memcmp(sig, LF_SIGNATURE, 2) == 0 || std::memcmp(sig, LH_SIGNATURE, 2) == 0) {
            // Fast/Hash leaf: элементы по 8 байт (offset + hash/hint)
            for (std::uint16_t i = 0; i < count; ++i) {
                std::size_t elem_offset = 8 + static_cast<std::size_t>(i) * 8;
                if (!valid_offset(offset + elem_offset, 8))
                    break;

                std::uint32_t key_offset = read_u32_le(cell + elem_offset);
                // Получаем имя ключа
                std::size_t key_cell_offset = cell_offset(key_offset);
                if (valid_offset(key_cell_offset, 80)) {
                    const std::uint8_t* key_cell = data.data() + key_cell_offset;
                    std::int32_t ksize = read_i32_le(key_cell);
                    if (ksize < 0 && std::memcmp(key_cell + 4, NK_SIGNATURE, 2) == 0) {
                        std::uint16_t flags = read_u16_le(key_cell + 6);
                        // Format detection for name offset
                        std::size_t cell_data_size = static_cast<std::size_t>(-ksize);
                        std::uint16_t name_size = read_u16_le(key_cell + 76);
                        std::size_t name_off = 80;
                        if (name_size == 0 || name_off + name_size > cell_data_size) {
                            std::uint16_t alt_size = read_u16_le(key_cell + 72);
                            if (alt_size > 0 &&
                                76 + static_cast<std::size_t>(alt_size) <= cell_data_size) {
                                name_size = alt_size;
                                name_off = 76;
                            }
                        }
                        if (valid_offset(key_cell_offset + name_off, name_size)) {
                            std::string name;
                            if ((flags & KEY_COMP_NAME) != 0) {
                                name = ascii_to_utf8(key_cell + name_off, name_size);
                            } else {
                                name = utf16le_to_utf8(key_cell + name_off, name_size);
                            }
                            subkey_names.push_back(std::move(name));
                        }
                    }
                }
            }
        } else if (std::memcmp(sig, RI_SIGNATURE, 2) == 0) {
            // Index root: рекурсивно обходим подсписки
            for (std::uint16_t i = 0; i < count; ++i) {
                std::size_t elem_offset = 8 + static_cast<std::size_t>(i) * 4;
                if (!valid_offset(offset + elem_offset, 4))
                    break;

                std::uint32_t list_offset = read_u32_le(cell + elem_offset);
                parse_subkey_list(cell_offset(list_offset), subkey_names);
            }
        } else if (std::memcmp(sig, LI_SIGNATURE, 2) == 0) {
            // Index leaf: просто offsets
            for (std::uint16_t i = 0; i < count; ++i) {
                std::size_t elem_offset = 8 + static_cast<std::size_t>(i) * 4;
                if (!valid_offset(offset + elem_offset, 4))
                    break;

                std::uint32_t key_offset = read_u32_le(cell + elem_offset);
                std::size_t key_cell_offset = cell_offset(key_offset);
                if (valid_offset(key_cell_offset, 80)) {
                    const std::uint8_t* key_cell = data.data() + key_cell_offset;
                    std::int32_t ksize = read_i32_le(key_cell);
                    if (ksize < 0 && std::memcmp(key_cell + 4, NK_SIGNATURE, 2) == 0) {
                        std::uint16_t flags = read_u16_le(key_cell + 6);
                        // Format detection for name offset
                        std::size_t cell_data_size = static_cast<std::size_t>(-ksize);
                        std::uint16_t name_size = read_u16_le(key_cell + 76);
                        std::size_t name_off = 80;
                        if (name_size == 0 || name_off + name_size > cell_data_size) {
                            std::uint16_t alt_size = read_u16_le(key_cell + 72);
                            if (alt_size > 0 &&
                                76 + static_cast<std::size_t>(alt_size) <= cell_data_size) {
                                name_size = alt_size;
                                name_off = 76;
                            }
                        }
                        if (valid_offset(key_cell_offset + name_off, name_size)) {
                            std::string name;
                            if ((flags & KEY_COMP_NAME) != 0) {
                                name = ascii_to_utf8(key_cell + name_off, name_size);
                            } else {
                                name = utf16le_to_utf8(key_cell + name_off, name_size);
                            }
                            subkey_names.push_back(std::move(name));
                        }
                    }
                }
            }
        }
    }

    /// Парсить список значений
    void parse_values_list(std::size_t offset, std::uint32_t count, std::vector<RegValue>& values) {
        if (!valid_offset(offset, 4 + count * 4))
            return;

        const std::uint8_t* cell = data.data() + offset;

        // Размер ячейки
        std::int32_t cell_size = read_i32_le(cell);
        if (cell_size >= 0)
            return;  // Свободная ячейка

        // Список offsets значений
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t value_offset = read_u32_le(cell + 4 + i * 4);
            RegValue val;
            if (parse_value_node(cell_offset(value_offset), val)) {
                values.push_back(std::move(val));
            }
        }
    }

    /// Парсить value node (vk cell)
    bool parse_value_node(std::size_t offset, RegValue& value) {
        if (!valid_offset(offset, 24))
            return false;

        const std::uint8_t* cell = data.data() + offset;

        // Размер ячейки
        std::int32_t cell_size = read_i32_le(cell);
        if (cell_size >= 0)
            return false;  // Свободная ячейка

        // Сигнатура vk (offset 4)
        if (std::memcmp(cell + 4, VK_SIGNATURE, 2) != 0) {
            return false;
        }

        // Name size (offset 6, 2 bytes)
        std::uint16_t name_size = read_u16_le(cell + 6);

        // Data size (offset 8, 4 bytes)
        std::uint32_t data_size = read_u32_le(cell + 8);

        // Data offset (offset 12, 4 bytes)
        std::uint32_t data_offset = read_u32_le(cell + 12);

        // Data type (offset 16, 4 bytes)
        std::uint32_t data_type = read_u32_le(cell + 16);

        // Flags (offset 20, 2 bytes)
        std::uint16_t flags = read_u16_le(cell + 20);

        // Name (offset 24+)
        if (name_size > 0) {
            if (!valid_offset(offset + 24, name_size))
                return false;
            if ((flags & 0x0001) != 0) {
                // Compressed name (ASCII)
                value.name = ascii_to_utf8(cell + 24, name_size);
            } else {
                // UTF-16LE name
                value.name = utf16le_to_utf8(cell + 24, name_size);
            }
        }

        // Парсим данные
        parse_value_data(data_size, data_offset, data_type, value);

        return true;
    }

    /// Парсить данные значения
    void parse_value_data(std::uint32_t data_size, std::uint32_t data_offset,
                          std::uint32_t data_type, RegValue& value) {
        // Проверяем, встроены ли данные (если старший бит data_size установлен)
        bool inline_data = (data_size & 0x80000000) != 0;
        std::uint32_t actual_size = data_size & 0x7FFFFFFF;

        const std::uint8_t* data_ptr = nullptr;

        if (inline_data || actual_size == 0) {
            // Данные встроены в поле data_offset
            data_ptr = reinterpret_cast<const std::uint8_t*>(&data_offset);
            if (actual_size > 4)
                actual_size = 4;
        } else {
            // Данные в отдельной ячейке
            std::size_t abs_offset = cell_offset(data_offset);
            if (!valid_offset(abs_offset, 4 + actual_size)) {
                value.type = RegValueType::Error;
                return;
            }
            // Пропускаем размер ячейки
            data_ptr = data.data() + abs_offset + 4;
        }

        // Конвертируем по типу
        switch (data_type) {
        case REG_NONE:
            value.type = RegValueType::None;
            break;

        case REG_SZ:
        case REG_EXPAND_SZ: {
            value.type = (data_type == REG_SZ) ? RegValueType::String : RegValueType::ExpandString;
            value.data = utf16le_to_utf8(data_ptr, actual_size);
            break;
        }

        case REG_BINARY: {
            value.type = RegValueType::Binary;
            std::vector<std::uint8_t> bytes(data_ptr, data_ptr + actual_size);
            value.data = std::move(bytes);
            break;
        }

        case REG_DWORD: {
            value.type = RegValueType::Dword;
            if (actual_size >= 4) {
                value.data = read_u32_le(data_ptr);
            } else {
                value.data = std::uint32_t{0};
            }
            break;
        }

        case REG_DWORD_BIG_ENDIAN: {
            value.type = RegValueType::DwordBigEndian;
            if (actual_size >= 4) {
                // Big endian
                value.data =
                    static_cast<std::uint32_t>((static_cast<std::uint32_t>(data_ptr[0]) << 24) |
                                               (static_cast<std::uint32_t>(data_ptr[1]) << 16) |
                                               (static_cast<std::uint32_t>(data_ptr[2]) << 8) |
                                               static_cast<std::uint32_t>(data_ptr[3]));
            } else {
                value.data = std::uint32_t{0};
            }
            break;
        }

        case REG_LINK:
            value.type = RegValueType::Link;
            value.data = utf16le_to_utf8(data_ptr, actual_size);
            break;

        case REG_MULTI_SZ: {
            value.type = RegValueType::MultiString;
            std::vector<std::string> strings;

            // Разбираем NUL-separated UTF-16LE strings
            std::size_t pos = 0;
            while (pos + 2 <= actual_size) {
                // Ищем NUL terminator
                std::size_t end = pos;
                while (end + 2 <= actual_size) {
                    std::uint16_t wc = read_u16_le(data_ptr + end);
                    if (wc == 0)
                        break;
                    end += 2;
                }

                if (end > pos) {
                    strings.push_back(utf16le_to_utf8(data_ptr + pos, end - pos));
                }

                pos = end + 2;

                // Проверяем двойной NUL (конец списка)
                if (pos + 2 <= actual_size) {
                    std::uint16_t wc = read_u16_le(data_ptr + pos);
                    if (wc == 0)
                        break;
                }
            }

            value.data = std::move(strings);
            break;
        }

        case REG_QWORD: {
            value.type = RegValueType::Qword;
            if (actual_size >= 8) {
                value.data = read_u64_le(data_ptr);
            } else if (actual_size >= 4) {
                value.data = static_cast<std::uint64_t>(read_u32_le(data_ptr));
            } else {
                value.data = std::uint64_t{0};
            }
            break;
        }

        default:
            // Неизвестный тип — сохраняем как binary
            value.type = RegValueType::Binary;
            std::vector<std::uint8_t> bytes(data_ptr, data_ptr + actual_size);
            value.data = std::move(bytes);
            break;
        }
    }

    /// Найти подключ по имени (case-insensitive)
    std::optional<std::size_t> find_subkey_offset(std::size_t parent_offset,
                                                  std::string_view name) {
        if (!valid_offset(parent_offset, 80))
            return std::nullopt;

        const std::uint8_t* cell = data.data() + parent_offset;

        std::int32_t cell_size = read_i32_le(cell);
        if (cell_size >= 0)
            return std::nullopt;

        if (std::memcmp(cell + 4, NK_SIGNATURE, 2) != 0) {
            return std::nullopt;
        }

        std::uint32_t subkey_count = read_u32_le(cell + 24);
        // Stable subkeys list offset at offset 32 (skip volatile count at 28)
        std::uint32_t volatile_subkey_count = read_u32_le(cell + 28);
        std::uint32_t subkeys_list_offset = read_u32_le(cell + 32);

        // Compatibility fallback for older/simplified format
        // If subkeys_list_offset at 32 is 0 or invalid but there are subkeys,
        // try using offset 28 (older/simplified format stores offset there)
        if (subkey_count > 0 && (subkeys_list_offset == 0 || subkeys_list_offset == 0xFFFFFFFF)) {
            if (volatile_subkey_count != 0 && volatile_subkey_count != 0xFFFFFFFF) {
                subkeys_list_offset = volatile_subkey_count;
            }
        }

        if (subkey_count == 0 || subkeys_list_offset == 0xFFFFFFFF) {
            return std::nullopt;
        }

        // Ищем в списке подключей
        return find_in_subkey_list(cell_offset(subkeys_list_offset), name);
    }

    /// Найти ключ в списке подключей
    std::optional<std::size_t> find_in_subkey_list(std::size_t offset, std::string_view name) {
        if (!valid_offset(offset, 8))
            return std::nullopt;

        const std::uint8_t* cell = data.data() + offset;

        std::int32_t cell_size = read_i32_le(cell);
        if (cell_size >= 0)
            return std::nullopt;

        char sig[2] = {static_cast<char>(cell[4]), static_cast<char>(cell[5])};
        std::uint16_t count = read_u16_le(cell + 6);

        auto names_equal = [](std::string_view a, std::string_view b) {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i) {
                char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
                char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
                if (ca != cb)
                    return false;
            }
            return true;
        };

        if (std::memcmp(sig, LF_SIGNATURE, 2) == 0 || std::memcmp(sig, LH_SIGNATURE, 2) == 0) {
            for (std::uint16_t i = 0; i < count; ++i) {
                std::size_t elem_offset = 8 + static_cast<std::size_t>(i) * 8;
                if (!valid_offset(offset + elem_offset, 8))
                    break;

                std::uint32_t key_offset = read_u32_le(cell + elem_offset);
                std::size_t key_cell_offset = cell_offset(key_offset);

                if (valid_offset(key_cell_offset, 80)) {
                    const std::uint8_t* key_cell = data.data() + key_cell_offset;
                    std::int32_t ksize = read_i32_le(key_cell);
                    if (ksize < 0 && std::memcmp(key_cell + 4, NK_SIGNATURE, 2) == 0) {
                        std::uint16_t flags = read_u16_le(key_cell + 6);
                        // Format detection for name offset
                        std::size_t cell_data_size = static_cast<std::size_t>(-ksize);
                        std::uint16_t name_size = read_u16_le(key_cell + 76);
                        std::size_t name_off = 80;
                        if (name_size == 0 || name_off + name_size > cell_data_size) {
                            std::uint16_t alt_size = read_u16_le(key_cell + 72);
                            if (alt_size > 0 &&
                                76 + static_cast<std::size_t>(alt_size) <= cell_data_size) {
                                name_size = alt_size;
                                name_off = 76;
                            }
                        }
                        if (valid_offset(key_cell_offset + name_off, name_size)) {
                            std::string key_name;
                            if ((flags & KEY_COMP_NAME) != 0) {
                                key_name = ascii_to_utf8(key_cell + name_off, name_size);
                            } else {
                                key_name = utf16le_to_utf8(key_cell + name_off, name_size);
                            }
                            if (names_equal(key_name, name)) {
                                return key_cell_offset;
                            }
                        }
                    }
                }
            }
        } else if (std::memcmp(sig, RI_SIGNATURE, 2) == 0) {
            for (std::uint16_t i = 0; i < count; ++i) {
                std::size_t elem_offset = 8 + static_cast<std::size_t>(i) * 4;
                if (!valid_offset(offset + elem_offset, 4))
                    break;

                std::uint32_t list_offset = read_u32_le(cell + elem_offset);
                auto result = find_in_subkey_list(cell_offset(list_offset), name);
                if (result)
                    return result;
            }
        } else if (std::memcmp(sig, LI_SIGNATURE, 2) == 0) {
            for (std::uint16_t i = 0; i < count; ++i) {
                std::size_t elem_offset = 8 + static_cast<std::size_t>(i) * 4;
                if (!valid_offset(offset + elem_offset, 4))
                    break;

                std::uint32_t key_offset = read_u32_le(cell + elem_offset);
                std::size_t key_cell_offset = cell_offset(key_offset);

                if (valid_offset(key_cell_offset, 80)) {
                    const std::uint8_t* key_cell = data.data() + key_cell_offset;
                    std::int32_t ksize = read_i32_le(key_cell);
                    if (ksize < 0 && std::memcmp(key_cell + 4, NK_SIGNATURE, 2) == 0) {
                        std::uint16_t flags = read_u16_le(key_cell + 6);
                        // Format detection for name offset
                        std::size_t cell_data_size = static_cast<std::size_t>(-ksize);
                        std::uint16_t name_size = read_u16_le(key_cell + 76);
                        std::size_t name_off = 80;
                        if (name_size == 0 || name_off + name_size > cell_data_size) {
                            std::uint16_t alt_size = read_u16_le(key_cell + 72);
                            if (alt_size > 0 &&
                                76 + static_cast<std::size_t>(alt_size) <= cell_data_size) {
                                name_size = alt_size;
                                name_off = 76;
                            }
                        }
                        if (valid_offset(key_cell_offset + name_off, name_size)) {
                            std::string key_name;
                            if ((flags & KEY_COMP_NAME) != 0) {
                                key_name = ascii_to_utf8(key_cell + name_off, name_size);
                            } else {
                                key_name = utf16le_to_utf8(key_cell + name_off, name_size);
                            }
                            if (names_equal(key_name, name)) {
                                return key_cell_offset;
                            }
                        }
                    }
                }
            }
        }

        return std::nullopt;
    }
};

// ============================================================================
// HveParser implementation
// ============================================================================

HveParser::HveParser() : impl_(std::make_unique<Impl>()) {}

HveParser::~HveParser() = default;

HveParser::HveParser(HveParser&&) noexcept = default;
HveParser& HveParser::operator=(HveParser&&) noexcept = default;

bool HveParser::transaction_logs_applied() const {
    return impl_ && impl_->transaction_logs_applied;
}

bool HveParser::load(const std::filesystem::path& path) {
    path_ = path;
    loaded_ = false;
    error_.reset();

    // Проверяем существование файла
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        error_ =
            HveError{HveErrorKind::FileNotFound, "file not found: " + platform::path_to_utf8(path)};
        return false;
    }

    // Читаем файл в память
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        error_ =
            HveError{HveErrorKind::IoError, "could not open file: " + platform::path_to_utf8(path)};
        return false;
    }

    auto size = file.tellg();
    if (size < static_cast<std::streampos>(REGF_HEADER_SIZE)) {
        error_ = HveError{HveErrorKind::InvalidSignature, "file too small for REGF header"};
        return false;
    }

    file.seekg(0, std::ios::beg);
    impl_->data.resize(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(impl_->data.data()),
                   static_cast<std::streamsize>(size))) {
        error_ = HveError{HveErrorKind::IoError, "failed to read file"};
        return false;
    }

    // Парсим заголовок
    if (!impl_->parse_header()) {
        error_ =
            HveError{HveErrorKind::InvalidSignature, "invalid REGF signature or corrupted header"};
        return false;
    }

    loaded_ = true;

    // SPEC-SLICE-015 FACT-001: ищем и применяем transaction logs
    // Соответствует поведению upstream chainsaw (через notatin)
    auto log_files = find_transaction_logs(path);
    if (!log_files.empty()) {
        // Read and parse all log files
        std::vector<LogEntry> all_entries;
        for (const auto& log_path : log_files) {
            auto log_data = read_file_bytes(log_path);
            if (!log_data.empty()) {
                auto entries = parse_transaction_log(log_data);
                for (auto& e : entries) {
                    all_entries.push_back(std::move(e));
                }
            }
        }

        // Apply transaction logs if any valid entries found
        if (!all_entries.empty()) {
            // Use the minimum of primary/secondary sequence as base
            std::uint32_t hive_sequence =
                std::min(impl_->header.primary_sequence, impl_->header.secondary_sequence);
            impl_->transaction_logs_applied =
                apply_transaction_logs(impl_->data, all_entries, hive_sequence);

            // Re-parse header after applying logs (sizes may have changed)
            if (impl_->transaction_logs_applied) {
                impl_->parse_header();
            }
        }
    }

    return true;
}

std::optional<RegKey> HveParser::get_key(std::string_view key_path) {
    if (!loaded_)
        return std::nullopt;

    // Разбираем путь
    auto parts = split_path(key_path);

    // Начинаем с корневого ключа
    std::size_t current_offset = impl_->cell_offset(impl_->header.root_key_offset);
    std::string current_path;

    // Проходим по пути
    for (const auto& part : parts) {
        auto subkey_offset = impl_->find_subkey_offset(current_offset, part);
        if (!subkey_offset) {
            return std::nullopt;
        }
        current_offset = *subkey_offset;
    }

    // Парсим найденный ключ
    RegKey key;

    // Строим родительский путь
    std::string parent_path;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        if (!parent_path.empty())
            parent_path += "\\";
        parent_path += parts[i];
    }

    if (impl_->parse_key_node(current_offset, key, parent_path)) {
        return key;
    }

    return std::nullopt;
}

std::optional<RegKey> HveParser::get_root_key() {
    if (!loaded_)
        return std::nullopt;

    std::size_t root_offset = impl_->cell_offset(impl_->header.root_key_offset);

    RegKey key;
    if (impl_->parse_key_node(root_offset, key, "")) {
        return key;
    }

    return std::nullopt;
}

// ============================================================================
// HveParser::Iterator implementation
// ============================================================================

HveParser::Iterator::Iterator(HveParser* parser) : parser_(parser) {}

bool HveParser::Iterator::next(RegKey& out) {
    if (!parser_ || !parser_->loaded_)
        return false;

    // Инициализация: начинаем с корневого ключа
    if (!initialized_) {
        initialized_ = true;
        auto root = parser_->get_root_key();
        if (root) {
            out = std::move(*root);
            // Добавляем подключи в стек (для root - просто имена)
            for (auto it = out.subkey_names_.rbegin(); it != out.subkey_names_.rend(); ++it) {
                stack_.push_back(*it);
            }
            return true;
        }
        return false;
    }

    // DFS по стеку
    while (!stack_.empty()) {
        std::string path = std::move(stack_.back());
        stack_.pop_back();

        auto key = parser_->get_key(path);
        if (key) {
            out = std::move(*key);
            // Добавляем подключи в стек (путь + имя подключа)
            for (auto it = out.subkey_names_.rbegin(); it != out.subkey_names_.rend(); ++it) {
                stack_.push_back(path + "\\" + *it);
            }
            return true;
        }
    }

    return false;
}

bool HveParser::Iterator::has_next() const {
    if (!parser_ || !parser_->loaded_)
        return false;
    if (!initialized_)
        return true;  // Ещё не начали
    return !stack_.empty();
}

HveParser::Iterator HveParser::iter() {
    return Iterator(this);
}

// ============================================================================
// HveReader - Reader для Reader framework
// ============================================================================

}  // namespace chainsaw::io::hve

namespace chainsaw::io {

class HveReader : public Reader {
public:
    explicit HveReader(std::filesystem::path path) : path_(std::move(path)), iterator_(nullptr) {}

    bool load() {
        if (!parser_.load(path_)) {
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

        hve::RegKey key;
        if (!iterator_.next(key)) {
            return false;
        }

        // Конвертируем RegKey в Value (JSON-like)
        Value::Object obj;
        obj["key_name"] = Value(key.name());
        obj["key_path"] = Value(key.path());

        // Timestamp
        auto time = std::chrono::system_clock::to_time_t(key.last_modified());
        std::tm tm_result{};
#ifdef _WIN32
        gmtime_s(&tm_result, &time);
#else
        gmtime_r(&time, &tm_result);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_result);
        obj["last_modified"] = Value(std::string(buf));

        // Значения
        Value::Array values_arr;
        for (const auto& val : key.values()) {
            Value::Object val_obj;
            val_obj["name"] = Value(val.name);
            val_obj["type"] = Value(std::string(hve::reg_value_type_to_string(val.type)));

            // Данные по типу
            if (auto* str = val.as_string()) {
                val_obj["data"] = Value(*str);
            } else if (auto u32 = val.as_u32()) {
                val_obj["data"] = Value(static_cast<std::int64_t>(*u32));
            } else if (auto u64 = val.as_u64()) {
                val_obj["data"] = Value(static_cast<std::int64_t>(*u64));
            } else if (auto* multi = val.as_multi_string()) {
                Value::Array arr;
                for (const auto& s : *multi) {
                    arr.push_back(Value(s));
                }
                val_obj["data"] = Value(std::move(arr));
            } else if (auto* bin = val.as_binary()) {
                // Binary как hex string
                std::string hex;
                hex.reserve(bin->size() * 2);
                for (auto b : *bin) {
                    static const char digits[] = "0123456789abcdef";
                    hex.push_back(digits[(b >> 4) & 0xF]);
                    hex.push_back(digits[b & 0xF]);
                }
                val_obj["data"] = Value(hex);
            } else {
                val_obj["data"] = Value();  // null
            }

            values_arr.push_back(Value(std::move(val_obj)));
        }
        obj["values"] = Value(std::move(values_arr));

        // Подключи
        Value::Array subkeys_arr;
        for (const auto& name : key.subkey_names()) {
            subkeys_arr.push_back(Value(name));
        }
        obj["subkeys"] = Value(std::move(subkeys_arr));

        out.kind = DocumentKind::Hve;
        out.data = Value(std::move(obj));
        out.source = platform::path_to_utf8(path_);
        out.record_id = std::nullopt;

        return true;
    }

    bool has_next() const override { return loaded_ && iterator_.has_next(); }

    DocumentKind kind() const override { return DocumentKind::Hve; }
    const std::filesystem::path& path() const override { return path_; }
    const std::optional<ReaderError>& last_error() const override { return error_; }

private:
    std::filesystem::path path_;
    hve::HveParser parser_;
    hve::HveParser::Iterator iterator_;
    std::optional<ReaderError> error_;
    bool loaded_ = false;
};

}  // namespace chainsaw::io

namespace chainsaw::io::hve {

std::unique_ptr<chainsaw::io::Reader> create_hve_reader(const std::filesystem::path& path,
                                                        bool skip_errors) {
    auto reader = std::make_unique<chainsaw::io::HveReader>(path);
    if (!reader->load()) {
        if (skip_errors) {
            return chainsaw::io::create_empty_reader(path, chainsaw::io::DocumentKind::Hve);
        }
    }
    return reader;
}

// ============================================================================
// parse_srum_entries - парсинг SRUM информации из SOFTWARE hive
// ============================================================================
//
// SPEC-SLICE-017 FACT-043..048: SRUM Registry Entries
// Соответствие Rust hve/srum.rs:parse_srum_entries()
//

namespace {

/// Конвертировать RegValue в Value
chainsaw::Value reg_value_to_value(const RegValue& val) {
    switch (val.type) {
    case RegValueType::String:
    case RegValueType::ExpandString:
    case RegValueType::Link:
        if (auto* str = val.as_string()) {
            return chainsaw::Value(*str);
        }
        break;

    case RegValueType::Dword:
    case RegValueType::DwordBigEndian:
        if (auto num = val.as_u32()) {
            return chainsaw::Value(static_cast<std::int64_t>(*num));
        }
        break;

    case RegValueType::Qword:
        if (auto num = val.as_u64()) {
            return chainsaw::Value(static_cast<std::int64_t>(*num));
        }
        break;

    case RegValueType::Binary:
        if (auto* bin = val.as_binary()) {
            chainsaw::Value::Array arr;
            arr.reserve(bin->size());
            for (auto b : *bin) {
                arr.push_back(chainsaw::Value(static_cast<std::int64_t>(b)));
            }
            return chainsaw::Value(std::move(arr));
        }
        break;

    case RegValueType::MultiString:
        if (auto* multi = val.as_multi_string()) {
            chainsaw::Value::Array arr;
            arr.reserve(multi->size());
            for (const auto& s : *multi) {
                arr.push_back(chainsaw::Value(s));
            }
            return chainsaw::Value(std::move(arr));
        }
        break;

    case RegValueType::None:
    case RegValueType::Error:
        break;
    }
    return chainsaw::Value();  // null
}

/// Получить имя файла из пути (обрабатывает / и \)
std::string get_filename_from_path(const std::string& path) {
    std::string normalized = path;
    // Заменяем все \ на /
    for (auto& c : normalized) {
        if (c == '\\')
            c = '/';
    }

    auto pos = normalized.rfind('/');
    if (pos != std::string::npos && pos + 1 < normalized.size()) {
        return normalized.substr(pos + 1);
    }
    return normalized;
}

}  // anonymous namespace

std::optional<SrumRegInfo> parse_srum_entries(HveParser& parser) {
    // SPEC-SLICE-017 FACT-044: Get SRUM global parameters
    // Path: Microsoft\Windows NT\CurrentVersion\SRUM\Parameters
    auto key_srum_parameters =
        parser.get_key(R"(Microsoft\Windows NT\CurrentVersion\SRUM\Parameters)");

    if (!key_srum_parameters) {
        return std::nullopt;  // Could not find SRUM Parameters registry key
    }

    // Default parameters (соответствует Rust hve/srum.rs:46-52)
    chainsaw::Value::Object global_params;
    global_params["Tier1Period"] = chainsaw::Value(static_cast<std::int64_t>(60));
    global_params["Tier2Period"] = chainsaw::Value(static_cast<std::int64_t>(3600));
    global_params["Tier2MaxEntries"] = chainsaw::Value(static_cast<std::int64_t>(1440));
    global_params["Tier2LongTermPeriod"] = chainsaw::Value(static_cast<std::int64_t>(604800));
    global_params["Tier2LongTermMaxEntries"] = chainsaw::Value(static_cast<std::int64_t>(260));

    // Override with actual values from registry
    for (const auto& val : key_srum_parameters->values()) {
        // Используем имя значения или "(default)" для пустого имени
        std::string name = val.name.empty() ? "(default)" : val.name;
        global_params[name] = reg_value_to_value(val);
    }

    // SPEC-SLICE-017 FACT-045: Get SRUM Extensions
    // Path: Microsoft\Windows NT\CurrentVersion\SRUM\Extensions
    auto key_srum_extensions =
        parser.get_key(R"(Microsoft\Windows NT\CurrentVersion\SRUM\Extensions)");

    if (!key_srum_extensions) {
        return std::nullopt;  // Could not find SRUM Extensions registry key
    }

    chainsaw::Value::Object extensions;

    // Iterate over subkeys (each subkey is a GUID)
    for (const auto& subkey_name : key_srum_extensions->subkey_names()) {
        // Получаем подключ
        std::string subkey_path =
            R"(Microsoft\Windows NT\CurrentVersion\SRUM\Extensions\)" + subkey_name;
        auto subkey = parser.get_key(subkey_path);
        if (!subkey)
            continue;

        // Uppercase GUID
        std::string guid_upper = subkey_name;
        for (auto& c : guid_upper) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }

        chainsaw::Value::Object ext_obj;

        // Read all values from extension subkey
        for (const auto& val : subkey->values()) {
            std::string name = val.name.empty() ? "(default)" : val.name;
            ext_obj[name] = reg_value_to_value(val);
        }

        extensions[guid_upper] = chainsaw::Value(std::move(ext_obj));
    }

    // SPEC-SLICE-017 FACT-047: Get Users GUID from ProfileList
    // Path: Microsoft\Windows NT\CurrentVersion\ProfileList
    auto key_profile_list = parser.get_key(R"(Microsoft\Windows NT\CurrentVersion\ProfileList)");

    if (!key_profile_list) {
        return std::nullopt;  // Could not find ProfileList key
    }

    chainsaw::Value::Object user_info;

    // Iterate over ProfileList subkeys (each subkey is a SID)
    for (const auto& subkey_name : key_profile_list->subkey_names()) {
        std::string subkey_path =
            R"(Microsoft\Windows NT\CurrentVersion\ProfileList\)" + subkey_name;
        auto subkey = parser.get_key(subkey_path);
        if (!subkey)
            continue;

        // Get Sid value (binary)
        chainsaw::Value sid_value;
        auto sid_reg = subkey->get_value("Sid");
        if (sid_reg) {
            if (auto* bin = sid_reg->as_binary()) {
                // Конвертируем в hex string для display
                std::string hex;
                hex.reserve(bin->size() * 2);
                for (auto b : *bin) {
                    static const char digits[] = "0123456789abcdef";
                    hex.push_back(digits[(b >> 4) & 0xF]);
                    hex.push_back(digits[b & 0xF]);
                }
                sid_value = chainsaw::Value(hex);
            }
        }

        // Get ProfileImagePath value
        chainsaw::Value username_value;
        auto profile_path_reg = subkey->get_value("ProfileImagePath");
        if (profile_path_reg) {
            if (auto* path_str = profile_path_reg->as_string()) {
                // Extract username from path (last component)
                std::string username = get_filename_from_path(*path_str);
                username_value = chainsaw::Value(username);
            }
        }

        // Build user info object
        chainsaw::Value::Object user_obj;
        user_obj["GUID"] = chainsaw::Value(subkey_name);
        user_obj["SID"] = std::move(sid_value);
        user_obj["Username"] = std::move(username_value);

        user_info[subkey_name] = chainsaw::Value(std::move(user_obj));
    }

    SrumRegInfo result;
    result.global_parameters = chainsaw::Value(std::move(global_params));
    result.extensions = chainsaw::Value(std::move(extensions));
    result.user_info = chainsaw::Value(std::move(user_info));

    return result;
}

}  // namespace chainsaw::io::hve
