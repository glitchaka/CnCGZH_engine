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
                Vec3 v{};
                if (ss >> v.x >> v.y >> v.z) vertices.push_back(v);
            } else if (line.size() > 2 && line[0] == 'f' && line[1] == ' ') {
                std::istringstream ss(line.substr(2));
                std::vector<int> idx;
                std::string token;
                while (ss >> token) {
                    auto slash = token.find('/');
                    if (slash != std::string::npos) token.resize(slash);
                    if (token.empty()) continue;
                    try {
                        int i = std::stoi(token);
                        if (i < 0) i = static_cast<int>(vertices.size()) + i + 1;
                        if (i > 0 && i <= static_cast<int>(vertices.size())) idx.push_back(i - 1);
                    } catch (...) {}
                }
                for (size_t i = 1; i + 1 < idx.size(); ++i)
                    triangles.push_back({vertices[idx[0]], vertices[idx[i]], vertices[idx[i + 1]]});
            }
        }
        return !triangles.empty();
    }
};

struct AssetEntry {
    fs::path absolute;
    std::string relative;
    std::wstring name;
};

enum class ToolMode { Select, Move, Rotate, Scale, Place };

constexpr int ID_SAVE = 1001;
constexpr int ID_PLAY = 1002;
constexpr int ID_CLEAR = 1003;
constexpr int ID_SEARCH = 1004;
constexpr int ID_ASSETS = 1005;
constexpr int ID_HIERARCHY = 1006;
constexpr int ID_STATUS = 1007;
constexpr int ID_TOOL_SELECT = 1010;
constexpr int ID_TOOL_MOVE = 1011;
constexpr int ID_TOOL_ROTATE = 1012;
constexpr int ID_TOOL_SCALE = 1013;
constexpr int ID_SNAP = 1014;
constexpr int ID_X = 1020;
constexpr int ID_Y = 1021;
constexpr int ID_Z = 1022;
constexpr int ID_ROT = 1023;
constexpr int ID_SCALE = 1024;
constexpr int ID_APPLY = 1025;
constexpr int ID_DUPLICATE = 1026;
constexpr int ID_DELETE = 1027;

constexpr int kToolbarH = 52;
constexpr int kStatusH = 24;
constexpr int kLeftW = 300;
constexpr int kRightW = 320;

HWND gMain{}, gViewport{}, gSearch{}, gAssetsList{}, gHierarchy{}, gStatus{};
HWND gBtnSelect{}, gBtnMove{}, gBtnRotate{}, gBtnScale{}, gSnap{};
HWND gEditX{}, gEditY{}, gEditZ{}, gEditRot{}, gEditScale{}, gApply{}, gDuplicate{}, gDelete{};
HFONT gFont{}, gFontBold{};
HBRUSH gPanelBrush{}, gEditBrush{};
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
ToolMode gTool = ToolMode::Select;
bool gDirty = false;
bool gDraggingObject = false;
bool gOrbiting = false;
bool gPanning = false;
bool gTransformDrag = false;
POINT gLastMouse{};
POINT gTransformStartMouse{};
cncgzh::SceneObject gTransformStart{};
float gCameraX = 0.0f, gCameraZ = 0.0f, gDistance = 18.0f, gYaw = 45.0f, gPitch = 52.0f;
int gViewportW = 1, gViewportH = 1;
GLdouble gModel[16]{}, gProj[16]{};
GLint gView[4]{};

