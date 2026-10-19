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
struct Mesh {
    std::vector<Triangle> triangles;
    bool load(const fs::path& path) {
        std::ifstream in(path);
        if (!in) return false;
        std::vector<Vec3> verts;
        std::string line;
        while (std::getline(in, line)) {
            if (line.size() > 2 && line[0] == 'v' && line[1] == ' ') {
                std::istringstream ss(line.substr(2));
                Vec3 v{}; if (ss >> v.x >> v.y >> v.z) verts.push_back(v);
            } else if (line.size() > 2 && line[0] == 'f' && line[1] == ' ') {
                std::istringstream ss(line.substr(2));
                std::vector<int> idx; std::string token;
                while (ss >> token) {
                    auto slash = token.find('/'); if (slash != std::string::npos) token.resize(slash);
                    try { int n = std::stoi(token); if (n < 0) n = (int)verts.size() + n + 1; if (n > 0 && n <= (int)verts.size()) idx.push_back(n - 1); } catch (...) {}
                }
                for (size_t i = 1; i + 1 < idx.size(); ++i) triangles.push_back({verts[idx[0]], verts[idx[i]], verts[idx[i + 1]]});
            }
        }
        return !triangles.empty();
    }
};

HDC gDC{}; HGLRC gGL{};
fs::path gRoot, gScenePath;
std::vector<cncgzh::SceneObject> gObjects;
std::map<std::string, Mesh> gMeshes;
float gCamX{}, gCamZ{}, gDistance{18.0f}, gYaw{45.0f}, gPitch{55.0f};
int gWidth{1280}, gHeight{720};

fs::path executablePath() {
    std::wstring b(32768, L'\0'); DWORD n = GetModuleFileNameW(nullptr, b.data(), (DWORD)b.size()); b.resize(n); return fs::path(b);
}

fs::path findRoot() {
    fs::path p = executablePath().parent_path();
    for (int i = 0; i < 8 && !p.empty(); ++i) {
        if (fs::exists(p / "assets" / "sample" / "kaykit-dungeon") && fs::exists(p / "projects")) return p;
        if (fs::exists(p / "engine") && fs::exists(p / "projects")) return p;
        p = p.parent_path();
    }
    return fs::current_path();
}

fs::path resolveAsset(const std::string& asset) {
    fs::path p = fs::u8path(asset); return p.is_absolute() ? p : gRoot / p;
}

Vec3 normal(const Triangle& t) {
    Vec3 u{t.b.x-t.a.x,t.b.y-t.a.y,t.b.z-t.a.z}, v{t.c.x-t.a.x,t.c.y-t.a.y,t.c.z-t.a.z};
    Vec3 n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
    float l=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z); if(l>0.00001f){n.x/=l;n.y/=l;n.z/=l;} return n;
}

void perspective(float fov,float aspect,float zn,float zf) {
    constexpr double pi=3.14159265358979323846; double top=zn*std::tan(fov*pi/360.0), right=top*aspect; glFrustum(-right,right,-top,top,zn,zf);
}

void colorFor(const std::string& s) {
    unsigned h=2166136261u; for(unsigned char c:s){h^=c;h*=16777619u;} glColor3f(0.50f+((h>>0)&63)/220.0f,0.50f+((h>>8)&63)/220.0f,0.50f+((h>>16)&63)/220.0f);
}

void drawGrid() {
    glDisable(GL_LIGHTING); glColor3f(0.18f,0.20f,0.22f); glBegin(GL_LINES);
    for(int i=-50;i<=50;++i){glVertex3f((float)i,0,-50);glVertex3f((float)i,0,50);glVertex3f(-50,0,(float)i);glVertex3f(50,0,(float)i);} glEnd(); glEnable(GL_LIGHTING);
}

void drawMesh(const Mesh& m) {
    glBegin(GL_TRIANGLES); for(const auto&t:m.triangles){Vec3 n=normal(t);glNormal3f(n.x,n.y,n.z);glVertex3f(t.a.x,t.a.y,t.a.z);glVertex3f(t.b.x,t.b.y,t.b.z);glVertex3f(t.c.x,t.c.y,t.c.z);} glEnd();
}

void loadMeshes() {
    gMeshes.clear();
    for(const auto&o:gObjects){if(gMeshes.count(o.asset))continue;Mesh m;m.load(resolveAsset(o.asset));gMeshes.emplace(o.asset,std::move(m));}
}

