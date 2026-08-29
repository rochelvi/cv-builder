#include "layout.h"

#include <algorithm>

namespace cvb {

// A4 at 72 dpi.
const double kPageWidth = 595.2755905511812;
const double kPageHeight = 841.8897637795276;

namespace {

// The template's measurements. These are the numbers the original design was
// built on; every gap below is a deliberate part of the look, not a round
// number waiting to be tidied up.
const double kLeft = 42.85;
const double kRight = 552.43;
const double kBottom = 28.0;
const double kColumnWidth = kRight - kLeft;
const double kColumn2X = 297.6;  // right column of the skills grid
const double kSkillX = 48.2;
const double kColumnStep = 173.85;  // contacts and the education grid
const double kTopAfterBreak = 52.0;

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

bool blank(const std::string& s) {
    for (char c : s)
        if (!isSpace(c)) return false;
    return true;
}

std::string upper(const std::string& s) {
    // ASCII-only, which is all the section titles need; Cyrillic titles are
    // left as typed rather than mangled by a byte-wise upcase.
    std::string out = s;
    for (char& c : out)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}

std::vector<std::string> splitWords(const std::string& text) {
    std::vector<std::string> words;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && isSpace(text[i])) ++i;
        size_t start = i;
        while (i < text.size() && !isSpace(text[i])) ++i;
        if (i > start) words.push_back(text.substr(start, i - start));
    }
    return words;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (text[i] != '\r') {
            current += text[i];
        }
    }
    lines.push_back(current);
    return lines;
}

const char* const kBulletMark = "\xE2\x80\xBA";   // ›
const char* const kMiddot = " \xC2\xB7 ";         // " · "
const char* const kSkillMark = "\xC2\xB7 ";       // "· "

class Builder {
public:
    Builder(const CV& cv, const FontSet& fonts) : cv_(cv), fonts_(fonts) {
        // Resolve against the built-in theme so a bad value falls back to the
        // role's original colour rather than to black.
        Theme defaults;
        for (int i = 0; i < TR_Count; ++i)
            palette_[i] = parseColor(cv.theme.c[i], parseColor(defaults.c[i], RGB{0, 0, 0}));
    }

    Document run() {
        doc_.width = kPageWidth;
        doc_.height = kPageHeight;
        doc_.title = cv_.name.empty() ? "CV" : cv_.name + " \xE2\x80\x94 CV";
        doc_.author = cv_.name;
        newPage();
        header();
        for (const SectionRef& ref : orderedSections(cv_)) draw(ref);
        return std::move(doc_);
    }

private:
    // Draws one section of the running order. An id this build does not know
    // is skipped rather than guessed at.
    void draw(const SectionRef& ref) {
        const std::string title =
            ref.label.empty() ? std::string(defaultSectionLabel(ref.id)) : ref.label;
        if (ref.id == "summary") summary(title);
        else if (ref.id == "jobs") experience(title);
        else if (ref.id == "skill_groups") skills(title);
        else if (ref.id == "soft_skills") softSkills(title);
        else if (ref.id == "studies") studies(title);
        else if (ref.id == "education") education(title);
        else if (ref.id == "volunteering") volunteering(title);
        else if (ref.id == "lab") lab(title);
    }

    // ---------- primitives ----------
    const Font& font(bool bold) const { return fonts_.face(bold); }
    RGB color(ThemeRole role) const { return palette_[role]; }

    void newPage() {
        Page page;
        page.background = RectItem{0, 0, kPageWidth, kPageHeight, color(TR_Background)};
        doc_.pages.push_back(std::move(page));
    }

    Page& page() { return doc_.pages.back(); }

    void text(double x, double y, const std::string& s, bool bold, double size, ThemeRole role) {
        if (s.empty()) return;
        TextItem item;
        item.x = x;
        item.y = y;
        item.size = size;
        item.bold = bold;
        item.color = color(role);
        item.text = s;
        page().texts.push_back(std::move(item));
    }

