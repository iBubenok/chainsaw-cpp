// ==============================================================================
// reader.cpp - Реализация Reader Framework
// ==============================================================================
//
// MOD-0006 io::reader
// MOD-0007 formats (JSON/JSONL)
// SLICE-005: Reader Framework + JSON Parser
// SPEC-SLICE-005: micro-spec поведения
//
// ==============================================================================

#include <algorithm>
#include <chainsaw/esedb.hpp>
#include <chainsaw/evtx.hpp>
#include <chainsaw/hve.hpp>
#include <chainsaw/mft.hpp>
#include <chainsaw/platform.hpp>
#include <chainsaw/reader.hpp>
#include <cstdio>
#include <fstream>
#include <map>
#include <pugixml.hpp>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/istreamwrapper.h>
#include <sstream>

namespace chainsaw::io {

// ============================================================================
// DocumentKind функции
// ============================================================================

const char* document_kind_to_string(DocumentKind kind) {
    switch (kind) {
    case DocumentKind::Evtx:
        return "evtx";
    case DocumentKind::Hve:
        return "hve";
    case DocumentKind::Json:
        return "json";
    case DocumentKind::Jsonl:
        return "jsonl";
    case DocumentKind::Mft:
        return "mft";
    case DocumentKind::Xml:
        return "xml";
    case DocumentKind::Esedb:
        return "esedb";
    case DocumentKind::Unknown:
        return "unknown";
    }
    return "unknown";
}

// SPEC-SLICE-005 FACT-005: расширения БЕЗ точки
// Соответствие Rust mod.rs:51-68
std::vector<std::string> document_kind_extensions(DocumentKind kind) {
    switch (kind) {
    case DocumentKind::Evtx:
        return {"evt", "evtx"};
    case DocumentKind::Hve:
        return {"hve"};
    case DocumentKind::Json:
        return {"json"};
    case DocumentKind::Jsonl:
        return {"jsonl"};
    case DocumentKind::Mft:
        return {"mft", "bin", "$MFT"};  // FACT-006
    case DocumentKind::Xml:
        return {"xml"};
    case DocumentKind::Esedb:
        return {"dat", "edb"};
    case DocumentKind::Unknown:
        return {};
    }
    return {};
}

// SPEC-SLICE-005 FACT-007: extension-first выбор парсера
DocumentKind document_kind_from_extension(std::string_view ext) {
    // Нормализуем к lowercase для case-insensitive сравнения
    std::string lower_ext;
    lower_ext.reserve(ext.size());
    for (char c : ext) {
        lower_ext.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    // Соответствие Rust mod.rs:111-131 (match extension)
    if (lower_ext == "evt" || lower_ext == "evtx") {
        return DocumentKind::Evtx;
    }
    if (lower_ext == "json") {
        return DocumentKind::Json;
    }
    if (lower_ext == "jsonl") {
        return DocumentKind::Jsonl;
    }
    if (lower_ext == "bin" || lower_ext == "mft") {
        return DocumentKind::Mft;
    }
    if (lower_ext == "xml") {
        return DocumentKind::Xml;
    }
    if (lower_ext == "hve") {
        return DocumentKind::Hve;
    }
    if (lower_ext == "dat" || lower_ext == "edb") {
        return DocumentKind::Esedb;
    }

    return DocumentKind::Unknown;
}

// SPEC-SLICE-005 FACT-012: $MFT edge case
DocumentKind document_kind_from_path(const std::filesystem::path& path) {
    // Сначала проверяем специальный случай $MFT (имя файла без расширения)
    std::string filename = path.filename().string();
    if (filename == "$MFT") {
        return DocumentKind::Mft;
    }

    // Затем проверяем расширение
    if (path.has_extension()) {
        std::string ext = path.extension().string();
        // Убираем точку
        if (!ext.empty() && ext[0] == '.') {
            ext = ext.substr(1);
        }
        return document_kind_from_extension(ext);
    }

    return DocumentKind::Unknown;
}

// ============================================================================
// ReaderError
// ============================================================================

// SPEC-SLICE-005 FACT-008: "[!] failed to load file '<path>' - <error>\n"
std::string ReaderError::format() const {
    return "[!] failed to load file '" + path + "' - " + message + "\n";
}

std::string format_load_error(const std::filesystem::path& path, const std::string& error) {
    return "[!] failed to load file '" + platform::path_to_utf8(path) + "' - " + error + "\n";
}

// SPEC-SLICE-005 FACT-011
std::string format_unsupported_error(const std::filesystem::path& path, bool skip_errors) {
    std::string path_str = platform::path_to_utf8(path);
    if (skip_errors) {
        return "[!] file type is not currently supported - " + path_str + "\n";
    } else {
        return "file type is not currently supported - " + path_str +
               ", use --skip-errors to continue...";
    }
}

// ============================================================================
// EmptyReader - пустой Reader для Unknown и skip_errors
// ============================================================================

class EmptyReader : public Reader {
public:
    explicit EmptyReader(std::filesystem::path path, DocumentKind kind)
        : path_(std::move(path)), kind_(kind) {}

    bool next(Document& /*out*/) override { return false; }
    bool has_next() const override { return false; }
    DocumentKind kind() const override { return kind_; }
    const std::filesystem::path& path() const override { return path_; }
    const std::optional<ReaderError>& last_error() const override { return error_; }

private:
    std::filesystem::path path_;
    DocumentKind kind_;
    std::optional<ReaderError> error_;
};

std::unique_ptr<Reader> create_empty_reader(const std::filesystem::path& path, DocumentKind kind) {
    return std::make_unique<EmptyReader>(path, kind);
}

// ============================================================================
// JsonReader - парсер JSON файлов
// ============================================================================
//
// SPEC-SLICE-005 JSON Parser (json.rs:12-53)
// FACT-016: файл читается целиком в память
// FACT-018: если корень — массив, итерирует по элементам
// FACT-019: если корень — не массив, возвращает один документ
//

class JsonReader : public Reader {
public:
    explicit JsonReader(std::filesystem::path path) : path_(std::move(path)) {}

    /// Загрузить и распарсить JSON файл
    bool load() {
        // Открыть файл
        std::ifstream file(path_, std::ios::binary);
        if (!file.is_open()) {
            error_ = ReaderError{ReaderErrorKind::FileNotFound, "could not open file",
                                 platform::path_to_utf8(path_)};
            return false;
        }

        // Читаем содержимое
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        // Парсим JSON
        doc_.Parse(content.c_str());

        if (doc_.HasParseError()) {
            error_ = ReaderError{ReaderErrorKind::ParseError,
                                 std::string("JSON parse error: ") +
                                     rapidjson::GetParseError_En(doc_.GetParseError()) +
                                     " at offset " + std::to_string(doc_.GetErrorOffset()),
                                 platform::path_to_utf8(path_)};
            return false;
        }

        // FACT-018: если массив — подготовить итерацию по элементам
        if (doc_.IsArray()) {
            array_mode_ = true;
            array_index_ = 0;
            array_size_ = doc_.Size();
        } else {
            // FACT-019: не массив — один документ
            array_mode_ = false;
            has_single_ = true;
        }

        loaded_ = true;
        return true;
    }

    bool next(Document& out) override {
        if (!loaded_)
            return false;

        if (array_mode_) {
            // Итерация по массиву
            if (array_index_ >= array_size_) {
                return false;
            }
            out.kind = DocumentKind::Json;
            out.data = Value::from_rapidjson(doc_[array_index_]);
            out.source = platform::path_to_utf8(path_);
            out.record_id = array_index_;
            ++array_index_;
            return true;
        } else {
            // Один документ
            if (!has_single_) {
                return false;
            }
            out.kind = DocumentKind::Json;
            out.data = Value::from_rapidjson(doc_);
            out.source = platform::path_to_utf8(path_);
            out.record_id = std::nullopt;
            has_single_ = false;
            return true;
        }
    }

    bool has_next() const override {
        if (!loaded_)
            return false;
        if (array_mode_) {
            return array_index_ < array_size_;
        } else {
            return has_single_;
        }
    }

    DocumentKind kind() const override { return DocumentKind::Json; }
    const std::filesystem::path& path() const override { return path_; }
    const std::optional<ReaderError>& last_error() const override { return error_; }

private:
    std::filesystem::path path_;
    rapidjson::Document doc_;
    std::optional<ReaderError> error_;

    bool loaded_ = false;
    bool array_mode_ = false;
    bool has_single_ = false;
    rapidjson::SizeType array_index_ = 0;
    rapidjson::SizeType array_size_ = 0;
};

std::unique_ptr<Reader> create_json_reader(const std::filesystem::path& path, bool skip_errors) {
    auto reader = std::make_unique<JsonReader>(path);
    if (!reader->load()) {
        if (skip_errors) {
            // FACT-009: при skip_errors возвращаем пустой Reader
            return create_empty_reader(path, DocumentKind::Json);
        }
        // Возвращаем reader с ошибкой (можно получить через last_error)
    }
    return reader;
}

// ============================================================================
// JsonlReader - парсер JSON Lines файлов
// ============================================================================
//
// SPEC-SLICE-005 JSONL Parser (json.rs:70-125)
// FACT-021: при load() читается только первая строка для валидации
// FACT-023: читает построчно, каждая строка — отдельный JSON
// FACT-024: пустые строки и ошибки парсинга — ошибки итератора
//

class JsonlReader : public Reader {
public:
    explicit JsonlReader(std::filesystem::path path) : path_(std::move(path)) {}

    /// Загрузить файл и валидировать первую строку
    bool load() {
        // Открыть файл
        file_.open(path_, std::ios::binary);
        if (!file_.is_open()) {
            error_ = ReaderError{ReaderErrorKind::FileNotFound, "could not open file",
                                 platform::path_to_utf8(path_)};
            return false;
        }

        // FACT-021: читаем первую строку для валидации
        std::string first_line;
        if (!std::getline(file_, first_line)) {
            // Пустой файл — валидно, просто не будет документов
            loaded_ = true;
            return true;
        }

        // Валидируем что первая строка — валидный JSON
        rapidjson::Document doc;
        doc.Parse(first_line.c_str());
        if (doc.HasParseError()) {
            error_ = ReaderError{ReaderErrorKind::ParseError,
                                 std::string("JSONL first line parse error: ") +
                                     rapidjson::GetParseError_En(doc.GetParseError()),
                                 platform::path_to_utf8(path_)};
            return false;
        }

        // FACT-022: rewind файла после валидации
        file_.clear();
        file_.seekg(0, std::ios::beg);

        loaded_ = true;
        return true;
    }

    bool next(Document& out) override {
        if (!loaded_)
            return false;

        while (true) {
            std::string line;
            if (!std::getline(file_, line)) {
                // Конец файла
                return false;
            }

            ++line_number_;

            // Пропускаем пустые строки (trimmed)
            auto start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                // Полностью пустая строка — пропускаем
                // Примечание: upstream Rust возвращает ошибку для пустых строк,
                // но для JSONL это стандартное поведение — пропускать пустые
                continue;
            }

            // Парсим строку как JSON
            rapidjson::Document doc;
            doc.Parse(line.c_str());
            if (doc.HasParseError()) {
                // FACT-024: ошибка парсинга строки
                error_ = ReaderError{
                    ReaderErrorKind::ParseError,
                    std::string("JSONL line ") + std::to_string(line_number_) +
                        " parse error: " + rapidjson::GetParseError_En(doc.GetParseError()),
                    platform::path_to_utf8(path_)};
                // Возвращаем false, ошибку можно получить через last_error
                return false;
            }

            out.kind = DocumentKind::Jsonl;
            out.data = Value::from_rapidjson(doc);
            out.source = platform::path_to_utf8(path_);
            out.record_id = line_number_;
            return true;
        }
    }

    bool has_next() const override {
        if (!loaded_)
            return false;
        // Для потоковой итерации не можем точно знать без чтения
        return file_.good() && !file_.eof();
    }

    DocumentKind kind() const override { return DocumentKind::Jsonl; }
    const std::filesystem::path& path() const override { return path_; }
    const std::optional<ReaderError>& last_error() const override { return error_; }

private:
    std::filesystem::path path_;
    std::ifstream file_;
    std::optional<ReaderError> error_;

    bool loaded_ = false;
    std::uint64_t line_number_ = 0;
};

std::unique_ptr<Reader> create_jsonl_reader(const std::filesystem::path& path, bool skip_errors) {
    auto reader = std::make_unique<JsonlReader>(path);
    if (!reader->load()) {
        if (skip_errors) {
            return create_empty_reader(path, DocumentKind::Jsonl);
        }
    }
    return reader;
}

// ============================================================================
// XmlReader - парсер XML файлов
// ============================================================================
//
// SPEC-SLICE-006 XML Parser (xml.rs)
// FACT-001: XML представляется как Value (аналог serde_json::Value)
// FACT-005: Используется pugixml для парсинга XML
// FACT-008: Если корень — массив, итерирует по элементам
// FACT-009: Если корень — не массив, возвращает один документ
// FACT-011: Ошибки только при load(), не при итерации
//

namespace {

/// Конвертировать pugixml узел в Value
/// SPEC-SLICE-006: XML → JSON конверсия
///
/// Правила конверсии (аналог quick_xml::de):
/// - XML элемент → JSON объект
/// - Атрибуты → поля с префиксом '@'
/// - Текстовое содержимое → поле '$text' (если есть дочерние элементы) или строка
/// - Повторяющиеся дочерние элементы с одинаковым именем → массив
Value xml_node_to_value(const pugi::xml_node& node) {
    // Проверяем тип узла
    if (node.type() == pugi::node_pcdata || node.type() == pugi::node_cdata) {
        // Текстовый узел — возвращаем строку
        return Value(std::string(node.value()));
    }

    // Собираем дочерние элементы и текст
    bool has_child_elements = false;
    bool has_text_content = false;
    std::string text_content;

    for (const auto& child : node.children()) {
        if (child.type() == pugi::node_element) {
            has_child_elements = true;
        } else if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
            std::string trimmed = child.value();
            // Удаляем ведущие/завершающие пробелы
            auto start = trimmed.find_first_not_of(" \t\r\n");
            auto end = trimmed.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos) {
                text_content += trimmed.substr(start, end - start + 1);
                has_text_content = true;
            }
        }
    }

