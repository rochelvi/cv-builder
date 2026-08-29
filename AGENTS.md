# Working on this repository

## Build and test

One CMake description for every platform; presets are in `CMakePresets.json`.

```bash
cmake --preset linux            # or windows-mingw / windows-msvc / macos
cmake --build build/linux -j
ctest --preset linux            # needs no display: the Qt case runs offscreen
```

`build.ps1` is the Windows path: it cross-compiles with mingw-w64 inside WSL and
then runs the test suite. `run.ps1` builds if needed and starts the app.

Targets follow the layers of the program, and that is load-bearing: a link error
means a layer was crossed. `resume_core` and `resume_render` must not include
`windows.h`, `d2d1.h`, `dwrite.h` or any `#ifdef _WIN32` - platform sources are
chosen in CMake, not in the preprocessor. Worth checking before committing:

```bash
grep -rn 'windows.h\|_WIN32\|d2d1\|dwrite' src/core src/render   # must be silent
```

## Things that will bite

* **Never format a number with `printf`/`strtod` in the core.** Both honour
  `LC_NUMERIC`, and Qt calls `setlocale(LC_ALL, "")` on start-up: in a locale with
  a decimal comma a PDF coordinate becomes `595,276` and the file will not open.
  Use `numfmt::fixed` / `numfmt::shortest` / `numfmt::parse` (`src/core/numeric.h`).
* **Never build a path from a narrow string with `Path(text)`.** A `std::string`
  handed to `std::filesystem::path` is read in the platform's narrow encoding,
  which on Windows cannot spell most paths. Use `cvb::fromUtf8` / `cvb::toUtf8`.
* **Do not change the bundled font without measuring.** Advance widths decide
  where lines break, so a different set of widths reflows every resume ever
  written. `tests/test_font.cpp` pins the widths; `tools/fontprobe.sh` compares
  the layout of a whole resume and `cvcli --font-report` compares metrics per code
  point.
* **`tests/test_pdf.cpp` pins the bytes of a generated PDF.** If a deliberate
  change to the template or the writer breaks it, move the number in the same
  commit - and say in that commit that every exported resume now looks different.
* The Win32 front end (`src/ui_win32`) is the reference implementation and is
  frozen. New interface work goes to `src/ui_qt`.
* Interface colours and the editor's words are shared between both front ends in
  `src/uicommon`. Do not copy them into a front end.

## Qt

Only Core, Gui, Widgets and PrintSupport, 6.5 or newer. Without Qt everything
else still builds; CMake says so and turns `CVB_BUILD_QT_UI` off.

## This machine, specifically

No compiler on Windows: everything is built in WSL (`wsl -d Ubuntu`), where `sudo`
needs no password. `cmake`, `qt6-base-dev` and `qt6-wayland` are installed from
apt, so the plain commands work and nothing needs pointing at anything:

```bash
cmake --preset linux && cmake --build build/linux -j && ctest --preset linux
```

`wsl -d Ubuntu -- bash tools/run-qt-wslg.sh` puts the Qt window on the Windows
desktop through WSLg. `resume_qt_shot` renders it to a PNG with no display at all,
which is the more reliable way to look at it.

A Qt unpacked under `$HOME` by `aqtinstall` also works, and then Qt6::Gui insists
on the OpenGL imported targets even for a Widgets program, which is what
`tools/qtsysroot.sh` and `tools/qtshim.cmake` are for:

```bash
cmake --preset linux \
      -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/gcc_64 \
      -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=tools/qtshim.cmake
```

Two traps that cost time here:

* **Quoting through `wsl.exe`.** PowerShell 7 rewrites embedded quotes when it
  passes arguments to a native program, and the shell these commands are typed
  into expands `$VAR` before bash sees it. Anything with quotes or shell variables
  belongs in a script under `tools/` - which is why `build.ps1` calls
  `tools/build-windows.sh` instead of a one-liner. The same mistake otherwise
  turns `cmake-$V.tar.gz` into `cmake-.tar.gz` and takes ten minutes to notice.
* **`/tmp` in WSL does not survive the distribution shutting down when idle.**
  Caches go under `$HOME`.
