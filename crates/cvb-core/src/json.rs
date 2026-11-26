//! A small JSON reader and writer with the exact manners of the C++ one.
//!
//! Not serde_json, deliberately. The resume format is defined by what the old
//! reader accepted and what the old writer produced, and both had habits a
//! general-purpose library does not share: object keys keep their order and the
//! first of two equal keys wins; "+5" and ".5" are numbers; raw control
//! characters inside a string are kept; errors name the byte they stopped at;
//! the writer prints numbers with `%.10g` and never escapes non-ASCII. Saving a
//! file that was only opened must give back the same bytes, so those habits are
//! part of the format.
//!
//! The input is bytes, not `str`: a resume edited by hand may carry a stray
//! invalid byte, and the old reader loaded such a file rather than refusing it.
//! Each string is decoded on its own, invalid sequences becoming U+FFFD.

use crate::numfmt;

#[derive(Debug, Clone, PartialEq, Default)]
pub enum Value {
    #[default]
    Null,
    Bool(bool),
    Number(f64),
    String(String),
    Array(Vec<Value>),
    /// In file order. Duplicate keys are kept; lookups see the first.
    Object(Vec<(String, Value)>),
}

static NULL: Value = Value::Null;

/// Nesting beyond this is refused rather than recursed into.
pub const MAX_DEPTH: usize = 64;

impl Value {
    pub fn object() -> Value {
        Value::Object(Vec::new())
    }

    pub fn is_array(&self) -> bool {
        matches!(self, Value::Array(_))
    }

    pub fn is_object(&self) -> bool {
        matches!(self, Value::Object(_))
    }

    /// The first field called `key`, or null when there is none or this is not
    /// an object - so `v.get("theme").get("accent")` is always safe.
    pub fn get(&self, key: &str) -> &Value {
        if let Value::Object(fields) = self {
            for (name, value) in fields {
                if name == key {
                    return value;
                }
            }
        }
        &NULL
    }

    /// The items of an array; nothing for anything else.
    pub fn items(&self) -> &[Value] {
        match self {
            Value::Array(items) => items,
            _ => &[],
        }
    }

    pub fn as_str(&self) -> Option<&str> {
        match self {
            Value::String(s) => Some(s),
            _ => None,
        }
    }

    /// The string, or `fallback` for any other type.
    pub fn string_or(&self, fallback: &str) -> String {
        self.as_str().unwrap_or(fallback).to_string()
    }

    pub fn number_or(&self, fallback: f64) -> f64 {
        match self {
            Value::Number(n) => *n,
            _ => fallback,
        }
    }

    /// The old reader's notion of truth: a number is true when non-zero, null is
    /// false, and only strings, arrays and objects fall back.
    pub fn bool_or(&self, fallback: bool) -> bool {
        match self {
            Value::Bool(b) => *b,
            Value::Number(n) => *n != 0.0,
            Value::Null => false,
            _ => fallback,
        }
    }

    /// Appends a field, as the writer's builders do. Order is file order.
    pub fn set(&mut self, key: &str, value: impl Into<Value>) {
        if let Value::Object(fields) = self {
            fields.push((key.to_string(), value.into()));
        }
    }
}

impl From<bool> for Value {
    fn from(v: bool) -> Self {
        Value::Bool(v)
    }
}
impl From<f64> for Value {
    fn from(v: f64) -> Self {
        Value::Number(v)
    }
}
impl From<&str> for Value {
    fn from(v: &str) -> Self {
        Value::String(v.to_string())
    }
}
impl From<String> for Value {
    fn from(v: String) -> Self {
        Value::String(v)
    }
}
impl From<Vec<Value>> for Value {
    fn from(v: Vec<Value>) -> Self {
        Value::Array(v)
    }
}

// ------------------------------------------------------------------ reading

/// Parses a whole document. A leading UTF-8 BOM is skipped; byte offsets in
/// errors count from after it, as they always did.
pub fn parse(input: &[u8]) -> Result<Value, String> {
    let body = input.strip_prefix(b"\xEF\xBB\xBF").unwrap_or(input);
    let mut parser = Parser { s: body, i: 0, error: None };
    parser.skip();
    let mut out = Value::Null;
    if !parser.value(&mut out, 0) {
        return Err(parser.error.unwrap_or_else(|| "unexpected end of input".into()));
    }
    parser.skip();
    if parser.i != body.len() {
        return Err(format!("trailing data at byte {}", parser.i));
    }
    Ok(out)
}

