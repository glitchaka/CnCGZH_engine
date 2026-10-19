#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <gl/GL.h>
#include <gl/GLU.h>

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
                Vec3 v{}; if (ss >> v.x >> v.y >> v.z) vertices.push_back(v);
            } else if (line.size() > 2 && line[0] == 'f' && line[1] == ' ') {
                std::istringstream ss(line.substr(2));
                std::vector<int> indices; std::string token;
                while (ss >> token) {
                    const auto slash = token.find('/');
                    if (slash != std::string::npos) token.resize(slash);
                    if (token.empty()) continue;
                    try {
                        int idx = std::stoi(token);
                        if (idx < 0) idx = static_cast<int>(vertices.size()) + idx + 1;
                        if (idx > 0 && idx <= static_cast<int>(vertices.size())) indices.push_back(idx - 1);
                    } catch (...) {}
                }
                for (size_t i = 1; i + 1 < indices.size(); ++i)
                    triangles.push_back({vertices[indices[0]], vertices[indices[i]], vertices[indices[i + 1]]});
            }
        }
        return !triangles.empty();
    }
};

struct AssetEntry { fs::path absolute; std::string relative; std::wstring name; };

constexpr int ID_SAVE = 1001;
constexpr int ID_PLAY = 1002;
constexpr int ID_CLEAR = 1003;
constexpr int ID_SEARCH = 1004;
constexpr int ID_ASSETS = 1005;
constexpr int ID_HIERARCHY = 1006;
constexpr int ID_INSPECTOR = 1007;
constexpr int ID_STATUS = 1008;
constexpr int kToolbarH = 46;
constexpr int kStatusH = 24;
constexpr int kLeftW = 280;
constexpr int kRightW = 300;

HWND gMain{}, gViewport{}, gSearch{}, gAssetsList{}, gHierarchy{}, gInspector{}, gStatus{};
HFONT gFont{};
HBRUSH gPanelBrush{};
HDC gGLDC{};
HGLRC gGL{};
fs::path gRoot, gScenePath;
std::vector<AssetEntry> gAssets;
std::vector<int> gFilteredAssets;
std::vector<cncgzh::SceneObject> gObjects;
std::vector<std::vector<cncgzh::SceneObject>> gUndo;
std::map<std::string, ObjMesh> gMeshes;
int gActiveAsset = -1;
int gSelected = -1;
bool gDirty = false;
bool gDraggingObject = false;
bool gOrbiting = false;
bool gPanning = false;
POINT gLastMouse{};
float gCameraX = 0.0f, gCameraZ = 0.0f, gDistance = 18.0f, gYaw = 45.0f, gPitch = 52.0f;
int gViewportW = 1, gViewportH = 1;
GLdouble gModel[16]{}, gProj[16]{};
GLint gView[4]{};

fs::path executablePath() {
    std::wstring buffer(32768, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(len); return fs::path(buffer);
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

std::string toUtf8(const fs::path& p) {
    const std::wstring w = p.generic_wstring();
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

void pushUndo() {
    gUndo.push_back(gObjects);
    if (gUndo.size() > 50) gUndo.erase(gUndo.begin());
}

void setStatus(const std::wstring& text) { if (gStatus) SetWindowTextW(gStatus, text.c_str()); }

void scanAssets() {
    gAssets.clear();
    const fs::path root = gRoot / "assets" / "sample" / "kaykit-dungeon" / "obj";
    std::error_code ec;
    if (!fs::exists(root)) return;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec) || it->path().extension() != L".obj") continue;
        fs::path rel = fs::relative(it->path(), gRoot, ec);
        if (ec) { ec.clear(); rel = it->path(); }
        gAssets.push_back({it->path(), toUtf8(rel), it->path().stem().wstring()});
    }
    std::sort(gAssets.begin(), gAssets.end(), [](const auto& a, const auto& b){ return a.name < b.name; });
}