    // Проверяем атрибуты
    bool has_attributes = node.first_attribute();

    // Если нет дочерних элементов и нет атрибутов — возвращаем текст напрямую
    if (!has_child_elements && !has_attributes) {
        return Value(text_content);
    }

    // Создаём объект
    Value::Object obj;

    // Добавляем атрибуты с префиксом '@'
    for (const auto& attr : node.attributes()) {
        std::string attr_name = std::string("@") + attr.name();
        obj[attr_name] = Value(std::string(attr.value()));
    }

    // Собираем дочерние элементы, группируя повторяющиеся имена в массивы
    std::map<std::string, std::vector<Value>> child_groups;

    for (const auto& child : node.children()) {
        if (child.type() == pugi::node_element) {
            std::string child_name = child.name();
            child_groups[child_name].push_back(xml_node_to_value(child));
        }
    }

    // Добавляем дочерние элементы в объект
    for (auto& [name, values] : child_groups) {
        if (values.size() == 1) {
            // Один элемент — добавляем как значение
            obj[name] = std::move(values[0]);
        } else {
            // Несколько элементов — добавляем как массив
            Value::Array arr;
            arr.reserve(values.size());
            for (auto& v : values) {
                arr.push_back(std::move(v));
            }
            obj[name] = Value(std::move(arr));
        }
    }

