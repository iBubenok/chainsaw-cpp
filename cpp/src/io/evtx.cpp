// ==============================================================================
// evtx.cpp - Реализация EVTX Parser
// ==============================================================================
//
// MOD-0007 formats/evtx
// SLICE-007: EVTX Parser
// SPEC-SLICE-007: micro-spec поведения
//
// Формат EVTX документирован:
// https://github.com/libyal/libevtx/blob/main/documentation/
//
// ==============================================================================

#include <algorithm>
#include <chainsaw/evtx.hpp>
#include <chainsaw/platform.hpp>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <pugixml.hpp>
#include <sstream>
#include <stack>

namespace chainsaw::evtx {

// ============================================================================
// EvtxRecordIterator
// ============================================================================

EvtxRecordIterator::EvtxRecordIterator(EvtxParser* parser, bool end) : parser_(parser), end_(end) {
    if (!end_) {
        advance();
    }
}

void EvtxRecordIterator::advance() {
    if (end_)
        return;

    EvtxRecord record;
    if (parser_->next(record)) {
        current_ = std::move(record);
    } else {
        end_ = true;
        current_.reset();
    }
}

EvtxRecordIterator& EvtxRecordIterator::operator++() {
    advance();
    return *this;
}

bool EvtxRecordIterator::operator!=(const EvtxRecordIterator& other) const {
    return end_ != other.end_;
}

const EvtxRecord& EvtxRecordIterator::operator*() const {
    return *current_;
}

// ============================================================================
// EvtxParser - загрузка файла
// ============================================================================

bool EvtxParser::load(const std::filesystem::path& path) {
    path_ = path;
    error_.reset();
    string_cache_.clear();
    current_chunk_ = 0;
    current_chunk_offset_ = 0;
    current_record_offset_ = 0;
    chunk_end_offset_ = 0;
    eof_ = false;

    // Открываем файл
    file_.open(path, std::ios::binary);
    if (!file_.is_open()) {
        error_ = EvtxError{"could not open file", 0};
        return false;
    }

    // Получаем размер файла
    file_.seekg(0, std::ios::end);
    file_size_ = static_cast<std::uint64_t>(file_.tellg());
    file_.seekg(0, std::ios::beg);

    // Проверяем минимальный размер
    if (file_size_ < FILE_HEADER_SIZE) {
        error_ = EvtxError{"file too small for EVTX header", 0};
        return false;
    }

    // Читаем заголовок файла
    if (!read_file_header()) {
        return false;
    }

    // Готовы к чтению первого чанка
    current_chunk_offset_ = FILE_HEADER_SIZE;
    return true;
}

bool EvtxParser::read_file_header() {
    char magic[8];
    if (!read_bytes(magic, 8)) {
        error_ = EvtxError{"failed to read file magic", 0};
        return false;
    }

    // Проверяем магию "ElfFile\0"
    if (std::memcmp(magic, FILE_HEADER_MAGIC, 8) != 0) {
        error_ = EvtxError{"invalid EVTX file magic", 0};
        return false;
    }

    // Пропускаем остаток заголовка, переходим к первому чанку
    file_.seekg(FILE_HEADER_SIZE, std::ios::beg);
    return true;
}

// ============================================================================
// EvtxParser - чтение записей
// ============================================================================

bool EvtxParser::next(EvtxRecord& record) {
    if (eof_ || !file_.is_open()) {
        return false;
    }

    while (true) {
        // Проверяем, нужно ли загрузить новый чанк
        if (current_record_offset_ == 0) {
            // Начинаем чтение нового чанка
            if (!read_chunk_header()) {
                eof_ = true;
                return false;
            }
        } else if (current_record_offset_ >= chunk_end_offset_) {
            // Закончили текущий чанк - переходим к следующему
            current_chunk_offset_ += CHUNK_SIZE;
            current_record_offset_ = 0;
            if (current_chunk_offset_ >= file_size_) {
                eof_ = true;
                return false;
            }
            continue;  // Перезайти в цикл для чтения нового заголовка
        }

        // Пробуем прочитать запись
        if (read_record(record)) {
            return true;
        }

        // Если не удалось прочитать запись (невалидная сигнатура),
        // переходим к следующему чанку
        current_chunk_offset_ += CHUNK_SIZE;
        current_record_offset_ = 0;

        if (current_chunk_offset_ >= file_size_) {
            eof_ = true;
            return false;
        }
    }
}

bool EvtxParser::read_chunk_header() {
    // Проверяем, не вышли ли за пределы файла
    if (current_chunk_offset_ >= file_size_) {
        return false;
    }

    // Переходим к началу чанка
    file_.seekg(static_cast<std::streamoff>(current_chunk_offset_), std::ios::beg);

    // Читаем магию чанка
    char magic[8];
    if (!read_bytes(magic, 8)) {
        return false;
    }

    // Проверяем магию "ElfChnk\0"
    if (std::memcmp(magic, CHUNK_HEADER_MAGIC, 8) != 0) {
        // Не чанк — возможно, конец файла или пустое пространство
        return false;
    }

    // Читаем остальные поля заголовка чанка
    std::uint64_t first_event_record_number;
    std::uint64_t last_event_record_number;
    std::uint64_t first_event_record_id;
    std::uint64_t last_event_record_id;
    std::uint32_t header_size;
    std::uint32_t last_event_record_data_offset;
    std::uint32_t free_space_offset;

    if (!read_value(first_event_record_number) || !read_value(last_event_record_number) ||
        !read_value(first_event_record_id) || !read_value(last_event_record_id) ||
        !read_value(header_size) || !read_value(last_event_record_data_offset) ||
        !read_value(free_space_offset)) {
        return false;
    }

    // Очищаем кеши при переходе к новому чанку
    string_cache_.clear();
    template_cache_.clear();

    // Устанавливаем позицию первой записи (после заголовка чанка)
    current_record_offset_ = current_chunk_offset_ + CHUNK_HEADER_SIZE;
    chunk_end_offset_ = current_chunk_offset_ + free_space_offset;

    return true;
}

bool EvtxParser::read_record(EvtxRecord& record) {
    // Переходим к позиции записи
    file_.seekg(static_cast<std::streamoff>(current_record_offset_), std::ios::beg);

    // Читаем заголовок записи
    std::uint32_t signature;
    std::uint32_t size;
    std::uint64_t record_id;
    std::uint64_t timestamp;

    if (!read_value(signature)) {
        return false;
    }

    // Проверяем сигнатуру записи 0x00002a2a ("**\0\0")
    if (signature != 0x00002a2a) {
        return false;
    }

    if (!read_value(size) || !read_value(record_id) || !read_value(timestamp)) {
        return false;
    }

    // Читаем Binary XML данные
    // Размер данных = size - 24 (заголовок) - 4 (копия размера в конце)
    std::size_t data_size = size - 28;
    std::vector<std::uint8_t> binxml_data(data_size);

    if (!read_bytes(binxml_data.data(), data_size)) {
        return false;
    }

    // Парсим Binary XML
    record.data = parse_binxml(binxml_data);
    record.record_id = record_id;
    record.timestamp = filetime_to_iso8601(timestamp);

    // Переходим к следующей записи
    current_record_offset_ += size;

    return true;
}

// ============================================================================
// Binary XML парсинг
// ============================================================================

/// Контекст парсинга Binary XML
struct BinXmlContext {
    const std::vector<std::uint8_t>& data;
    std::size_t offset = 0;
    std::unordered_map<std::uint32_t, std::string>& string_cache;
    std::unordered_map<std::uint32_t, BinXmlTemplate>& template_cache;  // BUGFIX: теперь ссылка

