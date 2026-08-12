"""Renders a CV to PDF, reproducing the dark template exactly."""

from __future__ import annotations

import io
from pathlib import Path

from reportlab.lib.colors import HexColor
from reportlab.lib.pagesizes import A4
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas as pdfcanvas

from .model import CV, Job, SkillGroup, Theme


def color(value: str, fallback: str = "#000000"):
    """Parse a #rrggbb string, tolerating whatever the user typed."""
    try:
        return HexColor(value if value.startswith("#") else f"#{value}")
    except Exception:
        return HexColor(fallback)


class Palette:
    """Resolved ReportLab colours for one theme."""

    def __init__(self, theme: Theme):
        defaults = Theme()
        for key, default in defaults.to_dict().items():
            setattr(self, key, color(getattr(theme, key, default), default))


PAGE_W, PAGE_H = A4
LEFT = 42.85
RIGHT = 552.43
BOTTOM = 28.0
COL_W = RIGHT - LEFT
COL2_X = 297.6  # right column of the skills grid
SKILL_X = 48.2

REGULAR = "CVSans"
BOLD = "CVSans-Bold"

_FONT_CANDIDATES = [
    # (regular, bold) - Arial is metrically identical to the template's Helvetica
    (r"C:\Windows\Fonts\arial.ttf", r"C:\Windows\Fonts\arialbd.ttf"),
    ("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
     "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"),
    ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
     "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"),
]

_fonts_ready = False


def ensure_fonts() -> None:
    """Register a Unicode (Cyrillic-capable) sans font, falling back to Helvetica."""
    global _fonts_ready, REGULAR, BOLD
    if _fonts_ready:
        return
    for regular, bold in _FONT_CANDIDATES:
        if Path(regular).exists() and Path(bold).exists():
            pdfmetrics.registerFont(TTFont(REGULAR, regular))
            pdfmetrics.registerFont(TTFont(BOLD, bold))
            _fonts_ready = True
            return
    REGULAR, BOLD = "Helvetica", "Helvetica-Bold"
    _fonts_ready = True


