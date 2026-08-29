# Cross-builds the Windows executables with mingw-w64.
#
#   make            -> build/CVBuilder.exe and build/cvcli.exe
#   make cli        -> just the console renderer (handy for quick checks)
#   make CXX=g++    -> force the host compiler
#
# Under MSYS2 (the UCRT64 / MINGW64 shell) plain `make` is enough: the
# cross-prefixed tools do not exist there, so the host g++ and windres — which
# already target Windows — are picked up automatically.
#
# Everything links statically, so the .exe files run on a machine with no
# runtime installed.

# Prefer the cross-compiler when it is on PATH (WSL / Linux), otherwise fall
# back to the host toolchain (MSYS2, or any native mingw-w64 shell). Plain `:=`,
# not `?=`: make has a built-in default for CXX, so `?=` would quietly leave it
# as the host g++ even on Linux. Passing CXX=... on the command line still wins.
CROSS   := x86_64-w64-mingw32
CXX     := $(if $(shell command -v $(CROSS)-g++ 2>/dev/null),$(CROSS)-g++,g++)
WINDRES := $(if $(shell command -v $(CROSS)-windres 2>/dev/null),$(CROSS)-windres,windres)
BUILD   ?= build

# The .rc files are UTF-8 and hold Russian descriptions; without this windres
# reads them as a single-byte code page and the strings come out as mojibake,
# truncated at the first byte it cannot make sense of.
WINDRESFLAGS := -I res --codepage=65001

# gnu++17 rather than c++17: strict ISO mode hides the MSVC-style _wfopen that
# wide (Unicode) file paths need on Windows.
CXXFLAGS := -std=gnu++17 -O2 -Wall -Wextra -Wno-unknown-pragmas -Ires \
            -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 \
            -ffunction-sections -fdata-sections
LDFLAGS  := -static -static-libgcc -static-libstdc++ -Wl,--gc-sections
LIBS     := -lshell32 -lole32 -luuid
GUI_LIBS := -lcomctl32 -lcomdlg32 -ld2d1 -ldwrite -ldwmapi -luxtheme -lgdi32 -luser32 -lshlwapi \
            -ladvapi32

CORE_SRC := src/numeric.cpp src/json.cpp src/model.cpp src/font.cpp src/layout.cpp src/pdf.cpp
GUI_SRC  := src/app.cpp src/form.cpp src/preview.cpp src/theme.cpp src/settings.cpp src/print.cpp
CLI_SRC  := src/cli.cpp

CORE_OBJ := $(CORE_SRC:%.cpp=$(BUILD)/%.o)
GUI_OBJ  := $(GUI_SRC:%.cpp=$(BUILD)/%.o)
CLI_OBJ  := $(CLI_SRC:%.cpp=$(BUILD)/%.o)
RES_OBJ  := $(BUILD)/res/app.o
CLI_RES  := $(BUILD)/res/cli.o

.PHONY: all cli clean
all: $(BUILD)/CVBuilder.exe $(BUILD)/cvcli.exe
cli: $(BUILD)/cvcli.exe

$(BUILD)/CVBuilder.exe: $(CORE_OBJ) $(GUI_OBJ) $(RES_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -mwindows -o $@ $^ $(LDFLAGS) $(GUI_LIBS) $(LIBS)

$(BUILD)/cvcli.exe: $(CORE_OBJ) $(CLI_OBJ) $(CLI_RES)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

# Everything depends on the Makefile itself: it holds the compiler flags and
# the list of libraries, so editing it has to invalidate what was built with
# the old ones. Without this, changing CXXFLAGS leaves every stale object in
# place and make cheerfully reports there is nothing to do.
$(BUILD)/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(RES_OBJ): res/app.rc res/app.manifest res/app.ico res/version.h Makefile
	@mkdir -p $(dir $@)
	$(WINDRES) $(WINDRESFLAGS) -i $< -o $@

$(CLI_RES): res/cli.rc res/app.ico res/version.h Makefile
	@mkdir -p $(dir $@)
	$(WINDRES) $(WINDRESFLAGS) -i $< -o $@

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