void refreshAssets() {
    wchar_t query[256]{}; GetWindowTextW(gSearch, query, 256);
    std::wstring q = query;
    std::transform(q.begin(), q.end(), q.begin(), ::towlower);
    SendMessageW(gAssetsList, LB_RESETCONTENT, 0, 0);
    gFilteredAssets.clear();
    for (int i = 0; i < static_cast<int>(gAssets.size()); ++i) {
        std::wstring n = gAssets[i].name; std::wstring low = n;
        std::transform(low.begin(), low.end(), low.begin(), ::towlower);
        if (!q.empty() && low.find(q) == std::wstring::npos) continue;
        gFilteredAssets.push_back(i);
        SendMessageW(gAssetsList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n.c_str()));
    }
    std::wstring s = std::to_wstring(gFilteredAssets.size()) + L" assets ready";
    setStatus(s);
}

void refreshHierarchy() {
    SendMessageW(gHierarchy, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < static_cast<int>(gObjects.size()); ++i) {
        std::wstring n = fs::u8path(gObjects[i].asset).stem().wstring() + L"  #" + std::to_wstring(i + 1);
        SendMessageW(gHierarchy, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n.c_str()));
    }
    if (gSelected >= 0) SendMessageW(gHierarchy, LB_SETCURSEL, gSelected, 0);
}

void refreshInspector() {
    if (gSelected < 0 || gSelected >= static_cast<int>(gObjects.size())) {
        SetWindowTextW(gInspector, L"No object selected\r\n\r\nSelect an object in the viewport or Scene list.\r\n\r\nAsset placement:\r\nChoose an asset on the left, then click the 3D viewport.\r\n\r\nControls:\r\nLMB: select / place / move\r\nRMB drag: orbit\r\nMMB drag: pan\r\nWheel: zoom\r\nR: rotate 15°\r\n+/-: scale\r\nCtrl+D: duplicate\r\nDel: delete\r\nCtrl+Z: undo\r\nEsc: cancel placement");
        return;
    }
    const auto& o = gObjects[gSelected];
    std::wostringstream ss;
    ss << L"OBJECT\r\n" << fs::u8path(o.asset).stem().wstring()
       << L"\r\n\r\nTRANSFORM\r\nX   " << o.x << L"\r\nY   " << o.y << L"\r\nZ   " << o.z
       << L"\r\nRot Y   " << o.rotationY << L"°\r\nScale   " << o.scale
       << L"\r\n\r\nR rotate   +/- scale\r\nCtrl+D duplicate   Del delete";
    SetWindowTextW(gInspector, ss.str().c_str());
}

ObjMesh& meshFor(const std::string& asset) {
    auto it = gMeshes.find(asset);
    if (it != gMeshes.end()) return it->second;
    ObjMesh m; m.load(gRoot / fs::u8path(asset));
    return gMeshes.emplace(asset, std::move(m)).first->second;
}

Vec3 normal(const Triangle& t) {
    Vec3 u{t.b.x-t.a.x,t.b.y-t.a.y,t.b.z-t.a.z}, v{t.c.x-t.a.x,t.c.y-t.a.y,t.c.z-t.a.z};
    Vec3 n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
    float l = std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z); if (l > 0.00001f) { n.x/=l;n.y/=l;n.z/=l; } return n;
}

void perspective(float fovY, float aspect, float zn, float zf) {
    constexpr double pi = 3.14159265358979323846;
    double top = zn * std::tan(fovY*pi/360.0), right = top*aspect;
    glFrustum(-right,right,-top,top,zn,zf);
}

void colorFor(const std::string& name, bool selected) {
    if (selected) { glColor3f(0.30f,0.78f,0.45f); return; }
    unsigned h=2166136261u; for (unsigned char c:name){h^=c;h*=16777619u;}
    glColor3f(0.50f+((h>>0)&63)/220.0f,0.50f+((h>>8)&63)/220.0f,0.50f+((h>>16)&63)/220.0f);
}

void drawMesh(const ObjMesh& mesh) {
    glBegin(GL_TRIANGLES);
    for (const auto& t:mesh.triangles) { Vec3 n=normal(t); glNormal3f(n.x,n.y,n.z); glVertex3f(t.a.x,t.a.y,t.a.z); glVertex3f(t.b.x,t.b.y,t.b.z); glVertex3f(t.c.x,t.c.y,t.c.z); }
    glEnd();
}

