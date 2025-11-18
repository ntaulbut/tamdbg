// Dear ImGui: standalone example application for DirectX 11

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <d3d11.h>
#include <tchar.h>
#include <dwmapi.h>
#include <cmath>
#include <string>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>
#include <codecvt>
#include <ranges>
#include <locale>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comdlg32.lib")
namespace fs = std::filesystem;

#include "tam/tam.h"
#include "tam/error.h"

// Data
static ID3D11Device*           g_pd3dDevice           = nullptr;
static ID3D11DeviceContext*    g_pd3dDeviceContext    = nullptr;
static IDXGISwapChain*         g_pSwapChain           = nullptr;
static bool                    g_SwapChainOccluded    = false;
static UINT                    g_ResizeWidth          = 0;
static UINT                    g_ResizeHeight         = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
std::wstring SelectFileDialog(HWND);

BOOL SetDarkModeTitleBar(HWND hwnd, BOOL enable);

// App globals =========================

bool g_Resizing = false;
std::string g_current_filename = "";
bool* breakpoints = NULL;

const char *stopped_due_to_exception = "";
std::vector<std::string> exceptions;

tam::TamEmulator emulator;
std::array<tam::TamAddr, 16> g_prev_registers = emulator.registers;

bool pref_highlight_changed_registers = true;
bool pref_suspend_on_error            = true;

HANDLE mutex_emulator = CreateMutexA(NULL, false, "MutexEmulator");
bool   run_emulator = false;

DWORD WINAPI thread_func_run_emulator(LPVOID param);
HANDLE thread_run_emulator = CreateThread(NULL, 0, thread_func_run_emulator, NULL, 0, NULL);
HANDLE sem_kick_the_emulator;

// =====================================

std::string wstring_to_utf8(const std::wstring& wstr)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    return conv.to_bytes(wstr);
}

std::wstring SelectFileDialog(HWND owner = NULL)
{
    std::wstring filePath;

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr))
        return filePath;

    IFileOpenDialog* pFileDialog = nullptr;

    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&pFileDialog));

    if (SUCCEEDED(hr)) {
        // Optional: set file filters
        COMDLG_FILTERSPEC filterSpecs[] = {
            { L"TAM Binary (*.tam-binary)", L"*.tam-binary" },
            { L"All Files (*.*)", L"*.*" }
        };
        pFileDialog->SetFileTypes(ARRAYSIZE(filterSpecs), filterSpecs);
        pFileDialog->SetFileTypeIndex(1);

        DWORD dwOptions = 0;
        if (SUCCEEDED(pFileDialog->GetOptions(&dwOptions))) {
            pFileDialog->SetOptions(dwOptions | FOS_FORCEFILESYSTEM);
        }

        // Show dialog
        hr = pFileDialog->Show(owner);
        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            hr = pFileDialog->GetResult(&pItem);
            if (SUCCEEDED(hr)) {
                PWSTR pszPath = nullptr;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
                if (SUCCEEDED(hr)) {
                    filePath = pszPath;
                    CoTaskMemFree(pszPath);
                }
                pItem->Release();
            }
        }
        pFileDialog->Release();
    }

    CoUninitialize();
    return filePath;
}

void StartSession(std::string filename)
{
    try {
        size_t old_size    = emulator.program.size();
        g_current_filename = filename;

        DWORD wait = WaitForSingleObject(mutex_emulator, INFINITE);
        assert(wait == WAIT_OBJECT_0);

        emulator = tam::TamEmulator();
        emulator.LoadProgramFromFile(filename);

        assert(ReleaseMutex(mutex_emulator));

        size_t new_size = emulator.program.size();
        breakpoints = (bool *)realloc(breakpoints, new_size * sizeof(bool));
        if (new_size > old_size)
            memset(&breakpoints[old_size], false, (new_size - old_size) * sizeof(bool));

        exceptions.clear();

        g_prev_registers = emulator.registers;

    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl; // @ToDo tell the user this
    }
}

void RestartSession()
{
    DWORD wait = WaitForSingleObject(mutex_emulator, INFINITE);
    assert(wait == WAIT_OBJECT_0);

    emulator.Reset();

    exceptions.clear();
    stopped_due_to_exception = "";

    assert(ReleaseMutex(mutex_emulator));

    g_prev_registers = emulator.registers;
}

void Step()
{
    try {
        const tam::TamInstruction Instr = emulator.FetchDecode();
        emulator.Execute(Instr);
        stopped_due_to_exception = "";
    } catch (const std::exception& e) {
        stopped_due_to_exception = strdup(e.what());

        if (pref_suspend_on_error)
            run_emulator = false;

        exceptions.push_back(e.what());

        std::cerr << e.what() << std::endl;
    }
}

