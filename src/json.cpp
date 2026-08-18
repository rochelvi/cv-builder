#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace js {
namespace {

const Value kNull;

// Appends one code point as UTF-8.
void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

class Parser {
public:
    Parser(const std::string& text) : s_(text) {}

    bool run(Value& out, std::string& error) {
        skip();
        if (!value(out, 0)) {
            error = error_.empty() ? "unexpected end of input" : error_;
            return false;
        }
        skip();
        if (i_ != s_.size()) {
            error = "trailing data at byte " + std::to_string(i_);
            return false;
        }
        return true;
    }

private:
    void skip() {
        while (i_ < s_.size()) {
            char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++i_;
            else
                break;
        }
    }

    bool fail(const char* what) {
        if (error_.empty()) error_ = std::string(what) + " at byte " + std::to_string(i_);
        return false;
    }

    bool literal(const char* word) {
        size_t n = 0;
        while (word[n]) ++n;
        if (s_.compare(i_, n, word) != 0) return fail("bad literal");
        i_ += n;
        return true;
    }

    bool value(Value& out, int depth) {
        if (depth > 64) return fail("nesting too deep");
        if (i_ >= s_.size()) return fail("unexpected end");
        switch (s_[i_]) {
            case '{': return object(out, depth);
            case '[': return array(out, depth);
            case '"': {
                std::string str;
                if (!string(str)) return false;
                out = Value(std::move(str));
                return true;
            }
            case 't': if (!literal("true")) return false; out = Value(true); return true;
            case 'f': if (!literal("false")) return false; out = Value(false); return true;
            case 'n': if (!literal("null")) return false; out = Value(); return true;
            default: return number(out);
        }
    }

    bool object(Value& out, int depth) {
        ++i_;  // '{'
        Object fields;
        skip();
        if (i_ < s_.size() && s_[i_] == '}') { ++i_; out = Value(std::move(fields)); return true; }
        for (;;) {
            skip();
            std::string key;
            if (!string(key)) return false;
            skip();
            if (i_ >= s_.size() || s_[i_] != ':') return fail("expected ':'");
            ++i_;
            skip();
            Value item;
            if (!value(item, depth + 1)) return false;
            fields.emplace_back(std::move(key), std::move(item));
            skip();
            if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; break; }
            return fail("expected ',' or '}'");
        }
        out = Value(std::move(fields));
        return true;
    }

    bool array(Value& out, int depth) {
        ++i_;  // '['
        Array items;
        skip();
        if (i_ < s_.size() && s_[i_] == ']') { ++i_; out = Value(std::move(items)); return true; }
        for (;;) {
            skip();
            Value item;
            if (!value(item, depth + 1)) return false;
            items.push_back(std::move(item));
            skip();
            if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; break; }
            return fail("expected ',' or ']'");
        }
        out = Value(std::move(items));
        return true;
    }

    bool hex4(uint32_t& out) {
        if (i_ + 4 > s_.size()) return fail("truncated \\u escape");
        out = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s_[i_ + k];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<uint32_t>(c - 'A' + 10);
            else return fail("bad hex digit");
        }
        i_ += 4;
        return true;
    }

    bool string(std::string& out) {
        if (i_ >= s_.size() || s_[i_] != '"') return fail("expected string");
        ++i_;
        out.clear();
        while (i_ < s_.size()) {
            unsigned char c = static_cast<unsigned char>(s_[i_]);
            if (c == '"') { ++i_; return true; }
            if (c != '\\') { out += static_cast<char>(c); ++i_; continue; }
            ++i_;
            if (i_ >= s_.size()) return fail("truncated escape");
            char e = s_[i_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 1 < s_.size() &&
                        s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                        size_t save = i_;
                        i_ += 2;
                        uint32_t low = 0;
                        if (!hex4(low)) return false;
                        if (low >= 0xDC00 && low <= 0xDFFF)
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        else
                            i_ = save;  // a lone high surrogate; emit it as-is
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("unknown escape");
            }
        }
        return fail("unterminated string");
    }

    bool number(Value& out) {
        size_t start = i_;
        if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
        bool digits = false;
        while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') { ++i_; digits = true; }
        if (i_ < s_.size() && s_[i_] == '.') {
            ++i_;
            while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') { ++i_; digits = true; }
        }
        if (digits && i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            ++i_;
            if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
            while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
        }
        if (!digits) return fail("expected value");
        out = Value(std::strtod(s_.substr(start, i_ - start).c_str(), nullptr));
        return true;
    }

    const std::string& s_;
    size_t i_ = 0;
    std::string error_;
};

void escape(const std::string& in, std::string& out) {
    out += '"';
    for (unsigned char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);  // UTF-8 passes through untouched
                }
        }
    }
    out += '"';
}

void writeNumber(double v, std::string& out) {
    char buf[40];
    if (v == std::floor(v) && std::fabs(v) < 1e15)
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v));
    else
        std::snprintf(buf, sizeof buf, "%.10g", v);
    out += buf;
}

void write(const Value& v, int indent, int level, std::string& out) {
    const std::string pad = indent > 0 ? std::string(static_cast<size_t>(indent * (level + 1)), ' ') : std::string();
    const std::string padEnd = indent > 0 ? std::string(static_cast<size_t>(indent * level), ' ') : std::string();
    const char* nl = indent > 0 ? "\n" : "";
    switch (v.type()) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += v.asBool() ? "true" : "false"; break;
        case Type::Number: writeNumber(v.asNumber(), out); break;
        case Type::String: escape(v.asString(), out); break;
        case Type::Array: {
            const Array& items = v.arr();
            if (items.empty()) { out += "[]"; break; }
            out += '['; out += nl;
            for (size_t k = 0; k < items.size(); ++k) {
                out += pad;
                write(items[k], indent, level + 1, out);
                if (k + 1 < items.size()) out += ',';
                out += nl;
            }
            out += padEnd; out += ']';
            break;
        }
        case Type::Object: {
            const Object& fields = v.obj();
            if (fields.empty()) { out += "{}"; break; }
            out += '{'; out += nl;
            for (size_t k = 0; k < fields.size(); ++k) {
                out += pad;
                escape(fields[k].first, out);
                out += indent > 0 ? ": " : ":";
                write(fields[k].second, indent, level + 1, out);
                if (k + 1 < fields.size()) out += ',';
                out += nl;
            }
            out += padEnd; out += '}';
            break;
        }
    }
}

}  // namespace

bool Value::asBool(bool fallback) const {
    switch (type_) {
        case Type::Bool: return bool_;
        case Type::Number: return num_ != 0.0;
        case Type::Null: return false;
        default: return fallback;
    }
}

const Value& Value::operator[](const std::string& key) const {
    if (type_ == Type::Object)
        for (const auto& field : obj_)
            if (field.first == key) return field.second;
    return kNull;
}

bool parse(const std::string& text, Value& out, std::string& error) {
    // Skip a UTF-8 BOM; Notepad and friends like to add one.
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        std::string body = text.substr(3);
        return Parser(body).run(out, error);
    }
    return Parser(text).run(out, error);
}

std::string dump(const Value& value, int indent) {
    std::string out;
    write(value, indent, 0, out);
    if (indent > 0) out += '\n';
    return out;
}

}  // namespace js
