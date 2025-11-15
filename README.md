# TAMdbg

<img width="1284" height="808" alt="Screenshot 2025-11-15 192054" src="https://github.com/user-attachments/assets/d09d2d2d-2ce4-4191-bfe5-edcd51085f71" />

## Build
Currently only Windows is supported using Win32 API and DirectX11.
- Use the Visual Studio 2022 Developer Command Prompt, 64 bit.
- Run `build_win32_release.bat`.
- To build for debug: `build_win32.bat`. The debug build uses address sanitisation to detect memory errors. You need to link to the appropriate library for this to work: `clang_rt.asan_dynamic-x86_64.dll`.

## Acknowledgements
- Made using [Dear ImGui](https://github.com/ocornut/imgui), MIT licensed
- Incorporates [Ian Knight's TAM Emulator](https://github.com/pszik/triangle-abstract-machine), GPLv3 licensed
