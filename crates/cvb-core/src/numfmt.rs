//! Numbers to text and back, digit for digit what the C++ version wrote.
//!
//! Rust's formatting never consults the locale, so the decimal-comma trap the
//! C++ code had to dodge does not exist here. What remains is to produce the
//! same digits: `%.*f` and `%.10g` as the C library printed them, because those
//! digits end up in every PDF and every saved resume.

/// `decimals` digits after the point, always '.', e.g. `fixed(0.5, 3) == "0.500"`.
///
/// Rust rounds the exact binary value half to even, which is what both glibc
/// and the Windows CRT do for `%.*f`.
pub fn fixed(value: f64, decimals: usize) -> String {
    if !value.is_finite() {
        return non_finite(value);
    }
    format!("{value:.decimals$}")
}

/// The shortest faithful form the JSON writer used: whole numbers below 1e15 as
/// integers, everything else as `%.10g`.
pub fn shortest(value: f64) -> String {
    if !value.is_finite() {
        return non_finite(value);
    }
    if value == value.floor() && value.abs() < 1e15 {
        // `value as i64` is exact in this range; -0.0 becomes "0", as "%lld" did.
        return (value as i64).to_string();
    }
    general(value, 10)
}

/// `%.{precision}g`: scientific when the exponent is below -4 or at least the
/// precision, fixed otherwise; trailing zeros and a bare point removed.
fn general(value: f64, precision: usize) -> String {
    debug_assert!(precision > 0);
    if value == 0.0 {
        return if value.is_sign_negative() { "-0".into() } else { "0".into() };
    }
    // The exponent %g decides by is the one of the value rounded to `precision`
    // significant digits, which is exactly what {:e} with precision-1 gives.
    let sci = format!("{:.*e}", precision - 1, value);
    let (mantissa, exponent) = sci.split_once('e').expect("{:e} always has an exponent");
    let exponent: i32 = exponent.parse().expect("{:e} exponent is an integer");

    if exponent < -4 || exponent >= precision as i32 {
        let mantissa = trim_fraction(mantissa);
        let sign = if exponent < 0 { '-' } else { '+' };
        format!("{mantissa}e{sign}{:02}", exponent.unsigned_abs())
    } else {
        let decimals = (precision as i32 - 1 - exponent) as usize;
        trim_fraction(&format!("{value:.decimals$}")).to_string()
    }
}

fn trim_fraction(text: &str) -> &str {
    if !text.contains('.') {
        return text;
    }
    text.trim_end_matches('0').trim_end_matches('.')
}

fn non_finite(value: f64) -> String {
    if value.is_nan() {
        "nan".into()
    } else if value > 0.0 {
        "inf".into()
    } else {
        "-inf".into()
    }
}

