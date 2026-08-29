// Numbers to text and back, without the C locale having a say.
//
// This matters more than it looks. `printf("%.3f")` and `strtod` both honour
// LC_NUMERIC, so in a German or Russian locale a PDF coordinate comes out as
// "595,276" - which makes the file invalid - and reading back "1.5" yields 1.
// Nothing in the program calls setlocale today, so the process stays in the "C"
// locale and the bug sleeps; a UI toolkit calls setlocale(LC_ALL, "") while it
// starts up, and it wakes.
//
// Rather than fight over the process-wide setting, the conversions here go
// through the platform's own formatter and then normalise the separator. Same
// digits, same rounding as before - the output cannot drift - but the result no
// longer depends on who called setlocale last.
#pragma once

#include <string>

namespace numfmt {

// `decimals` digits after the point, always '.', e.g. fixed(0.5, 3) == "0.500".
std::string fixed(double value, int decimals);

// The shortest faithful form: whole numbers print without a fractional part,
// everything else gets 10 significant digits. This is what the JSON writer
// emits, so its output stays byte for byte what it was.
std::string shortest(double value);

// Reads a JSON number. Returns false when `text` does not start with one.
// `consumed` (optional) receives how many characters were used.
bool parse(const std::string& text, double& out, size_t* consumed = nullptr);

}  // namespace numfmt