struct Parser<'a> {
    s: &'a [u8],
    i: usize,
    error: Option<String>,
}

impl Parser<'_> {
    fn peek(&self) -> Option<u8> {
        self.s.get(self.i).copied()
    }

    fn skip(&mut self) {
        while let Some(b' ' | b'\t' | b'\n' | b'\r') = self.peek() {
            self.i += 1;
        }
    }

    fn fail(&mut self, what: &str) -> bool {
        if self.error.is_none() {
            self.error = Some(format!("{what} at byte {}", self.i));
        }
        false
    }

    fn literal(&mut self, word: &[u8]) -> bool {
        if self.s[self.i..].starts_with(word) {
            self.i += word.len();
            true
        } else {
            self.fail("bad literal")
        }
    }

    fn value(&mut self, out: &mut Value, depth: usize) -> bool {
        if depth > MAX_DEPTH {
            return self.fail("nesting too deep");
        }
        let Some(c) = self.peek() else { return self.fail("unexpected end") };
        match c {
            b'{' => self.object(out, depth),
            b'[' => self.array(out, depth),
            b'"' => {
                let mut text = String::new();
                if !self.string(&mut text) {
                    return false;
                }
                *out = Value::String(text);
                true
            }
            b't' => self.literal(b"true") && {
                *out = Value::Bool(true);
                true
            },
            b'f' => self.literal(b"false") && {
                *out = Value::Bool(false);
                true
            },
            b'n' => self.literal(b"null") && {
                *out = Value::Null;
                true
            },
            _ => self.number(out),
        }
    }

    fn object(&mut self, out: &mut Value, depth: usize) -> bool {
        self.i += 1; // '{'
        let mut fields = Vec::new();
        self.skip();
        if self.peek() == Some(b'}') {
            self.i += 1;
            *out = Value::Object(fields);
            return true;
        }
        loop {
            self.skip();
            let mut key = String::new();
            if !self.string(&mut key) {
                return false;
            }
            self.skip();
            if self.peek() != Some(b':') {
                return self.fail("expected ':'");
            }
            self.i += 1;
            self.skip();
            let mut item = Value::Null;
            if !self.value(&mut item, depth + 1) {
                return false;
            }
            fields.push((key, item));
            self.skip();
            match self.peek() {
                Some(b',') => self.i += 1,
                Some(b'}') => {
                    self.i += 1;
                    break;
                }
                _ => return self.fail("expected ',' or '}'"),
            }
        }
        *out = Value::Object(fields);
        true
    }

    fn array(&mut self, out: &mut Value, depth: usize) -> bool {
        self.i += 1; // '['
        let mut items = Vec::new();
        self.skip();
        if self.peek() == Some(b']') {
            self.i += 1;
            *out = Value::Array(items);
            return true;
        }
        loop {
            self.skip();
            let mut item = Value::Null;
            if !self.value(&mut item, depth + 1) {
                return false;
            }
            items.push(item);
            self.skip();
            match self.peek() {
                Some(b',') => self.i += 1,
                Some(b']') => {
                    self.i += 1;
                    break;
                }
                _ => return self.fail("expected ',' or ']'"),
            }
        }
        *out = Value::Array(items);
        true
    }

    fn hex4(&mut self) -> Option<u32> {
        if self.i + 4 > self.s.len() {
            self.fail("truncated \\u escape");
            return None;
        }
        let mut out = 0u32;
        for k in 0..4 {
            let digit = match self.s[self.i + k] {
                c @ b'0'..=b'9' => c - b'0',
                c @ b'a'..=b'f' => c - b'a' + 10,
                c @ b'A'..=b'F' => c - b'A' + 10,
                _ => {
                    self.fail("bad hex digit");
                    return None;
                }
            };
            out = (out << 4) | u32::from(digit);
        }
        self.i += 4;
        Some(out)
    }

    fn string(&mut self, out: &mut String) -> bool {
        if self.peek() != Some(b'"') {
            return self.fail("expected string");
        }
        self.i += 1;
        // Raw bytes run between escapes; they are decoded in one go so a
        // multi-byte character is never split.
        let mut raw: Vec<u8> = Vec::new();
        while let Some(c) = self.peek() {
            if c == b'"' {
                self.i += 1;
                out.push_str(&String::from_utf8_lossy(&raw));
                return true;
            }
            if c != b'\\' {
                raw.push(c);
                self.i += 1;
                continue;
            }
            self.i += 1;
            let Some(e) = self.peek() else { return self.fail("truncated escape") };
            self.i += 1;
            let decoded = match e {
                b'"' => '"',
                b'\\' => '\\',
                b'/' => '/',
                b'b' => '\u{8}',
                b'f' => '\u{c}',
                b'n' => '\n',
                b'r' => '\r',
                b't' => '\t',
                b'u' => {
                    let Some(mut cp) = self.hex4() else { return false };
                    if (0xD800..=0xDBFF).contains(&cp)
                        && self.peek() == Some(b'\\')
                        && self.s.get(self.i + 1) == Some(&b'u')
                    {
                        let save = self.i;
                        self.i += 2;
                        let Some(low) = self.hex4() else { return false };
                        if (0xDC00..=0xDFFF).contains(&low) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            self.i = save; // a lone high surrogate
                        }
                    }
                    // A lone surrogate has no UTF-8 spelling. The C++ reader
                    // wrote its CESU-8 bytes into the string; here it becomes
                    // the replacement character.
                    char::from_u32(cp).unwrap_or(char::REPLACEMENT_CHARACTER)
                }
                _ => return self.fail("unknown escape"),
            };
            let mut buf = [0u8; 4];
            raw.extend_from_slice(decoded.encode_utf8(&mut buf).as_bytes());
        }
        self.fail("unterminated string")
    }

    fn number(&mut self, out: &mut Value) -> bool {
        let start = self.i;
        let at = |p: &Self| p.peek();
        if matches!(at(self), Some(b'-' | b'+')) {
            self.i += 1;
        }
        let mut digits = false;
        while matches!(at(self), Some(b'0'..=b'9')) {
            self.i += 1;
            digits = true;
        }
        if at(self) == Some(b'.') {
            self.i += 1;
            while matches!(at(self), Some(b'0'..=b'9')) {
                self.i += 1;
                digits = true;
            }
        }
        if digits && matches!(at(self), Some(b'e' | b'E')) {
            self.i += 1;
            if matches!(at(self), Some(b'-' | b'+')) {
                self.i += 1;
            }
            while matches!(at(self), Some(b'0'..=b'9')) {
                self.i += 1;
            }
        }
        if !digits {
            return self.fail("expected value");
        }
        // Only ASCII was consumed above, so this slice is valid UTF-8.
        let token = std::str::from_utf8(&self.s[start..self.i]).unwrap_or("");
        match numfmt::parse(token) {
            Some((value, _)) => {
                *out = Value::Number(value);
                true
            }
            None => self.fail("bad number"),
        }
    }
}

