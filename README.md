# TAMdbg

<img width="1287" height="815" alt="Screenshot 2025-11-16 073946" src="https://github.com/user-attachments/assets/e624619a-c662-4a2d-9cbf-9e2b64f3808d" />

## Build
Currently only Windows is supported using Win32 API and DirectX11.
- Use the Visual Studio 2022 Developer Command Prompt, 64 bit.
- Run `build_win32_release.bat`.
- To build for debug: `build_win32.bat`. The debug build uses address sanitisation to detect memory errors. You need to link to the appropriate library for this to work: `clang_rt.asan_dynamic-x86_64.dll`.

## Contributing
PRs welcome on making the application cross-platform, otherwise use the issues to suggest a feature before working on it.
This is a work-in-progress, code will be messy.

## Acknowledgements
- Made using [Dear ImGui](https://github.com/ocornut/imgui), MIT licensed
- Incorporates [Ian Knight's TAM Emulator](https://github.com/pszik/triangle-abstract-machine), GPLv3 licensed
