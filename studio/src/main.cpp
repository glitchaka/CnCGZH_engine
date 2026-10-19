#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "runtime/SceneFile.h"

namespace fs = std::filesystem;

namespace {

struct AssetEntry {
    fs::path absolute;
    std::string relative;
    std::wstring name;
};

constexpr LONG kToolbarH = 54;
constexpr LONG kAssetsW = 285;
constexpr LONG kInspectorW = 285;
constexpr LONG kAssetRowH = 48;
constexpr LONG kObjectSize = 44;

fs::path gRoot;
fs::path gScenePath;
std::vector<AssetEntry> gAssets;
std::vector<cncgzh::SceneObject> gObjects;
int gActiveAsset = -1;
int gSelected = -1;
int gDragging = -1;
int gAssetScroll = 0;
float gViewX = 0.0f;
float gViewZ = 0.0f;
float gZoom = 44.0f;
bool gDirty = false;
POINT gLastMouse{};

fs::path executablePath() {
    std::wstring buffer(32768, L'\0');
    const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(len);
    return fs::path(buffer);
}

fs::path findProjectRoot() {
    fs::path p = executablePath().parent_path();
    for (int i = 0; i < 8 && !p.empty(); ++i) {
        if (fs::exists(p / "assets") && fs::exists(p / "engine")) return p;
        p = p.parent_path();
    }
    return fs::current_path();
}

std::string pathToUtf8(const fs::path& p) {
#ifdef _WIN32
    const std::wstring w = p.generic_wstring();
    if (w.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), result.data(), size, nullptr, nullptr);
    return result;
#else
    return p.generic_string();
#endif
}

void scanAssets() {
    gAssets.clear();
    const fs::path root = gRoot / "assets" / "sample" / "kaykit-dungeon";
    std::error_code ec;
    if (!fs::exists(root)) return;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        std::wstring ext = it->path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (ext != L".obj") continue;
        fs::path relative = fs::relative(it->path(), gRoot, ec);
        if (ec) { ec.clear(); relative = it->path(); }
        gAssets.push_back({it->path(), pathToUtf8(relative), it->path().stem().wstring()});
    }
    std::sort(gAssets.begin(), gAssets.end(), [](const AssetEntry& a, const AssetEntry& b) { return a.name < b.name; });
    if (gActiveAsset >= static_cast<int>(gAssets.size())) gActiveAsset = -1;
    gAssetScroll = std::clamp(gAssetScroll, 0, std::max(0, static_cast<int>(gAssets.size()) - 1));
}

RECT viewportRect(const RECT& client) {
    return {kAssetsW, kToolbarH, std::max<LONG>(kAssetsW, client.right - kInspectorW), client.bottom};
}

POINT worldToScreen(const RECT& viewport, float x, float z) {
    const float cx = (viewport.left + viewport.right) * 0.5f;
    const float cy = (viewport.top + viewport.bottom) * 0.5f;
    return {static_cast<LONG>(cx + (x - gViewX) * gZoom), static_cast<LONG>(cy + (z - gViewZ) * gZoom)};
}

void screenToWorld(const RECT& viewport, POINT p, float& x, float& z) {
    const float cx = (viewport.left + viewport.right) * 0.5f;
    const float cy = (viewport.top + viewport.bottom) * 0.5f;
    x = gViewX + (p.x - cx) / gZoom;
    z = gViewZ + (p.y - cy) / gZoom;
}

void fill(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void button(HDC dc, RECT r, const wchar_t* text, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(80,82,87));
    auto oldBrush = SelectObject(dc, brush);
    auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, 8, 8);
    SelectObject(dc, oldPen); SelectObject(dc, oldBrush);
    DeleteObject(pen); DeleteObject(brush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(245,245,245));
    DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

RECT importButton() { return {18, 9, 190, 45}; }
RECT saveButton(LONG width) { return {width - 326, 9, width - 226, 45}; }
RECT playButton(LONG width) { return {width - 216, 9, width - 116, 45}; }
RECT clearButton(LONG width) { return {width - 106, 9, width - 14, 45}; }