    void textRight(double x, double y, const std::string& s, bool bold, double size, ThemeRole role) {
        text(x - font(bold).measure(s, size), y, s, bold, size, role);
    }

    void rule(double y, double width = 0.5) {
        page().lines.push_back(LineItem{kLeft, y, kRight, y, width, color(TR_Rule)});
    }

    void pageBreak() {
        newPage();
        y_ = kTopAfterBreak;
    }

    double room() const { return kPageHeight - kBottom - y_; }

    void need(double space) {
        if (space > room()) pageBreak();
    }

    std::vector<std::string> wrap(const std::string& text, bool bold, double size, double width) const {
        const Font& f = font(bold);
        std::vector<std::string> lines;
        std::string current;
        for (const std::string& word : splitWords(text)) {
            std::string candidate = current.empty() ? word : current + " " + word;
            if (current.empty() || f.measure(candidate, size) <= width) {
                current = candidate;
            } else {
                lines.push_back(current);
                current = word;
            }
        }
        if (!current.empty()) lines.push_back(current);
        if (lines.empty()) lines.push_back(std::string());
        return lines;
    }

    // ---------- blocks ----------
    void header() {
        y_ = 47.0;
        text(kLeft, y_, cv_.name, true, 20, TR_Heading);
        y_ += 16.8;
        if (!cv_.role.empty()) text(kLeft, y_, cv_.role, false, 9, TR_Accent);
        y_ += 18.6;
        const std::string contacts[3] = {cv_.email, cv_.location, cv_.website};
        for (int i = 0; i < 3; ++i)
            if (!contacts[i].empty())
                text(kLeft + i * kColumnStep, y_, contacts[i], false, 9, TR_Subtle);
        y_ += 8.5;
        rule(y_, 0.7);
    }

    void section(const std::string& title, double gap = 19.1) {
        need(60);
        y_ += gap;
        text(kLeft, y_, upper(title), true, 8, TR_Accent);
        y_ += 8.25;
        rule(y_);
    }

    void bullets(const std::vector<std::string>& items, double indent = 10.0) {
        const double textX = kLeft + indent + 5.4;
        for (size_t i = 0; i < items.size(); ++i) {
            std::vector<std::string> lines = wrap(items[i], false, 9, kRight - textX);
            need(14.4 + 13.0 * static_cast<double>(lines.size() - 1));
            y_ += (i == 0) ? 11.3 : 14.4;
            text(kLeft + indent, y_, kBulletMark, false, 9, TR_Accent);
            for (size_t j = 0; j < lines.size(); ++j) {
                if (j) y_ += 13.0;
                text(textX, y_, lines[j], false, 9, TR_Body);
            }
        }
    }