    BinXmlContext(const std::vector<std::uint8_t>& d,
                  std::unordered_map<std::uint32_t, std::string>& str_cache,
                  std::unordered_map<std::uint32_t, BinXmlTemplate>& tmpl_cache)
        : data(d), string_cache(str_cache), template_cache(tmpl_cache) {}

    bool eof() const { return offset >= data.size(); }

    std::uint8_t read_u8() {
        if (offset >= data.size())
            return 0;
        return data[offset++];
    }

    std::uint16_t read_u16() {
        if (offset + 2 > data.size())
            return 0;
        std::uint16_t v = static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
        offset += 2;
        return v;
    }

    std::uint32_t read_u32() {
        if (offset + 4 > data.size())
            return 0;
        std::uint32_t v = data[offset] | (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
                          (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
                          (static_cast<std::uint32_t>(data[offset + 3]) << 24);
        offset += 4;
        return v;
    }

    std::uint64_t read_u64() {
        if (offset + 8 > data.size())
            return 0;
        std::uint64_t lo = read_u32();
        std::uint64_t hi = read_u32();
        return lo | (hi << 32);
    }

    std::string read_utf16_string(std::size_t char_count) {
        if (offset + char_count * 2 > data.size())
            return "";
        std::string result;
        result.reserve(char_count);
        for (std::size_t i = 0; i < char_count; ++i) {
            std::uint16_t ch = static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
            offset += 2;
            // Простая конверсия UTF-16 → UTF-8 (только BMP)
            if (ch < 0x80) {
                result.push_back(static_cast<char>(ch));
            } else if (ch < 0x800) {
                result.push_back(static_cast<char>(0xC0 | (ch >> 6)));
                result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
            } else {
                result.push_back(static_cast<char>(0xE0 | (ch >> 12)));
                result.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
            }
        }
        return result;
    }

    // Read UTF-16 string, stopping at null terminator or char_count, whichever comes first
    std::string read_utf16_string_until_null(std::size_t char_count) {
        if (offset + char_count * 2 > data.size()) {
            char_count = (data.size() - offset) / 2;
        }
        std::string result;
        result.reserve(char_count);
        for (std::size_t i = 0; i < char_count; ++i) {
            std::uint16_t ch = static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
            offset += 2;
            if (ch == 0)
                break;  // Stop at null terminator
            // Простая конверсия UTF-16 → UTF-8 (только BMP)
            if (ch < 0x80) {
                result.push_back(static_cast<char>(ch));
            } else if (ch < 0x800) {
                result.push_back(static_cast<char>(0xC0 | (ch >> 6)));
                result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
            } else {
                result.push_back(static_cast<char>(0xE0 | (ch >> 12)));
                result.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
            }
        }
        return result;
    }

    void skip(std::size_t n) {
        offset += n;
        if (offset > data.size())
            offset = data.size();
    }
};

/// Конвертировать Binary XML value в строку
static std::string binxml_value_to_string(BinXmlContext& ctx, BinXmlValueType type,
                                          std::uint16_t size) {
    switch (type) {
    case BinXmlValueType::Null:
        return "";

    case BinXmlValueType::WString: {
        // For SubSpec, size is in bytes. For inline Values, size is char count.
        // We use the 'is_subspec' flag (passed via size_is_bytes parameter)
        // For now, assume size is byte count and stop at null terminator.
        std::size_t char_count = size / 2;
        if (char_count == 0 && size > 0)
            char_count = 1;  // Handle odd sizes
        std::string s = ctx.read_utf16_string_until_null(char_count);
        // Убираем trailing null если есть
        while (!s.empty() && s.back() == '\0')
            s.pop_back();
        return s;
    }

    case BinXmlValueType::AnsiString: {
        std::string s;
        for (std::size_t i = 0; i < size; ++i) {
            char c = static_cast<char>(ctx.read_u8());
            if (c == '\0')
                break;
            s.push_back(c);
        }
        return s;
    }

    case BinXmlValueType::Int8:
        return std::to_string(static_cast<std::int8_t>(ctx.read_u8()));

    case BinXmlValueType::UInt8:
        return std::to_string(ctx.read_u8());

    case BinXmlValueType::Int16:
        return std::to_string(static_cast<std::int16_t>(ctx.read_u16()));

    case BinXmlValueType::UInt16:
        return std::to_string(ctx.read_u16());

    case BinXmlValueType::Int32:
        return std::to_string(static_cast<std::int32_t>(ctx.read_u32()));

    case BinXmlValueType::UInt32:
        return std::to_string(ctx.read_u32());

    case BinXmlValueType::Int64:
        return std::to_string(static_cast<std::int64_t>(ctx.read_u64()));

    case BinXmlValueType::UInt64:
        return std::to_string(ctx.read_u64());

    case BinXmlValueType::Bool:
        return ctx.read_u8() ? "true" : "false";

    case BinXmlValueType::Hex32: {
        std::uint32_t v = ctx.read_u32();
        std::ostringstream oss;
        oss << "0x" << std::hex << v;
        return oss.str();
    }

    case BinXmlValueType::Hex64: {
        std::uint64_t v = ctx.read_u64();
        std::ostringstream oss;
        oss << "0x" << std::hex << v;
        return oss.str();
    }

    case BinXmlValueType::Guid: {
        std::uint32_t d1 = ctx.read_u32();
        std::uint16_t d2 = ctx.read_u16();
        std::uint16_t d3 = ctx.read_u16();
        std::ostringstream oss;
        oss << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << d1 << "-"
            << std::setw(4) << d2 << "-" << std::setw(4) << d3 << "-";
        for (int i = 0; i < 2; ++i) {
            oss << std::setw(2) << static_cast<int>(ctx.read_u8());
        }
        oss << "-";
        for (int i = 0; i < 6; ++i) {
            oss << std::setw(2) << static_cast<int>(ctx.read_u8());
        }
        return oss.str();
    }

    case BinXmlValueType::FileTime: {
        std::uint64_t ft = ctx.read_u64();
        return EvtxParser::filetime_to_iso8601(ft);
    }

    case BinXmlValueType::SystemTime: {
        std::uint16_t year = ctx.read_u16();
        std::uint16_t month = ctx.read_u16();
        ctx.read_u16();  // day of week
        std::uint16_t day = ctx.read_u16();
        std::uint16_t hour = ctx.read_u16();
        std::uint16_t minute = ctx.read_u16();
        std::uint16_t second = ctx.read_u16();
        std::uint16_t ms = ctx.read_u16();
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(4) << year << "-" << std::setw(2) << month << "-"
            << std::setw(2) << day << "T" << std::setw(2) << hour << ":" << std::setw(2) << minute
            << ":" << std::setw(2) << second << "." << std::setw(3) << ms << "Z";
        return oss.str();
    }

    case BinXmlValueType::Sid: {
        // SID: версия (1 байт), count (1 байт), authority (6 байт), sub-authorities
        std::uint8_t version = ctx.read_u8();
        std::uint8_t sub_auth_count = ctx.read_u8();
        std::uint64_t authority = 0;
        for (int i = 0; i < 6; ++i) {
            authority = (authority << 8) | ctx.read_u8();
        }
        std::ostringstream oss;
        oss << "S-" << static_cast<int>(version) << "-" << authority;
        for (int i = 0; i < sub_auth_count; ++i) {
            oss << "-" << ctx.read_u32();
        }
        return oss.str();
    }

    case BinXmlValueType::Binary: {
        std::ostringstream oss;
        for (std::size_t i = 0; i < size; ++i) {
            oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(ctx.read_u8());
        }
        return oss.str();
    }

    default:
        // Неизвестный тип — пропускаем
        ctx.skip(size);
        return "";
    }
}

// Forward declarations
static Value convert_xml_node(const pugi::xml_node& node);
static Value xml_document_to_json_with_attributes(const pugi::xml_document& doc);

/// Рекурсивный парсинг Binary XML в XML строку
static void parse_binxml_to_xml(
    BinXmlContext& ctx, std::string& xml,
    std::vector<std::pair<std::string, std::string>>* substitution_values = nullptr,
    bool /*is_template*/ = false) {
    std::stack<std::string> element_stack;
    bool in_attribute = false;
    std::string current_attr_name;

    while (!ctx.eof()) {
        std::uint8_t token_byte = ctx.read_u8();
        bool more_bits = (token_byte & 0x40) != 0;
        auto token = static_cast<BinXmlToken>(token_byte & 0x0f);

        switch (token) {
        case BinXmlToken::EndOfStream:
            return;

        case BinXmlToken::StartOfStream:
            ctx.skip(3);  // major/minor version + flags
            break;

        case BinXmlToken::OpenStartElement: {
            ctx.skip(2);  // unknown
            std::uint32_t size = ctx.read_u32();
            (void)size;

            // Читаем имя элемента
            std::uint32_t string_offset = ctx.read_u32();
            std::string name;

            auto it = ctx.string_cache.find(string_offset);
            if (it != ctx.string_cache.end()) {
                name = it->second;
            } else {
                std::uint32_t next_offset = ctx.read_u32();
                (void)next_offset;
                std::uint16_t hash = ctx.read_u16();
                (void)hash;
                std::uint16_t string_length = ctx.read_u16();
                name = ctx.read_utf16_string(string_length);
                ctx.skip(2);  // null terminator
                ctx.string_cache[string_offset] = name;
            }

            if (more_bits) {
                ctx.skip(4);  // unknown
            }

            xml += "<" + name;
            element_stack.push(name);
            break;
        }

        case BinXmlToken::CloseStartElement:
            xml += ">";
            break;

        case BinXmlToken::CloseEmptyElement:
            xml += "/>";
            if (!element_stack.empty())
                element_stack.pop();
            break;

        case BinXmlToken::CloseElement:
            if (!element_stack.empty()) {
                xml += "</" + element_stack.top() + ">";
                element_stack.pop();
            }
            break;

        case BinXmlToken::Attribute: {
            // Читаем имя атрибута
            std::uint32_t string_offset = ctx.read_u32();
            std::string name;

            auto it = ctx.string_cache.find(string_offset);
            if (it != ctx.string_cache.end()) {
                name = it->second;
            } else {
                std::uint32_t next_offset = ctx.read_u32();
                (void)next_offset;
                std::uint16_t hash = ctx.read_u16();
                (void)hash;
                std::uint16_t string_length = ctx.read_u16();
                name = ctx.read_utf16_string(string_length);
                ctx.skip(2);  // null terminator
                ctx.string_cache[string_offset] = name;
            }

            xml += " " + name + "=\"";
            in_attribute = true;
            current_attr_name = name;
            break;
        }

        case BinXmlToken::Value: {
            std::uint8_t value_type = ctx.read_u8();
            std::uint16_t value_size = ctx.read_u16();

            // For inline Value tokens, WString size is in characters, not bytes.
            // Convert to bytes for binxml_value_to_string which expects bytes.
            std::uint16_t adjusted_size = value_size;
            if (static_cast<BinXmlValueType>(value_type) == BinXmlValueType::WString) {
                adjusted_size = static_cast<std::uint16_t>(value_size * 2);
            }

            std::string value = binxml_value_to_string(
                ctx, static_cast<BinXmlValueType>(value_type), adjusted_size);

            // Escape XML special characters
            std::string escaped;
            for (char c : value) {
                switch (c) {
                case '<':
                    escaped += "&lt;";
                    break;
                case '>':
                    escaped += "&gt;";
                    break;
                case '&':
                    escaped += "&amp;";
                    break;
                case '"':
                    escaped += "&quot;";
                    break;
                case '\'':
                    escaped += "&apos;";
                    break;
                default:
                    escaped += c;
                    break;
                }
            }

            xml += escaped;
            if (in_attribute) {
                xml += "\"";
                in_attribute = false;
            }
            break;
        }

        case BinXmlToken::TemplateInstance: {
            ctx.skip(1);  // unknown
            std::uint32_t template_id = ctx.read_u32();
            std::uint32_t template_offset = ctx.read_u32();
            (void)template_offset;
            std::uint32_t next_offset = ctx.read_u32();
            (void)next_offset;

            bool new_template = false;
            BinXmlTemplate* tmpl = nullptr;
            std::string tmpl_xml;

            auto it = ctx.template_cache.find(template_id);
            if (it == ctx.template_cache.end()) {
                new_template = true;
                // Читаем определение шаблона
                std::uint32_t tmpl_id2 = ctx.read_u32();
                (void)tmpl_id2;
                ctx.skip(16);  // GUID

                // Парсим шаблон (substitution_values позволяет collect placeholder indices)
                std::vector<std::pair<std::string, std::string>> subs;
                parse_binxml_to_xml(ctx, tmpl_xml, &subs, true);

                // Сохраняем шаблон в кеш
                BinXmlTemplate new_tmpl;
                new_tmpl.xml_template = tmpl_xml;
                ctx.template_cache[template_id] = std::move(new_tmpl);
                tmpl = &ctx.template_cache[template_id];
            } else {
                tmpl = &it->second;
                tmpl_xml = tmpl->xml_template;
            }

            // Читаем substitution values
            std::uint32_t sub_count = 0;
            if (new_template) {
                sub_count = ctx.read_u32();
                tmpl->substitution_count = sub_count;
            } else {
                sub_count = static_cast<std::uint32_t>(tmpl->substitution_count);
                if (sub_count == 0)
                    sub_count = 32;  // reasonable default
            }

            // Читаем спецификации substitution
            std::vector<std::pair<std::uint16_t, BinXmlValueType>> sub_specs;
            for (std::uint32_t i = 0; i < sub_count; ++i) {
                std::uint16_t size = ctx.read_u16();
                auto type = static_cast<BinXmlValueType>(ctx.read_u8());
                ctx.skip(1);  // padding
                sub_specs.emplace_back(size, type);
            }

            // Читаем значения substitution
            std::vector<std::string> sub_values;
            for (std::size_t vi = 0; vi < sub_specs.size(); ++vi) {
                const auto& [size, type] = sub_specs[vi];
                if (type == BinXmlValueType::BinXml) {
                    // Вложенный Binary XML - use size field directly
                    if (size > 0 && ctx.offset + size <= ctx.data.size()) {
                        // Create a sub-context with just the BinXml data
                        std::vector<std::uint8_t> binxml_data(
                            ctx.data.begin() + static_cast<std::ptrdiff_t>(ctx.offset),
                            ctx.data.begin() + static_cast<std::ptrdiff_t>(ctx.offset + size));
                        ctx.offset += size;

                        // Parse the nested BinXml with its own context (shares template_cache)
                        BinXmlContext sub_ctx(binxml_data, ctx.string_cache, ctx.template_cache);
                        std::string nested_xml;
                        parse_binxml_to_xml(sub_ctx, nested_xml);
                        sub_values.push_back(std::move(nested_xml));
                    } else {
                        sub_values.push_back("");
                    }
                } else {
                    std::string val = binxml_value_to_string(ctx, type, size);
                    sub_values.push_back(std::move(val));
                }
            }

            // Применяем substitution values к шаблону
            std::string result_xml = tmpl_xml;
            for (std::size_t i = 0; i < sub_values.size(); ++i) {
                std::string placeholder = "${" + std::to_string(i) + "}";
                std::size_t pos = 0;
                while ((pos = result_xml.find(placeholder, pos)) != std::string::npos) {
                    result_xml.replace(pos, placeholder.length(), sub_values[i]);
                    pos += sub_values[i].length();
                }
            }

            // Добавляем результат к выходному XML
            xml += result_xml;

            if (substitution_values) {
                // Возвращаем значения для родительского контекста
                for (std::size_t i = 0; i < sub_values.size(); ++i) {
                    substitution_values->emplace_back(std::to_string(i), sub_values[i]);
                }
            }
            break;  // Продолжаем парсинг (не return!)
        }

        case BinXmlToken::NormalSubstitution:
        case BinXmlToken::ConditionalSubstitution: {
            std::uint16_t index = ctx.read_u16();
            std::uint8_t type = ctx.read_u8();
            (void)type;

            if (substitution_values && index < substitution_values->size()) {
                xml += (*substitution_values)[index].second;
            } else {
                xml += "${" + std::to_string(index) + "}";
            }

            if (in_attribute) {
                xml += "\"";
                in_attribute = false;
            }
            break;
        }

        default:
            // Неизвестный токен — пропускаем
            break;
        }
    }
}

Value EvtxParser::parse_binxml(const std::vector<std::uint8_t>& data) {
    if (data.empty()) {
        return Value();
    }

    BinXmlContext ctx(data, string_cache_, template_cache_);

    // Парсим Binary XML в XML строку
    std::string xml;
    parse_binxml_to_xml(ctx, xml);

    if (xml.empty()) {
        return Value();
    }

    // Парсим XML с pugixml
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(xml.c_str());

    if (!result) {
        // Ошибка парсинга — возвращаем пустое значение
        return Value();
    }

    // Конвертируем в JSON с _attributes семантикой
    return xml_document_to_json_with_attributes(doc);
}

// ============================================================================
// Конверсия XML → JSON с _attributes
// ============================================================================

/// Рекурсивная конверсия XML узла в Value с _attributes семантикой
/// SPEC-SLICE-007 FACT-005: separate_json_attributes(true)
static Value convert_xml_node(const pugi::xml_node& node) {
    Value::Object obj;

    // Собираем атрибуты в отдельный объект с суффиксом "_attributes"
    // если есть хотя бы один атрибут
    Value::Object attrs;
    for (const auto& attr : node.attributes()) {
        std::string attr_name = attr.name();
        std::string attr_value = attr.value();

        // Попытаемся определить тип значения
        // Числа сохраняем как числа
        char* end = nullptr;
        long long int_val = std::strtoll(attr_value.c_str(), &end, 10);
        if (end != attr_value.c_str() && *end == '\0' && !attr_value.empty()) {
            attrs[attr_name] = Value(static_cast<std::int64_t>(int_val));
        } else {
            attrs[attr_name] = Value(attr_value);
        }
    }

    // Собираем дочерние элементы
    std::map<std::string, std::vector<Value>> children;
    std::string text_content;

    for (const auto& child : node.children()) {
        if (child.type() == pugi::node_element) {
            Value child_value = convert_xml_node(child);
            children[child.name()].push_back(std::move(child_value));
        } else if (child.type() == pugi::node_pcdata) {
            text_content += child.value();
        }
    }

    // Если есть атрибуты, добавляем их как {имя_элемента}_attributes
    if (!attrs.empty()) {
        obj[std::string(node.name()) + "_attributes"] = Value(std::move(attrs));
    }

    // Добавляем дочерние элементы
    for (auto& [name, values] : children) {
        if (values.size() == 1) {
            // Если один элемент с таким именем — значение
            const Value& v = values[0];
            // Если значение — объект только с текстом, разворачиваем его
            if (v.is_object() && v.object_size() == 0) {
                obj[name] = Value(std::string());
            } else if (v.is_object()) {
                // Копируем содержимое объекта
                const auto& child_obj = v.as_object();
                Value::Object merged;
                for (const auto& [k, val] : child_obj) {
                    merged[k] = val;
                }
                // Если было текстовое содержимое
                if (!values[0].has("$text") && child_obj.empty()) {
                    // Пустой элемент
                    obj[name] = Value(std::string());
                } else {
                    obj[name] = Value(std::move(merged));
                }
            } else {
                obj[name] = values[0];
            }
        } else {
            // Несколько элементов — массив
            Value::Array arr;
            for (auto& v : values) {
                arr.push_back(std::move(v));
            }
            obj[name] = Value(std::move(arr));
        }
    }

    // Если есть текстовое содержимое
    if (!text_content.empty()) {
        // Пробуем преобразовать в число
        char* end = nullptr;
        long long int_val = std::strtoll(text_content.c_str(), &end, 10);
        if (end != text_content.c_str() && *end == '\0') {
            return Value(static_cast<std::int64_t>(int_val));
        }
        // Иначе возвращаем как строку
        if (obj.empty()) {
            return Value(text_content);
        }
        obj["$text"] = Value(text_content);
    }

    // Если объект пустой и нет атрибутов — возвращаем null
    if (obj.empty()) {
        return Value();
    }

    return Value(std::move(obj));
}

/// Конвертировать XML документ в Value с _attributes семантикой
static Value xml_document_to_json_with_attributes(const pugi::xml_document& doc) {
    auto root = doc.document_element();
    if (!root) {
        return Value();
    }

    // Конвертируем корневой элемент
    Value root_value = convert_xml_node(root);

    // Добавляем атрибуты корневого элемента как {root_name}_attributes
    // и оборачиваем всё в объект с именем корня
    Value::Object result;
    result[root.name()] = std::move(root_value);

    // Атрибуты корневого элемента
    Value::Object root_attrs;
    for (const auto& attr : root.attributes()) {
        root_attrs[attr.name()] = Value(std::string(attr.value()));
    }
    if (!root_attrs.empty()) {
        result[std::string(root.name()) + "_attributes"] = Value(std::move(root_attrs));
    }

    return Value(std::move(result));
}

// ============================================================================
// Утилиты
// ============================================================================

template <typename T>
bool EvtxParser::read_value(T& value) {
    return read_bytes(&value, sizeof(T));
}

bool EvtxParser::read_bytes(void* buffer, std::size_t size) {
    file_.read(static_cast<char*>(buffer), static_cast<std::streamsize>(size));
    return file_.gcount() == static_cast<std::streamsize>(size);
}

std::string EvtxParser::read_utf16_string(const std::vector<std::uint8_t>& data,
                                          std::size_t& offset, std::size_t char_count) {
    std::string result;
    result.reserve(char_count);

    for (std::size_t i = 0; i < char_count && offset + 1 < data.size(); ++i) {
        std::uint16_t ch = static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
        offset += 2;

        // Простая конверсия UTF-16 → UTF-8
        if (ch < 0x80) {
            result.push_back(static_cast<char>(ch));
        } else if (ch < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (ch >> 6)));
            result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xE0 | (ch >> 12)));
            result.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }

    return result;
}

std::string EvtxParser::filetime_to_iso8601(std::uint64_t filetime) {
    // Windows FILETIME: 100-наносекундные интервалы с 1601-01-01
    // Unix timestamp: секунды с 1970-01-01
    // Разница: 11644473600 секунд

    constexpr std::uint64_t FILETIME_UNIX_DIFF = 116444736000000000ULL;

    if (filetime < FILETIME_UNIX_DIFF) {
        return "";
    }

    std::uint64_t unix_100ns = filetime - FILETIME_UNIX_DIFF;
    std::uint64_t unix_seconds = unix_100ns / 10000000ULL;
    std::uint64_t microseconds = (unix_100ns % 10000000ULL) / 10;

    std::time_t time = static_cast<std::time_t>(unix_seconds);
    std::tm* tm = std::gmtime(&time);

    if (!tm) {
        return "";
    }

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << (tm->tm_year + 1900) << "-" << std::setw(2)
        << (tm->tm_mon + 1) << "-" << std::setw(2) << tm->tm_mday << "T" << std::setw(2)
        << tm->tm_hour << ":" << std::setw(2) << tm->tm_min << ":" << std::setw(2) << tm->tm_sec
        << "." << std::setw(6) << microseconds << "Z";

    return oss.str();
}

