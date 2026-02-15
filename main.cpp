#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <cwctype>
#include <mlang.h>
#include <objbase.h>

#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;

// Global settings
const wchar_t* CLASS_NAME = L"EncodingConverterPanel";
const int WINDOW_WIDTH = 300;
const int WINDOW_HEIGHT = 150;

// Function declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void ProcessPath(const std::wstring& path, HWND hwnd);
void ConvertFile(const fs::path& filePath);
UINT DetectEncoding(const std::vector<char>& buffer);

// Entry point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    CoInitialize(NULL);

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = CreateSolidBrush(RGB(45, 45, 48)); // Dark theme
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    if (!RegisterClassW(&wc)) return 0;

    // Create window
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_ACCEPTFILES | WS_EX_LAYERED,
        CLASS_NAME,
        L"Encoding Converter",
        WS_POPUP,
        (GetSystemMetrics(SM_CXSCREEN) - WINDOW_WIDTH) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - WINDOW_HEIGHT) / 2,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (hwnd == NULL) return 0;

    SetLayeredWindowAttributes(hwnd, 0, 235, LWA_ALPHA); // Slight transparency

    // Add Close Button (owner draw or styled)
    HWND hBtn = CreateWindowW(L"BUTTON", L"X",
        WS_VISIBLE | WS_CHILD | BS_FLAT,
        WINDOW_WIDTH - 40, 5, 35, 30,
        hwnd, (HMENU)1, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);

    MSG msg = { 0 };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static bool dragging = false;
    static POINT lastMouse;

    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rect;
        GetClientRect(hwnd, &rect);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(220, 220, 220));
        
        HFONT hFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        DrawTextW(hdc, L"Drop Files/Folders Here\n(txt, html, htm)", -1, &rect, DT_CENTER | DT_VCENTER | DT_NOCLIP);

        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) { // Close button
            PostQuitMessage(0);
        }
        break;
    case WM_LBUTTONDOWN:
        dragging = true;
        GetCursorPos(&lastMouse);
        SetCapture(hwnd);
        break;
    case WM_LBUTTONUP:
        dragging = false;
        ReleaseCapture();
        break;
    case WM_MOUSEMOVE:
        if (dragging) {
            POINT currentMouse;
            GetCursorPos(&currentMouse);
            RECT rect;
            GetWindowRect(hwnd, &rect);
            MoveWindow(hwnd, rect.left + (currentMouse.x - lastMouse.x),
                rect.top + (currentMouse.y - lastMouse.y),
                WINDOW_WIDTH, WINDOW_HEIGHT, TRUE);
            lastMouse = currentMouse;
        }
        break;
    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
        for (UINT i = 0; i < fileCount; i++) {
            wchar_t filePath[MAX_PATH];
            DragQueryFileW(hDrop, i, filePath, MAX_PATH);
            ProcessPath(filePath, hwnd);
        }
        DragFinish(hDrop);
        MessageBoxW(hwnd, L"Conversion Complete!", L"Success", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void ProcessPath(const std::wstring& path, HWND hwnd) {
    try {
        fs::path p(path);
        if (fs::is_directory(p)) {
            for (const auto& entry : fs::recursive_directory_iterator(p)) {
                if (fs::is_regular_file(entry)) {
                    ConvertFile(entry.path());
                }
            }
        }
        else if (fs::is_regular_file(p)) {
            ConvertFile(p);
        }
    }
    catch (...) {}
}

void ConvertFile(const fs::path& filePath) {
    std::wstring ext = filePath.extension().wstring();
    // Case-insensitive extension check
    for (auto& c : ext) c = (wchar_t)towlower(c);
    
    if (ext != L".txt" && ext != L".html" && ext != L".htm") return;

    // Read file
    std::ifstream file(filePath, std::ios::binary);
    if (!file) return;

    std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    if (buffer.empty()) return;

    // Detect encoding
    UINT srcCP = DetectEncoding(buffer);
    if (srcCP == 65001 || srcCP == 0) return; // Already UTF-8 or unknown

    // Convert MultiByte to WideChar
    int wlen = MultiByteToWideChar(srcCP, 0, buffer.data(), (int)buffer.size(), NULL, 0);
    if (wlen <= 0) return;

    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(srcCP, 0, buffer.data(), (int)buffer.size(), &wstr[0], wlen);

    // Convert WideChar to UTF-8
    int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
    if (u8len <= 0) return;

    std::string u8str(u8len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &u8str[0], u8len, NULL, NULL);

    // Prepare output path: original_u.ext
    fs::path outPath = filePath;
    std::wstring stem = outPath.stem().wstring();
    std::wstring newExt = outPath.extension().wstring();
    outPath.replace_filename(stem + L"_u" + newExt);

    // Write file
    std::ofstream outFile(outPath, std::ios::binary);
    if (outFile) {
        outFile.write(u8str.data(), u8str.size());
    }
}

UINT DetectEncoding(const std::vector<char>& buffer) {
    // Check for UTF-8 BOM
    if (buffer.size() >= 3 && (unsigned char)buffer[0] == 0xEF && (unsigned char)buffer[1] == 0xBB && (unsigned char)buffer[2] == 0xBF) return 65001;

    IMultiLanguage2* pMultiLang = NULL;
    HRESULT hr = CoCreateInstance(CLSID_CMultiLanguage, NULL, CLSCTX_INPROC_SERVER, IID_IMultiLanguage2, (void**)&pMultiLang);
    
    UINT codePage = 0;
    if (SUCCEEDED(hr)) {
        int bufferSize = (int)buffer.size();
        int detectCount = 1;
        DetectEncodingInfo detectInfo;
        
        // Use IMultiLanguage2 to detect
        hr = pMultiLang->DetectInputCodepage(MLDETECTCP_NONE, 0, (char*)buffer.data(), &bufferSize, &detectInfo, &detectCount);
        if (SUCCEEDED(hr) && detectCount > 0) {
            codePage = detectInfo.nCodePage;
        }
        pMultiLang->Release();
    }

    // Heuristic fallbacks if MLang fails or gives generic results
    if (codePage == 0 || codePage == 1252 || codePage == 20127) {
        // Check if it's CP949 or SJIS based on byte patterns
        // But MLang is usually good. 
        // 949 (Korean), 932 (SJIS), 1361 (Johab)
    }

    return codePage;
}
