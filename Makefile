# Cross-builds the Windows executables with mingw-w64.
#
#   make            -> build/CVBuilder.exe and build/cvcli.exe
#   make cli        -> just the console renderer (handy for quick checks)
#   make CXX=g++    -> native build, if you ever run this on Windows itself
#
# Everything links statically, so the .exe files run on a machine with no
# runtime installed.

# Plain `:=`, not `?=`: make has a built-in default for CXX, so `?=` would
# quietly leave it as the host g++. Passing CXX=... on the command line still
# wins, which is what the native-build note above relies on.
CXX     := x86_64-w64-mingw32-g++
WINDRES := x86_64-w64-mingw32-windres
BUILD   ?= build

# gnu++17 rather than c++17: strict ISO mode hides the MSVC-style _wfopen that
# wide (Unicode) file paths need on Windows.
CXXFLAGS := -std=gnu++17 -O2 -Wall -Wextra -Wno-unknown-pragmas \
            -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 \
            -ffunction-sections -fdata-sections
LDFLAGS  := -static -static-libgcc -static-libstdc++ -Wl,--gc-sections
LIBS     := -lshell32 -lole32 -luuid
GUI_LIBS := -lcomctl32 -lcomdlg32 -ld2d1 -ldwrite -ldwmapi -lgdi32 -luser32 -lshlwapi

CORE_SRC := src/json.cpp src/model.cpp src/font.cpp src/layout.cpp src/pdf.cpp
GUI_SRC  := src/app.cpp src/form.cpp src/preview.cpp
CLI_SRC  := src/cli.cpp

CORE_OBJ := $(CORE_SRC:%.cpp=$(BUILD)/%.o)
GUI_OBJ  := $(GUI_SRC:%.cpp=$(BUILD)/%.o)
CLI_OBJ  := $(CLI_SRC:%.cpp=$(BUILD)/%.o)
RES_OBJ  := $(BUILD)/res/app.o

.PHONY: all cli clean
all: $(BUILD)/CVBuilder.exe $(BUILD)/cvcli.exe
cli: $(BUILD)/cvcli.exe

$(BUILD)/CVBuilder.exe: $(CORE_OBJ) $(GUI_OBJ) $(RES_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -mwindows -o $@ $^ $(LDFLAGS) $(GUI_LIBS) $(LIBS)

$(BUILD)/cvcli.exe: $(CORE_OBJ) $(CLI_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(RES_OBJ): res/app.rc res/app.manifest
	@mkdir -p $(dir $@)
	$(WINDRES) -I res -i $< -o $@

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
