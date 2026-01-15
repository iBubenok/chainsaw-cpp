// ==============================================================================
// chainsaw/mft.hpp - MOD-0009: MFT Parser (NTFS Master File Table)
// ==============================================================================
//
// MOD-0009 io::mft
// SLICE-018: MFT Parser Implementation
// SPEC-SLICE-018: micro-spec поведения
// ADR-0009: custom MFT parser
//
// Назначение:
// - Парсинг NTFS Master File Table файлов
// - Извлечение метаданных файлов (timestamps, attributes, paths)
// - Поддержка resident и non-resident $DATA атрибутов
// - Интеграция с Reader framework
//
// Соответствие Rust:
// - upstream/chainsaw/src/file/mft.rs (Parser, load, parse)
// - external crate mft (MftParser, MftEntry, attribute::*)
//
// ==============================================================================

#ifndef CHAINSAW_MFT_HPP
#define CHAINSAW_MFT_HPP

#include <chainsaw/value.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chainsaw::io::mft {

// ----------------------------------------------------------------------------
// MFT Entry Flags
// ----------------------------------------------------------------------------
//
// SPEC-SLICE-018 FACT-003: MFT entry flags
//

/// MFT entry allocation state
enum class MftEntryFlags : std::uint16_t {
    None = 0x0000,
    Allocated = 0x0001,     // IN_USE
    IndexPresent = 0x0002,  // INDEX_PRESENT (directory)
};

/// Combine MFT entry flags to string representation
/// @return "ALLOCATED", "ALLOCATED | INDEX_PRESENT", etc.
std::string mft_entry_flags_to_string(std::uint16_t flags);

// ----------------------------------------------------------------------------
// File Attribute Flags
// ----------------------------------------------------------------------------
//
// SPEC-SLICE-018 FACT-004: Standard file attributes
//

/// Standard file attribute flags (from winnt.h)
enum class FileAttributeFlags : std::uint32_t {
    ReadOnly = 0x00000001,
    Hidden = 0x00000002,
    System = 0x00000004,
    Archive = 0x00000020,
    Device = 0x00000040,
    Normal = 0x00000080,
    Temporary = 0x00000100,
    SparseFile = 0x00000200,
    ReparsePoint = 0x00000400,
    Compressed = 0x00000800,
    Offline = 0x00001000,
    NotContentIndexed = 0x00002000,
    Encrypted = 0x00004000,
    Directory = 0x10000000,
};

/// Convert file attribute flags to string representation
/// @return "FILE_ATTRIBUTE_ARCHIVE", "FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM", etc.
std::string file_attribute_flags_to_string(std::uint32_t flags);

// ----------------------------------------------------------------------------
// MftTimestamp - FILETIME wrapper
// ----------------------------------------------------------------------------

/// NTFS timestamp (FILETIME format)
struct MftTimestamp {
    std::uint64_t filetime = 0;

    /// Convert to system_clock time_point
    std::chrono::system_clock::time_point to_time_point() const;

    /// Convert to ISO 8601 string (YYYY-MM-DDTHH:MM:SSZ)
    std::string to_iso8601() const;

    /// Check if timestamp is valid (non-zero)
    bool valid() const { return filetime != 0; }
};

// ----------------------------------------------------------------------------
// MftEntry - Parsed MFT entry
// ----------------------------------------------------------------------------
//
// SPEC-SLICE-018: Аналог mft::MftEntry + FlatMftEntryWithName
//

/// Parsed MFT entry with all relevant attributes
struct MftEntry {
    // -------------------------------------------------------------------------
    // Entry header fields
    // -------------------------------------------------------------------------

    /// Entry signature ("FILE" or "BAAD")
    std::string signature;

    /// Entry ID (record number)
    /// SPEC-SLICE-018 FACT-001: entry_id field
    std::uint64_t entry_id = 0;

    /// Sequence number
    std::uint16_t sequence = 0;

    /// Base entry ID (for extension entries)
    std::uint64_t base_entry_id = 0;

    /// Base entry sequence number
    std::uint16_t base_entry_sequence = 0;

    /// Hard link count
    std::uint16_t hard_link_count = 0;

    /// Entry flags (ALLOCATED, INDEX_PRESENT)
    std::uint16_t flags = 0;

    /// Used entry size (actual data size)
    std::uint32_t used_entry_size = 0;

    /// Total entry size (allocated size)
    std::uint32_t total_entry_size = 0;

    // -------------------------------------------------------------------------
    // $STANDARD_INFORMATION attribute (type 0x10)
    // -------------------------------------------------------------------------

    /// File attributes from $STANDARD_INFORMATION
    std::uint32_t standard_info_flags = 0;

    /// Created timestamp from $STANDARD_INFORMATION
    MftTimestamp standard_info_created;

    /// Last modified timestamp from $STANDARD_INFORMATION
    MftTimestamp standard_info_last_modified;

    /// Last access timestamp from $STANDARD_INFORMATION
    MftTimestamp standard_info_last_access;

    /// MFT entry modified timestamp from $STANDARD_INFORMATION
    MftTimestamp standard_info_mft_modified;

    // -------------------------------------------------------------------------
    // $FILE_NAME attribute (type 0x30)
    // -------------------------------------------------------------------------

    /// File name
    std::string file_name;

    /// Parent directory entry ID
    std::uint64_t parent_entry_id = 0;

    /// File attributes from $FILE_NAME
    std::uint32_t file_name_flags = 0;

    /// Created timestamp from $FILE_NAME
    MftTimestamp file_name_created;

    /// Last modified timestamp from $FILE_NAME
    MftTimestamp file_name_last_modified;

    /// Last access timestamp from $FILE_NAME
    MftTimestamp file_name_last_access;

