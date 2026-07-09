#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <cwctype>
#include <objbase.h>
#include <commctrl.h>
#include <regex>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")


namespace fs = std::filesystem;

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

const wchar_t* CLASS_NAME = L"EncodingConverterPanel";
const int BASE_WIDTH = 340;
const int BASE_HEIGHT = 220;
static bool g_mergeEnabled = false;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void ProcessPaths(const std::vector<std::wstring>& paths, HWND hwnd);
std::string ConvertFile(const fs::path& filePath, bool saveIndividual);
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
        WS_EX_ACCEPTFILES | WS_EX_LAYERED,
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
        HFONT hFontCheck = CreateFontW(Scale(14, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
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
        DrawTextW(hdc, L"(txt, html, htm 드래그앤 드롭)\nsjis, jis, euc-kr, cp949, 조합형, gbk, gb18030, big5 -> UTF-8", -1, &rcDesc, DT_CENTER | DT_TOP);

        // Draw Checkbox
        SelectObject(hdc, hFontCheck);
        const wchar_t* checkText = L"파일 합치기 (merged.txt)";
        RECT rcCalc = { 0, 0, 0, 0 };
        DrawTextW(hdc, checkText, -1, &rcCalc, DT_CALCRECT | DT_SINGLELINE);
        int actualTextWidth = rcCalc.right - rcCalc.left;
        
        int boxSize = Scale(14, dpi);
        int margin = Scale(10, dpi);
        int totalWidth = boxSize + margin + actualTextWidth;
        int startX = (rect.right - totalWidth) / 2;
        int boxTop = Scale(155, dpi);
        
        RECT boxRect = { startX, boxTop, startX + boxSize, boxTop + boxSize };
        
        HPEN hBoxPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
        HGDIOBJ hOldPen2 = SelectObject(hdc, hBoxPen);
        Rectangle(hdc, boxRect.left, boxRect.top, boxRect.right, boxRect.bottom);
        if (g_mergeEnabled) {
            HPEN hCheckPen = CreatePen(PS_SOLID, 2, RGB(0, 255, 128));
            SelectObject(hdc, hCheckPen);
            MoveToEx(hdc, boxRect.left + Scale(3, dpi), boxRect.top + Scale(5, dpi), NULL);
            LineTo(hdc, boxRect.left + Scale(6, dpi), boxRect.bottom - Scale(3, dpi));
            LineTo(hdc, boxRect.right - Scale(2, dpi), boxTop + Scale(3, dpi));
            DeleteObject(hCheckPen);
        }
        SelectObject(hdc, hOldPen2);
        DeleteObject(hBoxPen);

        RECT rcCheckText = { boxRect.right + margin, boxTop - Scale(1, dpi), boxRect.right + margin + actualTextWidth, boxTop + boxSize + Scale(1, dpi) };
        SetTextColor(hdc, RGB(220, 220, 220));
        DrawTextW(hdc, checkText, -1, &rcCheckText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, hOldFont);
        DeleteObject(hFontTitle);
        DeleteObject(hFontDesc);
        DeleteObject(hFontCheck);

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
    case WM_LBUTTONDOWN: {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        int dpi = GetDpiForWindowCompat(hwnd);

        HDC hdc = GetDC(hwnd);
        HFONT hFontCheck = CreateFontW(Scale(14, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Malgun Gothic");
        HGDIOBJ hOldFont = SelectObject(hdc, hFontCheck);
        const wchar_t* checkText = L"파일 합치기 (merged.txt)";
        RECT rcCalc = { 0, 0, 0, 0 };
        DrawTextW(hdc, checkText, -1, &rcCalc, DT_CALCRECT | DT_SINGLELINE);
        int actualTextWidth = rcCalc.right - rcCalc.left;
        SelectObject(hdc, hOldFont);
        DeleteObject(hFontCheck);
        ReleaseDC(hwnd, hdc);

        int boxSize = Scale(14, dpi);
        int margin = Scale(10, dpi);
        int totalWidth = boxSize + margin + actualTextWidth;
        RECT rect;
        GetClientRect(hwnd, &rect);
        int startX = (rect.right - totalWidth) / 2;
        int boxTop = Scale(155, dpi);
        RECT rcHit = { startX, boxTop, startX + totalWidth, boxTop + boxSize };
        
        if (PtInRect(&rcHit, pt)) {
            g_mergeEnabled = !g_mergeEnabled;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        
        dragging = true;
        GetCursorPos(&lastMouse);
        SetCapture(hwnd);
        break;
    }
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
        std::vector<std::wstring> paths;
        for (UINT i = 0; i < fileCount; i++) {
            wchar_t filePath[MAX_PATH];
            DragQueryFileW(hDrop, i, filePath, MAX_PATH);
            paths.push_back(filePath);
        }
        DragFinish(hDrop);
        ProcessPaths(paths, hwnd);
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

void ProcessPaths(const std::vector<std::wstring>& paths, HWND hwnd) {
    std::vector<fs::path> allFiles;
    for (const auto& path : paths) {
        try {
            fs::path p(path);
            if (fs::is_directory(p)) {
                for (const auto& entry : fs::recursive_directory_iterator(p)) {
                    if (fs::is_regular_file(entry)) allFiles.push_back(entry.path());
                }
            } else if (fs::is_regular_file(p)) {
                allFiles.push_back(p);
            }
        } catch (...) {}
    }

    if (allFiles.empty()) return;

    std::sort(allFiles.begin(), allFiles.end(), [](const fs::path& a, const fs::path& b) {
        return StrCmpLogicalW(a.c_str(), b.c_str()) < 0;
    });


    std::string mergedContent;
    for (const auto& filePath : allFiles) {
        std::string content = ConvertFile(filePath, true);
        if (g_mergeEnabled && !content.empty()) {
            if (!mergedContent.empty()) mergedContent += "\r\n\r\n";
            mergedContent += content;
        }
    }

    if (g_mergeEnabled && !mergedContent.empty()) {
        fs::path outDir = allFiles[0].parent_path();
        std::ofstream outFile(outDir / "merged.txt", std::ios::binary);
        if (outFile) outFile.write(mergedContent.data(), mergedContent.size());
    }
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
        if (charset == "iso-2022-jp" || charset == "jis" || charset == "cp50220" || charset == "cp50221") return 50220;
        if (charset == "euc-kr" || charset == "cp949") return 949;
        if (charset == "gbk" || charset == "gb2312" || charset == "cp936") return 936;
        if (charset == "gb18030" || charset == "cp54936") return 54936;
        if (charset == "big5" || charset == "cp950" || charset == "big5-hkscs") return 950;
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
        
        // EUC-KR Hangul range: b1 in 0xB0-0xC8, b2 in 0xA1-0xFE
        if (b1 >= 0xB0 && b1 <= 0xC8 && b2 >= 0xA1 && b2 <= 0xFE) {
            score += 5;
            i += 2;
            continue;
        }
        
        // Penalty for typical Chinese characters range in EUC-KR (0xC9-0xFD)
        if (b1 >= 0xC9 && b1 <= 0xFD && b2 >= 0xA1 && b2 <= 0xFE) {
            score -= 10;
            i += 2;
            continue;
        }
        
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

int GetGbkScore(const std::vector<char>& bytes) {
    int score = 0, i = 0, len = (int)bytes.size();
    while (i < len) {
        unsigned char b1 = (unsigned char)bytes[i];
        if (b1 < 0x80) { i++; continue; }
        if (i + 1 >= len) break;
        unsigned char b2 = (unsigned char)bytes[i + 1];
        
        // GBK (CP936) double-byte range check:
        // First byte: 0x81 - 0xFE
        // Second byte: 0x40 - 0xFE (excluding 0x7F)
        if (b1 >= 0x81 && b1 <= 0xFE && b2 >= 0x40 && b2 <= 0xFE && b2 != 0x7F) {
            // Highly common GB2312 Level 1 & 2 Hanzi & symbols
            if (b1 >= 0xA1 && b1 <= 0xF7 && b2 >= 0xA1 && b2 <= 0xFE) {
                // If the first byte is in 0xC9-0xF7 (Simplified Chinese specific, not common Korean EUC-KR Hangul range)
                if (b1 >= 0xC9) {
                    score += 5;
                } else {
                    score += 2;
                }
            } else {
                score += 1;
            }
            i += 2;
            continue;
        }
        i++;
    }
    return score;
}

int GetBig5Score(const std::vector<char>& bytes) {
    int score = 0, i = 0, len = (int)bytes.size();
    while (i < len) {
        unsigned char b1 = (unsigned char)bytes[i];
        if (b1 < 0x80) { i++; continue; }
        if (i + 1 >= len) break;
        unsigned char b2 = (unsigned char)bytes[i + 1];
        
        // Big5 (CP950) double-byte range check:
        // First byte: 0xA1 - 0xF9
        // Second byte: 0x40 - 0x7E or 0xA1 - 0xFE
        if (b1 >= 0xA1 && b1 <= 0xF9 && ((b2 >= 0x40 && b2 <= 0x7E) || (b2 >= 0xA1 && b2 <= 0xFE))) {
            // Big5 Level 1 (Common Traditional Chinese characters)
            if (b1 >= 0xA4 && b1 <= 0xC6) {
                if (b2 >= 0x40 && b2 <= 0x7E) {
                    score += 5;
                } else {
                    score += 2;
                }
            } else {
                score += 1;
            }
            i += 2;
            continue;
        }
        i++;
    }
    return score;
}

bool HasJisEscapeSequence(const std::vector<char>& bytes) {
    int len = (int)bytes.size();
    for (int i = 0; i + 2 < len; i++) {
        if ((unsigned char)bytes[i] == 0x1B) {
            unsigned char b1 = (unsigned char)bytes[i + 1];
            unsigned char b2 = (unsigned char)bytes[i + 2];
            if ((b1 == 0x24 && (b2 == 0x40 || b2 == 0x42)) ||
                (b1 == 0x28 && (b2 == 0x42 || b2 == 0x4A || b2 == 0x49))) {
                return true;
            }
        }
    }
    return false;
}

bool HasGb18030FourByteSequence(const std::vector<char>& bytes) {
    int len = (int)bytes.size();
    for (int i = 0; i + 3 < len; i++) {
        unsigned char b1 = (unsigned char)bytes[i];
        unsigned char b2 = (unsigned char)bytes[i + 1];
        unsigned char b3 = (unsigned char)bytes[i + 2];
        unsigned char b4 = (unsigned char)bytes[i + 3];
        if (b1 >= 0x81 && b1 <= 0xFE &&
            b2 >= 0x30 && b2 <= 0x39 &&
            b3 >= 0x81 && b3 <= 0xFE &&
            b4 >= 0x30 && b4 <= 0x39) {
            return true;
        }
    }
    return false;
}

UINT DetectEncoding(const std::vector<char>& buffer) {
    if (buffer.size() >= 3 && (unsigned char)buffer[0] == 0xEF && (unsigned char)buffer[1] == 0xBB && (unsigned char)buffer[2] == 0xBF) return 65001;
    if (buffer.size() >= 2 && (unsigned char)buffer[0] == 0xFF && (unsigned char)buffer[1] == 0xFE) return 1200;
    if (buffer.size() >= 2 && (unsigned char)buffer[0] == 0xFE && (unsigned char)buffer[1] == 0xFF) return 1201;

    // Check for JIS (ISO-2022-JP) escape sequences FIRST because JIS is 7-bit and passes IsValidUtf8
    if (HasJisEscapeSequence(buffer)) return 50220;

    if (IsValidUtf8(buffer)) return 65001;

    UINT htmlCP = GetHtmlCharset(buffer);
    if (htmlCP != 0) return htmlCP;

    int eucScore = GetEucKrScore(buffer);
    int sjisScore = GetSjisScore(buffer);
    int johabScore = GetJohabScore(buffer);
    int gbkScore = GetGbkScore(buffer);
    int big5Score = GetBig5Score(buffer);

    int maxScore = (std::max)({ eucScore, sjisScore, johabScore, gbkScore, big5Score });
    if (maxScore > 0) {
        if (maxScore == eucScore) return 949;
        if (maxScore == sjisScore) return 932;
        if (maxScore == gbkScore) {
            if (HasGb18030FourByteSequence(buffer)) return 54936;
            return 936;
        }
        if (maxScore == big5Score) return 950;
        if (maxScore == johabScore) return 1361;
    }

    return 949; // Default fallback for Korean environment
}

std::string ConvertFile(const fs::path& filePath, bool saveIndividual) {
    std::wstring ext = filePath.extension().wstring();
    for (auto& c : ext) c = (wchar_t)towlower(c);
    if (ext != L".txt" && ext != L".html" && ext != L".htm") return "";

    std::ifstream file(filePath, std::ios::binary);
    if (!file) return "";
    std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    if (buffer.empty()) return "";

    UINT srcCP = DetectEncoding(buffer);
    std::string u8str;

    if (srcCP == 65001) {
        u8str = std::string(buffer.begin(), buffer.end());
    } else {
        int wlen = MultiByteToWideChar(srcCP, 0, buffer.data(), (int)buffer.size(), NULL, 0);
        if (wlen <= 0) return "";
        std::wstring wstr(wlen, 0);
        MultiByteToWideChar(srcCP, 0, buffer.data(), (int)buffer.size(), &wstr[0], wlen);

        int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
        if (u8len <= 0) return "";
        u8str.assign(u8len, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &u8str[0], u8len, NULL, NULL);

        if (saveIndividual) {
            fs::path outPath = filePath;
            std::wstring stem = outPath.stem().wstring();
            std::wstring newExt = outPath.extension().wstring();
            outPath.replace_filename(stem + L"_u" + newExt);
            std::ofstream outFile(outPath, std::ios::binary);
            if (outFile) outFile.write(u8str.data(), u8str.size());
        }
    }
    return u8str;
}