static void HelpMarker(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void RenderFrame()
{
    /* Handle window being minimized or screen locked
    if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
        ::Sleep(10);
        continue;
    }
    g_SwapChainOccluded = false;*/

    // Handle window resize (we don't resize directly in the WM_SIZE handler)
    if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
        CleanupRenderTarget();
        g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
        g_ResizeWidth = g_ResizeHeight = 0;
        CreateRenderTarget();
    }

    // Start the Dear ImGui frame
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::DockSpaceOverViewport();

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open...")) {
                std::wstring filename = SelectFileDialog();
                if (!filename.empty()) {
                    //MessageBoxW(NULL, filename.c_str(), L"Selected Folder", MB_OK | MB_ICONINFORMATION);

                    StartSession(wstring_to_utf8(filename));

                    //std::string root_path;
                    //int result = WideCharToMultiByte(CP_ACP, 0, filename.c_str(), -1, root_path, sizeof(root_path), NULL, NULL);
                } else {
                    //MessageBoxW(NULL, L"No folder selected or dialog canceled.", L"Info", MB_OK | MB_ICONEXCLAMATION);
                }
            }
            if (ImGui::MenuItem("Reload")) {
                StartSession(g_current_filename);
            }
            // ShowExampleMenuFile();
            //if (ImGui::MenuItem("Undo", "CTRL+Z")) {}
            //if (ImGui::MenuItem("Redo", "CTRL+Y", false, false)) {} // Disabled item
            //ImGui::Separator();
            //if (ImGui::MenuItem("Cut", "CTRL+X")) {}
            //if (ImGui::MenuItem("Copy", "CTRL+C")) {}
            //if (ImGui::MenuItem("Paste", "CTRL+V")) {}
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Control")) {
            if (ImGui::MenuItem("Reset")) {
                RestartSession();
            }
            //ImGui::ColorEdit4("Match Colour", (float*)&colour_match_text);
            //ImGui::ColorEdit4("Line Colour", (float*)&colour_line_text);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Preferences")) {
            ImGui::Checkbox("Highlight Changed Registers", &pref_highlight_changed_registers);
            ImGui::Checkbox("Suspend on Error", &pref_suspend_on_error);
            ImGui::EndMenu();
        }

        //
        // Status Bar
        //

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        ImVec2 screen_pos = ImGui::GetCursorScreenPos();

        ImVec2 menu_bar_end = ImGui::GetWindowPos();
        menu_bar_end.x     += ImGui::GetWindowWidth();
        menu_bar_end.y     += ImGui::GetWindowHeight();

        ImU32 colour_running   = IM_COL32(202,  81,  0, 255);
        ImU32 colour_suspended = IM_COL32(202, 131,  0, 255);
        ImU32 colour_success   = IM_COL32( 40, 202,  0, 255);
        ImU32 colour_error     = IM_COL32(137,  11, 11, 255);

        if (emulator.halted) {
            draw_list->AddRectFilled(screen_pos, menu_bar_end, colour_success);
            ImGui::Text("   Program halted");
        } else if (run_emulator) {
            draw_list->AddRectFilled(screen_pos, menu_bar_end, colour_running);
            ImGui::Text("   Executing");
        } else if (std::strlen(stopped_due_to_exception) > 0) {
            draw_list->AddRectFilled(screen_pos, menu_bar_end, colour_error);
            ImGui::Text("   Suspended: %s", stopped_due_to_exception);
        } else if (g_current_filename != "") {
            draw_list->AddRectFilled(screen_pos, menu_bar_end, colour_suspended);
            ImGui::Text("   Suspended");
        }

        //
        // End Status Bar
        //

        ImGui::EndMainMenuBar();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("Disassembly");
    ImGui::PopStyleVar();

    ImGuiTableFlags flags = ImGuiTableFlags_Borders   |
                            ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_Hideable  |
                            ImGuiTableFlags_ScrollY   |
                            ImGuiTableFlags_ScrollX   |
                            ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable("DisassemblyTable", 2, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);

        ImGui::TableSetupColumn("LOC");
        ImGui::TableSetupColumn("Instruction");

        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(emulator.mnemonics.size());

        int hovered_row = ImGui::TableGetHoveredRow();
        static int select_hovered_row = -1;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hovered_row != -1) {
            ImGui::OpenPopup("my_toggle_popup");
        }

        if (ImGui::BeginPopup("my_toggle_popup")) {
            ImGui::MenuItem("Breakpoint", "", &breakpoints[select_hovered_row]);
            if (ImGui::MenuItem("Set Execution Point")) {
                emulator.registers[tam::CP] = select_hovered_row;
                emulator.halted = false;
            }
            ImGui::EndPopup();
        } else {
            select_hovered_row = hovered_row - 1;
        }

        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                std::string mnemonic = emulator.mnemonics[row];
                ImGui::TableNextRow();

                ImU32 breakpoint_colour = IM_COL32(170,  51,  79, 255);
                ImU32 step_colour       = IM_COL32( 41, 105, 173, 255);
                ImU32 hovered_colour    = IM_COL32( 29,  29,  29, 255);

                bool hovered = row == select_hovered_row;
                if (hovered)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, hovered_colour);

                if (breakpoints[row])
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, breakpoint_colour);

                bool program_counter = row == emulator.registers[tam::CP];
                if (program_counter)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, step_colour);

                ImGui::TableNextColumn();
                    ImGui::Text("%d", row);
                ImGui::TableNextColumn();
                    ImGui::Text("%s", mnemonic.c_str());
                //ImGui::Selectable(a.c_str(), &matched_lines[row].selected, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, 10));
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();

    ImGuiWindowFlags output_window_flags = 0;
    char *text = (char *)emulator.output.c_str(); // We can cast away the constness because the box is ReadOnly
    bool has_output = std::strlen(text) > 0;
    if (has_output)
        output_window_flags |= ImGuiWindowFlags_UnsavedDocument;

    ImGui::Begin("Output", NULL, output_window_flags);

    // static char str0[128] = "Hello, world!";
    // ImGui::InputText("Input", str0, IM_ARRAYSIZE(str0));

    static ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_ReadOnly;
    ImGui::InputTextMultiline("##output", text, IM_ARRAYSIZE(text), ImVec2(-FLT_MIN, -FLT_MIN), input_text_flags);

    ImGui::End();

    ImGuiWindowFlags errors_window_flags = 0;
    bool has_errors = !exceptions.empty();
    if (has_errors)
        errors_window_flags |= ImGuiWindowFlags_UnsavedDocument;

    ImGui::Begin("Errors", NULL, errors_window_flags);
    for (std::string error : exceptions)
        ImGui::Text("%s", error.c_str());
    ImGui::End();

    ImGui::Begin("Stack");
    bool instructions_left = emulator.registers[tam::CP] < emulator.registers[tam::CT];
    ImGui::BeginDisabled(run_emulator);
    if (ImGui::Button("Run")) {
        if (emulator.halted || !instructions_left)
            RestartSession();
        ReleaseSemaphore(sem_kick_the_emulator, 1, NULL);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!run_emulator);
    if (ImGui::Button("Stop")) {
        run_emulator = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(emulator.halted || !instructions_left || run_emulator);
    if (ImGui::Button("Step")) {
        g_prev_registers = emulator.registers;
        Step();
    }
    ImGui::EndDisabled();

    for (int I = 0; I < emulator.registers[tam::ST]; ++I) {
        ImGui::Text("%u", emulator.data_store[I]);
    }

    ImGui::End();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("Registers");
    ImGui::PopStyleVar();

    static const char* TamRegisterNames[] = {
        "Code Base",
        "Code Top",
        "Primitives Base",
        "Primitives Top",
        "Stack Base",
        "Stack Top",
        "Heap Base",
        "Heap Top",
        "Local Base",
        "Local Base 1",
        "Local Base 2",
        "Local Base 3",
        "Local Base 4",
        "Local Base 5",
        "Local Base 6",
        "Code Pointer"
    };

    ImGuiTableFlags registers_table_flags = ImGuiTableFlags_Borders |
                                            ImGuiTableFlags_RowBg   |
                                            ImGuiTableFlags_ScrollY |
                                            ImGuiTableFlags_ScrollX |
                                            ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable("RegistersTable", 2, registers_table_flags)) {
        ImGui::TableSetupColumn("Register");
        ImGui::TableSetupColumn("Value");

        ImGui::TableHeadersRow();

        for (int r = 0; r <= 15; r++) {
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text("%s", TamRegisterNames[r]);

            ImGui::TableNextColumn();
            if (g_prev_registers[r] != emulator.registers[r] && pref_highlight_changed_registers)
                ImGui::TextColored(ImVec4(0.90f, 0.30f, 0.35f, 1.0f), "%u", emulator.registers[r]);
            else
                ImGui::Text("%u", emulator.registers[r]);
        }

        ImGui::EndTable();
    }

    ImGui::End();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("Heap");
    ImGui::PopStyleVar();

    ImGuiTableFlags heap_table_flags = ImGuiTableFlags_Borders |
                                       ImGuiTableFlags_ScrollY |
                                       ImGuiTableFlags_ScrollX |
                                       ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable("Heap", 2, heap_table_flags)) {
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn("Value");

        ImGui::TableHeadersRow();

        int block_num = 1;
        for (auto Block : std::views::reverse(emulator.allocated_blocks)) {
            for (int I = Block.second - 1; I >= 0; I--) {
                ImGui::TableNextRow();

                ImU32 even_block_colour = IM_COL32(29, 29, 29, 255);
                bool  even_block        = block_num % 2 == 0;
                if (even_block)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, even_block_colour);

                ImGui::TableNextColumn();
                ImGui::Text("%s", std::format("{:04X}", Block.first + I));

                ImGui::TableNextColumn();
                ImGui::Text("%d", emulator.data_store[Block.first + I]);
            }
            block_num++;
        }
        ImGui::EndTable();
    }

    ImGui::End();

    ImGui::Render();
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    const float clear_color_with_alpha[4] = {
        clear_color.x * clear_color.w,
        clear_color.y * clear_color.w,
        clear_color.z * clear_color.w,
        clear_color.w
    };
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // Update and Render additional Platform Windows
    /*if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }*/

    // Present
    bool vsync = true;
    HRESULT hr = g_pSwapChain->Present(vsync, 0);

    g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
}