    /// MFT entry modified timestamp from $FILE_NAME
    MftTimestamp file_name_mft_modified;

    /// Allocated size from $FILE_NAME
    std::uint64_t file_name_allocated_size = 0;

    /// Real size from $FILE_NAME
    std::uint64_t file_name_real_size = 0;

    // -------------------------------------------------------------------------
    // $DATA attribute (type 0x80)
    // -------------------------------------------------------------------------

    /// File size (from $DATA attribute, logical size)
    std::uint64_t file_size = 0;

    /// Has alternate data streams (multiple $DATA attributes)
    bool has_alternate_data_streams = false;

    /// Resident data content (if small file stored inline)
    /// Only populated when decode_data_streams=true
    std::optional<std::vector<std::uint8_t>> resident_data;

    // -------------------------------------------------------------------------
    // Derived fields
    // -------------------------------------------------------------------------

    /// Full path (reconstructed from parent chain)
    std::string full_path;

    /// Is this a directory
    bool is_directory() const {
        return (flags & static_cast<std::uint16_t>(MftEntryFlags::IndexPresent)) != 0;
    }

    /// Is this entry deleted (not allocated)
    bool is_deleted() const {
        return (flags & static_cast<std::uint16_t>(MftEntryFlags::Allocated)) == 0;
    }

    /// Is this entry allocated
    bool is_allocated() const {
        return (flags & static_cast<std::uint16_t>(MftEntryFlags::Allocated)) != 0;
    }

    /// Is this entry valid (has FILE signature)
    bool is_valid() const { return signature == "FILE"; }
};

// ----------------------------------------------------------------------------
// MftError - MFT parser errors
// ----------------------------------------------------------------------------

/// Types of MFT parser errors
enum class MftErrorKind {
    FileNotFound,      // File not found
    InvalidSignature,  // Invalid MFT signature
    CorruptedData,     // Corrupted data
    ParseError,        // Parse error
    IoError            // I/O error
};

/// MFT parser error
struct MftError {
    MftErrorKind kind;
    std::string message;

    /// Format error for display
    std::string format() const;
};

// ----------------------------------------------------------------------------
// MftParserOptions - Parser options
// ----------------------------------------------------------------------------

/// Options for MFT parsing
struct MftParserOptions {
    /// Decode resident data streams
    bool decode_data_streams = false;

    /// Skip entries with parse errors
    bool skip_errors = true;
};

// ----------------------------------------------------------------------------
// MftParser - MFT file parser
// ----------------------------------------------------------------------------
//
// SPEC-SLICE-018: Аналог mft::MftParser
// FACT-001: MftParser::from_path() + iterate()
// FACT-002: FlatMftEntryWithName output
//

/// MFT file parser
class MftParser {
public:
    /// Alias for backward compatibility
    using Options = MftParserOptions;

    MftParser();
    ~MftParser();

    // Disable copying
    MftParser(const MftParser&) = delete;
    MftParser& operator=(const MftParser&) = delete;

    // Enable moving
    MftParser(MftParser&&) noexcept;
    MftParser& operator=(MftParser&&) noexcept;

    // -------------------------------------------------------------------------
    // Loading
    // -------------------------------------------------------------------------

    /// Load MFT file
    /// @param path Path to MFT file
    /// @param options Parser options
    /// @return true if successful
    bool load(const std::filesystem::path& path, const Options& options = Options{});

    /// Last error
    const std::optional<MftError>& last_error() const { return error_; }

    /// Path to loaded file
    const std::filesystem::path& path() const { return path_; }

    /// Is file loaded
    bool loaded() const { return loaded_; }

    /// Number of entries in MFT
    std::size_t entry_count() const;

    /// MFT entry size (typically 1024 bytes)
    std::uint32_t entry_size() const;

    // -------------------------------------------------------------------------
    // Iteration
    // -------------------------------------------------------------------------

    /// Iterator for MFT entries
    class Iterator {
    public:
        /// Default constructor (for use as member)
        Iterator(std::nullptr_t) : parser_(nullptr) {}

        /// Get next entry
        bool next(MftEntry& out);

        /// Check if more entries available
        bool has_next() const;

        /// Current entry index
        std::size_t current_index() const { return current_index_; }

    private:
        friend class MftParser;
        explicit Iterator(MftParser* parser);

        MftParser* parser_ = nullptr;
        std::size_t current_index_ = 0;
    };

    /// Create iterator
    Iterator iter();

    /// Get entry by ID
    /// @param entry_id Entry record number
    /// @return Entry or nullopt if not found/invalid
    std::optional<MftEntry> get_entry(std::uint64_t entry_id);

    // -------------------------------------------------------------------------
    // Path reconstruction
    // -------------------------------------------------------------------------

    /// Reconstruct full path for an entry
    /// @param entry Entry to get path for
    /// @return Full path (may be partial if parents not found)
    std::string reconstruct_path(const MftEntry& entry);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::filesystem::path path_;
    std::optional<MftError> error_;
    Options options_;
    bool loaded_ = false;
};

}  // namespace chainsaw::io::mft

// Forward declaration for Reader
namespace chainsaw::io {
class Reader;
}  // namespace chainsaw::io

namespace chainsaw::io::mft {

// ----------------------------------------------------------------------------
// MftReader - Reader for Reader framework
// ----------------------------------------------------------------------------

/// Create MFT Reader for integration with Reader::open()
/// @param path Path to MFT file
/// @param skip_errors Skip entries with errors
/// @param decode_data_streams Decode resident data streams
std::unique_ptr<chainsaw::io::Reader> create_mft_reader(const std::filesystem::path& path,
                                                        bool skip_errors,
                                                        bool decode_data_streams = false);

}  // namespace chainsaw::io::mft

#endif  // CHAINSAW_MFT_HPP
