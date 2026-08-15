"""CV Builder - a Windows desktop app for filling in the dark CV template."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

from PySide6.QtCore import QBuffer, QByteArray, QSize, Qt, QTimer
from PySide6.QtGui import QAction, QColor, QKeySequence, QPixmap
from PySide6.QtPdf import QPdfDocument
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QColorDialog,
    QComboBox,
    QFileDialog,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QScrollArea,
    QSizePolicy,
    QSpinBox,
    QToolButton,
    QVBoxLayout,
    QWidget,
)

from . import fluent
from .model import (
    CV,
    PRESETS,
    THEME_LABELS,
    Education,
    Job,
    Skill,
    SkillGroup,
    Theme,
    sample_cv,
)
from .renderer import render_bytes, render_pdf

APP_NAME = "CV Builder"

def heading(text: str) -> QLabel:
    label = QLabel(text)
    label.setProperty("heading", True)
    return label


def small(text: str) -> QLabel:
    label = QLabel(text)
    label.setProperty("sub", True)
    return label


class ColorButton(QPushButton):
    """A swatch that opens a colour picker and reports the chosen #rrggbb."""

    def __init__(self, value: str, on_change):
        super().__init__()
        self._value = value
        self.on_change = on_change
        self.setFixedSize(QSize(78, 24))
        self.setCursor(Qt.PointingHandCursor)
        self.clicked.connect(self.pick)
        self.apply()

    def apply(self) -> None:
        text_color = "#000000" if QColor(self._value).lightness() > 140 else "#ffffff"
        self.setStyleSheet(
            f"background: {self._value}; color: {text_color};"
            "border: 1px solid rgba(128, 128, 128, 0.45);"
            "border-radius: 4px; font-size: 11px;"
        )
        self.setText(self._value)
        self.setToolTip(f"Click to change ({self._value})")

    def value(self) -> str:
        return self._value

    def set_value(self, value: str) -> None:
        self._value = value
        self.apply()

    def pick(self) -> None:
        chosen = QColorDialog.getColor(QColor(self._value), self, "Pick a colour")
        if chosen.isValid():
            self.set_value(chosen.name())
            self.on_change()


class ThemeEditor(QWidget):
    """Preset picker plus one swatch per colour role."""

    def __init__(self, on_change):
        super().__init__()
        self.on_change = on_change
        self._loading = False
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        top = QHBoxLayout()
        top.addWidget(small("Preset"))
        self.presets = QComboBox()
        self.presets.addItems(list(PRESETS))
        self.presets.activated.connect(self.apply_preset)
        top.addWidget(self.presets, 1)
        layout.addLayout(top)

        grid = QGridLayout()
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(4)
        self.buttons: dict[str, ColorButton] = {}
        defaults = Theme().to_dict()
        for i, (key, label) in enumerate(THEME_LABELS.items()):
            button = ColorButton(defaults[key], self._changed)
            self.buttons[key] = button
            row, column = divmod(i, 2)
            grid.addWidget(button, row, column * 2)
            grid.addWidget(small(label), row, column * 2 + 1)
        grid.setColumnStretch(1, 1)
        grid.setColumnStretch(3, 1)
        layout.addLayout(grid)

    def _changed(self) -> None:
        if not self._loading:
            self.on_change()

    def apply_preset(self) -> None:
        self.set_theme(PRESETS[self.presets.currentText()])
        self.on_change()

    def set_theme(self, theme: Theme) -> None:
        self._loading = True
        values = theme.to_dict()
        for key, button in self.buttons.items():
            button.set_value(values[key])
        name = next((n for n, t in PRESETS.items() if t == theme), None)
        self.presets.setCurrentText(name or self.presets.currentText())
        self._loading = False

    def theme(self) -> Theme:
        return Theme(**{key: button.value() for key, button in self.buttons.items()})