/// Reads a number from the start of `text` the way `strtod` did for the JSON
/// reader: an optional sign, digits with an optional fraction, an optional
/// exponent. Returns the value and how many bytes it used, or `None` when
/// `text` does not start with a number.
///
/// Laxer than JSON on purpose, because the C++ reader was: "+5", "5." and ".5"
/// are numbers, and "1e" reads as 1 with the "e" left over.
pub fn parse(text: &str) -> Option<(f64, usize)> {
    let bytes = text.as_bytes();
    let digits_from = |mut at: usize| {
        let start = at;
        while at < bytes.len() && bytes[at].is_ascii_digit() {
            at += 1;
        }
        (start, at)
    };

    let mut i = 0;
    let negative = match bytes.first() {
        Some(b'-') => {
            i = 1;
            true
        }
        Some(b'+') => {
            i = 1;
            false
        }
        _ => false,
    };
    let (int_start, int_end) = digits_from(i);
    i = int_end;
    let mut fraction = (i, i);
    if bytes.get(i) == Some(&b'.') {
        let (start, end) = digits_from(i + 1);
        // A lone "." after digits still belongs to the number ("5." is 5); a
        // "." with digits on neither side is not a number at all.
        if int_end > int_start || end > start {
            fraction = (start, end);
            i = end;
        }
    }
    if int_end == int_start && fraction.1 == fraction.0 {
        return None;
    }
    let mut exponent = None;
    if matches!(bytes.get(i), Some(b'e' | b'E')) {
        let sign_at = i + 1;
        let signed = matches!(bytes.get(sign_at), Some(b'+' | b'-'));
        let (start, end) = digits_from(if signed { sign_at + 1 } else { sign_at });
        // "1e" with no digits: strtod stopped before the 'e', and so does this.
        if end > start {
            exponent = Some(&text[sign_at..end]);
            i = end;
        }
    }

    // One unambiguous spelling for Rust's parser, whatever shape came in.
    let mut normal = String::with_capacity(i + 4);
    if negative {
        normal.push('-');
    }
    normal.push_str(if int_end > int_start { &text[int_start..int_end] } else { "0" });
    normal.push('.');
    normal.push_str(if fraction.1 > fraction.0 { &text[fraction.0..fraction.1] } else { "0" });
    if let Some(exponent) = exponent {
        normal.push('e');
        normal.push_str(exponent);
    }
    normal.parse::<f64>().ok().map(|value| (value, i))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fixed_writes_a_point() {
        assert_eq!(fixed(0.5, 3), "0.500");
        assert_eq!(fixed(595.2755905511812, 3), "595.276");
        assert_eq!(fixed(-12.0, 3), "-12.000");
        assert_eq!(fixed(0.0, 3), "0.000");
    }

    #[test]
    fn shortest_matches_the_old_output() {
        assert_eq!(shortest(0.0), "0");
        assert_eq!(shortest(-0.0), "0");
        assert_eq!(shortest(7.0), "7");
        assert_eq!(shortest(-3.0), "-3");
        assert_eq!(shortest(9.5), "9.5");
        assert_eq!(shortest(8.25), "8.25");
        assert_eq!(shortest(1e15), "1e+15");
        assert_eq!(shortest(1e-5), "1e-05");
        assert_eq!(shortest(2.0 / 3.0), "0.6666666667");
    }

    #[test]
    fn parse_reads_what_the_json_reader_accepted() {
        assert_eq!(parse("1.5"), Some((1.5, 3)));
        assert_eq!(parse("-0.125"), Some((-0.125, 6)));
        assert_eq!(parse("2e3"), Some((2000.0, 3)));
        assert_eq!(parse("+5"), Some((5.0, 2)));
        assert_eq!(parse("5."), Some((5.0, 2)));
        assert_eq!(parse(".5"), Some((0.5, 2)));
        assert_eq!(parse("-.5"), Some((-0.5, 3)));
        assert_eq!(parse("1e"), Some((1.0, 1)));
        assert_eq!(parse("1e+"), Some((1.0, 1)));
        assert_eq!(parse("5.e2"), Some((500.0, 4)));
        assert_eq!(parse("1e400").map(|(v, _)| v), Some(f64::INFINITY));
        assert_eq!(parse(""), None);
        assert_eq!(parse("abc"), None);
        assert_eq!(parse("."), None);
        assert_eq!(parse("-"), None);
    }

    /// Every row of the table the C++ build printed: value, fixed(v, 3),
    /// fixed(v, 0), shortest(v).
    #[test]
    fn matches_the_cpp_table() {
        let table = include_str!("../../../fixtures/golden/numfmt.tsv");
        let mut rows = 0;
        for line in table.lines() {
            let cols: Vec<&str> = line.split('\t').collect();
            assert_eq!(cols.len(), 4, "malformed row {line:?}");
            let value: f64 = cols[0].parse().unwrap();
            assert_eq!(fixed(value, 3), cols[1], "fixed(.., 3) of {}", cols[0]);
            assert_eq!(fixed(value, 0), cols[2], "fixed(.., 0) of {}", cols[0]);
            assert_eq!(shortest(value), cols[3], "shortest of {}", cols[0]);
            rows += 1;
        }
        assert!(rows > 700);
    }
}
