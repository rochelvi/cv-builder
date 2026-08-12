"""Renders a CV to PDF, reproducing the dark template exactly."""

from __future__ import annotations

import io
from pathlib import Path

from reportlab.lib.colors import HexColor
from reportlab.lib.pagesizes import A4
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas as pdfcanvas

from .model import CV, Job, Theme


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

    def _need(self, space: float) -> None:
        """Start a new page if `space` points do not fit below the cursor."""
        if self.y + space > PAGE_H - BOTTOM:
            self.c.showPage()
            self._page_bg()
            self.y = 52.0

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

    def skills(self) -> None:
        groups = [g for g in self.cv.skill_groups if g.title or g.skills]
        if not groups:
            return
        self.section("Technical Skills")
        top = self.y
        columns = [groups[0::2], groups[1::2]]
        bottom = top
        for column, x in zip(columns, (SKILL_X, COL2_X)):
            self.y = top
            for i, group in enumerate(column):
                self.y += 11.3 if i == 0 else 16.9
                self._text(x, self.y, group.title, BOLD, 9, self.p.heading)
                for skill in group.skills:
                    if not skill.name.strip():
                        continue
                    self.y += 14.8 if skill is group.skills[0] else 11.0
                    tone = self.p.accent if skill.highlight else self.p.body
                    self._text(x, self.y, f"\u00b7 {skill.name}", REGULAR, 8, tone)
            bottom = max(bottom, self.y)
        self.y = bottom

    def soft_skills(self) -> None:
        items = [s for s in self.cv.soft_skills if s.strip()]
        if not items:
            return
        self.section("Soft Skills", gap=22.4)
        self.y += 10.3
        x = LEFT
        sep = " \u00b7 "
        for i, item in enumerate(items):
            if i:
                self._text(x, self.y, sep, REGULAR, 8, self.p.accent)
                x += pdfmetrics.stringWidth(sep, REGULAR, 8)
            width = pdfmetrics.stringWidth(item, REGULAR, 8)
            if x + width > RIGHT:
                x = LEFT
                self.y += 12.0
            self._text(x, self.y, item, REGULAR, 8, self.p.accent2)
            x += width

    def education(self) -> None:
        items = [e for e in self.cv.education if e.title or e.subtitle]
        if not items:
            return
        self.section("Education & Certifications", gap=16.6)
        top = self.y
        bottom = top
        for i, item in enumerate(items):
            col, row = i % 3, i // 3
            x = LEFT + col * 173.85
            y = top + 11.25 + row * 30.0
            tone = self.p.accent2 if item.highlight else self.p.faint
            self._text(x - 6, y, item.title, BOLD, 9, tone)
            if item.subtitle:
                self._text(x - 6, y + 13.0, item.subtitle, REGULAR, 8, self.p.subtle)
            bottom = max(bottom, y + (13.0 if item.subtitle else 0.0))
        self.y = bottom

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
    _Renderer(cv, canvas).run()
    canvas.save()
    return buffer.getvalue()