class ListEditor(QWidget):
    """A vertical list of one-line text fields with add/remove/reorder controls."""

    def __init__(self, on_change, placeholder: str = "", multiline: bool = False):
        super().__init__()
        self.on_change = on_change
        self.placeholder = placeholder
        self.multiline = multiline
        self.rows: list[QWidget] = []
        self.layout_ = QVBoxLayout(self)
        self.layout_.setContentsMargins(0, 0, 0, 0)
        self.layout_.setSpacing(4)
        self.add_button = QPushButton("+ Add")
        self.add_button.clicked.connect(lambda: (self.add_row(""), self.on_change()))
        self.layout_.addWidget(self.add_button, 0, Qt.AlignLeft)

    def add_row(self, text: str) -> None:
        row = QWidget()
        box = QHBoxLayout(row)
        box.setContentsMargins(0, 0, 0, 0)
        if self.multiline:
            editor = QPlainTextEdit(text)
            editor.setFixedHeight(48)
            editor.textChanged.connect(self.on_change)
        else:
            editor = QLineEdit(text)
            editor.setPlaceholderText(self.placeholder)
            editor.textChanged.connect(self.on_change)
        row.editor = editor  # type: ignore[attr-defined]
        box.addWidget(editor, 1)
        for symbol, slot in (
            ("\u2191", lambda: self.move(row, -1)),
            ("\u2193", lambda: self.move(row, 1)),
            ("\u2715", lambda: self.remove(row)),
        ):
            button = QToolButton()
            button.setText(symbol)
            button.clicked.connect(slot)
            box.addWidget(button)
        self.rows.append(row)
        self.layout_.insertWidget(self.layout_.count() - 1, row)

    def move(self, row: QWidget, delta: int) -> None:
        i = self.rows.index(row)
        j = i + delta
        if 0 <= j < len(self.rows):
            self.rows.insert(j, self.rows.pop(i))
            self.layout_.removeWidget(row)
            self.layout_.insertWidget(j, row)
            self.on_change()

    def remove(self, row: QWidget) -> None:
        self.rows.remove(row)
        row.setParent(None)
        self.on_change()

    def set_values(self, values: list[str]) -> None:
        for row in list(self.rows):
            self.rows.remove(row)
            row.setParent(None)
        for value in values:
            self.add_row(value)

    def values(self) -> list[str]:
        out = []
        for row in self.rows:
            editor = row.editor  # type: ignore[attr-defined]
            text = editor.toPlainText() if self.multiline else editor.text()
            if text.strip():
                out.append(text.strip())
        return out


class CardList(QWidget):
    """A list of removable/reorderable cards (jobs, skill groups, education)."""

    def __init__(self, on_change, factory, add_label: str):
        super().__init__()
        self.on_change = on_change
        self.factory = factory
        self.cards: list[QFrame] = []
        self.layout_ = QVBoxLayout(self)
        self.layout_.setContentsMargins(0, 0, 0, 0)
        self.layout_.setSpacing(8)
        self.add_button = QPushButton(add_label)
        self.add_button.clicked.connect(lambda: (self.add_card(None), self.on_change()))
        self.layout_.addWidget(self.add_button, 0, Qt.AlignLeft)

    def add_card(self, data) -> QFrame:
        card = QFrame()
        card.setProperty("card", True)
        outer = QVBoxLayout(card)
        outer.setContentsMargins(10, 8, 10, 10)
        tools = QHBoxLayout()
        tools.addStretch(1)
        for symbol, slot in (
            ("\u2191", lambda: self.move(card, -1)),
            ("\u2193", lambda: self.move(card, 1)),
            ("\u2715", lambda: self.remove(card)),
        ):
            button = QToolButton()
            button.setText(symbol)
            button.clicked.connect(slot)
            tools.addWidget(button)
        outer.addLayout(tools)
        body = QWidget()
        outer.addWidget(body)
        card.reader = self.factory(body, data, self.on_change)  # type: ignore[attr-defined]
        self.cards.append(card)
        self.layout_.insertWidget(self.layout_.count() - 1, card)
        return card

    def move(self, card: QFrame, delta: int) -> None:
        i = self.cards.index(card)
        j = i + delta
        if 0 <= j < len(self.cards):
            self.cards.insert(j, self.cards.pop(i))
            self.layout_.removeWidget(card)
            self.layout_.insertWidget(j, card)
            self.on_change()

    def remove(self, card: QFrame) -> None:
        self.cards.remove(card)
        card.setParent(None)
        self.on_change()

    def set_items(self, items: list) -> None:
        for card in list(self.cards):
            self.cards.remove(card)
            card.setParent(None)
        for item in items:
            self.add_card(item)

    def values(self) -> list:
        return [card.reader() for card in self.cards]  # type: ignore[attr-defined]


