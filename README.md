# CV Builder

A Windows 11 desktop app that builds your CV using the dark template from `cv.pdf`.
Fill in the form on the left, watch the live preview on the right, export a
pixel-faithful A4 PDF.

## Features

- Every section of the template is editable: header/contacts, summary, experience
  (jobs with bullet points), technical skills (two-column groups, accent highlight per
  skill), soft skills, education & certifications, volunteer work, personal lab.
- Full colour control: 8 colour roles (background, rules, headings, body, secondary text,
  faint text, accent 1, accent 2) editable with a colour picker, plus ready-made presets
  (dark original, midnight blue, graphite orange, paper light, warm cream). Nothing is
  hardcoded any more — the palette is stored with your CV JSON.
- Add / remove / reorder jobs, bullets, skills, groups and entries.
- Live PDF preview with zoom and page navigation; automatic overflow to page 2.
- Save/load your data as JSON (`Ctrl+S` / `Ctrl+O`) so you can keep several CV versions.
- Export PDF (`Ctrl+E`) and it opens in your default viewer.
- Cyrillic is fully supported (uses Arial from `C:\Windows\Fonts`, metrically identical
  to the template's Helvetica).

## Run from source

Requires Python 3.10+ (from python.org or the Microsoft Store).

```
run.bat
```

The first run creates a virtual environment and installs the dependencies.

## Build a standalone .exe

```
build.bat
```

The result is `dist\CVBuilder.exe` — a single file you can copy anywhere and run
without Python installed.

## Files

| File | Purpose |
| --- | --- |
| `main.py` | Entry point |
| `cvbuilder/model.py` | CV data model, `Theme` palette + presets, JSON save/load, default content |
| `cvbuilder/renderer.py` | PDF renderer (ReportLab) reproducing the template geometry and colours |
| `cvbuilder/app.py` | PySide6 GUI: form editor + live preview |
| `sample_cv.json` | The original CV content, ready to open |