void drawGrid() {
    glDisable(GL_LIGHTING);
    glColor3f(0.18f,0.20f,0.22f); glBegin(GL_LINES);
    for(int i=-50;i<=50;++i){glVertex3f((float)i,0,-50);glVertex3f((float)i,0,50);glVertex3f(-50,0,(float)i);glVertex3f(50,0,(float)i);} glEnd();
    glColor3f(0.55f,0.18f,0.18f); glBegin(GL_LINES); glVertex3f(-50,0.01f,0);glVertex3f(50,0.01f,0);glEnd();
    glColor3f(0.18f,0.42f,0.70f); glBegin(GL_LINES); glVertex3f(0,0.01f,-50);glVertex3f(0,0.01f,50);glEnd();
    glEnable(GL_LIGHTING);
}

void renderViewport() {
    if (!gGL || !gGLDC) return;
    wglMakeCurrent(gGLDC,gGL);
    glViewport(0,0,std::max(1,gViewportW),std::max(1,gViewportH));
    glClearColor(0.055f,0.061f,0.070f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); perspective(55.0f,(float)std::max(1,gViewportW)/std::max(1,gViewportH),0.1f,1000.0f);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glTranslatef(0,-3.0f,-gDistance); glRotatef(gPitch,1,0,0); glRotatef(gYaw,0,1,0); glTranslatef(-gCameraX,0,-gCameraZ);
    glGetDoublev(GL_MODELVIEW_MATRIX,gModel); glGetDoublev(GL_PROJECTION_MATRIX,gProj); glGetIntegerv(GL_VIEWPORT,gView);
    GLfloat lp[]={-15,30,-10,1}, amb[]={0.35f,0.35f,0.38f,1}, dif[]={0.85f,0.85f,0.82f,1};
    glLightfv(GL_LIGHT0,GL_POSITION,lp); glLightfv(GL_LIGHT0,GL_AMBIENT,amb); glLightfv(GL_LIGHT0,GL_DIFFUSE,dif);
    drawGrid();
    for(int i=0;i<(int)gObjects.size();++i){auto&o=gObjects[i];auto&m=meshFor(o.asset);glPushMatrix();glTranslatef(o.x,o.y,o.z);glRotatef(o.rotationY,0,1,0);glScalef(o.scale,o.scale,o.scale);colorFor(o.asset,i==gSelected);drawMesh(m);glPopMatrix();}
    SwapBuffers(gGLDC);
}

bool groundFromScreen(int x,int y,float& wx,float& wz) {
    GLdouble x0,y0,z0,x1,y1,z1;
    double sy = gView[3]-y;
    if(!gluUnProject(x,sy,0,gModel,gProj,gView,&x0,&y0,&z0) || !gluUnProject(x,sy,1,gModel,gProj,gView,&x1,&y1,&z1)) return false;
    double dy=y1-y0; if(std::abs(dy)<1e-8) return false; double t=-y0/dy; if(t<0) return false;
    wx=(float)(x0+(x1-x0)*t); wz=(float)(z0+(z1-z0)*t); return true;
}

int pickObject(int x,int y) {
    int best=-1; double bestD=900.0;
    for(int i=0;i<(int)gObjects.size();++i){GLdouble sx,sy,sz;auto&o=gObjects[i];if(!gluProject(o.x,o.y+0.5,o.z,gModel,gProj,gView,&sx,&sy,&sz))continue;double dx=sx-x,dy=(gView[3]-sy)-y,d=dx*dx+dy*dy;if(d<bestD){bestD=d;best=i;}}
    return best;
}

void markChanged(){gDirty=true;refreshHierarchy();refreshInspector();renderViewport();SetWindowTextW(gMain,L"CnCGZH Studio — Scene Editor *");}

void addActiveAssetAt(int x,int y){if(gActiveAsset<0||gActiveAsset>=(int)gAssets.size())return;float wx,wz;if(!groundFromScreen(x,y,wx,wz))return;pushUndo();cncgzh::SceneObject o;o.asset=gAssets[gActiveAsset].relative;o.x=std::round(wx*4)/4.0f;o.z=std::round(wz*4)/4.0f;gObjects.push_back(o);gSelected=(int)gObjects.size()-1;markChanged();}

bool saveScene(){if(!cncgzh::saveScene(gScenePath,gObjects)){MessageBoxW(gMain,L"Could not save the scene.",L"CnCGZH Studio",MB_ICONERROR);return false;}gDirty=false;SetWindowTextW(gMain,L"CnCGZH Studio — Scene Editor");setStatus(L"Scene saved");return true;}