def job_card(parent: QWidget, job: Job | None, on_change) -> callable:
    job = job or Job(title="Job title", period="2024 \u2013 2025", company="Company")
    grid = QVBoxLayout(parent)
    grid.setContentsMargins(0, 0, 0, 0)
    title = QLineEdit(job.title)
    title.setPlaceholderText("Position")
    period = QLineEdit(job.period)
    period.setPlaceholderText("2022 \u2013 2025 \u00b7 3 yrs")
    company = QLineEdit(job.company)
    company.setPlaceholderText("Company")
    location = QLineEdit(job.location)
    location.setPlaceholderText("City")
    top = QHBoxLayout()
    top.addWidget(title, 2)
    top.addWidget(period, 1)
    grid.addLayout(top)
    middle = QHBoxLayout()
    middle.addWidget(company, 2)
    middle.addWidget(location, 1)
    grid.addLayout(middle)
    grid.addWidget(small("Bullet points"))
    bullets = ListEditor(on_change, "Achievement or responsibility")
    bullets.set_values(job.bullets)
    grid.addWidget(bullets)
    for widget in (title, period, company, location):
        widget.textChanged.connect(on_change)
    return lambda: Job(
        title=title.text(),
        period=period.text(),
        company=company.text(),
        location=location.text(),
        bullets=bullets.values(),
    )


class SkillRow(QWidget):
    def __init__(self, skill: Skill, on_change, remove):
        super().__init__()
        box = QHBoxLayout(self)
        box.setContentsMargins(0, 0, 0, 0)
        self.name = QLineEdit(skill.name)
        self.name.setPlaceholderText("Skill")
        self.name.textChanged.connect(on_change)
        self.highlight = QCheckBox("highlight")
        self.highlight.setToolTip("Show this skill in green (strong skill)")
        self.highlight.setChecked(skill.highlight)
        self.highlight.stateChanged.connect(on_change)
        box.addWidget(self.name, 1)
        box.addWidget(self.highlight)
        button = QToolButton()
        button.setText("\u2715")
        button.clicked.connect(lambda: remove(self))
        box.addWidget(button)


def skill_group_card(parent: QWidget, group: SkillGroup | None, on_change) -> callable:
    group = group or SkillGroup(title="New group", skills=[Skill("Skill", True)])
    layout = QVBoxLayout(parent)
    layout.setContentsMargins(0, 0, 0, 0)
    title = QLineEdit(group.title)
    title.setPlaceholderText("Group name, e.g. Networking")
    title.textChanged.connect(on_change)
    layout.addWidget(title)
    rows_holder = QVBoxLayout()
    rows_holder.setSpacing(3)
    layout.addLayout(rows_holder)
    rows: list[SkillRow] = []

    def remove(row: SkillRow) -> None:
        rows.remove(row)
        row.setParent(None)
        on_change()

    def add(skill: Skill) -> None:
        row = SkillRow(skill, on_change, remove)
        rows.append(row)
        rows_holder.addWidget(row)

    for skill in group.skills:
        add(skill)
    add_button = QPushButton("+ Add skill")
    add_button.clicked.connect(lambda: (add(Skill("", False)), on_change()))
    layout.addWidget(add_button, 0, Qt.AlignLeft)
    return lambda: SkillGroup(
        title=title.text(),
        skills=[
            Skill(r.name.text().strip(), r.highlight.isChecked())
            for r in rows
            if r.name.text().strip()
        ],
    )


