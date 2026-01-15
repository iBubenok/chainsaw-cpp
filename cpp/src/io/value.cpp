// ==============================================================================
// value.cpp - Реализация Value (каноническая модель документа)
// ==============================================================================
//
// MOD-0006 io::reader
// SLICE-005: Reader Framework + JSON Parser
// SPEC-SLICE-005: Value конверсии
//
// ==============================================================================

#include <cassert>
#include <chainsaw/value.hpp>
#include <cmath>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <stdexcept>

namespace chainsaw {

// ----------------------------------------------------------------------------
// Value::from_rapidjson - конверсия из RapidJSON
// ----------------------------------------------------------------------------
//
// SPEC-SLICE-005 FACT-027: Number → UInt → Int → Float (порядок приоритета)
//
// Соответствие Rust value.rs:20-41:
//   impl From<Json> for Value {
//       fn from(json: Json) -> Self {
//           match json {
//               Json::Null => Value::Null,
//               Json::Bool(b) => Value::Bool(b),
//               Json::Number(n) => {
//                   if let Some(u) = n.as_u64() { return Value::UInt(u); }
//                   if let Some(i) = n.as_i64() { return Value::Int(i); }
//                   if let Some(f) = n.as_f64() { return Value::Float(f); }
//                   unreachable!()
//               }
//               Json::String(s) => Value::String(s),
//               Json::Array(a) => Value::Array(...),
//               Json::Object(o) => Value::Object(...),
//           }
//       }
//   }
//

Value Value::from_rapidjson(const rapidjson::Value& json) {
    if (json.IsNull()) {
        return Value();
    }

    if (json.IsBool()) {
        return Value(json.GetBool());
    }

    if (json.IsNumber()) {
        // FACT-027: порядок приоритета UInt → Int → Float
        if (json.IsUint64()) {
            return Value(json.GetUint64());
        }
        if (json.IsInt64()) {
            return Value(json.GetInt64());
        }
        if (json.IsDouble()) {
            return Value(json.GetDouble());
        }
        // Fallback для других числовых типов
        if (json.IsUint()) {
            return Value(static_cast<std::uint64_t>(json.GetUint()));
        }
        if (json.IsInt()) {
            return Value(static_cast<std::int64_t>(json.GetInt()));
        }
        // Не должно произойти для валидного JSON
        return Value(json.GetDouble());
    }

    if (json.IsString()) {
        return Value(std::string(json.GetString(), json.GetStringLength()));
    }

    if (json.IsArray()) {
        Array arr;
        arr.reserve(json.Size());
        for (rapidjson::SizeType i = 0; i < json.Size(); ++i) {
            arr.push_back(from_rapidjson(json[i]));
        }
        return Value(std::move(arr));
    }

    if (json.IsObject()) {
        Object obj;
        for (auto it = json.MemberBegin(); it != json.MemberEnd(); ++it) {
            std::string key(it->name.GetString(), it->name.GetStringLength());
            obj[key] = from_rapidjson(it->value);
        }
        return Value(std::move(obj));
    }

    // Неизвестный тип (не должно произойти)
    return Value();
}

// ----------------------------------------------------------------------------
// Value::to_rapidjson - конверсия в RapidJSON
// ----------------------------------------------------------------------------
//
// SPEC-SLICE-005 FACT-029: Float → Number::from_f64()
// SPEC-SLICE-005 FACT-030: panic при невозможности конвертации Float
//
// Соответствие Rust value.rs:43-58
//

void Value::to_rapidjson(rapidjson::Value& out, rapidjson::Document::AllocatorType& alloc) const {
    if (is_null()) {
        out.SetNull();
        return;
    }

    if (is_bool()) {
        out.SetBool(as_bool());
        return;
    }

    if (is_int()) {
        out.SetInt64(as_int());
        return;
    }

    if (is_uint()) {
        out.SetUint64(as_uint());
        return;
    }

    if (is_double()) {
        // FACT-029, FACT-030: конверсия Float
        double d = as_double();
        // RapidJSON SetDouble не делает валидацию, но для соответствия Rust
        // проверяем что число конечное
        if (!std::isfinite(d)) {
            // В Rust это вызывает panic через .expect()
            // В C++ бросаем исключение
            throw std::runtime_error("could not convert float to JSON: non-finite value");
        }
        out.SetDouble(d);
        return;
    }

    if (is_string()) {
        const auto& s = as_string();
        out.SetString(s.c_str(), static_cast<rapidjson::SizeType>(s.size()), alloc);
        return;
    }

    if (is_array()) {
        out.SetArray();
        const auto& arr = as_array();
        out.Reserve(static_cast<rapidjson::SizeType>(arr.size()), alloc);
        for (const auto& elem : arr) {
            rapidjson::Value v;
            elem.to_rapidjson(v, alloc);
            out.PushBack(v, alloc);
        }
        return;
    }

    if (is_object()) {
        out.SetObject();
        const auto& obj = as_object();
        for (const auto& [key, val] : obj) {
            rapidjson::Value k;
            k.SetString(key.c_str(), static_cast<rapidjson::SizeType>(key.size()), alloc);
            rapidjson::Value v;
            val.to_rapidjson(v, alloc);
            out.AddMember(k, v, alloc);
        }
        return;
    }

    // Не должно произойти
    out.SetNull();
}

// ----------------------------------------------------------------------------
// Value::to_rapidjson_document
// ----------------------------------------------------------------------------

rapidjson::Document Value::to_rapidjson_document() const {
    rapidjson::Document doc;
    to_rapidjson(doc, doc.GetAllocator());
    return doc;
}

}  // namespace chainsaw