fs::path executablePath() {
    std::wstring buffer(32768, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(len);
    return fs::path(buffer);
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
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

float snapValue(float v) {
    if (SendMessageW(gSnap, BM_GETCHECK, 0, 0) != BST_CHECKED) return v;
    constexpr float step = 0.25f;
    return std::round(v / step) * step;
}

void setStatus(const std::wstring& text) {
    if (gStatus) SetWindowTextW(gStatus, text.c_str());
}

void pushUndo() {
    gUndo.push_back(gObjects);
    if (gUndo.size() > 80) gUndo.erase(gUndo.begin());
}

void updateWindowTitle() {
    SetWindowTextW(gMain, gDirty ? L"CnCGZH Studio - Scene Editor *" : L"CnCGZH Studio - Scene Editor");
}

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
    std::sort(gAssets.begin(), gAssets.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
}

void refreshAssets() {
    wchar_t query[256]{};
    GetWindowTextW(gSearch, query, 256);
    std::wstring q = query;
    std::transform(q.begin(), q.end(), q.begin(), ::towlower);
    SendMessageW(gAssetsList, LB_RESETCONTENT, 0, 0);
    gFilteredAssets.clear();
    for (int i = 0; i < static_cast<int>(gAssets.size()); ++i) {
        std::wstring low = gAssets[i].name;
        std::transform(low.begin(), low.end(), low.begin(), ::towlower);
        if (!q.empty() && low.find(q) == std::wstring::npos) continue;
        gFilteredAssets.push_back(i);
        SendMessageW(gAssetsList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(gAssets[i].name.c_str()));
    }
    setStatus(std::to_wstring(gFilteredAssets.size()) + L" assets ready");
}

void refreshHierarchy() {
    SendMessageW(gHierarchy, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < static_cast<int>(gObjects.size()); ++i) {
        std::wstring n = fs::u8path(gObjects[i].asset).stem().wstring() + L"  #" + std::to_wstring(i + 1);
        SendMessageW(gHierarchy, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n.c_str()));
    }
    if (gSelected >= 0 && gSelected < static_cast<int>(gObjects.size()))
        SendMessageW(gHierarchy, LB_SETCURSEL, gSelected, 0);
}

void setFloatEdit(HWND edit, float value) {
    wchar_t buf[64]{};
    swprintf_s(buf, L"%.3f", value);
    SetWindowTextW(edit, buf);
}

bool readFloatEdit(HWND edit, float& value) {
    wchar_t buf[64]{};
    GetWindowTextW(edit, buf, 64);
    wchar_t* end = nullptr;
    double v = wcstod(buf, &end);
    if (end == buf || !std::isfinite(v)) return false;
    value = static_cast<float>(v);
    return true;
}

void enableInspector(bool enabled) {
    for (HWND h : {gEditX,gEditY,gEditZ,gEditRot,gEditScale,gApply,gDuplicate,gDelete}) EnableWindow(h, enabled);
}

void refreshInspector() {
    bool ok = gSelected >= 0 && gSelected < static_cast<int>(gObjects.size());
    enableInspector(ok);
    if (!ok) {
        for (HWND h : {gEditX,gEditY,gEditZ,gEditRot,gEditScale}) SetWindowTextW(h, L"");
        return;
    }
    const auto& o = gObjects[gSelected];
    setFloatEdit(gEditX, o.x);
    setFloatEdit(gEditY, o.y);
    setFloatEdit(gEditZ, o.z);
    setFloatEdit(gEditRot, o.rotationY);
    setFloatEdit(gEditScale, o.scale);
}

void updateToolButtons() {
    SendMessageW(gBtnSelect, BM_SETCHECK, gTool == ToolMode::Select ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(gBtnMove, BM_SETCHECK, gTool == ToolMode::Move ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(gBtnRotate, BM_SETCHECK, gTool == ToolMode::Rotate ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(gBtnScale, BM_SETCHECK, gTool == ToolMode::Scale ? BST_CHECKED : BST_UNCHECKED, 0);
}

void setTool(ToolMode tool) {
    gTool = tool;
    if (tool != ToolMode::Place) {
        gActiveAsset = -1;
        SendMessageW(gAssetsList, LB_SETCURSEL, -1, 0);
    }
    updateToolButtons();
    switch (tool) {
        case ToolMode::Select: setStatus(L"Select tool - click an object"); break;
        case ToolMode::Move: setStatus(L"Move tool - drag selected object on the ground"); break;
        case ToolMode::Rotate: setStatus(L"Rotate tool - drag horizontally"); break;
        case ToolMode::Scale: setStatus(L"Scale tool - drag vertically"); break;
        case ToolMode::Place: setStatus(L"Placement mode - click the viewport; Esc cancels"); break;
    }
}

ObjMesh& meshFor(const std::string& asset) {
    auto it = gMeshes.find(asset);
    if (it != gMeshes.end()) return it->second;
    ObjMesh m;
    m.load(gRoot / fs::u8path(asset));
    return gMeshes.emplace(asset, std::move(m)).first->second;
}

Vec3 normal(const Triangle& t) {
    Vec3 u{t.b.x-t.a.x,t.b.y-t.a.y,t.b.z-t.a.z};
    Vec3 v{t.c.x-t.a.x,t.c.y-t.a.y,t.c.z-t.a.z};
    Vec3 n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
    float l = std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
    if (l > 0.00001f) { n.x/=l; n.y/=l; n.z/=l; }
    return n;
}

void perspective(float fovY, float aspect, float zn, float zf) {
    constexpr double pi = 3.14159265358979323846;
    double top = zn * std::tan(fovY*pi/360.0);
    double right = top*aspect;
    glFrustum(-right,right,-top,top,zn,zf);
}

void colorFor(const std::string& name, bool selected) {
    if (selected) { glColor3f(0.35f,0.85f,0.52f); return; }
    unsigned h=2166136261u;
    for (unsigned char c:name){h^=c;h*=16777619u;}
    glColor3f(0.50f+((h>>0)&63)/220.0f,0.50f+((h>>8)&63)/220.0f,0.50f+((h>>16)&63)/220.0f);
}

void drawMesh(const ObjMesh& mesh) {
    glBegin(GL_TRIANGLES);
    for (const auto& t:mesh.triangles) {
        Vec3 n=normal(t);
        glNormal3f(n.x,n.y,n.z);
        glVertex3f(t.a.x,t.a.y,t.a.z);
        glVertex3f(t.b.x,t.b.y,t.b.z);
        glVertex3f(t.c.x,t.c.y,t.c.z);
    }
    glEnd();
}

void drawGrid() {
    glDisable(GL_LIGHTING);
    glLineWidth(1.0f);
    glColor3f(0.17f,0.19f,0.22f);
    glBegin(GL_LINES);
    for(int i=-50;i<=50;++i){
        glVertex3f((float)i,0,-50); glVertex3f((float)i,0,50);
        glVertex3f(-50,0,(float)i); glVertex3f(50,0,(float)i);
    }
    glEnd();
    glColor3f(0.62f,0.22f,0.22f);
    glBegin(GL_LINES); glVertex3f(-50,0.01f,0); glVertex3f(50,0.01f,0); glEnd();
    glColor3f(0.20f,0.45f,0.78f);
    glBegin(GL_LINES); glVertex3f(0,0.01f,-50); glVertex3f(0,0.01f,50); glEnd();
    glEnable(GL_LIGHTING);
}

void drawGizmo(const cncgzh::SceneObject& o) {
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glLineWidth(3.0f);
    glBegin(GL_LINES);
    glColor3f(0.95f,0.25f,0.25f);
    glVertex3f(o.x,o.y+0.05f,o.z); glVertex3f(o.x+2.0f,o.y+0.05f,o.z);
    glColor3f(0.25f,0.95f,0.35f);
    glVertex3f(o.x,o.y+0.05f,o.z); glVertex3f(o.x,o.y+2.0f,o.z);
    glColor3f(0.25f,0.50f,1.0f);
    glVertex3f(o.x,o.y+0.05f,o.z); glVertex3f(o.x,o.y+0.05f,o.z+2.0f);
    glEnd();
    glLineWidth(1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

void renderViewport() {
    if (!gGL || !gGLDC) return;
    wglMakeCurrent(gGLDC,gGL);
    glViewport(0,0,std::max(1,gViewportW),std::max(1,gViewportH));
    glClearColor(0.045f,0.050f,0.058f,1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    perspective(55.0f,(float)std::max(1,gViewportW)/std::max(1,gViewportH),0.1f,1000.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0,-3.0f,-gDistance);
    glRotatef(gPitch,1,0,0);
    glRotatef(gYaw,0,1,0);
    glTranslatef(-gCameraX,0,-gCameraZ);

    glGetDoublev(GL_MODELVIEW_MATRIX,gModel);
    glGetDoublev(GL_PROJECTION_MATRIX,gProj);
    glGetIntegerv(GL_VIEWPORT,gView);

    GLfloat lp[]={-15,30,-10,1}, amb[]={0.36f,0.36f,0.40f,1}, dif[]={0.88f,0.88f,0.84f,1};
    glLightfv(GL_LIGHT0,GL_POSITION,lp);
    glLightfv(GL_LIGHT0,GL_AMBIENT,amb);
    glLightfv(GL_LIGHT0,GL_DIFFUSE,dif);

    drawGrid();
    for(int i=0;i<(int)gObjects.size();++i){
        auto& o=gObjects[i];
        auto& m=meshFor(o.asset);
        glPushMatrix();
        glTranslatef(o.x,o.y,o.z);
        glRotatef(o.rotationY,0,1,0);
        glScalef(o.scale,o.scale,o.scale);
        colorFor(o.asset,i==gSelected);
        drawMesh(m);
        glPopMatrix();
    }
    if (gSelected >= 0 && gSelected < static_cast<int>(gObjects.size())) drawGizmo(gObjects[gSelected]);
    SwapBuffers(gGLDC);
}

bool groundFromScreen(int x,int y,float& wx,float& wz) {
    renderViewport();
    GLdouble x0{},y0{},z0{},x1{},y1{},z1{};
    double sy = static_cast<double>(gView[3] - 1 - y);
    if (gluUnProject((GLdouble)x,sy,0.0,gModel,gProj,gView,&x0,&y0,&z0) != GL_TRUE) return false;
    if (gluUnProject((GLdouble)x,sy,1.0,gModel,gProj,gView,&x1,&y1,&z1) != GL_TRUE) return false;
    double dy = y1-y0;
    if (std::abs(dy) < 1e-9) return false;
    double t = -y0/dy;
    if (!std::isfinite(t)) return false;
    wx = static_cast<float>(x0+(x1-x0)*t);
    wz = static_cast<float>(z0+(z1-z0)*t);
    return std::isfinite(wx) && std::isfinite(wz);
}

int pickObject(int x,int y) {
    int best=-1;
    double bestD=1600.0;
    for(int i=0;i<(int)gObjects.size();++i){
        GLdouble sx{},sy{},sz{};
        auto& o=gObjects[i];
        if(!gluProject(o.x,o.y+0.5f,o.z,gModel,gProj,gView,&sx,&sy,&sz)) continue;
        double dx=sx-x, dy=(gView[3]-sy)-y, d=dx*dx+dy*dy;
        if(d<bestD){bestD=d;best=i;}
    }
    return best;
}

void markChanged(const std::wstring& status = L"Scene modified") {
    gDirty=true;
    refreshHierarchy();
    refreshInspector();
    updateWindowTitle();
    setStatus(status);
    renderViewport();
}

void selectObject(int index) {
    gSelected = (index >= 0 && index < static_cast<int>(gObjects.size())) ? index : -1;
    refreshHierarchy();
    refreshInspector();
    renderViewport();
}

void placeActiveAt(float x, float z) {
    if(gActiveAsset<0 || gActiveAsset >= static_cast<int>(gAssets.size())) return;
    pushUndo();
    cncgzh::SceneObject o;
    o.asset=gAssets[gActiveAsset].relative;
    o.x=snapValue(x);
    o.y=0.0f;
    o.z=snapValue(z);
    o.scale=1.0f;
    gObjects.push_back(o);
    gSelected=static_cast<int>(gObjects.size())-1;
    markChanged(L"Placed " + gAssets[gActiveAsset].name);
}

void addActiveAssetAt(int x,int y) {
    float wx{},wz{};
    if(!groundFromScreen(x,y,wx,wz)) {
        wx=gCameraX;
        wz=gCameraZ;
    }
    placeActiveAt(wx,wz);
}

void applyInspector() {
    if (gSelected < 0 || gSelected >= static_cast<int>(gObjects.size())) return;
    float x,y,z,r,s;
    if (!readFloatEdit(gEditX,x) || !readFloatEdit(gEditY,y) || !readFloatEdit(gEditZ,z) ||
        !readFloatEdit(gEditRot,r) || !readFloatEdit(gEditScale,s)) {
        MessageBoxW(gMain,L"One or more transform values are invalid.",L"Transform",MB_ICONWARNING);
        return;
    }
    pushUndo();
    auto& o=gObjects[gSelected];
    o.x=snapValue(x); o.y=y; o.z=snapValue(z); o.rotationY=r; o.scale=std::clamp(s,0.01f,100.0f);
    markChanged(L"Transform applied");
}

void duplicateSelected() {
    if(gSelected<0 || gSelected >= static_cast<int>(gObjects.size())) return;
    pushUndo();
    auto o=gObjects[gSelected];
    o.x += 0.5f; o.z += 0.5f;
    gObjects.push_back(o);
    gSelected=static_cast<int>(gObjects.size())-1;
    markChanged(L"Object duplicated");
}

void deleteSelected() {
    if(gSelected<0 || gSelected >= static_cast<int>(gObjects.size())) return;
    pushUndo();
    gObjects.erase(gObjects.begin()+gSelected);
    gSelected=-1;
    markChanged(L"Object deleted");
}

void undo() {
    if(gUndo.empty()) return;
    gObjects=gUndo.back();
    gUndo.pop_back();
    gSelected=-1;
    markChanged(L"Undo");
}

bool saveScene() {
    if(!cncgzh::saveScene(gScenePath,gObjects)){
        MessageBoxW(gMain,L"Could not save the scene.",L"CnCGZH Studio",MB_ICONERROR);
        return false;
    }
    gDirty=false;
    updateWindowTitle();
    setStatus(L"Scene saved");
    return true;
}

void playScene() {
    if(!saveScene()) return;
    fs::path exe=executablePath().parent_path()/"CnCGZHEngine.exe";
    if(!fs::exists(exe)){
        MessageBoxW(gMain,L"CnCGZHEngine.exe was not found beside Studio.",L"Play",MB_ICONERROR);
        return;
    }
    std::wstring params=L"--scene \""+gScenePath.wstring()+L"\"";
    ShellExecuteW(gMain,L"open",exe.c_str(),params.c_str(),gRoot.c_str(),SW_SHOWNORMAL);
}

void updateLayout(HWND hwnd) {
    RECT r{}; GetClientRect(hwnd,&r);
    int w=r.right,h=r.bottom;
    int rightX=w-kRightW;

    MoveWindow(gSearch,10,kToolbarH+34,kLeftW-20,28,TRUE);
    MoveWindow(gAssetsList,10,kToolbarH+68,kLeftW-20,std::max(100,h-kToolbarH-kStatusH-78),TRUE);

    MoveWindow(gHierarchy,rightX+10,kToolbarH+34,kRightW-20,190,TRUE);

    int iy=kToolbarH+260;
    int labelX=rightX+14, editX=rightX+86;
    for (auto pair : std::vector<std::pair<HWND,int>>{{gEditX,iy},{gEditY,iy+34},{gEditZ,iy+68},{gEditRot,iy+102},{gEditScale,iy+136}})
        MoveWindow(pair.first,editX,pair.second,kRightW-104,26,TRUE);
    MoveWindow(gApply,rightX+14,iy+178,92,30,TRUE);
    MoveWindow(gDuplicate,rightX+112,iy+178,92,30,TRUE);
    MoveWindow(gDelete,rightX+210,iy+178,92,30,TRUE);

    MoveWindow(gViewport,kLeftW,kToolbarH,std::max(1,w-kLeftW-kRightW),std::max(1,h-kToolbarH-kStatusH),TRUE);
    MoveWindow(gStatus,0,h-kStatusH,w,kStatusH,TRUE);

    MoveWindow(GetDlgItem(hwnd,ID_SAVE),w-300,10,86,30,TRUE);
    MoveWindow(GetDlgItem(hwnd,ID_PLAY),w-208,10,86,30,TRUE);
    MoveWindow(GetDlgItem(hwnd,ID_CLEAR),w-116,10,86,30,TRUE);

    MoveWindow(gBtnSelect,kLeftW+10,10,76,30,TRUE);
    MoveWindow(gBtnMove,kLeftW+90,10,76,30,TRUE);
    MoveWindow(gBtnRotate,kLeftW+170,10,76,30,TRUE);
    MoveWindow(gBtnScale,kLeftW+250,10,76,30,TRUE);
    MoveWindow(gSnap,kLeftW+338,14,110,24,TRUE);
}

bool initGL(HWND hwnd) {
    gGLDC=GetDC(hwnd);
    PIXELFORMATDESCRIPTOR p{};
    p.nSize=sizeof(p); p.nVersion=1;
    p.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
    p.iPixelType=PFD_TYPE_RGBA; p.cColorBits=32; p.cDepthBits=24; p.iLayerType=PFD_MAIN_PLANE;
    int f=ChoosePixelFormat(gGLDC,&p);
    if(!f || !SetPixelFormat(gGLDC,f,&p)) return false;
    gGL=wglCreateContext(gGLDC);
    if(!gGL || !wglMakeCurrent(gGLDC,gGL)) return false;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);
    glShadeModel(GL_SMOOTH);
    return true;
}

LRESULT CALLBACK ViewportProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
        case WM_CREATE:
            if(!initGL(hwnd)) return -1;
            SetTimer(hwnd,1,16,nullptr);
            return 0;
        case WM_SIZE:
            gViewportW=LOWORD(lp); gViewportH=HIWORD(lp);
            return 0;
        case WM_TIMER:
            renderViewport();
            return 0;
        case WM_MOUSEWHEEL:
            gDistance*=GET_WHEEL_DELTA_WPARAM(wp)>0?0.90f:1.10f;
            gDistance=std::clamp(gDistance,3.0f,90.0f);
            return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            int x=GET_X_LPARAM(lp), y=GET_Y_LPARAM(lp);
            if(gTool==ToolMode::Place && gActiveAsset>=0){ addActiveAssetAt(x,y); return 0; }
            int hit=pickObject(x,y);
            if(hit>=0) selectObject(hit);
            else if(gTool==ToolMode::Select) selectObject(-1);
            if(gSelected>=0 && (gTool==ToolMode::Move || gTool==ToolMode::Rotate || gTool==ToolMode::Scale)) {
                pushUndo();
                gTransformDrag=true;
                gTransformStartMouse={x,y};
                gTransformStart=gObjects[gSelected];
                SetCapture(hwnd);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            int x=GET_X_LPARAM(lp), y=GET_Y_LPARAM(lp);
            if(gTransformDrag && gSelected>=0 && (wp&MK_LBUTTON)) {
                auto& o=gObjects[gSelected];
                if(gTool==ToolMode::Move) {
                    float wx{},wz{};
                    if(groundFromScreen(x,y,wx,wz)){ o.x=snapValue(wx); o.z=snapValue(wz); }
                } else if(gTool==ToolMode::Rotate) {
                    o.rotationY=gTransformStart.rotationY + (x-gTransformStartMouse.x)*0.5f;
                } else if(gTool==ToolMode::Scale) {
                    float factor=1.0f-(y-gTransformStartMouse.y)*0.01f;
                    o.scale=std::clamp(gTransformStart.scale*factor,0.01f,100.0f);
                }
                gDirty=true; refreshInspector(); updateWindowTitle();
            } else if(gOrbiting && (wp&MK_RBUTTON)) {
                gYaw+=(x-gLastMouse.x)*0.4f;
                gPitch=std::clamp(gPitch+(y-gLastMouse.y)*0.35f,18.0f,82.0f);
                gLastMouse={x,y};
            } else if(gPanning && (wp&MK_MBUTTON)) {
                float s=0.015f*gDistance;
                gCameraX-=(x-gLastMouse.x)*s;
                gCameraZ+=(y-gLastMouse.y)*s;
                gLastMouse={x,y};
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if(gTransformDrag){ gTransformDrag=false; ReleaseCapture(); markChanged(L"Transform changed"); }
            return 0;
        case WM_RBUTTONDOWN:
            gOrbiting=true; gLastMouse={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; SetCapture(hwnd); return 0;
        case WM_RBUTTONUP:
            gOrbiting=false; ReleaseCapture(); return 0;
        case WM_MBUTTONDOWN:
            gPanning=true; gLastMouse={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; SetCapture(hwnd); return 0;
        case WM_MBUTTONUP:
            gPanning=false; ReleaseCapture(); return 0;
        case WM_KEYDOWN:
            if(wp==VK_ESCAPE){ setTool(ToolMode::Select); return 0; }
            if((GetKeyState(VK_CONTROL)&0x8000) && wp=='Z'){ undo(); return 0; }
            if((GetKeyState(VK_CONTROL)&0x8000) && wp=='D'){ duplicateSelected(); return 0; }
            if(wp==VK_DELETE){ deleteSelected(); return 0; }
            if(wp=='Q'){ setTool(ToolMode::Select); return 0; }
            if(wp=='W'){ setTool(ToolMode::Move); return 0; }
            if(wp=='E'){ setTool(ToolMode::Rotate); return 0; }
            if(wp=='R'){ setTool(ToolMode::Scale); return 0; }
            if(gSelected>=0 && gSelected<(int)gObjects.size()) {
                float step=(GetKeyState(VK_SHIFT)&0x8000)?0.05f:0.25f;
                bool moved=false;
                pushUndo();
                if(wp==VK_LEFT){gObjects[gSelected].x-=step;moved=true;}
                if(wp==VK_RIGHT){gObjects[gSelected].x+=step;moved=true;}
                if(wp==VK_UP){gObjects[gSelected].z-=step;moved=true;}
                if(wp==VK_DOWN){gObjects[gSelected].z+=step;moved=true;}
                if(moved){markChanged(L"Object nudged");return 0;}
                gUndo.pop_back();
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; BeginPaint(hwnd,&ps); EndPaint(hwnd,&ps); renderViewport(); return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd,1);
            if(gGL){wglMakeCurrent(nullptr,nullptr);wglDeleteContext(gGL);gGL=nullptr;}
            if(gGLDC){ReleaseDC(hwnd,gGLDC);gGLDC=nullptr;}
            return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

void createLabel(HWND parent,const wchar_t* text,int id=0) {
    HWND h=CreateWindowW(L"STATIC",text,WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,60,22,parent,(HMENU)(INT_PTR)id,nullptr,nullptr);
    SendMessageW(h,WM_SETFONT,(WPARAM)gFont,TRUE);
}

LRESULT CALLBACK MainProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
        case WM_CREATE: {
            gMain=hwnd;
            gRoot=findRoot();
            gScenePath=gRoot/"projects"/"sample-rts"/"main.scene";
            scanAssets();
            cncgzh::loadScene(gScenePath,gObjects);

            gFont=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            gFontBold=CreateFontW(-16,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            gPanelBrush=CreateSolidBrush(RGB(31,33,37));
            gEditBrush=CreateSolidBrush(RGB(42,45,50));

            CreateWindowW(L"BUTTON",L"Save",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,90,30,hwnd,(HMENU)ID_SAVE,nullptr,nullptr);
            CreateWindowW(L"BUTTON",L"PLAY",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,90,30,hwnd,(HMENU)ID_PLAY,nullptr,nullptr);
            CreateWindowW(L"BUTTON",L"Clear",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,90,30,hwnd,(HMENU)ID_CLEAR,nullptr,nullptr);

            gBtnSelect=CreateWindowW(L"BUTTON",L"Select [Q]",WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON|BS_PUSHLIKE,0,0,0,0,hwnd,(HMENU)ID_TOOL_SELECT,nullptr,nullptr);
            gBtnMove=CreateWindowW(L"BUTTON",L"Move [W]",WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON|BS_PUSHLIKE,0,0,0,0,hwnd,(HMENU)ID_TOOL_MOVE,nullptr,nullptr);
            gBtnRotate=CreateWindowW(L"BUTTON",L"Rotate [E]",WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON|BS_PUSHLIKE,0,0,0,0,hwnd,(HMENU)ID_TOOL_ROTATE,nullptr,nullptr);
            gBtnScale=CreateWindowW(L"BUTTON",L"Scale [R]",WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON|BS_PUSHLIKE,0,0,0,0,hwnd,(HMENU)ID_TOOL_SCALE,nullptr,nullptr);
            gSnap=CreateWindowW(L"BUTTON",L"Grid snap 0.25",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,0,0,0,0,hwnd,(HMENU)ID_SNAP,nullptr,nullptr);
            SendMessageW(gSnap,BM_SETCHECK,BST_CHECKED,0);

            gSearch=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,hwnd,(HMENU)ID_SEARCH,nullptr,nullptr);
            gAssetsList=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|LBS_NOTIFY|WS_VSCROLL,0,0,0,0,hwnd,(HMENU)ID_ASSETS,nullptr,nullptr);
            gHierarchy=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|LBS_NOTIFY|WS_VSCROLL,0,0,0,0,hwnd,(HMENU)ID_HIERARCHY,nullptr,nullptr);

            gEditX=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,hwnd,(HMENU)ID_X,nullptr,nullptr);
            gEditY=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,hwnd,(HMENU)ID_Y,nullptr,nullptr);
            gEditZ=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,hwnd,(HMENU)ID_Z,nullptr,nullptr);
            gEditRot=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,hwnd,(HMENU)ID_ROT,nullptr,nullptr);
            gEditScale=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,hwnd,(HMENU)ID_SCALE,nullptr,nullptr);
            gApply=CreateWindowW(L"BUTTON",L"Apply",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,0,0,hwnd,(HMENU)ID_APPLY,nullptr,nullptr);
            gDuplicate=CreateWindowW(L"BUTTON",L"Duplicate",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,0,0,hwnd,(HMENU)ID_DUPLICATE,nullptr,nullptr);
            gDelete=CreateWindowW(L"BUTTON",L"Delete",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,0,0,hwnd,(HMENU)ID_DELETE,nullptr,nullptr);

            gStatus=CreateWindowW(L"STATIC",L"Ready",WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,0,0,hwnd,(HMENU)ID_STATUS,nullptr,nullptr);
            gViewport=CreateWindowExW(0,L"CnCGZHViewport",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,0,0,100,100,hwnd,nullptr,nullptr,nullptr);

            EnumChildWindows(hwnd,[](HWND c,LPARAM f)->BOOL{SendMessageW(c,WM_SETFONT,(WPARAM)f,TRUE);return TRUE;},(LPARAM)gFont);
            refreshAssets();
            refreshHierarchy();
            refreshInspector();
            updateToolButtons();
            updateLayout(hwnd);
            setStatus(L"Ready - select an asset to place it, or choose a transform tool");
            return 0;
        }
        case WM_SIZE:
            updateLayout(hwnd);
            return 0;
        case WM_COMMAND: {
            int id=LOWORD(wp), code=HIWORD(wp);
            if(id==ID_SAVE){saveScene();return 0;}
            if(id==ID_PLAY){playScene();return 0;}
            if(id==ID_CLEAR){
                if(!gObjects.empty()&&MessageBoxW(hwnd,L"Clear the entire scene?",L"CnCGZH Studio",MB_YESNO|MB_ICONQUESTION)!=IDYES)return 0;
                pushUndo();gObjects.clear();gSelected=-1;markChanged(L"Scene cleared");return 0;
            }
            if(id==ID_TOOL_SELECT){setTool(ToolMode::Select);SetFocus(gViewport);return 0;}
            if(id==ID_TOOL_MOVE){setTool(ToolMode::Move);SetFocus(gViewport);return 0;}
            if(id==ID_TOOL_ROTATE){setTool(ToolMode::Rotate);SetFocus(gViewport);return 0;}
            if(id==ID_TOOL_SCALE){setTool(ToolMode::Scale);SetFocus(gViewport);return 0;}
            if(id==ID_APPLY){applyInspector();SetFocus(gViewport);return 0;}
            if(id==ID_DUPLICATE){duplicateSelected();SetFocus(gViewport);return 0;}
            if(id==ID_DELETE){deleteSelected();SetFocus(gViewport);return 0;}
            if(id==ID_SEARCH&&code==EN_CHANGE){refreshAssets();return 0;}
            if(id==ID_ASSETS&&code==LBN_SELCHANGE){
                int row=(int)SendMessageW(gAssetsList,LB_GETCURSEL,0,0);
                if(row>=0&&row<(int)gFilteredAssets.size()){
                    gActiveAsset=gFilteredAssets[row];
                    gTool=ToolMode::Place;
                    updateToolButtons();
                    setStatus(L"Placement mode - click the 3D viewport; Esc cancels");
                    SetFocus(gViewport);
                }
                return 0;
            }
            if(id==ID_ASSETS&&code==LBN_DBLCLK){
                int row=(int)SendMessageW(gAssetsList,LB_GETCURSEL,0,0);
                if(row>=0&&row<(int)gFilteredAssets.size()){
                    gActiveAsset=gFilteredAssets[row];
                    gTool=ToolMode::Place;
                    placeActiveAt(gCameraX,gCameraZ);
                }
                return 0;
            }
            if(id==ID_HIERARCHY&&code==LBN_SELCHANGE){
                int row=(int)SendMessageW(gHierarchy,LB_GETCURSEL,0,0);
                selectObject(row);
                setTool(ToolMode::Select);
                return 0;
            }
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc=(HDC)wp;
            SetTextColor(dc,RGB(226,230,235));
            SetBkColor(dc,RGB(31,33,37));
            if((HWND)lp==gSearch || (HWND)lp==gEditX || (HWND)lp==gEditY || (HWND)lp==gEditZ || (HWND)lp==gEditRot || (HWND)lp==gEditScale)
                return (LRESULT)gEditBrush;
            return (LRESULT)gPanelBrush;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps);
            RECT r{};GetClientRect(hwnd,&r);
            HBRUSH bg=CreateSolidBrush(RGB(24,26,30)); FillRect(dc,&r,bg); DeleteObject(bg);
            SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(235,238,242));
            SelectObject(dc,gFontBold);
            TextOutW(dc,12,16,L"CnCGZH Studio",13);
            TextOutW(dc,12,kToolbarH+10,L"ASSETS",6);
            TextOutW(dc,r.right-kRightW+10,kToolbarH+10,L"SCENE",5);
            TextOutW(dc,r.right-kRightW+10,kToolbarH+236,L"TRANSFORM",9);
            SelectObject(dc,gFont);
            int rx=r.right-kRightW+14, iy=kToolbarH+267;
            TextOutW(dc,rx,iy+4,L"X",1); TextOutW(dc,rx,iy+38,L"Y",1); TextOutW(dc,rx,iy+72,L"Z",1);
            TextOutW(dc,rx,iy+106,L"Rotation",8); TextOutW(dc,rx,iy+140,L"Scale",5);
            EndPaint(hwnd,&ps); return 0;
        }
        case WM_CLOSE:
            if(gDirty&&MessageBoxW(hwnd,L"Close without saving changes?",L"CnCGZH Studio",MB_YESNO|MB_ICONWARNING)!=IDYES)return 0;
            DestroyWindow(hwnd); return 0;
        case WM_DESTROY:
            if(gFont)DeleteObject(gFont);
            if(gFontBold)DeleteObject(gFontBold);
            if(gPanelBrush)DeleteObject(gPanelBrush);
            if(gEditBrush)DeleteObject(gEditBrush);
            PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&ic);

    WNDCLASSW vw{};
    vw.style=CS_OWNDC|CS_HREDRAW|CS_VREDRAW;
    vw.lpfnWndProc=ViewportProc;
    vw.hInstance=instance;
    vw.lpszClassName=L"CnCGZHViewport";
    vw.hCursor=LoadCursor(nullptr,IDC_CROSS);
    RegisterClassW(&vw);

    WNDCLASSW wc{};
    wc.lpfnWndProc=MainProc;
    wc.hInstance=instance;
    wc.lpszClassName=L"CnCGZHStudioWindow";
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 1;

    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"CnCGZH Studio - Scene Editor",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,1600,950,nullptr,nullptr,instance,nullptr);
    if(!hwnd)return 2;
    ShowWindow(hwnd,show);
    UpdateWindow(hwnd);

    MSG msg{};
    while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    return (int)msg.wParam;
}
