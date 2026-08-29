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

No compiler on Windows: everything is built in WSL (`wsl -d Ubuntu`), which has
g++ and mingw-w64 but no cmake or Qt from apt, and `sudo` wants a password.
What was set up, all under `$HOME` and without root:

| What | Where | How |
| --- | --- | --- |
| CMake 4.4 | `~/.local/bin/cmake` | official tarball unpacked into `~/.local/opt` |
| Qt 6.8.3 | `~/Qt/6.8.3/gcc_64` | `aqtinstall` in a venv at `~/qtenv` |
| OpenGL dev files | `~/qtsysroot/tree` | `tools/qtsysroot.sh` - unpacks .deb files |

Qt6::Gui insists on the OpenGL imported targets even for a Widgets program, so
configuring needs the shim:

```bash
cmake --preset linux \
      -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/gcc_64 \
      -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=tools/qtshim.cmake
```

None of that is needed on a machine with `qt6-base-dev` installed.

Also: the shell these commands are typed into expands `$VAR` before bash sees it.
Anything with shell variables in it belongs in a script file under `tools/`, not
on the command line - the same mistake otherwise turns `cmake-$V.tar.gz` into
`cmake-.tar.gz` and takes ten minutes to notice. And `/tmp` in WSL does not
survive the distribution shutting down when idle; caches go under `$HOME`.