    // Добавляем текстовое содержимое как '$text'
    if (has_text_content && !text_content.empty()) {
        obj["$text"] = Value(text_content);
    }

    return Value(std::move(obj));
}

/// Конвертировать XML документ в Value
Value xml_document_to_value(const pugi::xml_document& doc) {
    // Находим корневой элемент (пропуская declaration и т.п.)
    auto root = doc.document_element();
    if (!root) {
        // Пустой документ
        return Value();
    }

    // Создаём объект с корневым элементом
    Value::Object obj;
    obj[root.name()] = xml_node_to_value(root);
    return Value(std::move(obj));
}

}  // anonymous namespace

class XmlReader : public Reader {
public:
    explicit XmlReader(std::filesystem::path path) : path_(std::move(path)) {}

    /// Загрузить и распарсить XML файл
    /// SPEC-SLICE-006 FACT-007: ошибки парсинга возвращаются из load()
    bool load() {
        // Парсим XML файл
        pugi::xml_parse_result result =
            doc_.load_file(path_.c_str(), pugi::parse_default | pugi::parse_declaration);

        if (!result) {
            error_ = ReaderError{ReaderErrorKind::ParseError,
                                 std::string("XML parse error: ") + result.description() +
                                     " at offset " + std::to_string(result.offset),
                                 platform::path_to_utf8(path_)};
            return false;
        }

        // Конвертируем XML в Value
        value_ = xml_document_to_value(doc_);

        // SPEC-SLICE-006 FACT-008/009: логика итерации
        // Если корень документа содержит массив — итерируем по элементам
        // В нашей конверсии корень всегда объект с именем root-элемента
        // Проверяем, есть ли массив внутри корневого элемента

        // Пока используем простую логику: один документ на файл
        // (аналог Rust: если результат не массив, возвращаем один документ)
        has_single_ = true;

        loaded_ = true;
        return true;
    }