void playScene(){if(!saveScene())return;fs::path exe=executablePath().parent_path()/"CnCGZHEngine.exe";if(!fs::exists(exe)){MessageBoxW(gMain,L"CnCGZHEngine.exe was not found beside Studio.",L"Play",MB_ICONERROR);return;}std::wstring params=L"--scene \""+gScenePath.wstring()+L"\"";ShellExecuteW(gMain,L"open",exe.c_str(),params.c_str(),gRoot.c_str(),SW_SHOWNORMAL);}

void updateLayout(HWND hwnd){RECT r{};GetClientRect(hwnd,&r);int w=r.right,h=r.bottom;MoveWindow(gSearch,10,kToolbarH+8,kLeftW-20,28,TRUE);MoveWindow(gAssetsList,10,kToolbarH+42,kLeftW-20,std::max(80,h-kToolbarH-kStatusH-52),TRUE);int rightX=w-kRightW;MoveWindow(gHierarchy,rightX+10,kToolbarH+28,kRightW-20,180,TRUE);MoveWindow(gInspector,rightX+10,kToolbarH+226,kRightW-20,std::max(120,h-kToolbarH-kStatusH-236),TRUE);MoveWindow(gViewport,kLeftW,kToolbarH,std::max(1,w-kLeftW-kRightW),std::max(1,h-kToolbarH-kStatusH),TRUE);MoveWindow(gStatus,0,h-kStatusH,w,kStatusH,TRUE);}

bool initGL(HWND hwnd){gGLDC=GetDC(hwnd);PIXELFORMATDESCRIPTOR p{};p.nSize=sizeof(p);p.nVersion=1;p.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;p.iPixelType=PFD_TYPE_RGBA;p.cColorBits=32;p.cDepthBits=24;p.iLayerType=PFD_MAIN_PLANE;int f=ChoosePixelFormat(gGLDC,&p);if(!f||!SetPixelFormat(gGLDC,f,&p))return false;gGL=wglCreateContext(gGLDC);if(!gGL||!wglMakeCurrent(gGLDC,gGL))return false;glEnable(GL_DEPTH_TEST);glEnable(GL_LIGHTING);glEnable(GL_LIGHT0);glEnable(GL_COLOR_MATERIAL);glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);glShadeModel(GL_SMOOTH);return true;}

