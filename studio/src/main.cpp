#include <windows.h>
#include <commdlg.h>
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct BlockView {
    int type{};
    RECT rect{};
};

const wchar_t* kBlockNames[] = {
    L"When game starts",
    L"Spawn entity",
    L"Move entity",
    L"Set health",
    L"Give resource",
    L"Show message",
    L"Wait",
    L"Repeat"
};

COLORREF kBlockColors[] = {
    RGB(220, 170, 40),
    RGB(75, 160, 210),
    RGB(70, 175, 145),
    RGB(205, 85, 85),
    RGB(150, 105, 200),
    RGB(90, 145, 220),
    RGB(220, 125, 55),
    RGB(220, 125, 55)
};

constexpr int kPaletteWidth = 230;
constexpr int kInspectorWidth = 260;
constexpr int kToolbarHeight = 48;
constexpr int kBlockW = 190;
constexpr int kBlockH = 42;

std::vector<BlockView> gBlocks;
int gSelected = -1;
int gDragging = -1;
POINT gDragOffset{};
bool gDirty = false;

RECT paletteBlockRect(int index) {
    int y = kToolbarHeight + 22 + index * 54;
    return {18, y, kPaletteWidth - 18, y + kBlockH};
}

void fillRect(HDC dc, const RECT& r, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

void drawRoundedBlock(HDC dc, const RECT& r, COLORREF color, const wchar_t* label, bool selected) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, selected ? 3 : 1, selected ? RGB(255,255,255) : RGB(45,45,48));
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, 12, 12);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(245,245,245));
    RECT text = r;
    text.left += 12;
    DrawTextW(dc, label, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
}

void drawToolbar(HDC dc, int width) {
    RECT r{0,0,width,kToolbarHeight};
    fillRect(dc, r, RGB(32,33,36));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(245,245,245));
    RECT title{16,0,340,kToolbarHeight};
    DrawTextW(dc, L"CnCGZH Studio  •  Visual RTS Builder", -1, &title, DT_SINGLELINE | DT_VCENTER);

    RECT save{width-210,8,width-120,40};
    RECT clear{width-112,8,width-20,40};
    drawRoundedBlock(dc, save, RGB(56,118,178), L"Save", false);
    drawRoundedBlock(dc, clear, RGB(120,72,72), L"Clear", false);
}

void saveProject(HWND hwnd) {
    wchar_t file[MAX_PATH] = L"main.blocks.txt";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"CnCGZH Blocks (*.blocks.txt)\0*.blocks.txt\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"txt";
    if (!GetSaveFileNameW(&ofn)) return;

    std::ofstream out(file, std::ios::binary);
    out << "CNCGZH_BLOCKS_V1\n";
    for (const auto& block : gBlocks) {
        out << block.type << ' ' << block.rect.left << ' ' << block.rect.top << '\n';
    }
    gDirty = false;
}

void clearProject(HWND hwnd) {
    if (!gBlocks.empty()) {
        int answer = MessageBoxW(hwnd, L"Clear every block from the canvas?", L"CnCGZH Studio", MB_YESNO | MB_ICONQUESTION);
        if (answer != IDYES) return;
    }
    gBlocks.clear();
    gSelected = -1;
    gDragging = -1;
    gDirty = true;
    InvalidateRect(hwnd, nullptr, FALSE);
}