// ============================================================================
// Вспомогательные функции для поиска с алиасами
// ============================================================================

const Value* find_by_path(const Value& value, std::string_view path) {
    if (path.empty()) {
        return &value;
    }

    const Value* current = &value;

    while (!path.empty()) {
        if (!current->is_object()) {
            return nullptr;
        }

        // Находим следующий сегмент пути
        std::size_t dot_pos = path.find('.');
        std::string_view segment;

        if (dot_pos == std::string_view::npos) {
            segment = path;
            path = "";
        } else {
            segment = path.substr(0, dot_pos);
            path = path.substr(dot_pos + 1);
        }

        // Ищем ключ
        current = current->get(std::string(segment));
        if (!current) {
            return nullptr;
        }
    }

    return current;
}

const Value* find_with_aliases(const Value& value, std::string_view key) {
    // SPEC-SLICE-007 FACT-012: алиасы для tau_engine
    //
    // Event.System.Provider → Event.System.Provider_attributes.Name
    // Event.System.TimeCreated → Event.System.TimeCreated_attributes.SystemTime

    if (key == "Event.System.Provider") {
        return find_by_path(value, "Event.System.Provider_attributes.Name");
    }

    if (key == "Event.System.TimeCreated") {
        return find_by_path(value, "Event.System.TimeCreated_attributes.SystemTime");
    }

    // Без алиаса — прямой поиск
    return find_by_path(value, key);
}

}  // namespace chainsaw::evtx