LRESULT CALLBACK ViewportProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
        case WM_CREATE: if(!initGL(hwnd))return -1; SetTimer(hwnd,1,16,nullptr); return 0;
        case WM_SIZE: gViewportW=LOWORD(lp);gViewportH=HIWORD(lp);return 0;
        case WM_TIMER: renderViewport(); return 0;
        case WM_MOUSEWHEEL: gDistance*=GET_WHEEL_DELTA_WPARAM(wp)>0?0.90f:1.10f;gDistance=std::clamp(gDistance,3.0f,80.0f);return 0;
        case WM_LBUTTONDOWN:{SetFocus(hwnd);int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);if(gActiveAsset>=0){addActiveAssetAt(x,y);return 0;}int hit=pickObject(x,y);gSelected=hit;refreshHierarchy();refreshInspector();if(hit>=0){pushUndo();gDraggingObject=true;SetCapture(hwnd);}renderViewport();return 0;}
        case WM_MOUSEMOVE:{int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);if(gDraggingObject&&gSelected>=0&&(wp&MK_LBUTTON)){float wx,wz;if(groundFromScreen(x,y,wx,wz)){gObjects[gSelected].x=std::round(wx*4)/4.0f;gObjects[gSelected].z=std::round(wz*4)/4.0f;gDirty=true;refreshInspector();}}else if(gOrbiting&&(wp&MK_RBUTTON)){gYaw+=(x-gLastMouse.x)*0.4f;gPitch=std::clamp(gPitch+(y-gLastMouse.y)*0.35f,18.0f,82.0f);gLastMouse={x,y};}else if(gPanning&&(wp&MK_MBUTTON)){float s=0.015f*gDistance;gCameraX-=(x-gLastMouse.x)*s;gCameraZ+=(y-gLastMouse.y)*s;gLastMouse={x,y};}return 0;}
        case WM_LBUTTONUP: if(gDraggingObject){gDraggingObject=false;ReleaseCapture();markChanged();}return 0;
        case WM_RBUTTONDOWN:gOrbiting=true;gLastMouse={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};SetCapture(hwnd);return 0;
        case WM_RBUTTONUP:gOrbiting=false;ReleaseCapture();return 0;
        case WM_MBUTTONDOWN:gPanning=true;gLastMouse={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};SetCapture(hwnd);return 0;
        case WM_MBUTTONUP:gPanning=false;ReleaseCapture();return 0;
        case WM_KEYDOWN:{
            if(wp==VK_ESCAPE){gActiveAsset=-1;SendMessageW(gAssetsList,LB_SETCURSEL,-1,0);setStatus(L"Placement cancelled");return 0;}
            if((GetKeyState(VK_CONTROL)&0x8000)&&wp=='Z'&&!gUndo.empty()){gObjects=gUndo.back();gUndo.pop_back();gSelected=-1;markChanged();return 0;}
            if(gSelected>=0&&gSelected<(int)gObjects.size()){
                if(wp==VK_DELETE){pushUndo();gObjects.erase(gObjects.begin()+gSelected);gSelected=-1;markChanged();}
                else if(wp=='R'){pushUndo();gObjects[gSelected].rotationY=std::fmod(gObjects[gSelected].rotationY+15.0f,360.0f);markChanged();}
                else if(wp==VK_OEM_PLUS||wp==VK_ADD){pushUndo();gObjects[gSelected].scale=std::min(20.0f,gObjects[gSelected].scale*1.1f);markChanged();}
                else if(wp==VK_OEM_MINUS||wp==VK_SUBTRACT){pushUndo();gObjects[gSelected].scale=std::max(0.05f,gObjects[gSelected].scale/1.1f);markChanged();}
                else if((GetKeyState(VK_CONTROL)&0x8000)&&wp=='D'){pushUndo();auto o=gObjects[gSelected];o.x+=0.5f;o.z+=0.5f;gObjects.push_back(o);gSelected=(int)gObjects.size()-1;markChanged();}
            } return 0;}
        case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(hwnd,&ps);EndPaint(hwnd,&ps);renderViewport();return 0;}
        case WM_DESTROY:KillTimer(hwnd,1);if(gGL){wglMakeCurrent(nullptr,nullptr);wglDeleteContext(gGL);gGL=nullptr;}if(gGLDC){ReleaseDC(hwnd,gGLDC);gGLDC=nullptr;}return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