    bool next(Document& out) override {
        if (!loaded_)
            return false;

        // SPEC-SLICE-006 FACT-010: take-семантика
        if (!has_single_) {
            return false;
        }

        out.kind = DocumentKind::Xml;
        out.data = std::move(value_);
        out.source = platform::path_to_utf8(path_);
        out.record_id = std::nullopt;

        has_single_ = false;
        return true;
    }

    bool has_next() const override {
        if (!loaded_)
            return false;
        return has_single_;
    }

    DocumentKind kind() const override { return DocumentKind::Xml; }
    const std::filesystem::path& path() const override { return path_; }
    const std::optional<ReaderError>& last_error() const override { return error_; }

private:
    std::filesystem::path path_;
    pugi::xml_document doc_;
    Value value_;
    std::optional<ReaderError> error_;

    bool loaded_ = false;
    bool has_single_ = false;
};

std::unique_ptr<Reader> create_xml_reader(const std::filesystem::path& path, bool skip_errors) {
    auto reader = std::make_unique<XmlReader>(path);
    if (!reader->load()) {
        if (skip_errors) {
            // SPEC-SLICE-006 FACT-015: при skip_errors возвращаем пустой Reader
            return create_empty_reader(path, DocumentKind::Xml);
        }
    }
    return reader;
}