bool saveCurrentScene(HWND hwnd) {
    if (!cncgzh::saveScene(gScenePath, gObjects)) {
        MessageBoxW(hwnd, L"Could not save the scene.", L"CnCGZH Studio", MB_ICONERROR);
        return false;
    }
    gDirty = false;
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

void playScene(HWND hwnd) {
    if (!saveCurrentScene(hwnd)) return;
    fs::path engine = executablePath().parent_path() / "CnCGZHEngine.exe";
    if (!fs::exists(engine)) {
        MessageBoxW(hwnd, L"CnCGZHEngine.exe is not beside the Studio executable. Build the CnCGZHEngine target too.", L"Play", MB_ICONERROR);
        return;
    }
    std::wstring params = L"--scene \"" + gScenePath.wstring() + L"\"";
    HINSTANCE result = ShellExecuteW(hwnd, L"open", engine.c_str(), params.c_str(), gRoot.c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) MessageBoxW(hwnd, L"Could not start the game runtime.", L"Play", MB_ICONERROR);
}

std::wstring escapePsLiteral(std::wstring value) {
    size_t pos = 0;
    while ((pos = value.find(L'\'', pos)) != std::wstring::npos) { value.insert(pos, 1, L'\''); pos += 2; }
    return value;
}

void importKayKit(HWND hwnd) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"KayKit ZIP (*.zip)\0*.zip\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    const fs::path target = gRoot / "assets" / "sample" / "kaykit-dungeon";
    std::error_code ec;
    fs::create_directories(target, ec);
    const std::wstring zip = escapePsLiteral(fs::path(file).wstring());
    const std::wstring dest = escapePsLiteral(target.wstring());
    std::wstring args = L"-NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath '" + zip + L"' -DestinationPath '" + dest + L"' -Force\"";

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = hwnd;
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) {
        MessageBoxW(hwnd, L"Could not import the ZIP.", L"Import KayKit", MB_ICONERROR);
        return;
    }
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    if (code != 0) {
        MessageBoxW(hwnd, L"PowerShell could not extract the ZIP.", L"Import KayKit", MB_ICONERROR);
        return;
    }
    scanAssets();
    InvalidateRect(hwnd, nullptr, FALSE);
}

