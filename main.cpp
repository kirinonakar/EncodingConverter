#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <cwctype>
#include <objbase.h>
#include <commctrl.h>
#include <regex>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

const wchar_t* CLASS_NAME = L"EncodingConverterPanel";
const int BASE_WIDTH = 340;
const int BASE_HEIGHT = 180;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void ProcessPath(const std::wstring& path, HWND hwnd);
void ConvertFile(const fs::path& filePath);
UINT DetectEncoding(const std::vector<char>& buffer);

int GetDpiForWindowCompat(HWND hwnd) {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        if (hwnd) {
            typedef UINT(WINAPI* GetDpiForWindowProc)(HWND);
            auto pGetDpiForWindow = (GetDpiForWindowProc)GetProcAddress(hUser32, "GetDpiForWindow");
            if (pGetDpiForWindow) return (int)pGetDpiForWindow(hwnd);
        } else {
            typedef UINT(WINAPI* GetDpiForSystemProc)();
            auto pGetDpiForSystem = (GetDpiForSystemProc)GetProcAddress(hUser32, "GetDpiForSystem");
            if (pGetDpiForSystem) return (int)pGetDpiForSystem();
        }
    }
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    return dpi ? dpi : 96;
}

inline int Scale(int value, int dpi) {
    return MulDiv(value, dpi, 96);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL(WINAPI* SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
    if (hUser32) {
        auto pSetProcessDpiAwarenessContext = (SetProcessDpiAwarenessContextProc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetProcessDpiAwarenessContext) {
            pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            SetProcessDPIAware();
        }
    }

    CoInitialize(NULL);

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = CreateSolidBrush(RGB(30, 30, 30));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));

    if (!RegisterClassW(&wc)) return 0;

    int dpi = GetDpiForWindowCompat(NULL);
    int window_width = Scale(BASE_WIDTH, dpi);
    int window_height = Scale(BASE_HEIGHT, dpi);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_ACCEPTFILES | WS_EX_LAYERED,
        CLASS_NAME,
        L"Encoding Converter",
        WS_POPUP,
        (GetSystemMetrics(SM_CXSCREEN) - window_width) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - window_height) / 2,
        window_width,
        window_height,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (hwnd == NULL) return 0;

    SetLayeredWindowAttributes(hwnd, 0, 245, LWA_ALPHA);

    int btnSize = Scale(32, dpi);
    int margin = Scale(5, dpi);
    CreateWindowW(L"BUTTON", L"",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        window_width - btnSize - margin, margin, btnSize, btnSize,
        hwnd, (HMENU)1, hInstance, NULL);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

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
    static bool bHovered = false;

    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rect;
        GetClientRect(hwnd, &rect);
        
        HBRUSH hBg = CreateSolidBrush(RGB(30, 30, 30));
        FillRect(hdc, &rect, hBg);
        DeleteObject(hBg);

        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
        HGDIOBJ hOldPen = SelectObject(hdc, (HGDIOBJ)hPen);
        SelectObject(hdc, (HGDIOBJ)GetStockObject(NULL_BRUSH));
        Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);

        SetBkMode(hdc, TRANSPARENT);
        int dpi = GetDpiForWindowCompat(hwnd);
        
        HFONT hFontTitle = CreateFontW(Scale(24, dpi), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Malgun Gothic");
        HFONT hFontDesc = CreateFontW(Scale(16, dpi), 0, 0, 0, FW_LIGHT, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Malgun Gothic");

        SetTextColor(hdc, RGB(255, 255, 255));
        HGDIOBJ hOldFont = SelectObject(hdc, (HGDIOBJ)hFontTitle);
        RECT rcTitle = rect;
        rcTitle.top = Scale(35, dpi);
        DrawTextW(hdc, L"인코딩 변환기", -1, &rcTitle, DT_CENTER | DT_TOP | DT_SINGLELINE);

        SelectObject(hdc, (HGDIOBJ)hFontDesc);
        SetTextColor(hdc, RGB(200, 200, 200));
        RECT rcDesc = rect;
        rcDesc.top = Scale(85, dpi);
        DrawTextW(hdc, L"(txt, html, htm 드래그앤 드롭)\nsjis, euc-kr, cp949, 한글 조합형 -> UTF-8", -1, &rcDesc, DT_CENTER | DT_TOP);

        SelectObject(hdc, hOldFont);
        DeleteObject(hFontTitle);
        DeleteObject(hFontDesc);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;
        if (lpdis->CtlID == 1) {
            HDC hBtnDc = lpdis->hDC;
            RECT btnRect = lpdis->rcItem;
            bool bPressed = (lpdis->itemState & ODS_SELECTED);
            COLORREF bgColor = bPressed ? RGB(150, 50, 50) : (bHovered ? RGB(220, 40, 40) : RGB(30, 30, 30));
            HBRUSH hBrush = CreateSolidBrush(bgColor);
            FillRect(hBtnDc, &btnRect, hBrush);
            DeleteObject(hBrush);
            int dpi = GetDpiForWindowCompat(hwnd);
            HPEN hPen = CreatePen(PS_SOLID, max(1, Scale(1, dpi)), (bHovered || bPressed) ? RGB(255, 255, 255) : RGB(180, 180, 180));
            HGDIOBJ hOldPen = SelectObject(hBtnDc, (HGDIOBJ)hPen);
            int padding = Scale(11, dpi);
            MoveToEx(hBtnDc, btnRect.left + padding, btnRect.top + padding, NULL);
            LineTo(hBtnDc, btnRect.right - padding, btnRect.bottom - padding);
            MoveToEx(hBtnDc, btnRect.right - padding, btnRect.top + padding, NULL);
            LineTo(hBtnDc, btnRect.left + padding, btnRect.bottom - padding);
            SelectObject(hBtnDc, (HGDIOBJ)hOldPen);
            DeleteObject(hPen);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) PostQuitMessage(0);
        break;
    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            HWND hChild = ChildWindowFromPoint(hwnd, pt);
            if (hChild == GetDlgItem(hwnd, 1)) {
                if (!bHovered) {
                    bHovered = true;
                    InvalidateRect(hChild, NULL, FALSE);
                    TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                    TrackMouseEvent(&tme);
                }
            } else if (bHovered) {
                bHovered = false;
                InvalidateRect(GetDlgItem(hwnd, 1), NULL, FALSE);
            }
        }
        break;
    }
    case WM_MOUSELEAVE:
        if (bHovered) {
            bHovered = false;
            InvalidateRect(GetDlgItem(hwnd, 1), NULL, FALSE);
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
                rect.right - rect.left, rect.bottom - rect.top, TRUE);
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
        MessageBoxW(hwnd, L"변환 완료!", L"성공", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    case WM_DPICHANGED: {
        int dpi = HIWORD(wParam);
        RECT* prcNewWindow = (RECT*)lParam;
        SetWindowPos(hwnd, NULL, prcNewWindow->left, prcNewWindow->top,
            prcNewWindow->right - prcNewWindow->left,
            prcNewWindow->bottom - prcNewWindow->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        int window_width = prcNewWindow->right - prcNewWindow->left;
        int btnSize = Scale(32, dpi);
        int margin = Scale(5, dpi);
        SetWindowPos(GetDlgItem(hwnd, 1), NULL, window_width - btnSize - margin, margin, btnSize, btnSize, SWP_NOZORDER | SWP_NOACTIVATE);
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
                if (fs::is_regular_file(entry)) ConvertFile(entry.path());
            }
        } else if (fs::is_regular_file(p)) {
            ConvertFile(p);
        }
    } catch (...) {}
}