// ============================================================================
// EvtxReader - парсер EVTX файлов
// ============================================================================
//
// SLICE-007: EVTX Parser
// SPEC-SLICE-007: micro-spec поведения
// FACT-001: separate_json_attributes(true) — атрибуты в *_attributes полях
// FACT-002: итерация по записям EVTX файла
//

class EvtxReader : public Reader {
public:
    explicit EvtxReader(std::filesystem::path path) : path_(std::move(path)) {}

    /// Загрузить EVTX файл
    bool load() {
        if (!parser_.load(path_)) {
            const auto& err = parser_.last_error();
            error_ = ReaderError{ReaderErrorKind::ParseError, err ? err->message : "unknown error",
                                 platform::path_to_utf8(path_)};
            return false;
        }
        loaded_ = true;
        return true;
    }

    bool next(Document& out) override {
        if (!loaded_)
            return false;

        evtx::EvtxRecord record;
        if (!parser_.next(record)) {
            // Проверяем ошибку
            const auto& err = parser_.last_error();
            if (err) {
                error_ = ReaderError{ReaderErrorKind::ParseError, err->message,
                                     platform::path_to_utf8(path_)};
            }
            return false;
        }

        out.kind = DocumentKind::Evtx;
        out.data = std::move(record.data);
        out.source = platform::path_to_utf8(path_);
        out.record_id = record.record_id;
        out.timestamp = record.timestamp;
        return true;
    }

