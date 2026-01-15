// ==============================================================================
// shimcache.cpp - Реализация Shimcache Parser & Analyser
// ==============================================================================
//
// MOD-0015 analyse::shimcache
// SLICE-019: Analyse Shimcache Command
// SPEC-SLICE-019: micro-spec поведения
//
// Парсинг Windows shimcache (AppCompatCache) из SYSTEM registry hive.
// Поддерживаемые версии:
// - Windows 10 / 10 Creators
// - Windows 8 / 8.1 / Server 2012 / 2012 R2
// - Windows 7 x86/x64 / Server 2008 R2
//
// НЕ ПОДДЕРЖИВАЮТСЯ:
// - Windows Vista / Server 2003 / 2008
// - Windows XP
//
// ==============================================================================

#include <algorithm>
#include <cctype>
#include <chainsaw/hve.hpp>
#include <chainsaw/platform.hpp>
#include <chainsaw/shimcache.hpp>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace chainsaw::analyse::shimcache {

// ============================================================================
// Helper Functions
// ============================================================================

namespace {

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

/// Читать little-endian uint64
inline std::uint64_t read_u64_le(const std::uint8_t* data) {
    return static_cast<std::uint64_t>(read_u32_le(data)) |
           (static_cast<std::uint64_t>(read_u32_le(data + 4)) << 32);
}

/// Конвертировать FILETIME в time_point
std::chrono::system_clock::time_point filetime_to_timepoint(std::uint64_t filetime) {
    constexpr std::uint64_t EPOCH_DIFF = 116444736000000000ULL;
    if (filetime < EPOCH_DIFF || filetime == 0) {
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
        if (wchar == 0)
            break;
        if (wchar < 0x80) {
            result.push_back(static_cast<char>(wchar));
        } else if (wchar < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (wchar >> 6)));
            result.push_back(static_cast<char>(0x80 | (wchar & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xE0 | (wchar >> 12)));
            result.push_back(static_cast<char>(0x80 | ((wchar >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (wchar & 0x3F)));
        }
    }
    return result;
}

/// Привести строку к нижнему регистру
std::string to_lowercase(const std::string& str) {
    std::string result = str;
    for (auto& c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

/// Проверить, является ли строка Program entry (UWP app format)
/// Regex:
/// ^([0-9a-f]{8})\s+([0-9a-f]{16})\s+([0-9a-f]{16})\s+([0-9a-f]{4})\s+([\w.-]+)\s+(\w+)\s*(\w*)$
bool is_program_entry(const std::string& path) {
    // Упрощённая проверка формата UWP program entry
    if (path.size() < 50)
        return false;

    // Проверяем начало: 8 hex символов
    for (std::size_t i = 0; i < 8 && i < path.size(); ++i) {
        char c = path[i];
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }

    // Должен быть пробел после первых 8 символов
    if (path.size() > 8 && path[8] != ' ') {
        return false;
    }

    // Считаем группы разделённые пробелами
    std::size_t groups = 0;
    bool in_group = false;
    for (char c : path) {
        if (c == ' ' || c == '\t') {
            if (in_group) {
                in_group = false;
            }
        } else {
            if (!in_group) {
                groups++;
                in_group = true;
            }
        }
    }

    // Должно быть минимум 6 групп
    return groups >= 6;
}

/// Парсить версию из hex строки (16 символов -> a.b.c.d)
std::string parse_version_hex(const std::string& hex_str) {
    if (hex_str.size() != 16)
        return hex_str;

    try {
        std::uint16_t a = static_cast<std::uint16_t>(std::stoul(hex_str.substr(0, 4), nullptr, 16));
        std::uint16_t b = static_cast<std::uint16_t>(std::stoul(hex_str.substr(4, 4), nullptr, 16));
        std::uint16_t c = static_cast<std::uint16_t>(std::stoul(hex_str.substr(8, 4), nullptr, 16));
        std::uint16_t d =
            static_cast<std::uint16_t>(std::stoul(hex_str.substr(12, 4), nullptr, 16));

        std::ostringstream oss;
        oss << a << "." << b << "." << c << "." << d;
        return oss.str();
    } catch (...) {
        return hex_str;
    }
}

/// Парсить Program entry из path string
ProgramEntryType parse_program_entry(const std::string& path) {
    ProgramEntryType result;
    result.raw_entry = path;

    // Разбиваем на части по пробелам
    std::vector<std::string> parts;
    std::istringstream iss(path);
    std::string part;
    while (iss >> part) {
        parts.push_back(part);
    }

    if (parts.size() >= 6) {
        result.unknown_u32 = parts[0];
        result.program_version = parse_version_hex(parts[1]);
        result.sdk_version = parse_version_hex(parts[2]);

        if (parts[3].size() == 4) {
            try {
                std::uint16_t arch = static_cast<std::uint16_t>(std::stoul(parts[3], nullptr, 16));
                result.architecture = cpu_architecture_from_u16(arch);
            } catch (...) {
                result.architecture = CPUArchitecture::Unknown;
            }
        }

        result.program_name = parts[4];
        result.publisher_id = parts[5];

        if (parts.size() >= 7) {
            result.neutral = (parts[6] == "neutral");
        }
    }

    return result;
}

// ============================================================================
// Windows 10 Shimcache Parser
// ============================================================================

std::vector<ShimcacheEntry> parse_windows10_cache_from_offset(
    const std::vector<std::uint8_t>& bytes, std::uint32_t controlset, std::size_t start_offset) {
    std::vector<ShimcacheEntry> entries;

    std::size_t index = start_offset;
    std::uint32_t cache_entry_position = 0;

    while (index + 14 < bytes.size()) {
        // Проверяем сигнатуру "10ts"
        if (bytes[index] != '1' || bytes[index + 1] != '0' || bytes[index + 2] != 't' ||
            bytes[index + 3] != 's') {
            break;
        }

        std::string signature(reinterpret_cast<const char*>(&bytes[index]), 4);
        index += 4;

        // Пропускаем 4 unknown байта
        index += 4;

        // Cache entry size
        if (index + 4 > bytes.size())
            break;
        // std::uint32_t cache_entry_size = read_u32_le(&bytes[index]);
        index += 4;

        // Path size
        if (index + 2 > bytes.size())
            break;
        std::uint16_t path_size = read_u16_le(&bytes[index]);
        index += 2;

        // Path (UTF-16LE)
        if (index + path_size > bytes.size())
            break;
        std::string path = utf16le_to_utf8(&bytes[index], path_size);
        index += path_size;

        // Last modified timestamp
        if (index + 8 > bytes.size())
            break;
        std::uint64_t last_modified_win32 = read_u64_le(&bytes[index]);
        index += 8;

        // Data size
        if (index + 4 > bytes.size())
            break;
        std::uint32_t data_size = read_u32_le(&bytes[index]);
        index += 4;

        // Data
        std::optional<std::vector<std::uint8_t>> data;
        if (data_size > 0 && index + data_size <= bytes.size()) {
            data = std::vector<std::uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(index),
                                             bytes.begin() +
                                                 static_cast<std::ptrdiff_t>(index + data_size));
            index += data_size;
        }

        // Определяем тип entry
        EntryType entry_type;
        if (is_program_entry(path)) {
            entry_type = parse_program_entry(path);
        } else {
            entry_type = FileEntryType{path};
        }

        // Last modified timestamp
        std::optional<std::chrono::system_clock::time_point> last_modified_ts;
        if (last_modified_win32 != 0) {
            last_modified_ts = filetime_to_timepoint(last_modified_win32);
        }

        ShimcacheEntry entry;
        entry.cache_entry_position = cache_entry_position;
        entry.controlset = controlset;
        entry.data_size = data_size;
        entry.data = std::move(data);
        entry.entry_type = std::move(entry_type);
        entry.executed = std::nullopt;  // Windows 10 не имеет executed flag
        entry.last_modified_ts = last_modified_ts;
        entry.path_size = path_size;
        entry.signature = signature;

        entries.push_back(std::move(entry));
        ++cache_entry_position;
    }

    return entries;
}

std::vector<ShimcacheEntry> parse_windows10_cache(const std::vector<std::uint8_t>& bytes,
                                                  std::uint32_t controlset) {
    if (bytes.size() < 4)
        return {};

    std::size_t start_offset = read_u32_le(bytes.data());
    return parse_windows10_cache_from_offset(bytes, controlset, start_offset);
}

// ============================================================================
// Windows 7 x64 Shimcache Parser
// ============================================================================

std::vector<ShimcacheEntry> parse_windows7x64_cache(const std::vector<std::uint8_t>& bytes,
                                                    std::uint32_t controlset) {
    std::vector<ShimcacheEntry> entries;

    if (bytes.size() < 132)
        return entries;

    // Entry count at offset 4
    std::uint32_t entry_count = read_u32_le(&bytes[4]);
    if (entry_count == 0)
        return entries;

    std::size_t index = 128;
    std::uint32_t cache_entry_position = 0;

    constexpr std::uint32_t EXECUTED_FLAG = 0x00000002;

    while (index + 48 < bytes.size() && entries.size() < entry_count) {
        // Path size
        std::uint16_t path_size = read_u16_le(&bytes[index]);
        index += 2;

        // Max path size (skip)
        index += 2;

        // Padding (skip)
        index += 4;

        // Path offset
        std::uint64_t path_offset = read_u64_le(&bytes[index]);
        index += 8;

        // Last modified timestamp
        std::uint64_t last_modified_win32 = read_u64_le(&bytes[index]);
        index += 8;

        // Insert flags
        std::uint32_t insert_flags = read_u32_le(&bytes[index]);
        index += 4;

        // Shim flags (skip)
        index += 4;

        // Data size
        std::uint64_t data_size = read_u64_le(&bytes[index]);
        index += 8;

        // Data offset
        std::uint64_t data_offset = read_u64_le(&bytes[index]);
        index += 8;

        // Read path
        std::string path;
        if (path_offset + path_size <= bytes.size()) {
            path = utf16le_to_utf8(&bytes[path_offset], path_size);
            // Remove \??\ prefix
            if (path.size() >= 4 && path.substr(0, 4) == "\\??\\") {
                path = path.substr(4);
            }
        }

        // Read data
        std::optional<std::vector<std::uint8_t>> data;
        if (data_size > 0 && data_offset + data_size <= bytes.size()) {
            data = std::vector<std::uint8_t>(
                bytes.begin() + static_cast<std::ptrdiff_t>(data_offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(data_offset + data_size));
        }

        std::optional<std::chrono::system_clock::time_point> last_modified_ts;
        if (last_modified_win32 != 0) {
            last_modified_ts = filetime_to_timepoint(last_modified_win32);
        }

        ShimcacheEntry entry;
        entry.cache_entry_position = cache_entry_position;
        entry.controlset = controlset;
        entry.data_size = static_cast<std::size_t>(data_size);
        entry.data = std::move(data);
        entry.entry_type = FileEntryType{path};
        entry.executed = (insert_flags & EXECUTED_FLAG) != 0;
        entry.last_modified_ts = last_modified_ts;
        entry.path_size = path_size;
        entry.signature = std::nullopt;

        entries.push_back(std::move(entry));
        ++cache_entry_position;
    }

    return entries;
}

// ============================================================================
// Windows 7 x86 Shimcache Parser
// ============================================================================

std::vector<ShimcacheEntry> parse_windows7x86_cache(const std::vector<std::uint8_t>& bytes,
                                                    std::uint32_t controlset) {
    std::vector<ShimcacheEntry> entries;

    if (bytes.size() < 132)
        return entries;

    std::uint32_t entry_count = read_u32_le(&bytes[4]);
    if (entry_count == 0)
        return entries;

    std::size_t index = 128;
    std::uint32_t cache_entry_position = 0;

    constexpr std::uint32_t EXECUTED_FLAG = 0x00000002;

    while (index + 32 < bytes.size() && entries.size() < entry_count) {
        std::uint16_t path_size = read_u16_le(&bytes[index]);
        index += 2;

        // Max path size (skip)
        index += 2;

        std::uint32_t path_offset = read_u32_le(&bytes[index]);
        index += 4;

        std::uint64_t last_modified_win32 = read_u64_le(&bytes[index]);
        index += 8;

        std::uint32_t insert_flags = read_u32_le(&bytes[index]);
        index += 4;

        // Shim flags (skip)
        index += 4;

        std::uint32_t data_size = read_u32_le(&bytes[index]);
        index += 4;

        std::uint32_t data_offset = read_u32_le(&bytes[index]);
        index += 4;

        std::string path;
        if (path_offset + path_size <= bytes.size()) {
            path = utf16le_to_utf8(&bytes[path_offset], path_size);
            if (path.size() >= 4 && path.substr(0, 4) == "\\??\\") {
                path = path.substr(4);
            }
        }

        std::optional<std::vector<std::uint8_t>> data;
        if (data_size > 0 && data_offset + data_size <= bytes.size()) {
            data = std::vector<std::uint8_t>(
                bytes.begin() + static_cast<std::ptrdiff_t>(data_offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(data_offset + data_size));
        }

        std::optional<std::chrono::system_clock::time_point> last_modified_ts;
        if (last_modified_win32 != 0) {
            last_modified_ts = filetime_to_timepoint(last_modified_win32);
        }

        ShimcacheEntry entry;
        entry.cache_entry_position = cache_entry_position;
        entry.controlset = controlset;
        entry.data_size = data_size;
        entry.data = std::move(data);
        entry.entry_type = FileEntryType{path};
        entry.executed = (insert_flags & EXECUTED_FLAG) != 0;
        entry.last_modified_ts = last_modified_ts;
        entry.path_size = path_size;
        entry.signature = std::nullopt;

        entries.push_back(std::move(entry));
        ++cache_entry_position;
    }

    return entries;
}

// ============================================================================
// Windows 8/8.1 Shimcache Parser
// ============================================================================

std::vector<ShimcacheEntry> parse_windows8_cache(const std::vector<std::uint8_t>& bytes,
                                                 std::uint32_t controlset) {
    std::vector<ShimcacheEntry> entries;

    if (bytes.size() < 136)
        return entries;

    // Проверяем сигнатуру на offset 128
    std::string cache_signature(reinterpret_cast<const char*>(&bytes[128]), 4);
    if (cache_signature != "00ts" && cache_signature != "10ts") {
        return entries;
    }

    std::size_t index = 128;
    std::uint32_t cache_entry_position = 0;

    constexpr std::uint32_t EXECUTED_FLAG = 0x00000002;

    while (index + 20 < bytes.size()) {
        // Проверяем сигнатуру
        std::string signature(reinterpret_cast<const char*>(&bytes[index]), 4);
        if (signature != cache_signature) {
            break;
        }
        index += 4;

        // Пропускаем 4 unknown
        index += 4;

        // Cache entry data size (skip)
        index += 4;

        // Path size
        if (index + 2 > bytes.size())
            break;
        std::uint16_t path_size = read_u16_le(&bytes[index]);
        index += 2;

        // Path
        if (index + path_size > bytes.size())
            break;
        std::string path = utf16le_to_utf8(&bytes[index], path_size);
        index += path_size;

        // Package length
        if (index + 2 > bytes.size())
            break;
        std::uint16_t package_len = read_u16_le(&bytes[index]);
        index += 2;

        // Skip package data
        index += package_len;

        // Insert flags
        if (index + 4 > bytes.size())
            break;
        std::uint32_t insert_flags = read_u32_le(&bytes[index]);
        index += 4;

        // Shim flags (skip)
        index += 4;

        // Last modified timestamp
        if (index + 8 > bytes.size())
            break;
        std::uint64_t last_modified_win32 = read_u64_le(&bytes[index]);
        index += 8;

        // Data size
        if (index + 4 > bytes.size())
            break;
        std::uint32_t data_size = read_u32_le(&bytes[index]);
        index += 4;

        // Data
        std::optional<std::vector<std::uint8_t>> data;
        if (data_size > 0 && index + data_size <= bytes.size()) {
            data = std::vector<std::uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(index),
                                             bytes.begin() +
                                                 static_cast<std::ptrdiff_t>(index + data_size));
            index += data_size;
        }

        // SYSVOL\ -> C:\ замена
        if (path.size() >= 7 && path.substr(0, 7) == "SYSVOL\\") {
            path = "C:\\" + path.substr(7);
        }

        std::optional<std::chrono::system_clock::time_point> last_modified_ts;
        if (last_modified_win32 != 0) {
            last_modified_ts = filetime_to_timepoint(last_modified_win32);
        }

        ShimcacheEntry entry;
        entry.cache_entry_position = cache_entry_position;
        entry.controlset = controlset;
        entry.data_size = data_size;
        entry.data = std::move(data);
        entry.entry_type = FileEntryType{path};
        entry.executed = (insert_flags & EXECUTED_FLAG) != 0;
        entry.last_modified_ts = last_modified_ts;
        entry.path_size = path_size;
        entry.signature = signature;

        entries.push_back(std::move(entry));
        ++cache_entry_position;
    }

    return entries;
}

}  // anonymous namespace

// ============================================================================
// Public Functions - String Conversions
// ============================================================================

const char* shimcache_version_to_string(ShimcacheVersion version) {
    switch (version) {
    case ShimcacheVersion::Unknown:
        return "Unknown";
    case ShimcacheVersion::Windows10:
        return "Windows 10";
    case ShimcacheVersion::Windows10Creators:
        return "Windows 10 Creators";
    case ShimcacheVersion::Windows7x64Windows2008R2:
        return "Windows 7 64-bit or Windows Server 2008 R2";
    case ShimcacheVersion::Windows7x86:
        return "Windows 7 32-bit";
    case ShimcacheVersion::Windows80Windows2012:
        return "Windows 8 or Windows Server 2012";
    case ShimcacheVersion::Windows81Windows2012R2:
        return "Windows 8.1 or Windows 2012 R2";
    case ShimcacheVersion::WindowsVistaWin2k3Win2k8:
        return "Windows Vista, Windows Server 2003 or Windows Server 2008";
    case ShimcacheVersion::WindowsXP:
        return "Windows XP";
    }
    return "Unknown";
}

CPUArchitecture cpu_architecture_from_u16(std::uint16_t value) {
    switch (value) {
    case 34404:
        return CPUArchitecture::Amd64;  // 0x8664
    case 452:
        return CPUArchitecture::Arm;  // 0x1C4
    case 332:
        return CPUArchitecture::I386;  // 0x14C
    case 512:
        return CPUArchitecture::Ia64;  // 0x200
    default:
        return CPUArchitecture::Unknown;
    }
}

const char* cpu_architecture_to_string(CPUArchitecture arch) {
    switch (arch) {
    case CPUArchitecture::Amd64:
        return "AMD64";
    case CPUArchitecture::Arm:
        return "ARM";
    case CPUArchitecture::I386:
        return "I386";
    case CPUArchitecture::Ia64:
        return "IA64";
    case CPUArchitecture::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

const char* timestamp_type_to_string(TimestampType type) {
    switch (type) {
    case TimestampType::AmcacheRangeMatch:
        return "AmcacheRangeMatch";
    case TimestampType::NearTSMatch:
        return "NearTSMatch";
    case TimestampType::PatternMatch:
        return "PatternMatch";
    case TimestampType::ShimcacheLastUpdate:
        return "ShimcacheLastUpdate";
    }
    return "Unknown";
}

// ============================================================================
// Entry Type Functions
// ============================================================================

bool is_file_entry(const EntryType& entry) {
    return std::holds_alternative<FileEntryType>(entry);
}

const std::string& get_entry_path(const EntryType& entry) {
    if (auto* file = std::get_if<FileEntryType>(&entry)) {
        return file->path;
    }
    static const std::string empty;
    return empty;
}

const std::string& get_entry_display_name(const EntryType& entry) {
    if (auto* file = std::get_if<FileEntryType>(&entry)) {
        return file->path;
    }
    if (auto* prog = std::get_if<ProgramEntryType>(&entry)) {
        return prog->program_name;
    }
    static const std::string empty;
    return empty;
}

// ============================================================================
// ShimcacheError
// ============================================================================

std::string ShimcacheError::format() const {
    return message;
}

// ============================================================================
// TimelineTimestamp
// ============================================================================

TimelineTimestamp TimelineTimestamp::make_exact(std::chrono::system_clock::time_point ts,
                                                TimestampType type) {
    TimelineTimestamp result;
    result.kind = Kind::Exact;
    result.exact_ts = ts;
    result.exact_type = type;
    return result;
}

TimelineTimestamp TimelineTimestamp::make_range(std::chrono::system_clock::time_point from,
                                                std::chrono::system_clock::time_point to) {
    TimelineTimestamp result;
    result.kind = Kind::Range;
    result.range_from = from;
    result.range_to = to;
    return result;
}

TimelineTimestamp TimelineTimestamp::make_range_start(std::chrono::system_clock::time_point ts) {
    TimelineTimestamp result;
    result.kind = Kind::RangeStart;
    result.exact_ts = ts;
    return result;
}

TimelineTimestamp TimelineTimestamp::make_range_end(std::chrono::system_clock::time_point ts) {
    TimelineTimestamp result;
    result.kind = Kind::RangeEnd;
    result.exact_ts = ts;
    return result;
}

std::chrono::system_clock::time_point TimelineTimestamp::display_timestamp() const {
    switch (kind) {
    case Kind::Exact:
        return exact_ts;
    case Kind::Range:
        return range_from;  // Или range_to, зависит от контекста
    case Kind::RangeStart:
    case Kind::RangeEnd:
        return exact_ts;
    }
    return {};
}

// ============================================================================
// TimelineEntity
// ============================================================================

TimelineEntity TimelineEntity::with_shimcache_entry(ShimcacheEntry entry) {
    TimelineEntity result;
    result.shimcache_entry = std::move(entry);
    return result;
}

// ============================================================================
// parse_shimcache
// ============================================================================

std::variant<ShimcacheArtefact, ShimcacheError> parse_shimcache(
    chainsaw::io::hve::HveParser& parser) {
    // Находим текущий ControlSet
    auto select_key = parser.get_key("Select");
    if (!select_key) {
        return ShimcacheError{ShimcacheErrorKind::KeyNotFound,
                              "Key \"Select\" not found in shimcache!"};
    }

    auto current_value = select_key->get_value("Current");
    if (!current_value) {
        return ShimcacheError{ShimcacheErrorKind::ValueNotFound,
                              "Value \"Current\" not found under key \"Select\" in shimcache!"};
    }

    auto controlset_opt = current_value->as_u32();
    if (!controlset_opt) {
        return ShimcacheError{
            ShimcacheErrorKind::InvalidType,
            "Value \"Current\" under key \"Select\" was not of type U32 in shimcache!"};
    }
    std::uint32_t controlset = *controlset_opt;

    // Формируем путь к AppCompatCache
    std::ostringstream path_stream;
    path_stream << "ControlSet" << std::setw(3) << std::setfill('0') << controlset
                << "\\Control\\Session Manager\\AppCompatCache";
    std::string shimcache_key_path = path_stream.str();

    auto shimcache_key = parser.get_key(shimcache_key_path);
    if (!shimcache_key) {
        return ShimcacheError{ShimcacheErrorKind::KeyNotFound,
                              "Could not find AppCompatCache with path " + shimcache_key_path +
                                  "!"};
    }

    auto shimcache_last_update_ts = shimcache_key->last_modified();

    auto appcompat_value = shimcache_key->get_value("AppCompatCache");
    if (!appcompat_value) {
        return ShimcacheError{ShimcacheErrorKind::ValueNotFound,
                              "Value \"AppCompatCache\" not found under key \"" +
                                  shimcache_key_path + "\"!"};
    }

    auto shimcache_bytes = appcompat_value->as_binary();
    if (!shimcache_bytes || shimcache_bytes->empty()) {
        return ShimcacheError{ShimcacheErrorKind::InvalidType,
                              "Shimcache value was not of type Binary!"};
    }

    const auto& bytes = *shimcache_bytes;

    // Поиск первого "10ts" для Windows 11 fallback
    std::size_t first_10ts = 0;
    for (std::size_t i = 0; i + 4 <= bytes.size(); ++i) {
        if (bytes[i] == '1' && bytes[i + 1] == '0' && bytes[i + 2] == 't' && bytes[i + 3] == 's') {
            first_10ts = i;
            break;
        }
    }

    // Определяем версию shimcache
    if (bytes.size() < 4) {
        return ShimcacheError{ShimcacheErrorKind::InvalidFormat, "Shimcache data too small!"};
    }

    std::uint32_t signature_number = read_u32_le(bytes.data());
    ShimcacheVersion shimcache_version = ShimcacheVersion::Unknown;

    if (signature_number == 0xdeadbeef) {
        shimcache_version = ShimcacheVersion::WindowsXP;
    } else if (signature_number == 0xbadc0ffe) {
        shimcache_version = ShimcacheVersion::WindowsVistaWin2k3Win2k8;
    } else if (signature_number == 0xbadc0fee) {
        // Windows 7 - проверяем архитектуру процессора
        std::ostringstream env_path;
        env_path << "ControlSet" << std::setw(3) << std::setfill('0') << controlset
                 << "\\Control\\Session Manager\\Environment";

        auto env_key = parser.get_key(env_path.str());
        bool is_32bit = false;

        if (env_key) {
            auto proc_arch = env_key->get_value("PROCESSOR_ARCHITECTURE");
            if (proc_arch) {
                if (auto* str = proc_arch->as_string()) {
                    is_32bit = (*str == "x86");
                }
            }
        }

        shimcache_version =
            is_32bit ? ShimcacheVersion::Windows7x86 : ShimcacheVersion::Windows7x64Windows2008R2;
    } else {
        // Проверяем Windows 8 сигнатуру
        if (bytes.size() >= 132) {
            std::string win8_sig(reinterpret_cast<const char*>(&bytes[128]), 4);
            if (win8_sig == "00ts") {
                shimcache_version = ShimcacheVersion::Windows80Windows2012;
            } else if (win8_sig == "10ts") {
                shimcache_version = ShimcacheVersion::Windows81Windows2012R2;
            } else {
                // Windows 10
                std::size_t offset_to_records = signature_number;
                if (offset_to_records + 4 <= bytes.size()) {
                    std::string win10_sig(reinterpret_cast<const char*>(&bytes[offset_to_records]),
                                          4);
                    if (win10_sig == "10ts") {
                        shimcache_version = (offset_to_records == 0x34)
                                                ? ShimcacheVersion::Windows10Creators
                                                : ShimcacheVersion::Windows10;
                    }
                }

                // Windows 11 fallback: search for first "10ts" in data
                if (shimcache_version == ShimcacheVersion::Unknown && first_10ts > 0) {
                    shimcache_version = ShimcacheVersion::Windows10;
                }
            }
        }
    }

    // Парсим записи
    std::vector<ShimcacheEntry> entries;

    switch (shimcache_version) {
    case ShimcacheVersion::Unknown:
        return ShimcacheError{ShimcacheErrorKind::InvalidFormat,
                              "Could not recognize shimcache version!"};

    case ShimcacheVersion::Windows10:
    case ShimcacheVersion::Windows10Creators:
        // Если first_10ts установлен и не соответствует offset из header,
        // используем first_10ts (Windows 11 fallback)
        if (first_10ts > 0) {
            std::size_t header_offset = read_u32_le(bytes.data());
            if (header_offset + 4 > bytes.size() || bytes[header_offset] != '1' ||
                bytes[header_offset + 1] != '0' || bytes[header_offset + 2] != 't' ||
                bytes[header_offset + 3] != 's') {
                entries = parse_windows10_cache_from_offset(bytes, controlset, first_10ts);
            } else {
                entries = parse_windows10_cache(bytes, controlset);
            }
        } else {
            entries = parse_windows10_cache(bytes, controlset);
        }
        break;

    case ShimcacheVersion::Windows7x64Windows2008R2:
        entries = parse_windows7x64_cache(bytes, controlset);
        break;

    case ShimcacheVersion::Windows7x86:
        entries = parse_windows7x86_cache(bytes, controlset);
        break;

    case ShimcacheVersion::Windows80Windows2012:
    case ShimcacheVersion::Windows81Windows2012R2:
        entries = parse_windows8_cache(bytes, controlset);
        break;

    case ShimcacheVersion::WindowsVistaWin2k3Win2k8:
        return ShimcacheError{ShimcacheErrorKind::UnsupportedVersion,
                              "Windows Vista shimcache parsing not supported!"};

    case ShimcacheVersion::WindowsXP:
        return ShimcacheError{ShimcacheErrorKind::UnsupportedVersion,
                              "Windows XP shimcache parsing not supported!"};
    }

    ShimcacheArtefact artefact;
    artefact.entries = std::move(entries);
    artefact.last_update_ts = shimcache_last_update_ts;
    artefact.version = shimcache_version;

    return artefact;
}

// ============================================================================
// parse_amcache
// ============================================================================

std::variant<AmcacheArtefact, ShimcacheError> parse_amcache(chainsaw::io::hve::HveParser& parser) {
    std::vector<AmcacheFileEntry> file_entries;
    std::vector<AmcacheProgramEntry> program_entries;

    // Проверяем новый формат (Windows 10+)
    bool is_new_format = parser.get_key("Root\\InventoryApplicationFile").has_value();

    if (is_new_format) {
        // InventoryApplication
        auto inv_app_key = parser.get_key("Root\\InventoryApplication");
        if (inv_app_key) {
            for (const auto& subkey_name : inv_app_key->subkey_names()) {
                auto subkey = parser.get_key("Root\\InventoryApplication\\" + subkey_name);
                if (!subkey)
                    continue;

                AmcacheProgramEntry entry;
                entry.last_modified_ts = subkey->last_modified();
                entry.program_id = subkey_name;

                if (auto name_val = subkey->get_value("Name")) {
                    if (auto* str = name_val->as_string()) {
                        entry.program_name = *str;
                    }
                }

                if (auto ver_val = subkey->get_value("Version")) {
                    if (auto* str = ver_val->as_string()) {
                        entry.version = *str;
                    }
                }

                if (auto root_val = subkey->get_value("RootDirPath")) {
                    if (auto* str = root_val->as_string()) {
                        entry.root_directory_path = *str;
                    }
                }

                if (auto uninstall_val = subkey->get_value("UninstallString")) {
                    if (auto* str = uninstall_val->as_string()) {
                        entry.uninstall_string = *str;
                    }
                }

                program_entries.push_back(std::move(entry));
            }
        }

        // InventoryApplicationFile
        auto inv_file_key = parser.get_key("Root\\InventoryApplicationFile");
        if (inv_file_key) {
            for (const auto& subkey_name : inv_file_key->subkey_names()) {
                auto subkey = parser.get_key("Root\\InventoryApplicationFile\\" + subkey_name);
                if (!subkey)
                    continue;

                AmcacheFileEntry entry;
                entry.key_last_modified_ts = subkey->last_modified();

                if (auto prog_id = subkey->get_value("ProgramId")) {
                    if (auto* str = prog_id->as_string()) {
                        entry.program_id = *str;
                    }
                }

                if (auto file_id = subkey->get_value("FileId")) {
                    if (auto* str = file_id->as_string()) {
                        entry.file_id = *str;
                        // SHA-1 hash: FileId = "0000" + SHA-1
                        if (str->size() == 44 && str->substr(0, 4) == "0000") {
                            entry.sha1_hash = str->substr(4);
                        }
                    }
                }

                if (auto path_val = subkey->get_value("LowerCaseLongPath")) {
                    if (auto* str = path_val->as_string()) {
                        entry.path = *str;
                    }
                }

                file_entries.push_back(std::move(entry));
            }
        }
    } else {
        // Старый формат
        auto programs_key = parser.get_key("Root\\Programs");
        if (programs_key) {
            for (const auto& subkey_name : programs_key->subkey_names()) {
                auto subkey = parser.get_key("Root\\Programs\\" + subkey_name);
                if (!subkey)
                    continue;

                AmcacheProgramEntry entry;
                entry.last_modified_ts = subkey->last_modified();
                entry.program_id = subkey_name;

                if (auto name_val = subkey->get_value("0")) {
                    if (auto* str = name_val->as_string()) {
                        entry.program_name = *str;
                    }
                }

                if (auto ver_val = subkey->get_value("1")) {
                    if (auto* str = ver_val->as_string()) {
                        entry.version = *str;
                    }
                }

                program_entries.push_back(std::move(entry));
            }
        }

        auto file_key = parser.get_key("Root\\File");
        if (file_key) {
            for (const auto& volume_name : file_key->subkey_names()) {
                auto volume_key = parser.get_key("Root\\File\\" + volume_name);
                if (!volume_key)
                    continue;

                for (const auto& file_name : volume_key->subkey_names()) {
                    auto subkey = parser.get_key("Root\\File\\" + volume_name + "\\" + file_name);
                    if (!subkey)
                        continue;

                    AmcacheFileEntry entry;
                    entry.key_last_modified_ts = subkey->last_modified();

                    if (auto prog_id = subkey->get_value("100")) {
                        if (auto* str = prog_id->as_string()) {
                            entry.program_id = *str;
                        }
                    }

                    if (auto file_id = subkey->get_value("101")) {
                        if (auto* str = file_id->as_string()) {
                            entry.file_id = *str;
                            if (str->size() == 44 && str->substr(0, 4) == "0000") {
                                entry.sha1_hash = str->substr(4);
                            }
                        }
                    }

                    if (auto path_val = subkey->get_value("15")) {
                        if (auto* str = path_val->as_string()) {
                            entry.path = *str;
                        }
                    }

                    file_entries.push_back(std::move(entry));
                }
            }
        }
    }

    AmcacheArtefact artefact;
    artefact.file_entries = std::move(file_entries);
    artefact.program_entries = std::move(program_entries);

    return artefact;
}

// ============================================================================
// ShimcacheAnalyser::Impl
// ============================================================================

struct ShimcacheAnalyser::Impl {
    std::filesystem::path shimcache_path;
    std::optional<std::filesystem::path> amcache_path;
    std::optional<ShimcacheError> last_error;
    ShimcacheVersion version = ShimcacheVersion::Unknown;
};

// ============================================================================
// ShimcacheAnalyser
// ============================================================================

ShimcacheAnalyser::ShimcacheAnalyser(std::filesystem::path shimcache_path,
                                     std::optional<std::filesystem::path> amcache_path)
    : impl_(std::make_unique<Impl>()) {
    impl_->shimcache_path = std::move(shimcache_path);
    impl_->amcache_path = std::move(amcache_path);
}

ShimcacheAnalyser::~ShimcacheAnalyser() = default;

ShimcacheAnalyser::ShimcacheAnalyser(ShimcacheAnalyser&&) noexcept = default;
ShimcacheAnalyser& ShimcacheAnalyser::operator=(ShimcacheAnalyser&&) noexcept = default;

const std::optional<ShimcacheError>& ShimcacheAnalyser::last_error() const {
    return impl_->last_error;
}

ShimcacheVersion ShimcacheAnalyser::shimcache_version() const {
    return impl_->version;
}

std::variant<std::vector<TimelineEntity>, ShimcacheError>
ShimcacheAnalyser::amcache_shimcache_timeline(const std::vector<std::string>& regex_patterns,
                                              bool ts_near_pair_matching) {
    // Компилируем regex паттерны
    std::vector<std::regex> regexes;
    regexes.reserve(regex_patterns.size());
    for (const auto& pattern : regex_patterns) {
        try {
            regexes.emplace_back(pattern, std::regex::icase);
        } catch (const std::regex_error& e) {
            impl_->last_error = ShimcacheError{ShimcacheErrorKind::ParseError,
                                               std::string("Invalid regex pattern: ") + e.what()};
            return *impl_->last_error;
        }
    }

    // Загружаем shimcache
    chainsaw::io::hve::HveParser shimcache_parser;
    if (!shimcache_parser.load(impl_->shimcache_path)) {
        impl_->last_error =
            ShimcacheError{ShimcacheErrorKind::ParseError,
                           "Failed to load shimcache hive: " +
                               chainsaw::platform::path_to_utf8(impl_->shimcache_path)};
        return *impl_->last_error;
    }

    auto shimcache_result = parse_shimcache(shimcache_parser);
    if (auto* error = std::get_if<ShimcacheError>(&shimcache_result)) {
        impl_->last_error = *error;
        return *error;
    }
    auto shimcache = std::get<ShimcacheArtefact>(std::move(shimcache_result));
    impl_->version = shimcache.version;

    // Загружаем amcache (опционально)
    std::optional<AmcacheArtefact> amcache;
    if (impl_->amcache_path) {
        chainsaw::io::hve::HveParser amcache_parser;
        if (amcache_parser.load(*impl_->amcache_path)) {
            auto amcache_result = parse_amcache(amcache_parser);
            if (auto* artefact = std::get_if<AmcacheArtefact>(&amcache_result)) {
                amcache = std::move(*artefact);
            }
        }
    }

    // Создаём timeline entities
    std::vector<TimelineEntity> timeline_entities;
    timeline_entities.reserve(shimcache.entries.size() + 1);

    // Первый элемент — shimcache last update
    TimelineEntity first_entity;
    first_entity.timestamp =
        TimelineTimestamp::make_exact(shimcache.last_update_ts, TimestampType::ShimcacheLastUpdate);
    timeline_entities.push_back(std::move(first_entity));

    // Добавляем shimcache entries
    for (auto& entry : shimcache.entries) {
        timeline_entities.push_back(TimelineEntity::with_shimcache_entry(std::move(entry)));
    }

    // Pattern matching
    for (auto& entity : timeline_entities) {
        if (!entity.shimcache_entry)
            continue;
        const auto& entry = *entity.shimcache_entry;

        if (!is_file_entry(entry.entry_type))
            continue;
        const auto& path = get_entry_path(entry.entry_type);
        std::string path_lower = to_lowercase(path);

        for (const auto& re : regexes) {
            if (std::regex_search(path_lower, re)) {
                if (entry.last_modified_ts) {
                    entity.timestamp = TimelineTimestamp::make_exact(*entry.last_modified_ts,
                                                                     TimestampType::PatternMatch);
                }
                break;
            }
        }
    }

    // Получаем индексы с Exact timestamps
    auto get_exact_ts_indices = [](const std::vector<TimelineEntity>& entities) {
        std::vector<std::size_t> indices;
        for (std::size_t i = 0; i < entities.size(); ++i) {
            if (entities[i].timestamp &&
                entities[i].timestamp->kind == TimelineTimestamp::Kind::Exact) {
                indices.push_back(i);
            }
        }
        return indices;
    };

    // Устанавливаем timestamp ranges
    auto set_timestamp_ranges = [](const std::vector<std::size_t>& range_indices,
                                   std::vector<TimelineEntity>& entities) {
        if (range_indices.empty())
            return;

        std::size_t first_index = range_indices.front();

        // Entities до первого known timestamp получают RangeStart
        if (first_index > 0) {
            auto ts = entities[first_index].timestamp->display_timestamp();
            for (std::size_t i = 0; i < first_index; ++i) {
                entities[i].timestamp = TimelineTimestamp::make_range_start(ts);
            }
        }

        // Entities между known timestamps получают Range
        for (std::size_t p = 0; p + 1 < range_indices.size(); ++p) {
            std::size_t start_i = range_indices[p];
            std::size_t end_i = range_indices[p + 1];

            auto from_ts = entities[end_i].timestamp->display_timestamp();
            auto to_ts = entities[start_i].timestamp->display_timestamp();

            for (std::size_t i = start_i + 1; i < end_i; ++i) {
                entities[i].timestamp = TimelineTimestamp::make_range(from_ts, to_ts);
            }
        }

        // Entities после последнего known timestamp получают RangeEnd
        std::size_t last_index = range_indices.back();
        if (last_index + 1 < entities.size()) {
            auto ts = entities[last_index].timestamp->display_timestamp();
            for (std::size_t i = last_index + 1; i < entities.size(); ++i) {
                entities[i].timestamp = TimelineTimestamp::make_range_end(ts);
            }
        }
    };

    // Устанавливаем ranges на основе pattern matching
    set_timestamp_ranges(get_exact_ts_indices(timeline_entities), timeline_entities);

    // Amcache enrichment
    if (amcache) {
        // Match file entries
        for (const auto& file_entry : amcache->file_entries) {
            auto file_entry_ptr = std::make_shared<AmcacheFileEntry>(file_entry);
            std::string file_path_lower = to_lowercase(file_entry.path);

            for (auto& entity : timeline_entities) {
                if (!entity.shimcache_entry)
                    continue;
                if (!is_file_entry(entity.shimcache_entry->entry_type))
                    continue;

                const auto& path = get_entry_path(entity.shimcache_entry->entry_type);
                if (to_lowercase(path) == file_path_lower) {
                    entity.amcache_file = file_entry_ptr;
                }
            }
        }

        // Match program entries
        for (const auto& prog_entry : amcache->program_entries) {
            auto prog_entry_ptr = std::make_shared<AmcacheProgramEntry>(prog_entry);

            for (auto& entity : timeline_entities) {
                if (!entity.shimcache_entry)
                    continue;
                auto* prog = std::get_if<ProgramEntryType>(&entity.shimcache_entry->entry_type);
                if (!prog)
                    continue;

                if (prog->program_name == prog_entry.program_name &&
                    prog->program_version == prog_entry.version) {
                    entity.amcache_program = prog_entry_ptr;
                }
            }
        }

        // Near timestamp pair matching
        if (ts_near_pair_matching) {
            constexpr std::int64_t MAX_TIME_DIFFERENCE_MS = 60 * 1000;  // 1 минута

            for (auto& entity : timeline_entities) {
                if (!entity.shimcache_entry || !entity.amcache_file)
                    continue;
                if (!entity.shimcache_entry->last_modified_ts)
                    continue;

                auto shimcache_ts = *entity.shimcache_entry->last_modified_ts;
                auto amcache_ts = entity.amcache_file->key_last_modified_ts;

                auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(shimcache_ts -
                                                                                  amcache_ts);
                if (std::abs(diff.count()) > MAX_TIME_DIFFERENCE_MS)
                    continue;

                // Не перезаписываем PatternMatch
                if (entity.timestamp && entity.timestamp->kind == TimelineTimestamp::Kind::Exact &&
                    entity.timestamp->exact_type == TimestampType::PatternMatch) {
                    continue;
                }

                entity.timestamp =
                    TimelineTimestamp::make_exact(amcache_ts, TimestampType::NearTSMatch);
            }

            // Пересчитываем ranges
            set_timestamp_ranges(get_exact_ts_indices(timeline_entities), timeline_entities);
        }

        // Amcache range match
        for (auto& entity : timeline_entities) {
            if (!entity.shimcache_entry || !entity.amcache_file)
                continue;
            if (!is_file_entry(entity.shimcache_entry->entry_type))
                continue;
            if (!entity.timestamp || entity.timestamp->kind != TimelineTimestamp::Kind::Range)
                continue;

            auto amcache_ts = entity.amcache_file->key_last_modified_ts;
            if (entity.timestamp->range_from < amcache_ts &&
                amcache_ts < entity.timestamp->range_to) {
                entity.timestamp =
                    TimelineTimestamp::make_exact(amcache_ts, TimestampType::AmcacheRangeMatch);
            }
        }

        // Финальный пересчёт ranges
        set_timestamp_ranges(get_exact_ts_indices(timeline_entities), timeline_entities);
    }

    return timeline_entities;
}

// ============================================================================
// Output Helpers
// ============================================================================

std::string format_timestamp_rfc3339(const std::chrono::system_clock::time_point& ts) {
    auto time = std::chrono::system_clock::to_time_t(ts);
    auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(ts.time_since_epoch()) % 1000000;

    std::tm tm_utc{};
#ifdef _WIN32
    gmtime_s(&tm_utc, &time);
#else
    gmtime_r(&time, &tm_utc);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0') << std::setw(6)
        << micros.count() << 'Z';
    return oss.str();
}

std::string get_csv_header() {
    return "Timestamp,File Path,Program Name,SHA-1 Hash,Timeline Entry Number,"
           "Entry Type,Timestamp Description,Raw Entry";
}

std::string format_timeline_entity_csv(const TimelineEntity& entity, std::size_t entry_number) {
    std::ostringstream oss;

    // Timestamp
    if (entity.timestamp) {
        oss << format_timestamp_rfc3339(entity.timestamp->display_timestamp());
    }
    oss << ",";

    // File Path
    if (entity.shimcache_entry) {
        const auto& path = get_entry_path(entity.shimcache_entry->entry_type);
        // Escape quotes in path
        std::string escaped_path;
        for (char c : path) {
            if (c == '"')
                escaped_path += "\"\"";
            else
                escaped_path += c;
        }
        oss << "\"" << escaped_path << "\"";
    }
    oss << ",";

    // Program Name
    if (entity.shimcache_entry) {
        if (auto* prog = std::get_if<ProgramEntryType>(&entity.shimcache_entry->entry_type)) {
            oss << "\"" << prog->program_name << "\"";
        }
    }
    oss << ",";

    // SHA-1 Hash
    if (entity.amcache_file && entity.amcache_file->sha1_hash) {
        oss << *entity.amcache_file->sha1_hash;
    }
    oss << ",";

    // Timeline Entry Number
    oss << entry_number << ",";

    // Entry Type
    if (entity.shimcache_entry) {
        oss << (is_file_entry(entity.shimcache_entry->entry_type) ? "File" : "Program");
    } else {
        oss << "ShimcacheLastUpdate";
    }
    oss << ",";

    // Timestamp Description
    if (entity.timestamp) {
        switch (entity.timestamp->kind) {
        case TimelineTimestamp::Kind::Exact:
            oss << timestamp_type_to_string(entity.timestamp->exact_type);
            break;
        case TimelineTimestamp::Kind::Range:
            oss << "Range";
            break;
        case TimelineTimestamp::Kind::RangeStart:
            oss << "RangeStart";
            break;
        case TimelineTimestamp::Kind::RangeEnd:
            oss << "RangeEnd";
            break;
        }
    }
    oss << ",";

    // Raw Entry
    if (entity.shimcache_entry) {
        if (auto* prog = std::get_if<ProgramEntryType>(&entity.shimcache_entry->entry_type)) {
            std::string escaped_raw;
            for (char c : prog->raw_entry) {
                if (c == '"')
                    escaped_raw += "\"\"";
                else
                    escaped_raw += c;
            }
            oss << "\"" << escaped_raw << "\"";
        }
    }

    return oss.str();
}

}  // namespace chainsaw::analyse::shimcache
