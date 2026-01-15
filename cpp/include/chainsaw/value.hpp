// ==============================================================================
// chainsaw/value.hpp - MOD-0006: Каноническая модель документа (Value)
// ==============================================================================
//
// MOD-0006 io::reader, MOD-0007 formats
// SLICE-005: Reader Framework + JSON Parser
// SPEC-SLICE-005: micro-spec поведения
// ADR-0003: RapidJSON как базовая JSON библиотека
//
// Назначение:
// - Каноническое представление документа для pipeline (Value enum)
// - Конверсия из/в RapidJSON Value
// - Явная типизация чисел: UInt64 → Int64 → Double (FACT-027)
//
// Соответствие Rust:
// - upstream/chainsaw/src/value.rs:8-18 (Value enum)
// - upstream/chainsaw/src/value.rs:20-41 (From<Json> for Value)
// - upstream/chainsaw/src/value.rs:43-58 (From<Value> for Json)
//
// ==============================================================================

#ifndef CHAINSAW_VALUE_HPP
#define CHAINSAW_VALUE_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// RapidJSON forward declarations
#include <rapidjson/document.h>

// GCC 13 generates false positives for -Wnull-dereference when using
// std::get on std::variant at high optimization levels.
// See: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=108842
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#endif

namespace chainsaw {

// ----------------------------------------------------------------------------
// Value - каноническая модель документа
// ----------------------------------------------------------------------------
//
// SPEC-SLICE-005 FACT-025: Value явно разделяет Int/UInt/Float
// SPEC-SLICE-005 FACT-026: Object использует hash map (неупорядоченный)
//
// Соответствие Rust value.rs:8-18:
//   pub enum Value {
//       Null, Bool(bool), Float(f64), Int(i64), UInt(u64),
//       String(String), Array(Vec<Value>), Object(FxHashMap<String, Value>)
//   }
//

class Value;

/// Тип для массива значений
using ValueArray = std::vector<Value>;

/// Тип для объекта (map string -> Value)
using ValueObject = std::unordered_map<std::string, Value>;

/// Каноническое представление документа
/// Аналог Rust Value enum (value.rs:8-18)
class Value {
public:
    // Внутренние типы для variant
    struct Null {};
    using Bool = bool;
    using Int64 = std::int64_t;
    using UInt64 = std::uint64_t;
    using Double = double;
    using String = std::string;
    using Array = ValueArray;
    using Object = ValueObject;

private:
    // Variant хранит одно из значений
    std::variant<Null, Bool, Int64, UInt64, Double, String, std::shared_ptr<Array>,
                 std::shared_ptr<Object>>
        data_;

public:
    // -------------------------------------------------------------------------
    // Конструкторы
    // -------------------------------------------------------------------------

    /// Создать Null значение
    Value() : data_(Null{}) {}

    /// Создать Bool значение
    explicit Value(bool v) : data_(v) {}

    /// Создать Int64 значение
    explicit Value(std::int64_t v) : data_(v) {}

    /// Создать UInt64 значение
    explicit Value(std::uint64_t v) : data_(v) {}

    /// Создать Double значение
    explicit Value(double v) : data_(v) {}

    /// Создать String значение
    explicit Value(std::string v) : data_(std::move(v)) {}

    /// Создать String значение из C-строки
    explicit Value(const char* v) : data_(std::string(v)) {}

    /// Создать Array значение
    explicit Value(Array v) : data_(std::make_shared<Array>(std::move(v))) {}

    /// Создать Object значение
    explicit Value(Object v) : data_(std::make_shared<Object>(std::move(v))) {}

    // -------------------------------------------------------------------------
    // Статические фабричные методы
    // -------------------------------------------------------------------------

    /// Создать Null
    static Value make_null() { return Value(); }

    /// Создать Bool
    static Value make_bool(bool v) { return Value(v); }

    /// Создать Int64
    static Value make_int(std::int64_t v) { return Value(v); }

    /// Создать UInt64
    static Value make_uint(std::uint64_t v) { return Value(v); }

    /// Создать Double
    static Value make_double(double v) { return Value(v); }

    /// Создать String
    static Value make_string(std::string v) { return Value(std::move(v)); }

    /// Создать пустой Array
    static Value make_array() { return Value(Array{}); }

    /// Создать пустой Object
    static Value make_object() { return Value(Object{}); }

    // -------------------------------------------------------------------------
    // Проверка типа
    // -------------------------------------------------------------------------

    bool is_null() const { return std::holds_alternative<Null>(data_); }
    bool is_bool() const { return std::holds_alternative<Bool>(data_); }
    bool is_int() const { return std::holds_alternative<Int64>(data_); }
    bool is_uint() const { return std::holds_alternative<UInt64>(data_); }
    bool is_double() const { return std::holds_alternative<Double>(data_); }
    bool is_string() const { return std::holds_alternative<String>(data_); }
    bool is_array() const { return std::holds_alternative<std::shared_ptr<Array>>(data_); }
    bool is_object() const { return std::holds_alternative<std::shared_ptr<Object>>(data_); }

    /// Проверка на числовой тип (int, uint или double)
    bool is_number() const { return is_int() || is_uint() || is_double(); }

    // -------------------------------------------------------------------------
    // Доступ к значению
    // -------------------------------------------------------------------------