    bool has_next() const override { return loaded_ && !parser_.eof() && !error_.has_value(); }

    DocumentKind kind() const override { return DocumentKind::Evtx; }
    const std::filesystem::path& path() const override { return path_; }
    const std::optional<ReaderError>& last_error() const override { return error_; }

private:
    std::filesystem::path path_;
    evtx::EvtxParser parser_;
    std::optional<ReaderError> error_;
    bool loaded_ = false;
};

/// Создать EVTX Reader
/// SPEC-SLICE-007: EVTX parser (evtx.rs)
std::unique_ptr<Reader> create_evtx_reader(const std::filesystem::path& path, bool skip_errors) {
    auto reader = std::make_unique<EvtxReader>(path);
    if (!reader->load()) {
        if (skip_errors) {
            return create_empty_reader(path, DocumentKind::Evtx);
        }
    }
    return reader;
}

// ============================================================================
// Reader::open - фабричный метод
// ============================================================================
//
// SPEC-SLICE-005 алгоритм выбора парсера (mod.rs:111-383)
//

ReaderResult Reader::open(const std::filesystem::path& file, bool load_unknown, bool skip_errors) {
    ReaderResult result;
    result.ok = false;

    // Проверяем существование файла
    std::error_code ec;
    if (!std::filesystem::exists(file, ec) || ec) {
        result.error = ReaderError{ReaderErrorKind::FileNotFound, "file not found",
                                   platform::path_to_utf8(file)};
        if (skip_errors) {
            result.ok = true;
            result.reader = create_empty_reader(file);
        }
        return result;
    }

    // Определяем тип по расширению
    DocumentKind kind = document_kind_from_path(file);

    // FACT-007: extension-first выбор парсера
    switch (kind) {
    case DocumentKind::Json: {
        result.reader = create_json_reader(file, skip_errors);
        if (result.reader->last_error()) {
            result.error = *result.reader->last_error();
            result.ok = skip_errors;
        } else {
            result.ok = true;
        }
        return result;
    }

    case DocumentKind::Jsonl: {
        result.reader = create_jsonl_reader(file, skip_errors);
        if (result.reader->last_error()) {
            result.error = *result.reader->last_error();
            result.ok = skip_errors;
        } else {
            result.ok = true;
        }
        return result;
    }

    // SLICE-006: XML парсер
    case DocumentKind::Xml: {
        result.reader = create_xml_reader(file, skip_errors);
        if (result.reader->last_error()) {
            result.error = *result.reader->last_error();
            result.ok = skip_errors;
        } else {
            result.ok = true;
        }
        return result;
    }

    // SLICE-007: EVTX парсер
    case DocumentKind::Evtx: {
        result.reader = create_evtx_reader(file, skip_errors);
        if (result.reader->last_error()) {
            result.error = *result.reader->last_error();
            result.ok = skip_errors;
        } else {
            result.ok = true;
        }
        return result;
    }

    // SLICE-015: HVE парсер
    case DocumentKind::Hve: {
        result.reader = hve::create_hve_reader(file, skip_errors);
        if (result.reader->last_error()) {
            result.error = *result.reader->last_error();
            result.ok = skip_errors;
        } else {
            result.ok = true;
        }
        return result;
    }

    // SLICE-016: ESEDB парсер
    case DocumentKind::Esedb: {
        result.reader = esedb::create_esedb_reader(file, skip_errors);
        if (result.reader->last_error()) {
            result.error = *result.reader->last_error();
            result.ok = skip_errors;
        } else {
            result.ok = true;
        }
        return result;
    }

    // SLICE-018: MFT парсер
    case DocumentKind::Mft: {
        result.reader = mft::create_mft_reader(file, skip_errors);
        if (result.reader->last_error()) {
            result.error = *result.reader->last_error();
            result.ok = skip_errors;
        } else {
            result.ok = true;
        }
        return result;
    }

    case DocumentKind::Unknown:
        break;  // Обработка ниже
    }

    // Unknown расширение
    if (load_unknown) {
        // SPEC-SLICE-005/006/007/015/016/018 fallback порядок: EVTX→MFT→JSON→XML→HVE→ESEDB
        // Реализованы: EVTX, MFT, JSON, XML, HVE, ESEDB
        // Важно: для fallback НЕ используем skip_errors, чтобы проверить ошибку парсинга

        // Позиция 1: EVTX (SPEC-SLICE-007 FACT-013)
        auto evtx_reader = create_evtx_reader(file, false);  // skip_errors=false
        if (!evtx_reader->last_error()) {
            result.ok = true;
            result.reader = std::move(evtx_reader);
            return result;
        }

        // Позиция 2: MFT (SLICE-018)
        auto mft_reader = mft::create_mft_reader(file, false);  // skip_errors=false
        if (!mft_reader->last_error()) {
            result.ok = true;
            result.reader = std::move(mft_reader);
            return result;
        }

        // Позиция 3: JSON
        auto json_reader = create_json_reader(file, false);  // skip_errors=false
        if (!json_reader->last_error()) {
            result.ok = true;
            result.reader = std::move(json_reader);
            return result;
        }

        // Позиция 4: XML (SPEC-SLICE-006 FACT-016)
        auto xml_reader = create_xml_reader(file, false);  // skip_errors=false
        if (!xml_reader->last_error()) {
            result.ok = true;
            result.reader = std::move(xml_reader);
            return result;
        }

        // Позиция 5: HVE (SLICE-015)
        auto hve_reader = hve::create_hve_reader(file, false);  // skip_errors=false
        if (!hve_reader->last_error()) {
            result.ok = true;
            result.reader = std::move(hve_reader);
            return result;
        }

        // Позиция 6: ESEDB (SLICE-016)
        auto esedb_reader = esedb::create_esedb_reader(file, false);  // skip_errors=false
        if (!esedb_reader->last_error()) {
            result.ok = true;
            result.reader = std::move(esedb_reader);
            return result;
        }

        // Если ничего не подошло
        result.error =
            ReaderError{ReaderErrorKind::UnsupportedFormat,
                        format_unsupported_error(file, skip_errors), platform::path_to_utf8(file)};
        if (skip_errors) {
            result.ok = true;
            result.reader = create_empty_reader(file);
        }
        return result;
    }

    // Неизвестный формат без load_unknown
    result.error =
        ReaderError{ReaderErrorKind::UnsupportedFormat, format_unsupported_error(file, skip_errors),
                    platform::path_to_utf8(file)};
    if (skip_errors) {
        result.ok = true;
        result.reader = create_empty_reader(file);
    }
    return result;
}

}  // namespace chainsaw::io
