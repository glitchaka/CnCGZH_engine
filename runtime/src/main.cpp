#include <windows.h>
#include <windowsx.h>
#include <chrono>
#include <cmath>
#include <string>

#include "runtime/Runtime.h"

namespace {

cncgzh::World gWorld;
cncgzh::Camera gCamera;
bool gRunning = true;

cncgzh::Vec2 screenToWorld(HWND hwnd, int sx, int sy) {
    RECT r{};
    GetClientRect(hwnd, &r);
    const float cx = (r.right - r.left) * 0.5f;
    const float cy = (r.bottom - r.top) * 0.5f;
    return {
        gCamera.position.x + (static_cast<float>(sx) - cx) / gCamera.zoom,
        gCamera.position.y + (static_cast<float>(sy) - cy) / gCamera.zoom
    };
}

POINT worldToScreen(HWND hwnd, cncgzh::Vec2 p) {
    RECT r{};
    GetClientRect(hwnd, &r);
    const float cx = (r.right - r.left) * 0.5f;
    const float cy = (r.bottom - r.top) * 0.5f;
    return {
        static_cast<LONG>(cx + (p.x - gCamera.position.x) * gCamera.zoom),
        static_cast<LONG>(cy + (p.y - gCamera.position.y) * gCamera.zoom)
    };
}

void drawScene(HWND hwnd, HDC dc) {
    RECT client{};
    GetClientRect(hwnd, &client);
    HBRUSH bg = CreateSolidBrush(RGB(24, 28, 24));
    FillRect(dc, &client, bg);
    DeleteObject(bg);

    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(38, 44, 38));
    HGDIOBJ oldPen = SelectObject(dc, gridPen);
    for (int x = 0; x < client.right; x += 48) {
        MoveToEx(dc, x, 0, nullptr);
        LineTo(dc, x, client.bottom);
    }
    for (int y = 0; y < client.bottom; y += 48) {
        MoveToEx(dc, 0, y, nullptr);
        LineTo(dc, client.right, y);
    }
    SelectObject(dc, oldPen);
    DeleteObject(gridPen);

    for (const auto& e : gWorld.entities()) {
        POINT p = worldToScreen(hwnd, e.position);
        const int radius = static_cast<int>(e.radius * gCamera.zoom);
        RECT body{p.x - radius, p.y - radius, p.x + radius, p.y + radius};
        HBRUSH brush = CreateSolidBrush(e.selected ? RGB(235, 210, 75) : RGB(85, 160, 220));
        FillRect(dc, &body, brush);
        DeleteObject(brush);

        if (e.hasTarget) {
            POINT target = worldToScreen(hwnd, e.target);
            HPEN pathPen = CreatePen(PS_DOT, 1, RGB(170, 170, 170));
            HGDIOBJ prev = SelectObject(dc, pathPen);
            MoveToEx(dc, p.x, p.y, nullptr);
            LineTo(dc, target.x, target.y);
            SelectObject(dc, prev);
            DeleteObject(pathPen);
        }
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(235, 235, 235));
    RECT info{12, 10, 800, 80};
    const std::wstring text = L"CnCGZH Engine Runtime  |  Left click: select  |  Right click: move  |  Shift+Left: spawn  |  WASD: camera  |  Wheel: zoom";
    DrawTextW(dc, text.c_str(), -1, &info, DT_LEFT | DT_SINGLELINE);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            gWorld.spawn({-120.0f, -40.0f});
            gWorld.spawn({-70.0f, 20.0f});
            gWorld.spawn({40.0f, -10.0f});
            SetTimer(hwnd, 1, 16, nullptr);
            return 0;

        case WM_LBUTTONDOWN: {
            const auto worldPos = screenToWorld(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                gWorld.spawn(worldPos);
            } else {
                gWorld.clearSelection();
                if (auto* hit = gWorld.pick(worldPos)) hit->selected = true;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_RBUTTONDOWN: {
            const auto worldPos = screenToWorld(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            gWorld.commandMoveSelected(worldPos);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            gCamera.zoom *= delta > 0 ? 1.1f : 0.9f;
            if (gCamera.zoom < 0.35f) gCamera.zoom = 0.35f;
            if (gCamera.zoom > 3.0f) gCamera.zoom = 3.0f;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_TIMER: {
            const float cameraSpeed = 6.0f / gCamera.zoom;
            if (GetAsyncKeyState('W') & 0x8000) gCamera.position.y -= cameraSpeed;
            if (GetAsyncKeyState('S') & 0x8000) gCamera.position.y += cameraSpeed;
            if (GetAsyncKeyState('A') & 0x8000) gCamera.position.x -= cameraSpeed;
            if (GetAsyncKeyState('D') & 0x8000) gCamera.position.x += cameraSpeed;
            gWorld.update(0.016f);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            drawScene(hwnd, dc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            KillTimer(hwnd, 1);
            gRunning = false;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    const wchar_t* className = L"CnCGZHEngineRuntimeWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(
        0,
        className,
        L"CnCGZH Engine Runtime",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1280,
        720,
        nullptr,
        nullptr,
        instance,
        nullptr
    );
    if (!hwnd) return 2;

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg{};
    while (gRunning && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