// Main code
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    sem_kick_the_emulator = CreateSemaphore(NULL, 0, 1, NULL);

    // Create application window
    //ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"TAMdbg", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"TAMdbg", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking

    // Setup Dear ImGui style
    ImGui::StyleColorsDark(); //ImGui::StyleColorsLight();
    SetDarkModeTitleBar(hwnd, true);

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);

    // Main loop
    bool window_should_close = false;

    RenderFrame();

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    while (!window_should_close) {
        // Poll and handle messages (inputs, window resize, etc.)
        // See the WndProc() function below for our to dispatch events to the Win32 backend.
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                window_should_close = true;
        }
        if (window_should_close)
            break;

        // Rendering
        RenderFrame();
    }

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

DWORD WINAPI thread_func_run_emulator(LPVOID param)
{
    while (true) {
        DWORD wait = WaitForSingleObject(mutex_emulator, INFINITE);
        assert(wait == WAIT_OBJECT_0);

        if (emulator.halted)
            run_emulator = false;

        bool instructions_left = emulator.registers[tam::CP] < emulator.registers[tam::CT];
        if (instructions_left) {
            bool breakpoint = breakpoints[emulator.registers[tam::CP]];
            if (breakpoint)
                run_emulator = false;
        }

        assert(ReleaseMutex(mutex_emulator));

        if (run_emulator) {
            Step();
        } else {
            WaitForSingleObject(sem_kick_the_emulator, INFINITE);
            run_emulator = true;
            Step(); // Do this so we don't get hung up on breakpoints forever
        }
    }

    return 0;
}