// ------------------------------------------------------------------ writing

/// Pretty-prints with `indent` spaces per level and a final newline, or packs
/// everything onto one line when `indent` is 0.
pub fn dump(value: &Value, indent: usize) -> String {
    let mut out = String::new();
    write(value, indent, 0, &mut out);
    if indent > 0 {
        out.push('\n');
    }
    out
}

fn escape(text: &str, out: &mut String) {
    out.push('"');
    for c in text.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            '\u{8}' => out.push_str("\\b"),
            '\u{c}' => out.push_str("\\f"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c), // UTF-8 passes through untouched
        }
    }
    out.push('"');
}

fn write(value: &Value, indent: usize, level: usize, out: &mut String) {
    let pad = " ".repeat(indent * (level + 1));
    let pad_end = " ".repeat(indent * level);
    let nl = if indent > 0 { "\n" } else { "" };
    match value {
        Value::Null => out.push_str("null"),
        Value::Bool(b) => out.push_str(if *b { "true" } else { "false" }),
        Value::Number(n) => out.push_str(&numfmt::shortest(*n)),
        Value::String(s) => escape(s, out),
        Value::Array(items) => {
            if items.is_empty() {
                out.push_str("[]");
                return;
            }
            out.push('[');
            out.push_str(nl);
            for (k, item) in items.iter().enumerate() {
                out.push_str(&pad);
                write(item, indent, level + 1, out);
                if k + 1 < items.len() {
                    out.push(',');
                }
                out.push_str(nl);
            }
            out.push_str(&pad_end);
            out.push(']');
        }
        Value::Object(fields) => {
            if fields.is_empty() {
                out.push_str("{}");
                return;
            }
            out.push('{');
            out.push_str(nl);
            for (k, (key, item)) in fields.iter().enumerate() {
                out.push_str(&pad);
                escape(key, out);
                out.push_str(if indent > 0 { ": " } else { ":" });
                write(item, indent, level + 1, out);
                if k + 1 < fields.len() {
                    out.push(',');
                }
                out.push_str(nl);
            }
            out.push_str(&pad_end);
            out.push('}');
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn keeps_key_order_and_utf8() {
        let mut v = Value::object();
        v.set("z", "первый");
        v.set("a", "второй");
        let text = dump(&v, 0);
        assert_eq!(text, "{\"z\":\"первый\",\"a\":\"второй\"}");
        assert_eq!(dump(&parse(text.as_bytes()).unwrap(), 0), text);
    }

    #[test]
    fn reads_escapes_and_a_bom() {
        let v = parse(b"\xEF\xBB\xBF{\"s\":\"\\u0414\\u0430\\n\\\"x\\\"\"}").unwrap();
        assert_eq!(v.get("s").as_str(), Some("Да\n\"x\""));
        let v = parse(b"\"\\ud83d\\ude00\"").unwrap();
        assert_eq!(v.as_str(), Some("😀"));
    }

    #[test]
    fn first_of_two_equal_keys_wins() {
        let v = parse(br#"{"a": 1, "a": 2}"#).unwrap();
        assert_eq!(v.get("a").number_or(0.0), 1.0);
    }

    #[test]
    fn reports_the_byte_it_stopped_at() {
        assert_eq!(parse(b"{\"name\":").unwrap_err(), "unexpected end at byte 8");
        assert_eq!(parse(b"[1,2,]").unwrap_err(), "expected value at byte 5");
        assert_eq!(parse(b"{} x").unwrap_err(), "trailing data at byte 3");
        assert_eq!(parse(b"{\"a\" 1}").unwrap_err(), "expected ':' at byte 5");
        assert_eq!(parse(b"tru").unwrap_err(), "bad literal at byte 0");
        assert_eq!(parse(b"\"abc").unwrap_err(), "unterminated string at byte 4");
        assert_eq!(parse(b"\"\\q\"").unwrap_err(), "unknown escape at byte 3");
        assert_eq!(parse(b"").unwrap_err(), "unexpected end at byte 0");
    }

    /// Security: a document nested deeper than MAX_DEPTH is refused with an
    /// error instead of exhausting the stack.
    #[test]
    fn refuses_deep_nesting() {
        let deep = "[".repeat(100_000);
        assert!(parse(deep.as_bytes()).unwrap_err().starts_with("nesting too deep"));
        let ok = format!("{}{}", "[".repeat(MAX_DEPTH + 1), "]".repeat(MAX_DEPTH + 1));
        assert!(parse(ok.as_bytes()).is_ok());
        let too_deep = format!("{}{}", "[".repeat(MAX_DEPTH + 2), "]".repeat(MAX_DEPTH + 2));
        assert!(parse(too_deep.as_bytes()).is_err());
    }

    /// Security: invalid UTF-8 and lone surrogates never produce an invalid
    /// Rust string; they become U+FFFD.
    #[test]
    fn invalid_text_becomes_replacement_characters() {
        let v = parse(b"\"a\xFFb\"").unwrap();
        assert_eq!(v.as_str(), Some("a\u{FFFD}b"));
        let v = parse(b"\"x\\ud800y\"").unwrap();
        assert_eq!(v.as_str(), Some("x\u{FFFD}y"));
    }

    #[test]
    fn lax_numbers_as_before() {
        assert_eq!(parse(b"+5").unwrap(), Value::Number(5.0));
        assert_eq!(parse(b".5").unwrap(), Value::Number(0.5));
        assert_eq!(parse(b"5.").unwrap(), Value::Number(5.0));
        assert!(parse(b"1e").is_ok());
        assert_eq!(parse(b"-").unwrap_err(), "expected value at byte 1");
    }

    #[test]
    fn truthiness_matches_the_old_reader() {
        assert!(Value::Number(2.5).bool_or(false));
        assert!(!Value::Number(0.0).bool_or(true));
        assert!(!Value::Null.bool_or(true));
        assert!(Value::String("yes".into()).bool_or(true));
        assert!(!Value::String("yes".into()).bool_or(false));
    }

    #[test]
    fn escapes_control_characters() {
        let v = Value::String("a\u{7}\t\u{1f}é".into());
        assert_eq!(dump(&v, 0), "\"a\\u0007\\t\\u001fé\"");
    }
}