bool IsValidUtf8(const std::vector<char>& buffer) {
    int i = 0;
    int len = (int)buffer.size();
    while (i < len) {
        unsigned char b = (unsigned char)buffer[i];
        if (b <= 0x7F) i++;
        else if (b >= 0xC2 && b <= 0xDF) {
            if (i + 1 >= len || (unsigned char)buffer[i + 1] < 0x80 || (unsigned char)buffer[i + 1] > 0xBF) return false;
            i += 2;
        } else if (b >= 0xE0 && b <= 0xEF) {
            if (i + 2 >= len || (unsigned char)buffer[i + 1] < 0x80 || (unsigned char)buffer[i + 1] > 0xBF || (unsigned char)buffer[i + 2] < 0x80 || (unsigned char)buffer[i + 2] > 0xBF) return false;
            i += 3;
        } else if (b >= 0xF0 && b <= 0xF4) {
            if (i + 3 >= len || (unsigned char)buffer[i + 1] < 0x80 || (unsigned char)buffer[i + 1] > 0xBF || (unsigned char)buffer[i + 2] < 0x80 || (unsigned char)buffer[i + 2] > 0xBF || (unsigned char)buffer[i + 3] < 0x80 || (unsigned char)buffer[i + 3] > 0xBF) return false;
            i += 4;
        } else return false;
    }
    return true;
}