    /// Получить bool значение (undefined behavior если не is_bool())
    Bool as_bool() const { return std::get<Bool>(data_); }

    /// Получить int64 значение (undefined behavior если не is_int())
    Int64 as_int() const { return std::get<Int64>(data_); }

    /// Получить uint64 значение (undefined behavior если не is_uint())
    UInt64 as_uint() const { return std::get<UInt64>(data_); }

    /// Получить double значение (undefined behavior если не is_double())
    Double as_double() const { return std::get<Double>(data_); }

    /// Получить string значение (undefined behavior если не is_string())
    const String& as_string() const { return std::get<String>(data_); }

    /// Получить array (undefined behavior если не is_array())
    const Array& as_array() const { return *std::get<std::shared_ptr<Array>>(data_); }

    /// Получить array для модификации
    Array& as_array_mut() { return *std::get<std::shared_ptr<Array>>(data_); }

    /// Получить object (undefined behavior если не is_object())
    const Object& as_object() const { return *std::get<std::shared_ptr<Object>>(data_); }

    /// Получить object для модификации
    Object& as_object_mut() { return *std::get<std::shared_ptr<Object>>(data_); }

    // -------------------------------------------------------------------------
    // Безопасный доступ (возвращает nullptr если тип не совпадает)
    // -------------------------------------------------------------------------

    const Bool* get_bool() const {
        return std::holds_alternative<Bool>(data_) ? &std::get<Bool>(data_) : nullptr;
    }

    const Int64* get_int() const {
        return std::holds_alternative<Int64>(data_) ? &std::get<Int64>(data_) : nullptr;
    }

    const UInt64* get_uint() const {
        return std::holds_alternative<UInt64>(data_) ? &std::get<UInt64>(data_) : nullptr;
    }

    const Double* get_double() const {
        return std::holds_alternative<Double>(data_) ? &std::get<Double>(data_) : nullptr;
    }

    const String* get_string() const {
        return std::holds_alternative<String>(data_) ? &std::get<String>(data_) : nullptr;
    }

    const Array* get_array() const {
        auto* ptr = std::get_if<std::shared_ptr<Array>>(&data_);
        return ptr ? ptr->get() : nullptr;
    }

    const Object* get_object() const {
        auto* ptr = std::get_if<std::shared_ptr<Object>>(&data_);
        return ptr ? ptr->get() : nullptr;
    }

    // -------------------------------------------------------------------------
    // Операции с массивом
    // -------------------------------------------------------------------------

    /// Добавить элемент в массив (только если is_array())
    void push_back(Value v) {
        if (auto* arr = get_array_mut()) {
            arr->push_back(std::move(v));
        }
    }

    /// Размер массива (0 если не массив)
    std::size_t array_size() const {
        if (const auto* arr = get_array()) {
            return arr->size();
        }
        return 0;
    }

    /// Доступ к элементу массива по индексу
    const Value* at(std::size_t index) const {
        if (const auto* arr = get_array()) {
            if (index < arr->size()) {
                return &(*arr)[index];
            }
        }
        return nullptr;
    }

    // -------------------------------------------------------------------------
    // Операции с объектом
    // -------------------------------------------------------------------------

    /// Установить поле объекта (только если is_object())
    void set(const std::string& key, Value v) {
        if (auto* obj = get_object_mut()) {
            (*obj)[key] = std::move(v);
        }
    }

    /// Получить поле объекта по ключу (nullptr если не найдено или не объект)
    const Value* get(const std::string& key) const {
        if (const auto* obj = get_object()) {
            auto it = obj->find(key);
            if (it != obj->end()) {
                return &it->second;
            }
        }
        return nullptr;
    }

    /// Проверить наличие ключа в объекте
    bool has(const std::string& key) const {
        if (const auto* obj = get_object()) {
            return obj->find(key) != obj->end();
        }
        return false;
    }

    /// Размер объекта (0 если не объект)
    std::size_t object_size() const {
        if (const auto* obj = get_object()) {
            return obj->size();
        }
        return 0;
    }

    // -------------------------------------------------------------------------
    // Конверсия из RapidJSON
    // -------------------------------------------------------------------------

    /// Конвертировать из RapidJSON Value
    /// SPEC-SLICE-005 FACT-027: Number → UInt → Int → Float
    static Value from_rapidjson(const rapidjson::Value& json);

    // -------------------------------------------------------------------------
    // Конверсия в RapidJSON
    // -------------------------------------------------------------------------

    /// Конвертировать в RapidJSON Value
    /// SPEC-SLICE-005 FACT-029, FACT-030
    void to_rapidjson(rapidjson::Value& out, rapidjson::Document::AllocatorType& alloc) const;

    /// Создать новый RapidJSON Document из этого Value
    rapidjson::Document to_rapidjson_document() const;

private:
    Array* get_array_mut() {
        auto* ptr = std::get_if<std::shared_ptr<Array>>(&data_);
        return ptr ? ptr->get() : nullptr;
    }

    Object* get_object_mut() {
        auto* ptr = std::get_if<std::shared_ptr<Object>>(&data_);
        return ptr ? ptr->get() : nullptr;
    }
};

}  // namespace chainsaw

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#endif  // CHAINSAW_VALUE_HPP