LRESULT CALLBACK MainProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
        case WM_CREATE:{
            gMain=hwnd;gRoot=findRoot();gScenePath=gRoot/"projects"/"sample-rts"/"main.scene";scanAssets();cncgzh::loadScene(gScenePath,gObjects);
            gFont=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            gPanelBrush=CreateSolidBrush(RGB(31,33,37));
            CreateWindowW(L"BUTTON",L"Save",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,90,30,hwnd,(HMENU)ID_SAVE,nullptr,nullptr);
            CreateWindowW(L"BUTTON",L"PLAY",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,90,30,hwnd,(HMENU)ID_PLAY,nullptr,nullptr);
            CreateWindowW(L"BUTTON",L"Clear",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,90,30,hwnd,(HMENU)ID_CLEAR,nullptr,nullptr);
            gSearch=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,hwnd,(HMENU)ID_SEARCH,nullptr,nullptr);
            gAssetsList=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|LBS_NOTIFY|WS_VSCROLL,0,0,0,0,hwnd,(HMENU)ID_ASSETS,nullptr,nullptr);
            gHierarchy=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|LBS_NOTIFY|WS_VSCROLL,0,0,0,0,hwnd,(HMENU)ID_HIERARCHY,nullptr,nullptr);
            gInspector=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,0,0,0,0,hwnd,(HMENU)ID_INSPECTOR,nullptr,nullptr);
            gStatus=CreateWindowW(L"STATIC",L"Ready",WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,0,0,hwnd,(HMENU)ID_STATUS,nullptr,nullptr);
            gViewport=CreateWindowExW(0,L"CnCGZHViewport",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,0,0,100,100,hwnd,nullptr,nullptr,nullptr);
            EnumChildWindows(hwnd,[](HWND c,LPARAM f)->BOOL{SendMessageW(c,WM_SETFONT,(WPARAM)f,TRUE);return TRUE;},(LPARAM)gFont);
            refreshAssets();refreshHierarchy();refreshInspector();updateLayout(hwnd);setStatus(L"Ready — choose an asset and click the 3D viewport to place it");return 0;}
        case WM_SIZE:updateLayout(hwnd);return 0;
        case WM_COMMAND:{int id=LOWORD(wp),code=HIWORD(wp);
            if(id==ID_SAVE){saveScene();return 0;} if(id==ID_PLAY){playScene();return 0;} if(id==ID_CLEAR){if(!gObjects.empty()&&MessageBoxW(hwnd,L"Clear the entire scene?",L"CnCGZH Studio",MB_YESNO|MB_ICONQUESTION)!=IDYES)return 0;pushUndo();gObjects.clear();gSelected=-1;markChanged();return 0;}
            if(id==ID_SEARCH&&code==EN_CHANGE){refreshAssets();return 0;}
            if(id==ID_ASSETS&&(code==LBN_SELCHANGE||code==LBN_DBLCLK)){int row=(int)SendMessageW(gAssetsList,LB_GETCURSEL,0,0);if(row>=0&&row<(int)gFilteredAssets.size()){gActiveAsset=gFilteredAssets[row];setStatus(L"Placement mode — click the 3D viewport; Esc cancels");SetFocus(gViewport);}return 0;}
            if(id==ID_HIERARCHY&&code==LBN_SELCHANGE){int row=(int)SendMessageW(gHierarchy,LB_GETCURSEL,0,0);gSelected=row;gActiveAsset=-1;refreshInspector();renderViewport();return 0;} return 0;}
        case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:{HDC dc=(HDC)wp;SetTextColor(dc,RGB(225,228,232));SetBkColor(dc,RGB(31,33,37));return (LRESULT)gPanelBrush;}
        case WM_PAINT:{PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);RECT r{};GetClientRect(hwnd,&r);HBRUSH bg=CreateSolidBrush(RGB(24,26,30));FillRect(dc,&r,bg);DeleteObject(bg);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(235,238,242));SelectObject(dc,gFont);TextOutW(dc,12,14,L"CnCGZH Studio",13);TextOutW(dc,12,kToolbarH+10,L"ASSETS",6);TextOutW(dc,r.right-kRightW+10,kToolbarH+8,L"SCENE",5);RECT b1{r.right-298,8,r.right-208,38},b2{r.right-198,8,r.right-108,38},b3{r.right-98,8,r.right-8,38};HWND s=GetDlgItem(hwnd,ID_SAVE),p=GetDlgItem(hwnd,ID_PLAY),c=GetDlgItem(hwnd,ID_CLEAR);MoveWindow(s,b1.left,b1.top,90,30,TRUE);MoveWindow(p,b2.left,b2.top,90,30,TRUE);MoveWindow(c,b3.left,b3.top,90,30,TRUE);EndPaint(hwnd,&ps);return 0;}
        case WM_CLOSE:if(gDirty&&MessageBoxW(hwnd,L"Close without saving changes?",L"CnCGZH Studio",MB_YESNO|MB_ICONWARNING)!=IDYES)return 0;DestroyWindow(hwnd);return 0;
        case WM_DESTROY:if(gFont)DeleteObject(gFont);if(gPanelBrush)DeleteObject(gPanelBrush);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_STANDARD_CLASSES};InitCommonControlsEx(&ic);
    WNDCLASSW vw{};vw.style=CS_OWNDC|CS_HREDRAW|CS_VREDRAW;vw.lpfnWndProc=ViewportProc;vw.hInstance=instance;vw.lpszClassName=L"CnCGZHViewport";vw.hCursor=LoadCursor(nullptr,IDC_CROSS);RegisterClassW(&vw);
    WNDCLASSW wc{};wc.lpfnWndProc=MainProc;wc.hInstance=instance;wc.lpszClassName=L"CnCGZHStudioWindow";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 1;
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"CnCGZH Studio — Scene Editor",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1500,900,nullptr,nullptr,instance,nullptr);if(!hwnd)return 2;ShowWindow(hwnd,show);UpdateWindow(hwnd);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return (int)msg.wParam;
}