void clearScene(HWND hwnd) {
    if (!gObjects.empty() && MessageBoxW(hwnd, L"Clear the whole scene?", L"CnCGZH Studio", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    gObjects.clear();
    gSelected = -1;
    gDragging = -1;
    gDirty = true;
    InvalidateRect(hwnd, nullptr, FALSE);
}

int hitObject(const RECT& viewport, POINT p) {
    for (int i = static_cast<int>(gObjects.size()) - 1; i >= 0; --i) {
        POINT s = worldToScreen(viewport, gObjects[i].x, gObjects[i].z);
        RECT r{s.x-kObjectSize/2, s.y-kObjectSize/2, s.x+kObjectSize/2, s.y+kObjectSize/2};
        if (PtInRect(&r, p)) return i;
    }
    return -1;
}

int assetAt(POINT p, const RECT& client) {
    if (p.x < 0 || p.x >= kAssetsW || p.y < kToolbarH + 54) return -1;
    const int visibleIndex = (p.y - (kToolbarH + 54)) / kAssetRowH;
    const int index = gAssetScroll + visibleIndex;
    const int visibleRows = std::max(0, static_cast<int>((client.bottom - (kToolbarH + 54)) / kAssetRowH));
    if (visibleIndex < 0 || visibleIndex >= visibleRows || index < 0 || index >= static_cast<int>(gAssets.size())) return -1;
    return index;
}

void drawEditor(HWND hwnd, HDC dc) {
    RECT client{}; GetClientRect(hwnd, &client);
    fill(dc, client, RGB(22,23,26));

    RECT toolbar{0,0,client.right,kToolbarH};
    fill(dc, toolbar, RGB(31,32,36));
    button(dc, importButton(), L"Import KayKit ZIP", RGB(69,85,110));
    button(dc, saveButton(client.right), L"Save", RGB(54,113,166));
    button(dc, playButton(client.right), L"PLAY", RGB(48,145,85));
    button(dc, clearButton(client.right), L"Clear", RGB(124,70,70));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(235,235,238));
    RECT title{205,0,620,kToolbarH};
    DrawTextW(dc, L"CnCGZH Studio  |  SCENE", -1, &title, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    if (gDirty) {
        SetTextColor(dc, RGB(224,178,72));
        RECT dirty{500,0,620,kToolbarH};
        DrawTextW(dc, L"modified", -1, &dirty, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    }

    RECT assetsPanel{0,kToolbarH,kAssetsW,client.bottom};
    fill(dc, assetsPanel, RGB(37,38,42));
    SetTextColor(dc, RGB(225,225,228));
    RECT aTitle{16,kToolbarH+12,kAssetsW-10,kToolbarH+36};
    DrawTextW(dc, L"ASSETS", -1, &aTitle, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    SetTextColor(dc, RGB(145,148,154));
    RECT hint{16,kToolbarH+32,kAssetsW-10,kToolbarH+54};
    DrawTextW(dc, L"Select asset, then click scene", -1, &hint, DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    if (gAssets.empty()) {
        SetTextColor(dc, RGB(195,160,90));
        RECT empty{16,kToolbarH+76,kAssetsW-16,kToolbarH+180};
        DrawTextW(dc, L"No OBJ assets found.\nClick 'Import KayKit ZIP' and select KayKit_Dungeon_Pack_1.1_FREE.zip.", -1, &empty, DT_LEFT|DT_WORDBREAK);
    } else {
        int y = kToolbarH + 54;
        for (int i = gAssetScroll; i < static_cast<int>(gAssets.size()) && y + kAssetRowH <= client.bottom; ++i, y += kAssetRowH) {
            RECT row{10,y+3,kAssetsW-10,y+kAssetRowH-3};
            fill(dc, row, i == gActiveAsset ? RGB(65,92,116) : RGB(48,49,54));
            SetTextColor(dc, RGB(235,235,238));
            RECT text{20,y+3,kAssetsW-18,y+kAssetRowH-3};
            DrawTextW(dc, gAssets[i].name.c_str(), -1, &text, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
    }

    const RECT viewport = viewportRect(client);
    fill(dc, viewport, RGB(19,21,23));
    HPEN grid = CreatePen(PS_SOLID, 1, RGB(31,35,37));
    HGDIOBJ oldPen = SelectObject(dc, grid);
    const int spacing = std::max(8, static_cast<int>(gZoom));
    POINT origin = worldToScreen(viewport, 0.0f, 0.0f);
    for (int x = origin.x % spacing; x < viewport.right; x += spacing) if (x >= viewport.left) { MoveToEx(dc,x,viewport.top,nullptr); LineTo(dc,x,viewport.bottom); }
    for (int y = origin.y % spacing; y < viewport.bottom; y += spacing) if (y >= viewport.top) { MoveToEx(dc,viewport.left,y,nullptr); LineTo(dc,viewport.right,y); }
    SelectObject(dc, oldPen); DeleteObject(grid);

    for (int i = 0; i < static_cast<int>(gObjects.size()); ++i) {
        const auto& object = gObjects[i];
        POINT p = worldToScreen(viewport, object.x, object.z);
        RECT r{p.x-kObjectSize/2,p.y-kObjectSize/2,p.x+kObjectSize/2,p.y+kObjectSize/2};
        if (r.right < viewport.left || r.left > viewport.right || r.bottom < viewport.top || r.top > viewport.bottom) continue;
        HBRUSH brush = CreateSolidBrush(i == gSelected ? RGB(71,156,104) : RGB(70,105,136));
        HPEN pen = CreatePen(PS_SOLID, i == gSelected ? 3 : 1, i == gSelected ? RGB(235,235,235) : RGB(105,130,150));
        auto ob = SelectObject(dc,brush); auto op=SelectObject(dc,pen);
        RoundRect(dc,r.left,r.top,r.right,r.bottom,7,7);
        SelectObject(dc,op); SelectObject(dc,ob); DeleteObject(pen); DeleteObject(brush);
        std::wstring label = L"OBJ";
        for (const auto& asset : gAssets) if (asset.relative == object.asset) { label = asset.name; break; }
        SetTextColor(dc,RGB(245,245,245));
        DrawTextW(dc,label.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    }

    RECT inspector{client.right-kInspectorW,kToolbarH,client.right,client.bottom};
    fill(dc, inspector, RGB(35,36,40));
    SetTextColor(dc,RGB(225,225,228));
    RECT iTitle{inspector.left+18,kToolbarH+12,inspector.right-15,kToolbarH+38};
    DrawTextW(dc,L"INSPECTOR",-1,&iTitle,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT info{inspector.left+18,kToolbarH+58,inspector.right-16,client.bottom-20};
    if (gSelected >= 0 && gSelected < static_cast<int>(gObjects.size())) {
        const auto& o = gObjects[gSelected];
        std::wstring assetName = fs::u8path(o.asset).stem().wstring();
        std::wstring text = L"Asset\n" + assetName + L"\n\nTransform\nX: " + std::to_wstring(o.x) +
            L"\nY: " + std::to_wstring(o.y) + L"\nZ: " + std::to_wstring(o.z) +
            L"\nRotation Y: " + std::to_wstring(o.rotationY) + L"\nScale: " + std::to_wstring(o.scale) +
            L"\n\nDrag: move\nR: rotate 15 deg\n+ / -: scale\nDel: delete";
        SetTextColor(dc,RGB(210,212,216));
        DrawTextW(dc,text.c_str(),-1,&info,DT_LEFT|DT_WORDBREAK);
    } else {
        SetTextColor(dc,RGB(145,148,154));
        DrawTextW(dc,L"Select an object in the scene.\n\nMouse wheel over scene: zoom\nMiddle-drag: pan\nCtrl+S: save\nPLAY: run this scene",-1,&info,DT_LEFT|DT_WORDBREAK);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            gRoot = findProjectRoot();
            gScenePath = gRoot / "projects" / "sample-rts" / "main.scene";
            scanAssets();
            cncgzh::loadScene(gScenePath, gObjects);
            return 0;

        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            RECT client{}; GetClientRect(hwnd,&client);
            POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
            if (PtInRect(&importButton(),p)) { importKayKit(hwnd); return 0; }
            RECT save = saveButton(client.right), play = playButton(client.right), clear = clearButton(client.right);
            if (PtInRect(&save,p)) { saveCurrentScene(hwnd); return 0; }
            if (PtInRect(&play,p)) { playScene(hwnd); return 0; }
            if (PtInRect(&clear,p)) { clearScene(hwnd); return 0; }
            const int asset = assetAt(p,client);
            if (asset >= 0) { gActiveAsset = asset; InvalidateRect(hwnd,nullptr,FALSE); return 0; }
            const RECT viewport = viewportRect(client);
            if (PtInRect(&viewport,p)) {
                const int hit = hitObject(viewport,p);
                if (hit >= 0) {
                    gSelected = hit; gDragging = hit; SetCapture(hwnd);
                } else if (gActiveAsset >= 0 && gActiveAsset < static_cast<int>(gAssets.size())) {
                    float x{},z{}; screenToWorld(viewport,p,x,z);
                    cncgzh::SceneObject object;
                    object.asset = gAssets[gActiveAsset].relative;
                    object.x = std::round(x*4.0f)/4.0f;
                    object.z = std::round(z*4.0f)/4.0f;
                    gObjects.push_back(object);
                    gSelected = static_cast<int>(gObjects.size())-1;
                    gDirty = true;
                } else gSelected = -1;
                InvalidateRect(hwnd,nullptr,FALSE);
            }
            return 0;
        }

        case WM_MBUTTONDOWN:
            gLastMouse = {GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)}; SetCapture(hwnd); return 0;

        case WM_MOUSEMOVE: {
            POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
            RECT client{}; GetClientRect(hwnd,&client);
            const RECT viewport = viewportRect(client);
            if (gDragging >= 0 && (wParam & MK_LBUTTON) && gDragging < static_cast<int>(gObjects.size())) {
                float x{},z{}; screenToWorld(viewport,p,x,z);
                gObjects[gDragging].x = std::round(x*4.0f)/4.0f;
                gObjects[gDragging].z = std::round(z*4.0f)/4.0f;
                gDirty = true; InvalidateRect(hwnd,nullptr,FALSE);
            } else if (wParam & MK_MBUTTON) {
                gViewX -= (p.x-gLastMouse.x)/gZoom;
                gViewZ -= (p.y-gLastMouse.y)/gZoom;
                gLastMouse=p; InvalidateRect(hwnd,nullptr,FALSE);
            }
            return 0;
        }

        case WM_LBUTTONUP:
            if (gDragging >= 0) { gDragging=-1; ReleaseCapture(); } return 0;
        case WM_MBUTTONUP:
            ReleaseCapture(); return 0;

        case WM_MOUSEWHEEL: {
            POINT screen{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)}; ScreenToClient(hwnd,&screen);
            RECT client{}; GetClientRect(hwnd,&client);
            if (screen.x < kAssetsW) {
                const int direction = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -3 : 3;
                gAssetScroll = std::clamp(gAssetScroll + direction,0,std::max(0,static_cast<int>(gAssets.size())-1));
            } else if (PtInRect(&viewportRect(client),screen)) {
                gZoom *= GET_WHEEL_DELTA_WPARAM(wParam)>0 ? 1.12f : 0.89f;
                gZoom=std::clamp(gZoom,12.0f,140.0f);
            }
            InvalidateRect(hwnd,nullptr,FALSE); return 0;
        }

        case WM_KEYDOWN:
            if ((GetKeyState(VK_CONTROL)&0x8000) && wParam=='S') { saveCurrentScene(hwnd); return 0; }
            if (gSelected>=0 && gSelected<static_cast<int>(gObjects.size())) {
                auto& o=gObjects[gSelected];
                if (wParam==VK_DELETE) { gObjects.erase(gObjects.begin()+gSelected); gSelected=-1; gDirty=true; }
                else if (wParam=='R') { o.rotationY=std::fmod(o.rotationY+15.0f,360.0f); gDirty=true; }
                else if (wParam==VK_OEM_PLUS || wParam==VK_ADD) { o.scale=std::min(20.0f,o.scale*1.1f); gDirty=true; }
                else if (wParam==VK_OEM_MINUS || wParam==VK_SUBTRACT) { o.scale=std::max(0.05f,o.scale/1.1f); gDirty=true; }
                InvalidateRect(hwnd,nullptr,FALSE);
            }
            return 0;

        case WM_PAINT: { PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps); drawEditor(hwnd,dc); EndPaint(hwnd,&ps); return 0; }
        case WM_CLOSE:
            if (gDirty && MessageBoxW(hwnd,L"Close without saving the scene?",L"CnCGZH Studio",MB_YESNO|MB_ICONWARNING)!=IDYES) return 0;
            DestroyWindow(hwnd); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,wParam,lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    const wchar_t* className=L"CnCGZHStudioWindow";
    WNDCLASSW wc{}; wc.lpfnWndProc=WindowProc; wc.hInstance=instance; wc.lpszClassName=className; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    if (!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return 1;
    HWND hwnd=CreateWindowExW(0,className,L"CnCGZH Studio - Scene Editor",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1440,850,nullptr,nullptr,instance,nullptr);
    if (!hwnd) return 2;
    ShowWindow(hwnd,show); UpdateWindow(hwnd);
    MSG msg{}; while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);} return static_cast<int>(msg.wParam);
}