int hitBlock(POINT p) {
    for (int i = static_cast<int>(gBlocks.size()) - 1; i >= 0; --i) {
        if (PtInRect(&gBlocks[i].rect, p)) return i;
    }
    return -1;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            gBlocks.push_back({0, {300, 100, 300 + kBlockW, 100 + kBlockH}});
            return 0;

        case WM_LBUTTONDOWN: {
            RECT client{};
            GetClientRect(hwnd, &client);
            POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

            RECT save{client.right-210,8,client.right-120,40};
            RECT clear{client.right-112,8,client.right-20,40};
            if (PtInRect(&save, p)) { saveProject(hwnd); return 0; }
            if (PtInRect(&clear, p)) { clearProject(hwnd); return 0; }

            for (int i = 0; i < static_cast<int>(std::size(kBlockNames)); ++i) {
                RECT pr = paletteBlockRect(i);
                if (PtInRect(&pr, p)) {
                    RECT placed{kPaletteWidth + 45, p.y - kBlockH/2, kPaletteWidth + 45 + kBlockW, p.y + kBlockH/2};
                    gBlocks.push_back({i, placed});
                    gSelected = static_cast<int>(gBlocks.size()) - 1;
                    gDragging = gSelected;
                    gDragOffset = {p.x - placed.left, p.y - placed.top};
                    SetCapture(hwnd);
                    gDirty = true;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            int hit = hitBlock(p);
            gSelected = hit;
            if (hit >= 0) {
                gDragging = hit;
                gDragOffset = {p.x - gBlocks[hit].rect.left, p.y - gBlocks[hit].rect.top};
                SetCapture(hwnd);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_MOUSEMOVE:
            if (gDragging >= 0 && (wParam & MK_LBUTTON)) {
                RECT client{};
                GetClientRect(hwnd, &client);
                int x = std::max(kPaletteWidth + 12, GET_X_LPARAM(lParam) - gDragOffset.x);
                int y = std::max(kToolbarHeight + 8, GET_Y_LPARAM(lParam) - gDragOffset.y);
                x = std::min(x, client.right - kInspectorWidth - kBlockW - 12);
                y = std::min(y, client.bottom - kBlockH - 8);
                gBlocks[gDragging].rect = {x,y,x+kBlockW,y+kBlockH};
                gDirty = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_LBUTTONUP:
            if (gDragging >= 0) {
                gDragging = -1;
                ReleaseCapture();
            }
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_DELETE && gSelected >= 0) {
                gBlocks.erase(gBlocks.begin() + gSelected);
                gSelected = -1;
                gDirty = true;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'S') {
                saveProject(hwnd);
                return 0;
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);

            fillRect(dc, client, RGB(24,25,28));
            drawToolbar(dc, client.right);

            RECT palette{0,kToolbarHeight,kPaletteWidth,client.bottom};
            fillRect(dc, palette, RGB(39,40,44));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(190,190,195));
            RECT paletteTitle{18,kToolbarHeight+4,kPaletteWidth-10,kToolbarHeight+24};
            DrawTextW(dc, L"BLOCKS", -1, &paletteTitle, DT_SINGLELINE | DT_VCENTER);
            for (int i = 0; i < static_cast<int>(std::size(kBlockNames)); ++i) {
                drawRoundedBlock(dc, paletteBlockRect(i), kBlockColors[i], kBlockNames[i], false);
            }

            RECT inspector{client.right-kInspectorWidth,kToolbarHeight,client.right,client.bottom};
            fillRect(dc, inspector, RGB(35,36,40));
            RECT inspectorTitle{inspector.left+18,kToolbarHeight+14,inspector.right-12,kToolbarHeight+38};
            SetTextColor(dc, RGB(220,220,225));
            DrawTextW(dc, L"INSPECTOR", -1, &inspectorTitle, DT_SINGLELINE);

            if (gSelected >= 0 && gSelected < static_cast<int>(gBlocks.size())) {
                const auto& b = gBlocks[gSelected];
                RECT info{inspector.left+18,kToolbarHeight+56,inspector.right-16,client.bottom-20};
                std::wstring text = L"Block\n" + std::wstring(kBlockNames[b.type]) +
                    L"\n\nPosition\nX: " + std::to_wstring(b.rect.left) +
                    L"\nY: " + std::to_wstring(b.rect.top) +
                    L"\n\nDelete: Del\nSave: Ctrl+S";
                DrawTextW(dc, text.c_str(), -1, &info, DT_LEFT | DT_WORDBREAK);
            } else {
                RECT info{inspector.left+18,kToolbarHeight+56,inspector.right-16,kToolbarHeight+150};
                SetTextColor(dc, RGB(150,150,155));
                DrawTextW(dc, L"Select a block to inspect it.\nDrag blocks from the palette onto the canvas.", -1, &info, DT_LEFT | DT_WORDBREAK);
            }

            RECT canvas{kPaletteWidth,kToolbarHeight,client.right-kInspectorWidth,client.bottom};
            HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(34,35,39));
            HGDIOBJ oldPen = SelectObject(dc, gridPen);
            for (int x = canvas.left; x < canvas.right; x += 24) {
                MoveToEx(dc, x, canvas.top, nullptr); LineTo(dc, x, canvas.bottom);
            }
            for (int y = canvas.top; y < canvas.bottom; y += 24) {
                MoveToEx(dc, canvas.left, y, nullptr); LineTo(dc, canvas.right, y);
            }
            SelectObject(dc, oldPen);
            DeleteObject(gridPen);

            for (int i = 0; i < static_cast<int>(gBlocks.size()); ++i) {
                const auto& b = gBlocks[i];
                drawRoundedBlock(dc, b.rect, kBlockColors[b.type], kBlockNames[b.type], i == gSelected);
                if (i + 1 < static_cast<int>(gBlocks.size())) {
                    POINT a{(b.rect.left+b.rect.right)/2, b.rect.bottom};
                    POINT c{(gBlocks[i+1].rect.left+gBlocks[i+1].rect.right)/2, gBlocks[i+1].rect.top};
                    HPEN link = CreatePen(PS_SOLID, 2, RGB(120,125,135));
                    HGDIOBJ prev = SelectObject(dc, link);
                    MoveToEx(dc, a.x, a.y, nullptr);
                    LineTo(dc, c.x, c.y);
                    SelectObject(dc, prev);
                    DeleteObject(link);
                }
            }

            if (gDirty) {
                SetTextColor(dc, RGB(225,180,80));
                RECT dirty{360,0,470,kToolbarHeight};
                DrawTextW(dc, L"modified", -1, &dirty, DT_SINGLELINE | DT_VCENTER);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CLOSE:
            if (gDirty) {
                int answer = MessageBoxW(hwnd, L"There are unsaved changes. Close anyway?", L"CnCGZH Studio", MB_YESNO | MB_ICONWARNING);
                if (answer != IDYES) return 0;
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    const wchar_t* className = L"CnCGZHStudioWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, className, L"CnCGZH Studio", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760, nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 2;

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
