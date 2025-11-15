OUT_DIR = build
OUT_EXE = cleargrep.exe

CFLAGS = /std:c++20 /Zc:strictStrings- /Zi /DEBUG /Od /MD /EHacr /utf-8 /D UNICODE /D _UNICODE
INCLUDES = /I imgui /I imgui/backends /I "$(WindowsSdkDir)Include\um" /I "$(WindowsSdkDir)Include\shared" /I "$(DXSDK_DIR)Include"
LDFLAGS = /DEBUG
LIBS = /LIBPATH:"$(DXSDK_DIR)\Lib\x86"

# List of explicit extra source files
EXTRA_SRC = imgui\backends\imgui_impl_dx11.cpp imgui\backends\imgui_impl_win32.cpp

# Output EXE path
OUT_EXE_PATH = $(OUT_DIR)\$(OUT_EXE)

# Default target
all: $(OUT_DIR) $(OUT_EXE_PATH)

# Create output directory
$(OUT_DIR):
    if not exist $(OUT_DIR) mkdir $(OUT_DIR)

# Compile all .cpp files using a for loop (manual wildcard handling)
COMPILE_CMD = \
    for %%F in (*.cpp $(EXTRA_SRC) imgui\imgui*.cpp) do \
        cl /c $(CFLAGS) $(INCLUDES) /Fo$(OUT_DIR)\ %%F

# This rule runs the compile loop, then links all .obj files in build/
$(OUT_EXE_PATH):
    $(COMPILE_CMD)
    link $(LDFLAGS) /OUT:$(OUT_EXE_PATH) $(OUT_DIR)\*.obj $(LIBS)

clean:
    del /Q /F $(OUT_DIR)\*.obj $(OUT_DIR)\*.pdb $(OUT_EXE_PATH)