void render() {
    if(!gDC||!gGL)return; wglMakeCurrent(gDC,gGL);
    glViewport(0,0,std::max(1,gWidth),std::max(1,gHeight)); glClearColor(0.055f,0.061f,0.070f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); perspective(55.0f,(float)std::max(1,gWidth)/std::max(1,gHeight),0.1f,1000.0f);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glTranslatef(0,-3.0f,-gDistance); glRotatef(gPitch,1,0,0); glRotatef(gYaw,0,1,0); glTranslatef(-gCamX,0,-gCamZ);
    GLfloat lp[]={-15,30,-10,1},amb[]={0.35f,0.35f,0.38f,1},dif[]={0.85f,0.85f,0.82f,1}; glLightfv(GL_LIGHT0,GL_POSITION,lp);glLightfv(GL_LIGHT0,GL_AMBIENT,amb);glLightfv(GL_LIGHT0,GL_DIFFUSE,dif);
    drawGrid();
    for(const auto&o:gObjects){auto it=gMeshes.find(o.asset);if(it==gMeshes.end())continue;glPushMatrix();glTranslatef(o.x,o.y,o.z);glRotatef(o.rotationY,0,1,0);glScalef(o.scale,o.scale,o.scale);colorFor(o.asset);drawMesh(it->second);glPopMatrix();}
    SwapBuffers(gDC);
}

bool initGL(HWND hwnd) {
    gDC=GetDC(hwnd); PIXELFORMATDESCRIPTOR p{};p.nSize=sizeof(p);p.nVersion=1;p.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;p.iPixelType=PFD_TYPE_RGBA;p.cColorBits=32;p.cDepthBits=24;p.iLayerType=PFD_MAIN_PLANE;
    int f=ChoosePixelFormat(gDC,&p); if(!f||!SetPixelFormat(gDC,f,&p))return false; gGL=wglCreateContext(gDC); if(!gGL||!wglMakeCurrent(gDC,gGL))return false;
    glEnable(GL_DEPTH_TEST);glEnable(GL_LIGHTING);glEnable(GL_LIGHT0);glEnable(GL_COLOR_MATERIAL);glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);glShadeModel(GL_SMOOTH);return true;
}

LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg){
        case WM_CREATE: if(!initGL(hwnd))return -1;SetTimer(hwnd,1,16,nullptr);return 0;
        case WM_SIZE:gWidth=LOWORD(lp);gHeight=HIWORD(lp);return 0;
        case WM_MOUSEWHEEL:gDistance*=GET_WHEEL_DELTA_WPARAM(wp)>0?0.9f:1.1f;gDistance=std::clamp(gDistance,3.0f,80.0f);return 0;
        case WM_TIMER:{float s=0.18f*std::max(0.35f,gDistance/18.0f);if(GetAsyncKeyState('W')&0x8000)gCamZ-=s;if(GetAsyncKeyState('S')&0x8000)gCamZ+=s;if(GetAsyncKeyState('A')&0x8000)gCamX-=s;if(GetAsyncKeyState('D')&0x8000)gCamX+=s;if(GetAsyncKeyState(VK_LEFT)&0x8000)gYaw-=0.7f;if(GetAsyncKeyState(VK_RIGHT)&0x8000)gYaw+=0.7f;if(GetAsyncKeyState(VK_UP)&0x8000)gPitch=std::max(20.0f,gPitch-0.5f);if(GetAsyncKeyState(VK_DOWN)&0x8000)gPitch=std::min(80.0f,gPitch+0.5f);render();return 0;}
        case WM_KEYDOWN:if(wp==VK_ESCAPE)DestroyWindow(hwnd);return 0;
        case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(hwnd,&ps);EndPaint(hwnd,&ps);render();return 0;}
        case WM_DESTROY:KillTimer(hwnd,1);if(gGL){wglMakeCurrent(nullptr,nullptr);wglDeleteContext(gGL);}if(gDC)ReleaseDC(hwnd,gDC);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    gRoot=findRoot(); int argc=0;LPWSTR*argv=CommandLineToArgvW(GetCommandLineW(),&argc);for(int i=1;i+1<argc;++i)if(std::wstring(argv[i])==L"--scene")gScenePath=fs::path(argv[++i]);LocalFree(argv);
    if(gScenePath.empty())gScenePath=gRoot/"projects"/"sample-rts"/"main.scene";cncgzh::loadScene(gScenePath,gObjects);loadMeshes();
    WNDCLASSW wc{};wc.style=CS_OWNDC;wc.lpfnWndProc=Proc;wc.hInstance=instance;wc.lpszClassName=L"CnCGZHEnginePlayer";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 1;
    std::wstring title=L"CnCGZH Engine — Play | "+std::to_wstring(gObjects.size())+L" objects";HWND hwnd=CreateWindowExW(0,wc.lpszClassName,title.c_str(),WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1280,760,nullptr,nullptr,instance,nullptr);if(!hwnd)return 2;ShowWindow(hwnd,show);UpdateWindow(hwnd);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return(int)msg.wParam;
}
