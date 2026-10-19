#include <windows.h>
#include <shellapi.h>
#include <gl/GL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "runtime/SceneFile.h"

namespace fs = std::filesystem;

namespace {

struct Vec3 { float x{}, y{}, z{}; };
struct Triangle { Vec3 a{}, b{}, c{}; };

struct ObjMesh {
    std::vector<Triangle> triangles;

    bool load(const fs::path& path) {
        std::ifstream in(path);
        if (!in) return false;
        std::vector<Vec3> vertices;
        std::string line;
        while (std::getline(in, line)) {
            if (line.size() > 2 && line[0] == 'v' && line[1] == ' ') {
                std::istringstream ss(line.substr(2));
                Vec3 v{};
                if (ss >> v.x >> v.y >> v.z) vertices.push_back(v);
            } else if (line.size() > 2 && line[0] == 'f' && line[1] == ' ') {
                std::istringstream ss(line.substr(2));
                std::vector<int> indices;
                std::string token;
                while (ss >> token) {
                    const auto slash = token.find('/');
                    if (slash != std::string::npos) token.resize(slash);
                    if (token.empty()) continue;
                    try {
                        int index = std::stoi(token);
                        if (index < 0) index = static_cast<int>(vertices.size()) + index + 1;
                        if (index > 0 && index <= static_cast<int>(vertices.size())) indices.push_back(index - 1);
                    } catch (...) {}
                }
                for (size_t i = 1; i + 1 < indices.size(); ++i) {
                    triangles.push_back({vertices[indices[0]], vertices[indices[i]], vertices[indices[i + 1]]});
                }
            }
        }
        return !triangles.empty();
    }
};

HDC gDC{};
HGLRC gGL{};
std::vector<cncgzh::SceneObject> gObjects;
std::map<std::string, ObjMesh> gMeshes;
fs::path gRoot;
fs::path gScenePath;
float gCameraX = 0.0f;
float gCameraZ = 0.0f;
float gDistance = 18.0f;
float gYaw = 45.0f;
float gPitch = 55.0f;
int gWidth = 1280;
int gHeight = 720;

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

fs::path resolveAsset(const std::string& asset) {
    fs::path p = fs::u8path(asset);
    if (p.is_absolute()) return p;
    return gRoot / p;
}

void loadMeshes() {
    gMeshes.clear();
    for (const auto& object : gObjects) {
        if (gMeshes.find(object.asset) != gMeshes.end()) continue;
        ObjMesh mesh;
        mesh.load(resolveAsset(object.asset));
        gMeshes.emplace(object.asset, std::move(mesh));
    }
}

Vec3 normal(const Triangle& t) {
    Vec3 u{t.b.x - t.a.x, t.b.y - t.a.y, t.b.z - t.a.z};
    Vec3 v{t.c.x - t.a.x, t.c.y - t.a.y, t.c.z - t.a.z};
    Vec3 n{u.y*v.z-u.z*v.y, u.z*v.x-u.x*v.z, u.x*v.y-u.y*v.x};
    const float len = std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
    if (len > 0.00001f) { n.x/=len; n.y/=len; n.z/=len; }
    return n;
}

void perspective(float fovY, float aspect, float zNear, float zFar) {
    constexpr double pi = 3.14159265358979323846;
    const double top = zNear * std::tan(fovY * pi / 360.0);
    const double right = top * aspect;
    glFrustum(-right, right, -top, top, zNear, zFar);
}

void colorFor(const std::string& name) {
    unsigned h = 2166136261u;
    for (unsigned char c : name) { h ^= c; h *= 16777619u; }
    const float r = 0.45f + ((h >> 0) & 63) / 180.0f;
    const float g = 0.45f + ((h >> 8) & 63) / 180.0f;
    const float b = 0.45f + ((h >> 16) & 63) / 180.0f;
    glColor3f(std::min(r, 0.9f), std::min(g, 0.9f), std::min(b, 0.9f));
}

void drawGrid() {
    glDisable(GL_LIGHTING);
    glColor3f(0.22f, 0.25f, 0.24f);
    glBegin(GL_LINES);
    for (int i = -40; i <= 40; ++i) {
        glVertex3f(static_cast<float>(i), 0.0f, -40.0f);
        glVertex3f(static_cast<float>(i), 0.0f, 40.0f);
        glVertex3f(-40.0f, 0.0f, static_cast<float>(i));
        glVertex3f(40.0f, 0.0f, static_cast<float>(i));
    }
    glEnd();
    glEnable(GL_LIGHTING);
}

void drawMesh(const ObjMesh& mesh) {
    if (mesh.triangles.empty()) {
        glBegin(GL_QUADS);
        glVertex3f(-0.5f,0.0f,-0.5f); glVertex3f(0.5f,0.0f,-0.5f); glVertex3f(0.5f,1.0f,-0.5f); glVertex3f(-0.5f,1.0f,-0.5f);
        glVertex3f(-0.5f,0.0f,0.5f); glVertex3f(-0.5f,1.0f,0.5f); glVertex3f(0.5f,1.0f,0.5f); glVertex3f(0.5f,0.0f,0.5f);
        glVertex3f(-0.5f,0.0f,-0.5f); glVertex3f(-0.5f,1.0f,-0.5f); glVertex3f(-0.5f,1.0f,0.5f); glVertex3f(-0.5f,0.0f,0.5f);
        glVertex3f(0.5f,0.0f,-0.5f); glVertex3f(0.5f,0.0f,0.5f); glVertex3f(0.5f,1.0f,0.5f); glVertex3f(0.5f,1.0f,-0.5f);
        glVertex3f(-0.5f,1.0f,-0.5f); glVertex3f(0.5f,1.0f,-0.5f); glVertex3f(0.5f,1.0f,0.5f); glVertex3f(-0.5f,1.0f,0.5f);
        glEnd();
        return;
    }
    glBegin(GL_TRIANGLES);
    for (const auto& t : mesh.triangles) {
        const Vec3 n = normal(t);
        glNormal3f(n.x, n.y, n.z);
        glVertex3f(t.a.x,t.a.y,t.a.z);
        glVertex3f(t.b.x,t.b.y,t.b.z);
        glVertex3f(t.c.x,t.c.y,t.c.z);
    }
    glEnd();
}

void render() {
    if (!gDC || !gGL) return;
    glViewport(0, 0, std::max(1, gWidth), std::max(1, gHeight));
    glClearColor(0.075f, 0.085f, 0.095f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    perspective(55.0f, static_cast<float>(std::max(1,gWidth))/std::max(1,gHeight), 0.1f, 2000.0f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, -3.5f, -gDistance);
    glRotatef(gPitch, 1.0f, 0.0f, 0.0f);
    glRotatef(gYaw, 0.0f, 1.0f, 0.0f);
    glTranslatef(-gCameraX, 0.0f, -gCameraZ);

    GLfloat lightPos[] = {-20.0f, 35.0f, -10.0f, 1.0f};
    GLfloat ambient[] = {0.35f,0.35f,0.38f,1.0f};
    GLfloat diffuse[] = {0.8f,0.8f,0.78f,1.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);

    drawGrid();
    for (const auto& object : gObjects) {
        auto it = gMeshes.find(object.asset);
        if (it == gMeshes.end()) continue;
        glPushMatrix();
        glTranslatef(object.x, object.y, object.z);
        glRotatef(object.rotationY, 0.0f, 1.0f, 0.0f);
        glScalef(object.scale, object.scale, object.scale);
        colorFor(object.asset);
        drawMesh(it->second);
        glPopMatrix();
    }
    SwapBuffers(gDC);
}

bool createGL(HWND hwnd) {
    gDC = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int format = ChoosePixelFormat(gDC, &pfd);
    if (!format || !SetPixelFormat(gDC, format, &pfd)) return false;
    gGL = wglCreateContext(gDC);
    if (!gGL || !wglMakeCurrent(gDC, gGL)) return false;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glShadeModel(GL_SMOOTH);
    return true;
}

void updateTitle(HWND hwnd) {
    std::wstring title = L"CnCGZH Engine | Play Scene | Objects: " + std::to_wstring(gObjects.size());
    if (!gScenePath.empty()) title += L" | " + gScenePath.filename().wstring();
    SetWindowTextW(hwnd, title.c_str());
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            if (!createGL(hwnd)) return -1;
            SetTimer(hwnd, 1, 16, nullptr);
            updateTitle(hwnd);
            return 0;
        case WM_SIZE:
            gWidth = LOWORD(lParam); gHeight = HIWORD(lParam);
            return 0;
        case WM_MOUSEWHEEL:
            gDistance *= GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 0.9f : 1.1f;
            gDistance = std::clamp(gDistance, 3.0f, 80.0f);
            return 0;
        case WM_TIMER: {
            const float speed = 0.18f * std::max(0.35f, gDistance / 18.0f);
            if (GetAsyncKeyState('W') & 0x8000) gCameraZ -= speed;
            if (GetAsyncKeyState('S') & 0x8000) gCameraZ += speed;
            if (GetAsyncKeyState('A') & 0x8000) gCameraX -= speed;
            if (GetAsyncKeyState('D') & 0x8000) gCameraX += speed;
            if (GetAsyncKeyState(VK_LEFT) & 0x8000) gYaw -= 0.7f;
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000) gYaw += 0.7f;
            if (GetAsyncKeyState(VK_UP) & 0x8000) gPitch = std::max(20.0f, gPitch - 0.5f);
            if (GetAsyncKeyState(VK_DOWN) & 0x8000) gPitch = std::min(80.0f, gPitch + 0.5f);
            render();
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) DestroyWindow(hwnd);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps); render(); return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            if (gGL) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(gGL); gGL = nullptr; }
            if (gDC) { ReleaseDC(hwnd, gDC); gDC = nullptr; }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    gRoot = findProjectRoot();
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::wstring(argv[i]) == L"--scene") gScenePath = fs::path(argv[++i]);
    }
    LocalFree(argv);
    if (gScenePath.empty()) gScenePath = gRoot / "projects" / "sample-rts" / "main.scene";
    cncgzh::loadScene(gScenePath, gObjects);
    loadMeshes();

    const wchar_t* className = L"CnCGZHEngineRuntimeWindow";
    WNDCLASSW wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;

    HWND hwnd = CreateWindowExW(0, className, L"CnCGZH Engine", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760, nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 2;
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return static_cast<int>(msg.wParam);
}