// Helper functions
bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount       = 2;
    sd.BufferDesc.Width  = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags        = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count   = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed   = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pd3dDeviceContext) {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0 // From Windows SDK 8.1+ headers
#endif

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();  // destroy old RTs
            g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();   // recreate RTs
        }

        RenderFrame();
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    case WM_DPICHANGED:
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DpiEnableScaleViewports) {
            //const int dpi = HIWORD(wParam);
            //printf("WM_DPICHANGED to %d (%.0f%%)\n", dpi, (float)dpi / 96.0f * 100.0f);
            const RECT* suggested_rect = (RECT*)lParam;
            ::SetWindowPos(hWnd, nullptr, suggested_rect->left, suggested_rect->top, suggested_rect->right - suggested_rect->left, suggested_rect->bottom - suggested_rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        break;
    case WM_ENTERSIZEMOVE:
        g_Resizing = true;
        return 0;
    case WM_EXITSIZEMOVE:
        g_Resizing = false;
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

BOOL SetDarkModeTitleBar(HWND hwnd, BOOL enable)
{
    BOOL dark = enable;
    return SUCCEEDED(DwmSetWindowAttribute(
        hwnd,
        20,  // DWMWA_USE_IMMERSIVE_DARK_MODE (value 20 or 19 depending on OS version)
        &dark,
        sizeof(dark)
    ));
}
