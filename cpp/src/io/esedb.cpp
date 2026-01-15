// ==============================================================================
// esedb.cpp - Собственная реализация ESEDB Parser (ESE Database)
// ==============================================================================
//
// MOD-0010 io::esedb
// SLICE-016: ESEDB Parser Implementation
// SPEC-SLICE-016: micro-spec поведения
//
// Собственная кроссплатформенная реализация парсера ESE Database.
// Не требует внешних зависимостей (libesedb).
//
// Формат ESE DB:
// - Используется Windows для: SRUM, Windows Search, Exchange, AD
// - Формат файлов: .edb, .dat (SRUDB.dat)
// - Структура: B+ tree с переменным размером страниц
//
// Источники спецификации:
// - https://github.com/libyal/libesedb/blob/main/documentation/
// - https://github.com/Velocidex/go-ese
// - https://learn.microsoft.com/en-us/windows/win32/extensible-storage-engine/
//
// ==============================================================================

#include <algorithm>
#include <chainsaw/esedb.hpp>
#include <chainsaw/platform.hpp>
#include <chainsaw/reader.hpp>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>

// GCC 13 generates false positives for -Wfree-nonheap-object when inlining
// vector iterator operations at high optimization levels.
// See: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=109224
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
#endif

namespace chainsaw::io::esedb {

// ============================================================================
// EsedbError
// ============================================================================

std::string EsedbError::format() const {
    return "[!] ESEDB error: " + message + "\n";
}

// ============================================================================
// OLE Time Conversion
// ============================================================================
//
// SPEC-SLICE-016 FACT-007: DateTime -> OleTime -> ISO8601
// OLE Automation Date: количество дней с 30 декабря 1899 года
// Дробная часть — время дня (0.5 = полдень)
//

// Константы для OLE Time
// OLE epoch: 30 December 1899 00:00:00 UTC
// Unix epoch: 1 January 1970 00:00:00 UTC
// Разница: 25569 дней
constexpr double OLE_TO_UNIX_EPOCH_DAYS = 25569.0;
constexpr double SECONDS_PER_DAY = 86400.0;

std::string ole_time_to_iso8601(double ole_time) {
    // Валидация входных данных: проверка на NaN, Infinity и допустимые границы
    // OLE epoch (0.0) = 30 December 1899
    // Минимум для Unix: OLE_TO_UNIX_EPOCH_DAYS (Unix epoch 1970)
    // Максимум: ~2500 год (разумный лимит для SRUM данных)
    if (std::isnan(ole_time) || std::isinf(ole_time)) {
        return "";
    }

    // Проверка границ: от Unix epoch до ~2500 года
    // Unix epoch в OLE: 25569.0 (1 January 1970)
    // 2500 год в OLE: ~219146.0
    constexpr double MIN_VALID_OLE = OLE_TO_UNIX_EPOCH_DAYS;  // 1970-01-01
    constexpr double MAX_VALID_OLE = 219146.0;                // ~2500 год
    if (ole_time < MIN_VALID_OLE || ole_time > MAX_VALID_OLE) {
        return "";
    }

    // Конвертируем OLE time в Unix timestamp
    double unix_days = ole_time - OLE_TO_UNIX_EPOCH_DAYS;
    auto unix_seconds = static_cast<std::int64_t>(unix_days * SECONDS_PER_DAY);

    // Дополнительная проверка на overflow после умножения
    if (unix_seconds < 0 || unix_seconds > 16725225600LL) {  // ~2500 год
        return "";
    }

    // Используем std::gmtime для конверсии
    std::time_t time = static_cast<std::time_t>(unix_seconds);
    std::tm* tm = std::gmtime(&time);

    if (tm == nullptr) {
        return "";
    }

    // Форматируем как ISO8601 (RFC3339 без дробных секунд)
    // SPEC-SLICE-016 FACT-008: datetime.to_rfc3339_opts(SecondsFormat::Secs, true)
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << (tm->tm_year + 1900) << "-" << std::setw(2)
        << (tm->tm_mon + 1) << "-" << std::setw(2) << tm->tm_mday << "T" << std::setw(2)
        << tm->tm_hour << ":" << std::setw(2) << tm->tm_min << ":" << std::setw(2) << tm->tm_sec
        << "Z";

    return oss.str();
}

// FILETIME: 100-nanosecond intervals since January 1, 1601 UTC
// Unix epoch: January 1, 1970 UTC
// Разница: 116444736000000000 100-ns intervals
constexpr std::int64_t FILETIME_TO_UNIX_EPOCH = 116444736000000000LL;

std::string filetime_to_iso8601(std::int64_t filetime) {
    // Валидация входных данных: FILETIME должен быть >= Unix epoch
    // FILETIME_TO_UNIX_EPOCH = 116444736000000000 (1970-01-01)
    // Максимум: ~2500 год = 283681119990000000
    constexpr std::int64_t MAX_VALID_FILETIME = 283681119990000000LL;
    if (filetime < FILETIME_TO_UNIX_EPOCH || filetime > MAX_VALID_FILETIME) {
        return "";
    }

    // Конвертируем FILETIME в Unix timestamp (секунды)
    std::int64_t unix_100ns = filetime - FILETIME_TO_UNIX_EPOCH;
    std::int64_t unix_seconds = unix_100ns / 10000000LL;

    // Проверка на разумные границы (0 до ~2500 года)
    if (unix_seconds < 0 || unix_seconds > 16725225600LL) {
        return "";
    }

    std::time_t time = static_cast<std::time_t>(unix_seconds);
    std::tm* tm = std::gmtime(&time);

    if (tm == nullptr) {
        return "";
    }

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << (tm->tm_year + 1900) << "-" << std::setw(2)
        << (tm->tm_mon + 1) << "-" << std::setw(2) << tm->tm_mday << "T" << std::setw(2)
        << tm->tm_hour << ":" << std::setw(2) << tm->tm_min << ":" << std::setw(2) << tm->tm_sec
        << "Z";

    return oss.str();
}

// ============================================================================
// ESE Constants
// ============================================================================

// Сигнатура ESE файла
constexpr std::uint32_t ESE_SIGNATURE = 0x89ABCDEF;

// Page flags
constexpr std::uint32_t PAGE_FLAG_ROOT = 0x0001;
constexpr std::uint32_t PAGE_FLAG_LEAF = 0x0002;
constexpr std::uint32_t PAGE_FLAG_PARENT = 0x0004;
constexpr std::uint32_t PAGE_FLAG_EMPTY = 0x0008;
constexpr std::uint32_t PAGE_FLAG_SPACE_TREE = 0x0020;
constexpr std::uint32_t PAGE_FLAG_INDEX = 0x0040;
constexpr std::uint32_t PAGE_FLAG_LONG_VALUE = 0x0080;

// Catalog page number
[[maybe_unused]] constexpr std::uint32_t CATALOG_PAGE_NUMBER = 4;

// Column types (JET_coltyp)
enum class JetColtyp : std::uint32_t {
    Nil = 0,
    Bit = 1,
    UnsignedByte = 2,
    Short = 3,
    Long = 4,
    Currency = 5,
    IEEESingle = 6,
    IEEEDouble = 7,
    DateTime = 8,
    Binary = 9,
    Text = 10,
    LongBinary = 11,
    LongText = 12,
    SLV = 13,
    UnsignedLong = 14,
    LongLong = 15,
    GUID = 16,
    UnsignedShort = 17
};

// Catalog entry types
enum class CatalogType : std::uint16_t {
    Table = 1,
    Column = 2,
    Index = 3,
    LongValue = 4,
    Callback = 5
};

// ============================================================================
// Low-level binary reading helpers
// ============================================================================

inline std::uint8_t read_u8(const std::uint8_t* data) {
    return data[0];
}

inline std::int8_t read_i8(const std::uint8_t* data) {
    return static_cast<std::int8_t>(data[0]);
}

inline std::uint16_t read_u16_le(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}

inline std::int16_t read_i16_le(const std::uint8_t* data) {
    return static_cast<std::int16_t>(read_u16_le(data));
}

inline std::uint32_t read_u32_le(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

inline std::int32_t read_i32_le(const std::uint8_t* data) {
    return static_cast<std::int32_t>(read_u32_le(data));
}

inline std::uint64_t read_u64_le(const std::uint8_t* data) {
    return static_cast<std::uint64_t>(data[0]) | (static_cast<std::uint64_t>(data[1]) << 8) |
           (static_cast<std::uint64_t>(data[2]) << 16) |
           (static_cast<std::uint64_t>(data[3]) << 24) |
           (static_cast<std::uint64_t>(data[4]) << 32) |
           (static_cast<std::uint64_t>(data[5]) << 40) |
           (static_cast<std::uint64_t>(data[6]) << 48) |
           (static_cast<std::uint64_t>(data[7]) << 56);
}

inline std::int64_t read_i64_le(const std::uint8_t* data) {
    return static_cast<std::int64_t>(read_u64_le(data));
}

inline float read_f32_le(const std::uint8_t* data) {
    std::uint32_t u = read_u32_le(data);
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

inline double read_f64_le(const std::uint8_t* data) {
    std::uint64_t u = read_u64_le(data);
    double d;
    std::memcpy(&d, &u, sizeof(d));
    return d;
}

// ============================================================================
// ESE Structures
// ============================================================================

// Page header (40 bytes for small pages, extended for large pages)
struct PageHeader {
    std::uint32_t checksum;
    std::uint32_t page_number;
    std::uint64_t db_time;
    std::uint32_t previous_page;
    std::uint32_t next_page;
    std::uint32_t father_dp_obj_id;
    std::uint16_t available_data_size;
    std::uint16_t available_uncommitted_data_size;
    std::uint16_t available_data_offset;
    std::uint16_t available_page_tag;
    std::uint32_t page_flags;

    bool is_root() const { return (page_flags & PAGE_FLAG_ROOT) != 0; }
    bool is_leaf() const { return (page_flags & PAGE_FLAG_LEAF) != 0; }
    bool is_branch() const { return (page_flags & PAGE_FLAG_PARENT) != 0; }
    bool is_empty() const { return (page_flags & PAGE_FLAG_EMPTY) != 0; }
    bool is_space_tree() const { return (page_flags & PAGE_FLAG_SPACE_TREE) != 0; }
    bool is_index() const { return (page_flags & PAGE_FLAG_INDEX) != 0; }
    bool is_long_value() const { return (page_flags & PAGE_FLAG_LONG_VALUE) != 0; }
};

// Page tag entry - указывает на данные внутри страницы
struct PageTag {
    std::uint16_t offset;
    std::uint16_t size;
    std::uint16_t flags;
};

// Column specification from catalog
struct ColumnSpec {
    std::uint32_t id;
    std::string name;
    JetColtyp type;
    std::uint32_t flags;
    std::uint32_t space_usage;
    std::uint16_t codepage;
};

// Table specification from catalog
struct TableSpec {
    std::uint32_t obj_id;
    std::string name;
    std::uint32_t fdp_id;
    std::vector<ColumnSpec> columns;
    std::uint32_t lv_fdp_id;  // Long value FDP
};

// ============================================================================
// Native ESE Parser Implementation
// ============================================================================

class EsedbParser::Impl {
public:
    Impl() = default;

    ~Impl() { close(); }

    bool load(const std::filesystem::path& file_path) {
        close();
        path_ = file_path;

        // Проверяем существование файла
        std::error_code ec;
        if (!std::filesystem::exists(file_path, ec) || ec) {
            error_ = EsedbError{EsedbErrorKind::FileNotFound,
                                "file not found: " + platform::path_to_utf8(file_path)};
            return false;
        }

        // Открываем файл
        file_.open(file_path, std::ios::binary);
        if (!file_.is_open()) {
            error_ = EsedbError{EsedbErrorKind::OpenError,
                                "failed to open file: " + platform::path_to_utf8(file_path)};
            return false;
        }

        // Читаем и проверяем заголовок файла
        if (!read_file_header()) {
            return false;
        }

        // Читаем каталог (схему базы данных)
        if (!read_catalog()) {
            return false;
        }

        loaded_ = true;
        return true;
    }

    void close() {
        if (file_.is_open()) {
            file_.close();
        }
        loaded_ = false;
        parsed_entries_.clear();
        current_index_ = 0;
        tables_.clear();
        page_size_ = 0;
        format_revision_ = 0;
    }

    bool is_loaded() const { return loaded_; }
    const std::optional<EsedbError>& last_error() const { return error_; }
    const std::filesystem::path& path() const { return path_; }

    // Парсинг всех записей
    std::vector<std::unordered_map<std::string, Value>> parse() {
        if (!loaded_) {
            return {};
        }

        parsed_entries_.clear();

        // Итерируем по всем таблицам
        for (const auto& table : tables_) {
            parse_table(table);
        }

        current_index_ = 0;
        return parsed_entries_;
    }

    // Парсинг SruDbIdMapTable
    std::unordered_map<std::string, SruDbIdMapTableEntry> parse_sru_db_id_map_table() {
        std::unordered_map<std::string, SruDbIdMapTableEntry> result;

        // Сначала парсим все записи если ещё не парсили
        if (parsed_entries_.empty()) {
            parse();
        }

        // Фильтруем записи где Table == "SruDbIdMapTable"
        for (const auto& entry : parsed_entries_) {
            auto table_it = entry.find("Table");
            if (table_it == entry.end())
                continue;

            const std::string* table_name = table_it->second.get_string();
            if (table_name == nullptr || *table_name != "SruDbIdMapTable")
                continue;

            SruDbIdMapTableEntry sru_entry;

            // IdType
            auto id_type_it = entry.find("IdType");
            if (id_type_it != entry.end()) {
                const auto& val = id_type_it->second;
                if (val.is_number()) {
                    if (auto* int_ptr = val.get_int()) {
                        sru_entry.id_type = static_cast<std::int8_t>(*int_ptr);
                    } else if (auto* uint_ptr = val.get_uint()) {
                        sru_entry.id_type = static_cast<std::int8_t>(*uint_ptr);
                    } else if (auto* dbl_ptr = val.get_double()) {
                        sru_entry.id_type = static_cast<std::int8_t>(*dbl_ptr);
                    }
                }
            }

            // IdIndex
            auto id_index_it = entry.find("IdIndex");
            if (id_index_it != entry.end()) {
                const auto& val2 = id_index_it->second;
                if (val2.is_number()) {
                    if (auto* int_ptr = val2.get_int()) {
                        sru_entry.id_index = static_cast<std::int32_t>(*int_ptr);
                    } else if (auto* uint_ptr = val2.get_uint()) {
                        sru_entry.id_index = static_cast<std::int32_t>(*uint_ptr);
                    } else if (auto* dbl_ptr = val2.get_double()) {
                        sru_entry.id_index = static_cast<std::int32_t>(*dbl_ptr);
                    }
                }
            }

            // IdBlob
            auto id_blob_it = entry.find("IdBlob");
            if (id_blob_it != entry.end() && !id_blob_it->second.is_null()) {
                if (auto* arr = id_blob_it->second.get_array()) {
                    std::vector<std::uint8_t> blob;
                    blob.reserve(arr->size());
                    for (const auto& v : *arr) {
                        if (v.is_number()) {
                            if (auto* int_v = v.get_int()) {
                                blob.push_back(static_cast<std::uint8_t>(*int_v));
                            } else if (auto* uint_v = v.get_uint()) {
                                blob.push_back(static_cast<std::uint8_t>(*uint_v));
                            } else if (auto* dbl_v = v.get_double()) {
                                blob.push_back(static_cast<std::uint8_t>(*dbl_v));
                            }
                        }
                    }
                    sru_entry.id_blob = std::move(blob);
                }
            }

            // id_type != 3 -> convert to string
            if (sru_entry.id_type != 3 && sru_entry.id_blob.has_value()) {
                std::string blob_str;
                blob_str.reserve(sru_entry.id_blob->size());
                for (std::uint8_t byte : *sru_entry.id_blob) {
                    // Пропускаем null characters
                    if (byte != 0) {
                        blob_str.push_back(static_cast<char>(byte));
                    }
                }
                sru_entry.id_blob_as_string = std::move(blob_str);
            }

            result[std::to_string(sru_entry.id_index)] = std::move(sru_entry);
        }

        return result;
    }

    bool has_next() const { return current_index_ < parsed_entries_.size(); }

    bool eof() const { return current_index_ >= parsed_entries_.size(); }

    bool next(std::unordered_map<std::string, Value>& out) {
        if (current_index_ >= parsed_entries_.size()) {
            return false;
        }
        out = parsed_entries_[current_index_];
        ++current_index_;
        return true;
    }

private:
    // -------------------------------------------------------------------------
    // File Header Reading
    // -------------------------------------------------------------------------

    bool read_file_header() {
        // Читаем первую страницу (минимум 4096 байт)
        std::vector<std::uint8_t> header(4096);
        file_.seekg(0, std::ios::beg);
        file_.read(reinterpret_cast<char*>(header.data()),
                   static_cast<std::streamsize>(header.size()));

        if (file_.gcount() < 240) {
            error_ = EsedbError{EsedbErrorKind::OpenError,  // Not a valid ESE database
                                "file too small for ESE header"};
            return false;
        }

        // Проверяем сигнатуру (offset 4)
        std::uint32_t signature = read_u32_le(header.data() + 4);
        if (signature != ESE_SIGNATURE) {
            error_ = EsedbError{EsedbErrorKind::OpenError,  // Not a valid ESE database
                                "invalid ESE signature: expected 0x89ABCDEF, got 0x" +
                                    to_hex(signature)};
            return false;
        }

        // Читаем версию и revision (offset 8-11)
        format_version_ = read_u32_le(header.data() + 8);
        format_revision_ = read_u32_le(header.data() + 12);

        // Читаем размер страницы (offset 236)
        page_size_ = read_u32_le(header.data() + 236);

        // Валидация размера страницы
        if (page_size_ != 0x1000 && page_size_ != 0x2000 && page_size_ != 0x4000 &&
            page_size_ != 0x8000) {
            // Некоторые старые форматы имеют page_size = 0
            // В этом случае используем 4096 как default
            if (page_size_ == 0) {
                page_size_ = 0x1000;  // 4096
            } else {
                error_ = EsedbError{EsedbErrorKind::OpenError,  // Invalid database format
                                    "invalid page size: " + std::to_string(page_size_)};
                return false;
            }
        }

        return true;
    }

    // -------------------------------------------------------------------------
    // Page Reading
    // -------------------------------------------------------------------------

    std::vector<std::uint8_t> read_page(std::uint32_t page_num) {
        // ESE page numbering: page_num=N maps to file offset (N+1)*page_size
        // Pages 0 and 1 are file headers (main and shadow)
        // Logical page 0 starts at file offset page_size (after headers)

        // Защита от integer overflow при расчёте смещения
        // Используем 64-битную арифметику для безопасности
        std::uint64_t page_offset_u64 = static_cast<std::uint64_t>(page_num) + 1;
        page_offset_u64 *= page_size_;

        // Проверяем, что смещение не превышает максимальное значение streamoff
        if (page_offset_u64 >
            static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
            return {};  // Слишком большое смещение
        }

        std::streamoff offset = static_cast<std::streamoff>(page_offset_u64);

        // Clear any error flags before seeking
        file_.clear();

        std::vector<std::uint8_t> page(page_size_);
        file_.seekg(offset, std::ios::beg);
        file_.read(reinterpret_cast<char*>(page.data()), page_size_);

        if (file_.gcount() < static_cast<std::streamsize>(page_size_)) {
            return {};
        }

        return page;
    }

    PageHeader parse_page_header(const std::vector<std::uint8_t>& page) {
        PageHeader hdr{};

        // Для extended format (revision >= 0x11) заголовок больше
        bool is_extended = (format_revision_ >= 0x11) && (page_size_ > 8192);

        // Проверяем минимальный размер заголовка
        std::size_t min_header_size = is_extended ? 80 : 40;
        if (page.size() < min_header_size) {
            return hdr;
        }

        if (is_extended) {
            // Extended page header (80 bytes)
            // checksum1 (8), checksum2 (8), checksum3 (8), page_num (8), unknown (8)
            // + standard fields at offset 40
            hdr.checksum = read_u32_le(page.data());
            hdr.page_number = static_cast<std::uint32_t>(read_u64_le(page.data() + 24));
            // Остальные поля в другом месте для extended
            hdr.previous_page = read_u32_le(page.data() + 40 + 8);
            hdr.next_page = read_u32_le(page.data() + 40 + 12);
            hdr.father_dp_obj_id = read_u32_le(page.data() + 40 + 16);
            hdr.available_data_size = read_u16_le(page.data() + 40 + 20);
            hdr.available_uncommitted_data_size = read_u16_le(page.data() + 40 + 22);
            hdr.available_data_offset = read_u16_le(page.data() + 40 + 24);
            hdr.available_page_tag = read_u16_le(page.data() + 40 + 26);
            hdr.page_flags = read_u32_le(page.data() + 40 + 28);
        } else {
            // Standard page header (40 bytes)
            hdr.checksum = read_u32_le(page.data());
            hdr.page_number = read_u32_le(page.data() + 4);
            hdr.db_time = read_u64_le(page.data() + 8);
            hdr.previous_page = read_u32_le(page.data() + 16);
            hdr.next_page = read_u32_le(page.data() + 20);
            hdr.father_dp_obj_id = read_u32_le(page.data() + 24);
            hdr.available_data_size = read_u16_le(page.data() + 28);
            hdr.available_uncommitted_data_size = read_u16_le(page.data() + 30);
            hdr.available_data_offset = read_u16_le(page.data() + 32);
            hdr.available_page_tag = read_u16_le(page.data() + 34);
            hdr.page_flags = read_u32_le(page.data() + 36);
        }

        return hdr;
    }

    std::size_t page_header_size() const {
        bool is_extended = (format_revision_ >= 0x11) && (page_size_ > 8192);
        return is_extended ? 80 : 40;
    }

    // Получение тегов со страницы
    // Формат тега (4 bytes): 2 bytes size + 2 bytes offset (libesedb order)
    // Small page (<=8KB): size[0:12]+flags[13:15], offset[0:12]+flags[13:15]
    // Large page (>8KB):  size[0:14]+flags[15], offset[0:14]+flags[15]
    std::vector<PageTag> get_page_tags(const std::vector<std::uint8_t>& page,
                                       const PageHeader& hdr) {
        std::vector<PageTag> tags;

        std::uint16_t num_tags = hdr.available_page_tag;
        if (num_tags == 0) {
            return tags;
        }

        // Защита от integer overflow: num_tags * 4 должно быть <= page_size_
        std::size_t tag_array_size = static_cast<std::size_t>(num_tags) * 4;
        if (tag_array_size > page_size_ || tag_array_size > page.size()) {
            return tags;  // Невалидное количество тегов
        }

        // Теги располагаются в конце страницы
        std::size_t tag_array_offset = page_size_ - tag_array_size;

        for (std::uint16_t i = 0; i < num_tags; ++i) {
            std::size_t tag_pos = tag_array_offset + i * 4;
            if (tag_pos + 4 > page.size()) {
                break;
            }

            // libesedb format: first 2 bytes = size, next 2 bytes = offset
            std::uint16_t size_raw = read_u16_le(page.data() + tag_pos);
            std::uint16_t offset_raw = read_u16_le(page.data() + tag_pos + 2);

            PageTag tag;
            bool is_small_page = (page_size_ <= 8192);
            if (is_small_page) {
                // Small page: lower 13 bits for value, upper 3 bits for flags
                tag.size = size_raw & 0x1FFF;
                tag.offset = offset_raw & 0x1FFF;
                tag.flags = offset_raw >> 13;
            } else {
                // Large page: lower 15 bits for value, upper bit for flag
                tag.size = size_raw & 0x7FFF;
                tag.offset = offset_raw & 0x7FFF;
                tag.flags = offset_raw >> 15;
            }

            tags.push_back(tag);
        }

        return tags;
    }

    // Получение данных по тегу
    std::vector<std::uint8_t> get_tag_data(const std::vector<std::uint8_t>& page,
                                           const PageTag& tag) {
        std::size_t hdr_size = page_header_size();
        std::size_t data_offset = hdr_size + tag.offset;

        if (data_offset + tag.size > page.size()) {
            return {};
        }

        auto begin_off = static_cast<std::ptrdiff_t>(data_offset);
        auto end_off = static_cast<std::ptrdiff_t>(data_offset + tag.size);
        return std::vector<std::uint8_t>(page.begin() + begin_off, page.begin() + end_off);
    }

    // -------------------------------------------------------------------------
    // Catalog Reading
    // -------------------------------------------------------------------------

    bool read_catalog() {
        // MSysObjects catalog has father_dp_obj_id = 2
        // We scan all pages looking for leaf pages belonging to the catalog
        constexpr std::uint32_t CATALOG_OBJ_ID = 2;

        // Get file size to determine max pages
        file_.seekg(0, std::ios::end);
        auto file_size = file_.tellg();
        std::uint32_t max_pages = static_cast<std::uint32_t>(file_size / page_size_);

        // Two-pass parsing: columns may appear before their parent tables
        // in the catalog scan order, so we collect them separately first
        std::vector<std::vector<std::uint8_t>> catalog_entries;

        // First pass: collect all catalog entries
        for (std::uint32_t page_num = 0; page_num < max_pages; ++page_num) {
            auto page = read_page(page_num);
            if (page.empty()) {
                continue;
            }

            auto hdr = parse_page_header(page);

            // Look for leaf pages belonging to catalog (father_dp_obj_id == 2)
            // Skip space tree and long value pages
            if (hdr.is_space_tree() || hdr.is_long_value()) {
                continue;
            }

            if (hdr.is_leaf() && hdr.father_dp_obj_id == CATALOG_OBJ_ID) {
                auto tags = get_page_tags(page, hdr);
                // First tag (index 0) is page header, skip it
                for (std::size_t i = 1; i < tags.size(); ++i) {
                    auto data = get_tag_data(page, tags[i]);
                    if (!data.empty()) {
                        catalog_entries.push_back(std::move(data));
                    }
                }
            }
        }

        // Second pass: parse tables first (type == 1)
        for (const auto& data : catalog_entries) {
            parse_catalog_entry(data, true);  // tables_only = true
        }

        // Third pass: parse columns and other entries (type != 1)
        for (const auto& data : catalog_entries) {
            parse_catalog_entry(data, false);  // tables_only = false
        }

        return !tables_.empty();
    }

    void parse_catalog_entry(const std::vector<std::uint8_t>& data, bool tables_only) {
        // Leaf entry structure in ESE:
        // bytes 0-1: prefix_len (common key prefix to skip)
        // bytes 2-3: suffix_len (local key suffix length)
        // bytes 4 to 4+suffix_len-1: key suffix data
        // bytes 4+suffix_len+: entry data (DDH + fixed columns + variable data)

        if (data.size() < 10) {
            return;
        }

        // Read suffix length (prefix_len not needed for parsing)
        std::uint16_t suffix_len = read_u16_le(data.data() + 2);

        // Entry data starts after the key prefix/suffix
        std::size_t entry_offset = 4 + suffix_len;
        if (entry_offset + 20 > data.size()) {
            return;  // Not enough data for DDH + minimum fixed columns
        }

        const std::uint8_t* entry = data.data() + entry_offset;
        std::size_t entry_size = data.size() - entry_offset;

        // Data Definition Header (4 bytes):
        // Offset 0: last_fixed_type (1 byte)
        // Offset 1: last_variable_type (1 byte)
        // Offset 2: variable_data_offset (2 bytes) - relative to entry start
        std::uint16_t var_offset = read_u16_le(entry + 2);

        // ESE Catalog Fixed Column Layout (starting at entry + 4):
        // +0:  ObjidFDP (4 bytes) - Father Data Page Object ID
        // +4:  Type (2 bytes) - Catalog entry type (Table=1, Column=2, etc.)
        // +6:  Id (4 bytes) - Identifier
        // +10: ColtypOrPgnoFDP (4 bytes) - Column type or FDP page number
        // +14: SpaceUsage (4 bytes) - Space usage / Record offset
        // +18: Flags (4 bytes) - Entry flags
        // +22: Pages (4 bytes) - Reserved/Pages count
        // +26: Codepage (4 bytes) - Text codepage

        std::uint32_t objid_fdp = read_u32_le(entry + 4);
        std::uint16_t entry_type = read_u16_le(entry + 8);
        std::uint32_t id = read_u32_le(entry + 10);

        std::uint32_t type_or_pgno = 0;
        std::uint32_t space_or_rec = 0;
        std::uint32_t flags = 0;
        std::uint32_t codepage = 0;

        if (entry_size >= 18)
            type_or_pgno = read_u32_le(entry + 14);
        if (entry_size >= 22)
            space_or_rec = read_u32_le(entry + 18);
        if (entry_size >= 26)
            flags = read_u32_le(entry + 22);
        if (entry_size >= 34)
            codepage = read_u32_le(entry + 30);

        // Get name from variable data
        std::string name;
        if (var_offset > 0 && static_cast<std::size_t>(var_offset) + 2 <= entry_size) {
            // Variable offset table is at entry + var_offset
            // For catalog, there's typically one variable column (Name)
            // Name data starts after the offset table entry (2 bytes)
            std::size_t name_start = var_offset + 2;
            for (std::size_t i = name_start; i < entry_size; ++i) {
                if (entry[i] == 0)
                    break;
                if (entry[i] >= 32 && entry[i] < 127) {
                    name += static_cast<char>(entry[i]);
                }
            }
        }

        // Process catalog entry by type
        auto catalog_type = static_cast<CatalogType>(entry_type);

        // In tables_only mode, only process Table entries
        // Otherwise, process Column and LongValue entries
        if (tables_only) {
            if (catalog_type == CatalogType::Table) {
                TableSpec table;
                table.obj_id = objid_fdp;
                table.name = name;
                table.fdp_id = type_or_pgno;
                table.lv_fdp_id = 0;
                tables_.push_back(table);
            }
        } else {
            switch (catalog_type) {
            case CatalogType::Column: {
                // Add column to existing table
                for (auto& table : tables_) {
                    if (table.obj_id == objid_fdp) {
                        ColumnSpec col;
                        col.id = id;
                        col.name = name;
                        col.type = static_cast<JetColtyp>(type_or_pgno);
                        col.flags = flags;
                        col.space_usage = space_or_rec;
                        col.codepage = static_cast<std::uint16_t>(codepage);
                        table.columns.push_back(col);
                        break;
                    }
                }
                break;
            }
            case CatalogType::LongValue: {
                for (auto& table : tables_) {
                    if (table.obj_id == objid_fdp) {
                        table.lv_fdp_id = type_or_pgno;
                        break;
                    }
                }
                break;
            }
            default:
                // Ignore other types (Table in this pass, Index, Callback)
                break;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Table Data Parsing
    // -------------------------------------------------------------------------

    void parse_table(const TableSpec& table) {
        // Scan all pages looking for leaf pages belonging to this table
        // Table data pages have father_dp_obj_id == table.obj_id
        // Skip space tree pages (flag 0x0020) and long value pages (flag 0x0080)

        // Clear any error state and get file size
        file_.clear();
        file_.seekg(0, std::ios::end);
        auto file_size = file_.tellg();

        if (file_size <= 0 || page_size_ == 0) {
            return;
        }

        std::uint32_t max_pages = static_cast<std::uint32_t>(file_size / page_size_);

        for (std::uint32_t page_num = 0; page_num < max_pages; ++page_num) {
            auto page = read_page(page_num);
            if (page.empty()) {
                continue;
            }

            auto hdr = parse_page_header(page);

            // Check if this page belongs to the table
            if (hdr.father_dp_obj_id != table.obj_id) {
                continue;
            }

            // Skip space tree and long value pages
            if (hdr.is_space_tree() || hdr.is_long_value()) {
                continue;
            }

            // Only process leaf pages (not branch pages)
            if (!hdr.is_leaf()) {
                continue;
            }

            auto tags = get_page_tags(page, hdr);

            // Process data records (skip tag 0 which is the page header key)
            for (std::size_t i = 1; i < tags.size(); ++i) {
                auto data = get_tag_data(page, tags[i]);
                if (!data.empty()) {
                    auto record = parse_record(table, data);
                    // Add table name to record
                    record["Table"] = Value(table.name);
                    parsed_entries_.push_back(std::move(record));
                }
            }
        }
    }

    std::unordered_map<std::string, Value> parse_record(const TableSpec& table,
                                                        const std::vector<std::uint8_t>& data) {
        std::unordered_map<std::string, Value> record;

        if (data.size() < 10) {
            return record;
        }

        // Leaf entry structure:
        // bytes 0-1: prefix_len (common key prefix to skip)
        // bytes 2-3: suffix_len (local key suffix length)
        // bytes 4 to 4+suffix_len-1: key suffix data
        // bytes 4+suffix_len+: entry data (DDH + columns)

        std::uint16_t suffix_len = read_u16_le(data.data() + 2);
        std::size_t entry_offset = 4 + suffix_len;

        if (entry_offset + 4 > data.size()) {
            return record;
        }

        const std::uint8_t* entry = data.data() + entry_offset;
        std::size_t entry_size = data.size() - entry_offset;

        // Data Definition Header (at entry start)
        std::uint8_t last_fixed = entry[0];
        std::uint8_t last_var = entry[1];
        std::uint16_t var_offset = read_u16_le(entry + 2);

        // Sort columns by ID
        std::vector<const ColumnSpec*> sorted_columns;
        sorted_columns.reserve(table.columns.size());
        for (const auto& col : table.columns) {
            sorted_columns.push_back(&col);
        }
        std::sort(sorted_columns.begin(), sorted_columns.end(),
                  [](const ColumnSpec* a, const ColumnSpec* b) { return a->id < b->id; });

        // Fixed columns start at entry + 4 (after DDH)
        std::size_t offset = 4;

        // Read fixed columns (id <= 127)
        for (const auto* col : sorted_columns) {
            if (col->id > 127)
                break;  // Fixed columns have ID <= 127
            if (col->id > last_fixed)
                break;

            // Create a wrapper vector for read_column_value
            std::vector<std::uint8_t> entry_vec(entry, entry + entry_size);
            Value val = read_column_value(*col, entry_vec, offset);
            if (!val.is_null()) {
                record[col->name] = std::move(val);
            }

            offset += get_fixed_column_size(col->type);
        }

        // Variable columns (id 128-255)
        // var_offset должен быть >= 4 (после DDH) и < entry_size
        if (var_offset >= 4 && var_offset < entry_size) {
            std::vector<std::uint16_t> var_offsets;
            std::size_t var_table_start = var_offset;

            // Read variable column offsets
            for (const auto* col : sorted_columns) {
                if (col->id < 128 || col->id > 255)
                    continue;
                if (col->id > last_var)
                    break;

                if (var_table_start + 2 <= entry_size) {
                    std::uint16_t off = read_u16_le(entry + var_table_start);
                    var_offsets.push_back(off);
                    var_table_start += 2;
                }
            }

            // Read variable column data
            std::size_t var_data_start = var_table_start;
            std::size_t var_idx = 0;
            for (const auto* col : sorted_columns) {
                if (col->id < 128 || col->id > 255)
                    continue;
                if (col->id > last_var)
                    break;
                if (var_idx >= var_offsets.size())
                    break;

                std::uint16_t var_off = var_offsets[var_idx];
                std::uint16_t var_len = 0;

                // Length = next offset - current offset
                // Защита от underflow: проверяем, что next_offset >= current_offset
                if (var_idx + 1 < var_offsets.size()) {
                    std::uint16_t next_off = var_offsets[var_idx + 1];
                    if (next_off >= var_off) {
                        var_len = next_off - var_off;
                    }
                    // Если next_off < var_off, оставляем var_len = 0
                } else {
                    // Last variable column
                    std::size_t total_offset = var_data_start + var_off;
                    if (total_offset < entry_size) {
                        var_len = static_cast<std::uint16_t>(entry_size - total_offset);
                    }
                }

                if (var_len > 0) {
                    std::size_t data_start = var_data_start + var_off;
                    std::size_t data_end = data_start + var_len;
                    if (data_end <= entry_size) {
                        std::vector<std::uint8_t> var_data(entry + data_start, entry + data_end);
                        Value val = parse_variable_value(*col, var_data);
                        if (!val.is_null()) {
                            record[col->name] = std::move(val);
                        }
                    }
                }

                var_idx++;
            }
        }

        // Tagged columns (id >= 256) - sparse data
        // TODO: Implement tagged column parsing if needed

        return record;
    }

    std::size_t get_fixed_column_size(JetColtyp type) {
        switch (type) {
        case JetColtyp::Bit:
        case JetColtyp::UnsignedByte:
            return 1;
        case JetColtyp::Short:
        case JetColtyp::UnsignedShort:
            return 2;
        case JetColtyp::Long:
        case JetColtyp::UnsignedLong:
        case JetColtyp::IEEESingle:
            return 4;
        case JetColtyp::Currency:
        case JetColtyp::IEEEDouble:
        case JetColtyp::DateTime:
        case JetColtyp::LongLong:
            return 8;
        case JetColtyp::GUID:
            return 16;
        default:
            return 0;  // Variable size
        }
    }

    Value read_column_value(const ColumnSpec& col, const std::vector<std::uint8_t>& data,
                            std::size_t offset) {
        std::size_t col_size = get_fixed_column_size(col.type);
        if (col_size == 0 || offset + col_size > data.size()) {
            return Value();  // null
        }

        const std::uint8_t* ptr = data.data() + offset;

        switch (col.type) {
        case JetColtyp::Bit:
            return Value(read_u8(ptr) != 0);

        case JetColtyp::UnsignedByte:
            return Value(static_cast<std::int64_t>(read_u8(ptr)));

        case JetColtyp::Short:
            return Value(static_cast<std::int64_t>(read_i16_le(ptr)));

        case JetColtyp::UnsignedShort:
            return Value(static_cast<std::int64_t>(read_u16_le(ptr)));

        case JetColtyp::Long:
            return Value(static_cast<std::int64_t>(read_i32_le(ptr)));

        case JetColtyp::UnsignedLong:
            return Value(static_cast<std::int64_t>(read_u32_le(ptr)));

        case JetColtyp::Currency:
        case JetColtyp::LongLong:
            return Value(read_i64_le(ptr));

        case JetColtyp::IEEESingle:
            return Value(static_cast<double>(read_f32_le(ptr)));

        case JetColtyp::IEEEDouble:
            return Value(read_f64_le(ptr));

        case JetColtyp::DateTime: {
            double ole_time = read_f64_le(ptr);
            if (ole_time != 0.0) {
                std::string dt = ole_time_to_iso8601(ole_time);
                if (!dt.empty()) {
                    return Value(std::move(dt));
                }
            }
            return Value();
        }

        case JetColtyp::GUID: {
            // GUID как массив байт
            Value::Array arr;
            for (std::size_t i = 0; i < 16; ++i) {
                arr.push_back(Value(static_cast<std::int64_t>(ptr[i])));
            }
            return Value(std::move(arr));
        }

        default:
            return Value();
        }
    }

    Value parse_variable_value(const ColumnSpec& col, const std::vector<std::uint8_t>& data) {
        if (data.empty()) {
            return Value();
        }

        switch (col.type) {
        case JetColtyp::Text:
        case JetColtyp::LongText: {
            // Проверяем сжатие
            if (!data.empty() && (data[0] & 0x0F) != 0) {
                // Возможно сжатие, пробуем 7-bit decompression
                std::string decompressed = decompress_7bit(data);
                if (!decompressed.empty()) {
                    return Value(std::move(decompressed));
                }
            }

            // ASCII или UTF-16 строка
            if (col.codepage == 1200 || col.codepage == 1201) {
                // UTF-16
                return Value(utf16_to_utf8(data));
            } else {
                // ASCII/ANSI
                std::string str;
                for (std::uint8_t c : data) {
                    if (c == 0)
                        break;
                    str.push_back(static_cast<char>(c));
                }
                return Value(std::move(str));
            }
        }

        case JetColtyp::Binary:
        case JetColtyp::LongBinary: {
            Value::Array arr;
            for (std::uint8_t byte : data) {
                arr.push_back(Value(static_cast<std::int64_t>(byte)));
            }
            return Value(std::move(arr));
        }

        default:
            // Другие типы как бинарные данные
            Value::Array arr;
            for (std::uint8_t byte : data) {
                arr.push_back(Value(static_cast<std::int64_t>(byte)));
            }
            return Value(std::move(arr));
        }
    }

    // -------------------------------------------------------------------------
    // Utility Functions
    // -------------------------------------------------------------------------

    std::string to_hex(std::uint32_t val) {
        std::ostringstream oss;
        oss << std::hex << std::uppercase << val;
        return oss.str();
    }

    std::string decompress_7bit(const std::vector<std::uint8_t>& data) {
        // 7-bit compression: каждые 7 байт кодируют 8 символов
        if (data.empty()) {
            return "";
        }

        // Защита от DoS: лимит на размер вывода
        // Для SRUDB.dat строки обычно короткие (пути, имена приложений)
        // 64KB - разумный лимит для текстовых полей
        constexpr std::size_t MAX_OUTPUT_SIZE = 65536;

        std::string result;
        // Ограничиваем reserve максимальным размером
        std::size_t estimated_size = data.size() * 8 / 7 + 1;
        result.reserve(std::min(estimated_size, MAX_OUTPUT_SIZE));

        std::size_t bit_pos = 0;
        std::size_t byte_pos = 0;

        while (byte_pos < data.size()) {
            // Проверка лимита вывода
            if (result.size() >= MAX_OUTPUT_SIZE) {
                break;
            }

            // Извлекаем 7 бит
            std::uint32_t bits = 0;
            for (std::size_t i = 0; i < 7 && byte_pos < data.size(); ++i) {
                std::size_t current_byte = byte_pos + (bit_pos + i) / 8;
                std::size_t current_bit = (bit_pos + i) % 8;

                if (current_byte >= data.size())
                    break;

                if ((data[current_byte] >> current_bit) & 1) {
                    bits |= (1U << i);
                }
            }

            if (bits == 0) {
                break;  // Null terminator
            }

            result.push_back(static_cast<char>(bits));
            bit_pos += 7;
            byte_pos = bit_pos / 8;
        }

        return result;
    }

    std::string utf16_to_utf8(const std::vector<std::uint8_t>& data) {
        std::string result;

        for (std::size_t i = 0; i + 1 < data.size(); i += 2) {
            std::uint16_t wc = read_u16_le(data.data() + i);
            if (wc == 0)
                break;

            // UTF-16 to UTF-8 conversion с поддержкой surrogate pairs
            if (wc < 0x80) {
                // ASCII
                result.push_back(static_cast<char>(wc));
            } else if (wc < 0x800) {
                // 2-byte UTF-8
                result.push_back(static_cast<char>(0xC0 | (wc >> 6)));
                result.push_back(static_cast<char>(0x80 | (wc & 0x3F)));
            } else if (wc >= 0xD800 && wc <= 0xDBFF) {
                // High surrogate - нужна следующая пара
                if (i + 3 < data.size()) {
                    std::uint16_t low = read_u16_le(data.data() + i + 2);
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        // Валидная surrogate pair - декодируем в code point
                        std::uint32_t cp = 0x10000 +
                                           ((static_cast<std::uint32_t>(wc) - 0xD800) << 10) +
                                           (static_cast<std::uint32_t>(low) - 0xDC00);
                        // 4-byte UTF-8
                        result.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                        result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                        result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        i += 2;  // Пропускаем low surrogate
                        continue;
                    }
                }
                // Невалидная surrogate pair - пропускаем или заменяем
                result.push_back('\xEF');
                result.push_back('\xBF');
                result.push_back('\xBD');  // U+FFFD replacement character
            } else if (wc >= 0xDC00 && wc <= 0xDFFF) {
                // Lone low surrogate - невалидно, заменяем
                result.push_back('\xEF');
                result.push_back('\xBF');
                result.push_back('\xBD');  // U+FFFD replacement character
            } else {
                // 3-byte UTF-8
                result.push_back(static_cast<char>(0xE0 | (wc >> 12)));
                result.push_back(static_cast<char>(0x80 | ((wc >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (wc & 0x3F)));
            }
        }

        return result;
    }

    // -------------------------------------------------------------------------
    // Member Variables
    // -------------------------------------------------------------------------

    std::ifstream file_;
    bool loaded_ = false;
    std::filesystem::path path_;
    std::optional<EsedbError> error_;

    // File format info
    std::uint32_t page_size_ = 0;
    std::uint32_t format_version_ = 0;
    std::uint32_t format_revision_ = 0;

    // Database schema (from catalog)
    std::vector<TableSpec> tables_;

    // Parsed records cache
    std::vector<std::unordered_map<std::string, Value>> parsed_entries_;
    std::size_t current_index_ = 0;
};

// ============================================================================
// EsedbParser is always supported (native implementation)
// ============================================================================

bool EsedbParser::is_supported() {
    return true;  // Собственная реализация работает везде
}

// ============================================================================
// EsedbParser Public API
// ============================================================================

EsedbParser::EsedbParser() : impl_(std::make_unique<Impl>()) {}
EsedbParser::~EsedbParser() = default;
EsedbParser::EsedbParser(EsedbParser&&) noexcept = default;
EsedbParser& EsedbParser::operator=(EsedbParser&&) noexcept = default;

bool EsedbParser::load(const std::filesystem::path& file_path) {
    return impl_->load(file_path);
}

bool EsedbParser::is_loaded() const {
    return impl_->is_loaded();
}

const std::optional<EsedbError>& EsedbParser::last_error() const {
    return impl_->last_error();
}

std::vector<std::unordered_map<std::string, Value>> EsedbParser::parse() {
    return impl_->parse();
}

std::unordered_map<std::string, SruDbIdMapTableEntry> EsedbParser::parse_sru_db_id_map_table() {
    return impl_->parse_sru_db_id_map_table();
}

bool EsedbParser::has_next() const {
    return impl_->has_next();
}

bool EsedbParser::eof() const {
    return impl_->eof();
}

bool EsedbParser::next(std::unordered_map<std::string, Value>& out) {
    return impl_->next(out);
}

const std::filesystem::path& EsedbParser::path() const {
    return impl_->path();
}

// ============================================================================
// EsedbReader - Reader implementation for ESEDB files
// ============================================================================

class EsedbReader : public Reader {
public:
    explicit EsedbReader(std::filesystem::path path) : path_(std::move(path)) {}

    bool load() {
        if (!parser_.load(path_)) {
            const auto& err = parser_.last_error();
            if (err) {
                error_ = ReaderError{err->kind == EsedbErrorKind::NotSupported
                                         ? ReaderErrorKind::UnsupportedFormat
                                         : ReaderErrorKind::ParseError,
                                     err->message, platform::path_to_utf8(path_)};
            }
            return false;
        }

        // Парсим все записи
        entries_ = parser_.parse();
        current_index_ = 0;
        loaded_ = true;
        return true;
    }

    bool next(Document& out) override {
        if (!loaded_ || current_index_ >= entries_.size()) {
            return false;
        }

        // Конвертируем unordered_map<string, Value> в Value::Object
        Value::Object obj;
        for (auto& [key, val] : entries_[current_index_]) {
            obj[key] = std::move(val);
        }

        out.kind = DocumentKind::Esedb;
        out.data = Value(std::move(obj));
        out.source = platform::path_to_utf8(path_);
        out.record_id = current_index_;
        ++current_index_;
        return true;
    }

    bool has_next() const override { return loaded_ && current_index_ < entries_.size(); }

    DocumentKind kind() const override { return DocumentKind::Esedb; }
    const std::filesystem::path& path() const override { return path_; }
    const std::optional<ReaderError>& last_error() const override { return error_; }

private:
    std::filesystem::path path_;
    EsedbParser parser_;
    std::vector<std::unordered_map<std::string, Value>> entries_;
    std::size_t current_index_ = 0;
    bool loaded_ = false;
    std::optional<ReaderError> error_;
};

std::unique_ptr<Reader> create_esedb_reader(const std::filesystem::path& path, bool skip_errors) {
    auto reader = std::make_unique<EsedbReader>(path);
    if (!reader->load()) {
        if (skip_errors) {
            return create_empty_reader(path, DocumentKind::Esedb);
        }
    }
    return reader;
}

}  // namespace chainsaw::io::esedb

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