    void summary(const std::string& title) {
        if (blank(cv_.summary)) return;
        section(title, 15.9);
        std::vector<std::string> lines;
        for (const std::string& paragraph : splitLines(cv_.summary)) {
            if (blank(paragraph)) {
                lines.push_back(std::string());
            } else {
                std::vector<std::string> wrapped = wrap(paragraph, false, 9.5, kColumnWidth);
                lines.insert(lines.end(), wrapped.begin(), wrapped.end());
            }
        }
        y_ += 12.15;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i) {
                need(15);
                y_ += 15.0;
            }
            text(kLeft, y_, lines[i], false, 9.5, TR_Body);
        }
    }

    void jobEntries(const std::vector<Job>& jobs) {
        for (size_t i = 0; i < jobs.size(); ++i) {
            const Job& job = jobs[i];
            need(55);
            y_ += (i == 0) ? 13.1 : 21.8;
            text(kLeft - 6, y_, job.title, true, 11, TR_Heading);
            if (!job.period.empty()) textRight(kRight, y_, job.period, false, 9, TR_Faint);
            std::string subtitle = job.company;
            if (!job.location.empty())
                subtitle = subtitle.empty() ? job.location : subtitle + kMiddot + job.location;
            if (!subtitle.empty()) {
                y_ += 13.2;
                text(kLeft, y_, subtitle, false, 9, TR_Subtle);
            }
            y_ += 4.5;
            std::vector<std::string> items;
            for (const std::string& b : job.bullets)
                if (!blank(b)) items.push_back(b);
            bullets(items);
        }
    }

    void experience(const std::string& title) {
        if (cv_.jobs.empty()) return;
        section(title);
        jobEntries(cv_.jobs);
    }

    // The long form of education: same shape as a job, because the content is
    // the same shape - a place, a span of time, and what came of it.
    void studies(const std::string& title) {
        std::vector<Job> items;
        for (const Job& item : cv_.studies)
            if (!item.title.empty() || !item.company.empty() || !item.bullets.empty())
                items.push_back(item);
        if (items.empty()) return;
        section(title);
        jobEntries(items);
    }

    void volunteering(const std::string& title) {
        std::vector<Job> jobs;
        for (const Job& job : cv_.volunteering)
            if (!job.title.empty() || !job.company.empty() || !job.bullets.empty()) jobs.push_back(job);
        if (jobs.empty()) return;
        section(title);
        jobEntries(jobs);
    }

    // Space one skill group takes in its column, mirroring drawSkills.
    static double groupHeight(const SkillGroup& group, bool first) {
        size_t count = 0;
        for (const Skill& skill : group.skills)
            if (!blank(skill.name)) ++count;
        double gap = first ? 11.3 : 16.9;
        return gap + (count ? 14.8 + 11.0 * static_cast<double>(count - 1) : 0.0);
    }

    // How many groups fit in `space`, filling the two columns alternately.
    static size_t skillsThatFit(const std::vector<SkillGroup>& groups, size_t from, double space) {
        double heights[2] = {0.0, 0.0};
        int counts[2] = {0, 0};
        for (size_t i = from; i < groups.size(); ++i) {
            int column = static_cast<int>((i - from) % 2);
            double height = heights[column] + groupHeight(groups[i], counts[column] == 0);
            if (std::max(height, heights[1 - column]) > space) return i - from;
            heights[column] = height;
            counts[column] += 1;
        }
        return groups.size() - from;
    }

    void drawSkills(const std::vector<SkillGroup>& groups, size_t from, size_t count) {
        const double top = y_;
        double bottom = top;
        const double columnX[2] = {kSkillX, kColumn2X};
        for (int column = 0; column < 2; ++column) {
            y_ = top;
            int index = 0;
            for (size_t i = column; i < count; i += 2, ++index) {
                const SkillGroup& group = groups[from + i];
                y_ += (index == 0) ? 11.3 : 16.9;
                text(columnX[column], y_, group.title, true, 9, TR_Heading);
                int drawn = 0;
                for (const Skill& skill : group.skills) {
                    if (blank(skill.name)) continue;
                    y_ += (drawn == 0) ? 14.8 : 11.0;
                    text(columnX[column], y_, kSkillMark + skill.name, false, 8,
                         skill.highlight ? TR_Accent : TR_Body);
                    ++drawn;
                }
            }
            bottom = std::max(bottom, y_);
        }
        y_ = bottom;
    }

    void skills(const std::string& title) {
        std::vector<SkillGroup> groups;
        for (const SkillGroup& group : cv_.skillGroups)
            if (!group.title.empty() || !group.skills.empty()) groups.push_back(group);
        if (groups.empty()) return;
        section(title);
        size_t from = 0;
        while (from < groups.size()) {
            size_t take = skillsThatFit(groups, from, room());
            if (take == 0) {  // nothing fits below the cursor - carry on overleaf
                pageBreak();
                take = std::max<size_t>(1, skillsThatFit(groups, from, room()));
            }
            drawSkills(groups, from, take);
            from += take;
        }
    }

    void softSkills(const std::string& title) {
        std::vector<std::string> items;
        for (const std::string& s : cv_.softSkills)
            if (!blank(s)) items.push_back(s);
        if (items.empty()) return;
        section(title, 22.4);

        const Font& f = font(false);
        const std::string separator = kMiddot;
        const double separatorWidth = f.measure(separator, 8);

        std::vector<std::vector<std::string>> lines;
        double used = 0.0;
        for (const std::string& item : items) {
            double width = f.measure(item, 8);
            if (!lines.empty() && kLeft + used + separatorWidth + width <= kRight) {
                lines.back().push_back(item);
                used += separatorWidth + width;
            } else {
                lines.push_back({item});
                used = width;
            }
        }

        for (size_t i = 0; i < lines.size(); ++i) {
            need(12.0);
            y_ += (i == 0) ? 10.3 : 12.0;
            double x = kLeft;
            for (size_t j = 0; j < lines[i].size(); ++j) {
                text(x, y_, lines[i][j], false, 8, TR_Accent2);
                x += f.measure(lines[i][j], 8);
                bool last = (i == lines.size() - 1) && (j == lines[i].size() - 1);
                if (!last && x + separatorWidth <= kRight) {
                    text(x, y_, separator, false, 8, TR_Accent);
                    x += separatorWidth;
                }
            }
        }
    }

    void education(const std::string& title) {
        std::vector<Education> items;
        for (const Education& item : cv_.education)
            if (!item.title.empty() || !item.subtitle.empty()) items.push_back(item);
        if (items.empty()) return;
        section(title, 16.6);

        double trailing = 0.0;  // height the previous row's subtitles added below its baseline
        for (size_t start = 0, row = 0; start < items.size(); start += 3, ++row) {
            size_t end = std::min(start + 3, items.size());
            bool subtitled = false;
            for (size_t i = start; i < end; ++i)
                if (!items[i].subtitle.empty()) subtitled = true;
            double extra = subtitled ? 13.0 : 0.0;
            double gap = (row == 0) ? 11.25 : 17.0 + trailing;
            need(gap + extra);
            y_ += gap;
            for (size_t i = start; i < end; ++i) {
                double x = kLeft + static_cast<double>(i - start) * kColumnStep - 6;
                text(x, y_, items[i].title, true, 9,
                     items[i].highlight ? TR_Accent2 : TR_Faint);
                if (!items[i].subtitle.empty())
                    text(x, y_ + 13.0, items[i].subtitle, false, 8, TR_Subtle);
            }
            trailing = extra;
        }
        y_ += trailing;  // the last row's subtitles still sit below the cursor
    }

    void lab(const std::string& title) {
        std::vector<std::string> items;
        for (const std::string& b : cv_.labBullets)
            if (!blank(b)) items.push_back(b);
        if (items.empty()) return;
        section(title, 17.7);
        bullets(items);
    }

    const CV& cv_;
    const FontSet& fonts_;
    RGB palette_[TR_Count];
    Document doc_;
    double y_ = 0.0;  // distance from the top of the page
};

}  // namespace

RGB parseColor(const std::string& text, RGB fallback) {
    size_t at = (!text.empty() && text[0] == '#') ? 1 : 0;
    if (text.size() - at != 6) return fallback;
    int v[6];
    for (int i = 0; i < 6; ++i) {
        v[i] = hexDigit(text[at + static_cast<size_t>(i)]);
        if (v[i] < 0) return fallback;
    }
    RGB out;
    out.r = static_cast<uint8_t>(v[0] * 16 + v[1]);
    out.g = static_cast<uint8_t>(v[2] * 16 + v[3]);
    out.b = static_cast<uint8_t>(v[4] * 16 + v[5]);
    return out;
}

Document layout(const CV& cv, const FontSet& fonts) {
    return Builder(cv, fonts).run();
}

}  // namespace cvb