UINT GetHtmlCharset(const std::vector<char>& buffer) {
    int len = (int)max((int)buffer.size(), 2048);
    std::string head(buffer.begin(), buffer.begin() + len);
    std::regex re(R"(charset=["']?([a-zA-Z0-9-_]+))", std::regex_constants::icase);
    std::smatch match;
    if (std::regex_search(head, match, re)) {
        std::string charset = match[1].str();
        if (charset == "shift_jis" || charset == "sjis" || charset == "x-sjis") return 932;
        if (charset == "euc-kr" || charset == "cp949") return 949;
        if (charset == "utf-8" || charset == "utf8") return 65001;
    }
    return 0;
}

int GetSjisScore(const std::vector<char>& bytes) {
    int score = 0, i = 0, len = (int)bytes.size();
    while (i < len) {
        unsigned char b = (unsigned char)bytes[i];
        if (b < 0x80) { i++; continue; }
        if (b >= 0xA1 && b <= 0xDF) {
            if (i + 1 < len && (unsigned char)bytes[i + 1] < 0x80) score += 1;
            i++; continue;
        }
        if (i + 1 >= len) break;
        unsigned char b2 = (unsigned char)bytes[i + 1];
        if ((b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC)) {
            if ((b2 >= 0x40 && b2 <= 0x7E) || (b2 >= 0x80 && b2 <= 0xFC)) {
                if (b == 0x82 || b == 0x83) score += 5; else score += 1;
                i += 2; continue;
            }
        }
        i++;
    }
    return score;
}

int GetEucKrScore(const std::vector<char>& bytes) {
    int score = 0, i = 0, len = (int)bytes.size();
    while (i < len) {
        unsigned char b1 = (unsigned char)bytes[i];
        if (b1 < 0x80) { i++; continue; }
        if (i + 1 >= len) break;
        unsigned char b2 = (unsigned char)bytes[i + 1];
        if (b1 >= 0xB0 && b1 <= 0xC8 && b2 >= 0xA1 && b2 <= 0xFE) { score += 2; i += 2; continue; }
        i++;
    }
    return score;
}

int GetJohabScore(const std::vector<char>& bytes) {
    int score = 0, i = 0, len = (int)bytes.size();
    while (i < len) {
        unsigned char b = (unsigned char)bytes[i];
        if (b < 0x80) { i++; continue; }
        if (i + 1 >= len) break;
        unsigned char b2 = (unsigned char)bytes[i + 1];
        if (b >= 0x84 && b <= 0xD3) {
            if ((b2 >= 0x5B && b2 <= 0x60) || (b2 >= 0x7B && b2 <= 0x7E)) { score += 3; i += 2; continue; }
            if ((b2 >= 0x41 && b2 <= 0x7E) || (b2 >= 0x81 && b2 <= 0xFE)) { score += 1; i += 2; continue; }
        }
        i++;
    }
    return score;
}

UINT DetectEncoding(const std::vector<char>& buffer) {
    if (buffer.size() >= 3 && (unsigned char)buffer[0] == 0xEF && (unsigned char)buffer[1] == 0xBB && (unsigned char)buffer[2] == 0xBF) return 65001;
    if (buffer.size() >= 2 && (unsigned char)buffer[0] == 0xFF && (unsigned char)buffer[1] == 0xFE) return 1200;
    if (buffer.size() >= 2 && (unsigned char)buffer[0] == 0xFE && (unsigned char)buffer[1] == 0xFF) return 1201;

    if (IsValidUtf8(buffer)) return 65001;

    UINT htmlCP = GetHtmlCharset(buffer);
    if (htmlCP != 0) return htmlCP;

    int eucScore = GetEucKrScore(buffer);
    int sjisScore = GetSjisScore(buffer);
    int johabScore = GetJohabScore(buffer);

    if (sjisScore > eucScore && sjisScore > johabScore && sjisScore > 0) return 932;
    if (eucScore > sjisScore && eucScore > johabScore && eucScore > 0) return 949;
    if (johabScore > sjisScore && johabScore > eucScore && johabScore > 0) return 1361;

    if (eucScore > 0 && eucScore >= sjisScore) return 949;
    if (sjisScore > 0) return 932;
    if (johabScore > 0) return 1361;

    return 949; // Default fallback for Korean environment
}

void ConvertFile(const fs::path& filePath) {
    std::wstring ext = filePath.extension().wstring();
    for (auto& c : ext) c = (wchar_t)towlower(c);
    if (ext != L".txt" && ext != L".html" && ext != L".htm") return;

    std::ifstream file(filePath, std::ios::binary);
    if (!file) return;
    std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    if (buffer.empty()) return;

    UINT srcCP = DetectEncoding(buffer);
    if (srcCP == 65001) return;

    int wlen = MultiByteToWideChar(srcCP, 0, buffer.data(), (int)buffer.size(), NULL, 0);
    if (wlen <= 0) return;
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(srcCP, 0, buffer.data(), (int)buffer.size(), &wstr[0], wlen);

    int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
    if (u8len <= 0) return;
    std::string u8str(u8len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &u8str[0], u8len, NULL, NULL);

    fs::path outPath = filePath;
    std::wstring stem = outPath.stem().wstring();
    std::wstring newExt = outPath.extension().wstring();
    outPath.replace_filename(stem + L"_u" + newExt);
    std::ofstream outFile(outPath, std::ios::binary);
    if (outFile) outFile.write(u8str.data(), u8str.size());
}