def education_card(parent: QWidget, item: Education | None, on_change) -> callable:
    item = item or Education(title="Certification", subtitle="Planned")
    layout = QVBoxLayout(parent)
    layout.setContentsMargins(0, 0, 0, 0)
    title = QLineEdit(item.title)
    title.setPlaceholderText("Program or certification")
    subtitle = QLineEdit(item.subtitle)
    subtitle.setPlaceholderText("Institution, year / status")
    highlight = QCheckBox("highlight (blue)")
    highlight.setChecked(item.highlight)
    for widget in (title, subtitle):
        widget.textChanged.connect(on_change)
    highlight.stateChanged.connect(on_change)
    layout.addWidget(title)
    layout.addWidget(subtitle)
    layout.addWidget(highlight)
    return lambda: Education(title.text(), subtitle.text(), highlight.isChecked())


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(APP_NAME)
        self.resize(1500, 950)
        self.current_path: Path | None = None
        self.preview_page = 0
        self.page_count = 1
        self._pdf_cache = b""
        self._pdf_bytes = QByteArray()
        self._styled = False
        self.document = QPdfDocument(self)
        self.buffer = QBuffer(self)

        self.refresh_timer = QTimer(self)
        self.refresh_timer.setSingleShot(True)
        self.refresh_timer.setInterval(250)
        self.refresh_timer.timeout.connect(self.update_preview)

        central = QWidget()
        root = QHBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)
        root.addWidget(self._build_form(), 1)
        root.addWidget(self._build_preview(), 1)
        self.setCentralWidget(central)
        self._build_menu()

        QApplication.instance().styleHints().colorSchemeChanged.connect(
            lambda _scheme: self.apply_system_theme()
        )
        self.load_cv(sample_cv())

    # ---------- appearance ----------
    def showEvent(self, event) -> None:
        # the window handle DWM needs only exists once the window is realised
        super().showEvent(event)
        if not self._styled:
            self._styled = True
            self.apply_system_theme()

    def apply_system_theme(self) -> None:
        """Follow the Windows light/dark setting. The CV palette stays in the PDF."""
        dark = fluent.is_dark()
        fluent.apply_titlebar(self, dark)
        QApplication.instance().setStyleSheet(fluent.stylesheet(dark))

    # ---------- UI construction ----------
    def _build_form(self) -> QWidget:
        container = QWidget()
        container.setProperty("pane", True)
        layout = QVBoxLayout(container)
        layout.setContentsMargins(14, 14, 8, 14)

        toolbar = QHBoxLayout()
        for text, slot in (
            ("New", self.action_new),
            ("Open\u2026", self.action_open),
            ("Save", self.action_save),
            ("Save As\u2026", self.action_save_as),
            ("Export PDF\u2026", self.action_export),
        ):
            button = QPushButton(text)
            button.clicked.connect(slot)
            toolbar.addWidget(button)
        toolbar.addStretch(1)
        layout.addLayout(toolbar)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        inner = QWidget()
        form = QVBoxLayout(inner)
        form.setSpacing(6)

        self.name = QLineEdit()
        self.role = QLineEdit()
        self.email = QLineEdit()
        self.location = QLineEdit()
        self.website = QLineEdit()
        self.summary = QPlainTextEdit()
        self.summary.setFixedHeight(110)
        self.lab_title = QLineEdit()

        form.addWidget(heading("Colors"))
        self.theme_editor = ThemeEditor(self.schedule_refresh)
        form.addWidget(self.theme_editor)

        form.addWidget(heading("Header"))
        form.addWidget(self.name)
        self.name.setPlaceholderText("Full name")
        form.addWidget(self.role)
        self.role.setPlaceholderText("Job title, e.g. SYSTEM ADMINISTRATOR")
        contacts = QHBoxLayout()
        for widget, placeholder in (
            (self.email, "Email"),
            (self.location, "City, Country"),
            (self.website, "Website / GitHub"),
        ):
            widget.setPlaceholderText(placeholder)
            contacts.addWidget(widget)
        form.addLayout(contacts)

        form.addWidget(heading("Summary"))
        form.addWidget(self.summary)

        form.addWidget(heading("Experience"))
        self.jobs = CardList(self.schedule_refresh, job_card, "+ Add job")
        form.addWidget(self.jobs)

        form.addWidget(heading("Volunteer work"))
        self.volunteer_title = QLineEdit()
        self.volunteer_title.setPlaceholderText("Section title")
        form.addWidget(self.volunteer_title)
        self.volunteering = CardList(self.schedule_refresh, job_card, "+ Add volunteer role")
        form.addWidget(self.volunteering)

        form.addWidget(heading("Technical skills"))
        form.addWidget(small("Groups fill the two columns left-to-right."))
        self.skill_groups = CardList(self.schedule_refresh, skill_group_card, "+ Add skill group")
        form.addWidget(self.skill_groups)

        form.addWidget(heading("Soft skills"))
        self.soft_skills = ListEditor(self.schedule_refresh, "Soft skill")
        form.addWidget(self.soft_skills)

        form.addWidget(heading("Education & certifications"))
        self.education = CardList(self.schedule_refresh, education_card, "+ Add entry")
        form.addWidget(self.education)

        form.addWidget(heading("Personal lab"))
        self.lab_title.setPlaceholderText("Section title")
        form.addWidget(self.lab_title)
        self.lab_bullets = ListEditor(self.schedule_refresh, "Project / lab bullet")
        form.addWidget(self.lab_bullets)
        form.addStretch(1)

        for widget in (self.name, self.role, self.email, self.location, self.website,
                       self.lab_title, self.volunteer_title):
            widget.textChanged.connect(self.schedule_refresh)
        self.summary.textChanged.connect(self.schedule_refresh)

        scroll.setWidget(inner)
        layout.addWidget(scroll)
        return container

    def _build_preview(self) -> QWidget:
        container = QWidget()
        container.setProperty("pane", True)
        layout = QVBoxLayout(container)
        layout.setContentsMargins(8, 14, 14, 14)

        bar = QHBoxLayout()
        bar.addWidget(heading("Live preview"))
        bar.addStretch(1)
        self.page_label = small("Page 1 / 1")
        self.prev_button = QPushButton("\u2039")
        self.next_button = QPushButton("\u203a")
        self.prev_button.clicked.connect(lambda: self.change_page(-1))
        self.next_button.clicked.connect(lambda: self.change_page(1))
        bar.addWidget(self.prev_button)
        bar.addWidget(self.page_label)
        bar.addWidget(self.next_button)
        zoom_label = small("Zoom %")
        self.zoom = QSpinBox()
        self.zoom.setRange(50, 300)
        self.zoom.setValue(100)
        self.zoom.setSingleStep(10)
        self.zoom.valueChanged.connect(self.update_preview)
        bar.addWidget(zoom_label)
        bar.addWidget(self.zoom)
        layout.addLayout(bar)

        self.preview = QLabel()
        self.preview.setAlignment(Qt.AlignHCenter | Qt.AlignTop)
        self.preview.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Ignored)
        area = QScrollArea()
        area.setWidgetResizable(True)
        area.setWidget(self.preview)
        layout.addWidget(area)
        return container

    def _build_menu(self) -> None:
        menu = self.menuBar().addMenu("&File")
        for text, shortcut, slot in (
            ("&New", QKeySequence.New, self.action_new),
            ("&Open\u2026", QKeySequence.Open, self.action_open),
            ("&Save", QKeySequence.Save, self.action_save),
            ("Save &As\u2026", QKeySequence.SaveAs, self.action_save_as),
            ("&Export PDF\u2026", QKeySequence("Ctrl+E"), self.action_export),
            ("&Quit", QKeySequence.Quit, self.close),
        ):
            action = QAction(text, self)
            action.setShortcut(shortcut)
            action.triggered.connect(slot)
            menu.addAction(action)

    # ---------- data binding ----------
    def load_cv(self, cv: CV) -> None:
        self.name.setText(cv.name)
        self.role.setText(cv.role)
        self.email.setText(cv.email)
        self.location.setText(cv.location)
        self.website.setText(cv.website)
        self.summary.setPlainText(cv.summary)
        self.jobs.set_items(cv.jobs)
        self.volunteer_title.setText(cv.volunteer_title)
        self.volunteering.set_items(cv.volunteering)
        self.theme_editor.set_theme(cv.theme)
        self.skill_groups.set_items(cv.skill_groups)
        self.soft_skills.set_values(cv.soft_skills)
        self.education.set_items(cv.education)
        self.lab_title.setText(cv.lab_title)
        self.lab_bullets.set_values(cv.lab_bullets)
        self.update_preview()

    def collect(self) -> CV:
        return CV(
            name=self.name.text(),
            role=self.role.text(),
            email=self.email.text(),
            location=self.location.text(),
            website=self.website.text(),
            summary=self.summary.toPlainText(),
            jobs=self.jobs.values(),
            volunteer_title=self.volunteer_title.text(),
            volunteering=self.volunteering.values(),
            skill_groups=self.skill_groups.values(),
            soft_skills=self.soft_skills.values(),
            education=self.education.values(),
            lab_title=self.lab_title.text(),
            lab_bullets=self.lab_bullets.values(),
            theme=self.theme_editor.theme(),
        )

    # ---------- preview ----------
    def schedule_refresh(self) -> None:
        self.refresh_timer.start()

    def change_page(self, delta: int) -> None:
        self.preview_page = max(0, min(self.page_count - 1, self.preview_page + delta))
        self.render_preview_page()

    def update_preview(self) -> None:
        cv = self.collect()
        try:
            self._pdf_cache = render_bytes(cv)
        except Exception as exc:  # keep the UI alive on any rendering hiccup
            self.preview.setText(f"Preview error: {exc}")
            return
        self._load_preview()
        self.page_count = max(1, self.document.pageCount())
        self.preview_page = min(self.preview_page, self.page_count - 1)
        self.render_preview_page()

    def _load_preview(self) -> None:
        """Hand the freshly rendered PDF to QtPdf.

        Both the buffer and the byte array it wraps have to outlive this call -
        QPdfDocument keeps reading from the device while it renders pages.
        """
        self.document.close()
        self.buffer.close()
        self._pdf_bytes = QByteArray(self._pdf_cache)
        self.buffer.setData(self._pdf_bytes)
        self.buffer.open(QBuffer.ReadOnly)
        self.document.load(self.buffer)

    def render_preview_page(self) -> None:
        if not self.document.pageCount():
            return
        # 110 dpi at 100 % zoom, the scale the preview was tuned for
        scale = 110.0 * self.zoom.value() / 100.0 / 72.0
        points = self.document.pagePointSize(self.preview_page)
        size = QSize(round(points.width() * scale), round(points.height() * scale))
        self.preview.setPixmap(QPixmap.fromImage(self.document.render(self.preview_page, size)))
        self.preview.setMinimumSize(size)
        self.page_label.setText(f"Page {self.preview_page + 1} / {self.page_count}")

    # ---------- actions ----------
    def action_new(self) -> None:
        self.current_path = None
        self.load_cv(CV(name="Your Name", role="YOUR ROLE"))

    def action_open(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "Open CV", "", "CV files (*.json)")
        if path:
            try:
                self.load_cv(CV.load(path))
                self.current_path = Path(path)
                self.setWindowTitle(f"{APP_NAME} \u2014 {self.current_path.name}")
            except Exception as exc:
                QMessageBox.critical(self, APP_NAME, f"Could not open file:\n{exc}")

    def action_save(self) -> None:
        if self.current_path is None:
            self.action_save_as()
            return
        self.collect().save(self.current_path)
        self.statusBar().showMessage(f"Saved {self.current_path}", 4000)

    def action_save_as(self) -> None:
        default = f"{self.name.text().strip().replace(' ', '_') or 'cv'}.json"
        path, _ = QFileDialog.getSaveFileName(self, "Save CV", default, "CV files (*.json)")
        if path:
            self.current_path = Path(path)
            self.action_save()
            self.setWindowTitle(f"{APP_NAME} \u2014 {self.current_path.name}")

    def action_export(self) -> None:
        default = f"{self.name.text().strip().replace(' ', '_') or 'cv'}.pdf"
        path, _ = QFileDialog.getSaveFileName(self, "Export PDF", default, "PDF (*.pdf)")
        if not path:
            return
        try:
            render_pdf(self.collect(), path)
        except Exception as exc:
            QMessageBox.critical(self, APP_NAME, f"Could not export PDF:\n{exc}")
            return
        self.statusBar().showMessage(f"Exported {path}", 5000)
        self.open_file(path)

    @staticmethod
    def open_file(path: str) -> None:
        try:
            if sys.platform.startswith("win"):
                os.startfile(path)  # type: ignore[attr-defined]
            elif sys.platform == "darwin":
                subprocess.Popen(["open", path])
            else:
                subprocess.Popen(["xdg-open", path])
        except Exception:
            pass


def main() -> int:
    os.environ.setdefault("QT_ENABLE_HIGHDPI_SCALING", "1")
    app = QApplication(sys.argv)
    app.setApplicationName(APP_NAME)
    app.setFont(fluent.font())
    window = MainWindow()
    window.show()
    return app.exec()
