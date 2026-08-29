// Minimal dependency-free JSON: parse, build, dump. UTF-8 in, UTF-8 out.
//
// Object keys keep insertion order, so a saved file reads in the same order as
// the model declares its fields - the Python version produced that layout and
// existing cv.json files should keep looking familiar.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace js {

class Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
public:
    Value() : type_(Type::Null) {}
    Value(bool v) : type_(Type::Bool), bool_(v) {}
    Value(double v) : type_(Type::Number), num_(v) {}
    Value(int v) : type_(Type::Number), num_(v) {}
    Value(std::string v) : type_(Type::String), str_(std::move(v)) {}
    Value(const char* v) : type_(Type::String), str_(v) {}
    Value(Array v) : type_(Type::Array), arr_(std::move(v)) {}
    Value(Object v) : type_(Type::Object), obj_(std::move(v)) {}

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Tolerant readers: the wrong type (or a missing key) yields the fallback,
    // which is what keeps hand-edited or older files loadable.
    std::string asString(const std::string& fallback = std::string()) const {
        return type_ == Type::String ? str_ : fallback;
    }
    double asNumber(double fallback = 0.0) const {
        return type_ == Type::Number ? num_ : fallback;
    }
    bool asBool(bool fallback = false) const;

    const Array& arr() const { return arr_; }
    const Object& obj() const { return obj_; }

    // Missing keys return a shared null value, so chains like
    // v["theme"]["accent"].asString() are safe.
    const Value& operator[](const std::string& key) const;

    void set(std::string key, Value value) { obj_.emplace_back(std::move(key), std::move(value)); }

private:
    Type type_;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    Array arr_;
    Object obj_;
};

// Returns false and fills `error` on malformed input.
bool parse(const std::string& text, Value& out, std::string& error);

// `indent` > 0 pretty-prints. Non-ASCII is emitted as raw UTF-8 (never \uXXXX),
// matching the Python side's ensure_ascii=False.
std::string dump(const Value& value, int indent = 2);

}  // namespace js