class _Renderer:
    def __init__(self, cv: CV, canvas: pdfcanvas.Canvas):
        self.cv = cv
        self.p = Palette(cv.theme)
        self.c = canvas
        self.y = 0.0  # distance from the top of the page

    # ---------- low level helpers ----------
    def _text(self, x: float, y: float, s: str, font: str, size: float, color) -> None:
        self.c.setFont(font, size)
        self.c.setFillColor(color)
        self.c.drawString(x, PAGE_H - y, s)

    def _text_right(self, x: float, y: float, s: str, font: str, size: float, color) -> None:
        self.c.setFont(font, size)
        self.c.setFillColor(color)
        self.c.drawRightString(x, PAGE_H - y, s)

    def _rule(self, y: float, width: float = 0.5) -> None:
        self.c.setStrokeColor(self.p.rule)
        self.c.setLineWidth(width)
        self.c.line(LEFT, PAGE_H - y, RIGHT, PAGE_H - y)

    def _page_bg(self) -> None:
        self.c.setFillColor(self.p.background)
        self.c.rect(0, 0, PAGE_W, PAGE_H, stroke=0, fill=1)

    def _page_break(self) -> None:
        self.c.showPage()
        self._page_bg()
        self.y = 52.0

    def _room(self) -> float:
        """Points left between the cursor and the bottom margin."""
        return PAGE_H - BOTTOM - self.y

    def _need(self, space: float) -> None:
        """Start a new page if `space` points do not fit below the cursor."""
        if space > self._room():
            self._page_break()

    @staticmethod
    def wrap(text: str, font: str, size: float, width: float) -> list[str]:
        words, lines, current = text.split(), [], ""
        for word in words:
            candidate = f"{current} {word}".strip()
            if pdfmetrics.stringWidth(candidate, font, size) <= width or not current:
                current = candidate
            else:
                lines.append(current)
                current = word
        if current:
            lines.append(current)
        return lines or [""]

    # ---------- blocks ----------
    def header(self) -> None:
        cv = self.cv
        self.y = 47.0
        self._text(LEFT, self.y, cv.name, BOLD, 20, self.p.heading)
        self.y += 16.8
        if cv.role:
            self._text(LEFT, self.y, cv.role, REGULAR, 9, self.p.accent)
        self.y += 18.6
        for i, value in enumerate((cv.email, cv.location, cv.website)):
            if value:
                self._text(LEFT + i * 173.85, self.y, value, REGULAR, 9, self.p.subtle)
        self.y += 8.5
        self._rule(self.y, width=0.7)

    def section(self, title: str, gap: float = 19.1) -> None:
        self._need(60)
        self.y += gap
        self._text(LEFT, self.y, title.upper(), BOLD, 8, self.p.accent)
        self.y += 8.25
        self._rule(self.y)

    def bullets(self, items: list[str], indent: float = 10.0) -> None:
        text_x = LEFT + indent + 5.4
        for i, item in enumerate(items):
            lines = self.wrap(item, REGULAR, 9, RIGHT - text_x)
            self._need(14.4 + 13 * (len(lines) - 1))
            self.y += 11.3 if i == 0 else 14.4
            self._text(LEFT + indent, self.y, "\u203a", REGULAR, 9, self.p.accent)
            for j, line in enumerate(lines):
                if j:
                    self.y += 13.0
                self._text(text_x, self.y, line, REGULAR, 9, self.p.body)

    def summary(self) -> None:
        if not self.cv.summary.strip():
            return
        self.section("Summary", gap=15.9)
        lines = []
        for paragraph in self.cv.summary.splitlines():
            lines += self.wrap(paragraph, REGULAR, 9.5, COL_W) if paragraph.strip() else [""]
        self.y += 12.15
        for i, line in enumerate(lines):
            if i:
                self._need(15)
                self.y += 15.0
            self._text(LEFT, self.y, line, REGULAR, 9.5, self.p.body)

    def job_entries(self, jobs: list[Job]) -> None:
        for i, job in enumerate(jobs):
            self._need(55)
            self.y += 13.1 if i == 0 else 21.8
            self._text(LEFT - 6, self.y, job.title, BOLD, 11, self.p.heading)
            if job.period:
                self._text_right(RIGHT, self.y, job.period, REGULAR, 9, self.p.faint)
            subtitle = " \u00b7 ".join(p for p in (job.company, job.location) if p)
            if subtitle:
                self.y += 13.2
                self._text(LEFT, self.y, subtitle, REGULAR, 9, self.p.subtle)
            self.y += 4.5
            self.bullets([b for b in job.bullets if b.strip()])

    def experience(self) -> None:
        if not self.cv.jobs:
            return
        self.section("Experience")
        self.job_entries(self.cv.jobs)

    def volunteering(self) -> None:
        jobs = [j for j in self.cv.volunteering if j.title or j.company or j.bullets]
        if not jobs:
            return
        self.section(self.cv.volunteer_title or "Volunteer Work")
        self.job_entries(jobs)

    @staticmethod
    def _group_height(group: SkillGroup, first: bool) -> float:
        """Space one skill group takes in its column, mirroring _draw_skills."""
        count = len([s for s in group.skills if s.name.strip()])
        gap = 11.3 if first else 16.9
        return gap + (14.8 + 11.0 * (count - 1) if count else 0.0)

    def _skills_that_fit(self, groups: list[SkillGroup], space: float) -> int:
        """How many groups fit in `space`, filling the two columns alternately."""
        heights, counts = [0.0, 0.0], [0, 0]
        for i, group in enumerate(groups):
            column = i % 2
            height = heights[column] + self._group_height(group, counts[column] == 0)
            if max(height, heights[1 - column]) > space:
                return i
            heights[column] = height
            counts[column] += 1
        return len(groups)

    def _draw_skills(self, groups: list[SkillGroup]) -> None:
        top = self.y
        bottom = top
        for column, x in enumerate((SKILL_X, COL2_X)):
            self.y = top
            for i, group in enumerate(groups[column::2]):
                self.y += 11.3 if i == 0 else 16.9
                self._text(x, self.y, group.title, BOLD, 9, self.p.heading)
                named = [s for s in group.skills if s.name.strip()]
                for j, skill in enumerate(named):
                    self.y += 14.8 if j == 0 else 11.0
                    tone = self.p.accent if skill.highlight else self.p.body
                    self._text(x, self.y, f"\u00b7 {skill.name}", REGULAR, 8, tone)
            bottom = max(bottom, self.y)
        self.y = bottom

    def skills(self) -> None:
        groups = [g for g in self.cv.skill_groups if g.title or g.skills]
        if not groups:
            return
        self.section("Technical Skills")
        while groups:
            take = self._skills_that_fit(groups, self._room())
            if take == 0:  # nothing fits below the cursor - carry on overleaf
                self._page_break()
                take = max(1, self._skills_that_fit(groups, self._room()))
            self._draw_skills(groups[:take])
            groups = groups[take:]

    def soft_skills(self) -> None:
        items = [s for s in self.cv.soft_skills if s.strip()]
        if not items:
            return
        self.section("Soft Skills", gap=22.4)
        sep = " \u00b7 "
        sep_w = pdfmetrics.stringWidth(sep, REGULAR, 8)

        lines: list[list[str]] = []
        used = 0.0
        for item in items:
            width = pdfmetrics.stringWidth(item, REGULAR, 8)
            if lines and LEFT + used + sep_w + width <= RIGHT:
                lines[-1].append(item)
                used += sep_w + width
            else:
                lines.append([item])
                used = width

        for i, line in enumerate(lines):
            self._need(12.0)
            self.y += 10.3 if i == 0 else 12.0
            x = LEFT
            for j, item in enumerate(line):
                self._text(x, self.y, item, REGULAR, 8, self.p.accent2)
                x += pdfmetrics.stringWidth(item, REGULAR, 8)
                last = i == len(lines) - 1 and j == len(line) - 1
                if not last and x + sep_w <= RIGHT:
                    self._text(x, self.y, sep, REGULAR, 8, self.p.accent)
                    x += sep_w

    def education(self) -> None:
        items = [e for e in self.cv.education if e.title or e.subtitle]
        if not items:
            return
        self.section("Education & Certifications", gap=16.6)
        rows = [items[i:i + 3] for i in range(0, len(items), 3)]
        trailing = 0.0  # height the previous row's subtitles added below its baseline
        for r, row in enumerate(rows):
            subtitled = 13.0 if any(item.subtitle for item in row) else 0.0
            gap = 11.25 if r == 0 else 17.0 + trailing
            self._need(gap + subtitled)
            self.y += gap
            for col, item in enumerate(row):
                x = LEFT + col * 173.85 - 6
                tone = self.p.accent2 if item.highlight else self.p.faint
                self._text(x, self.y, item.title, BOLD, 9, tone)
                if item.subtitle:
                    self._text(x, self.y + 13.0, item.subtitle, REGULAR, 8, self.p.subtle)
            trailing = subtitled
        self.y += trailing

    def lab(self) -> None:
        items = [b for b in self.cv.lab_bullets if b.strip()]
        if not items:
            return
        self.section(self.cv.lab_title or "Personal Lab", gap=17.7)
        self.bullets(items)

    def run(self) -> None:
        self._page_bg()
        self.header()
        self.summary()
        self.experience()
        self.skills()
        self.soft_skills()
        self.education()
        self.volunteering()
        self.lab()
        self.c.showPage()


def render_pdf(cv: CV, path: str | Path) -> Path:
    ensure_fonts()
    canvas = pdfcanvas.Canvas(str(path), pagesize=A4)
    canvas.setTitle(f"{cv.name} \u2014 CV" if cv.name else "CV")
    canvas.setAuthor(cv.name)
    _Renderer(cv, canvas).run()
    canvas.save()
    return Path(path)


def render_bytes(cv: CV) -> bytes:
    ensure_fonts()
    buffer = io.BytesIO()
    canvas = pdfcanvas.Canvas(buffer, pagesize=A4)
    canvas.setTitle(f"{cv.name} \u2014 CV" if cv.name else "CV")
    canvas.setAuthor(cv.name)
    _Renderer(cv, canvas).run()
    canvas.save()
    return buffer.getvalue()
