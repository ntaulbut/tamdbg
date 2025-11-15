@REM Build for Visual Studio compiler. Run your copy of vcvars32.bat or vcvarsall.bat to setup command-line compiler.
@set OUT_DIR=build
@set OUT_EXE=tamdbg
@set INCLUDES=/I imgui/ /I imgui/backends /I "%WindowsSdkDir%Include\um" /I "%WindowsSdkDir%Include\shared" /I "%DXSDK_DIR%Include"
@set SOURCES=*.cpp tam\*.cpp imgui\backends\imgui_impl_dx11.cpp imgui\backends\imgui_impl_win32.cpp imgui\imgui*.cpp
@set LIBS=/LIBPATH:"%DXSDK_DIR%/Lib/x86" d3d11.lib d3dcompiler.lib user32.lib comdlg32.lib
mkdir %OUT_DIR%
cl /std:c++20 /Zc:strictStrings- /Fd%OUT_DIR%/ /O2 /MD /EHsc /utf-8 %INCLUDES% /D UNICODE /D _UNICODE %SOURCES% /Fe%OUT_DIR%/%OUT_EXE%.exe /Fo%OUT_DIR%/ /link %LIBS%
