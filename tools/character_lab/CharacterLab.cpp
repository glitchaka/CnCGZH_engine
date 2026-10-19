#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct CharacterEntry {
    std::string name;
    fs::path model;
    fs::path packRoot;
};

struct AnimationSet {
    ModelAnimation* animations{};
    int count{};
    fs::path source;
};

struct AnimationRef {
    int set{};
    int index{};
    std::string name;
};

struct RectButton {
    Rectangle rect{};
    std::string text;
};

fs::path executablePath() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(len);
    return fs::path(buffer);
#else
    return fs::current_path();
#endif
}

fs::path findRoot() {
    fs::path p = executablePath().parent_path();
    for (int i = 0; i < 9 && !p.empty(); ++i) {
        if (fs::exists(p / "assets" / "sample" / "kaykit-characters") && fs::exists(p / "projects")) return p;
        if (fs::exists(p / "engine") && fs::exists(p / "projects")) return p;
        p = p.parent_path();
    }
    return fs::current_path();
}

std::string pathUtf8(const fs::path& p) {
#ifdef _WIN32
    std::wstring w = p.wstring();
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
#else
    return p.string();
#endif
}

std::vector<CharacterEntry> scanCharacters(const fs::path& root) {
    std::vector<CharacterEntry> out;
    const fs::path base = root / "assets" / "sample" / "kaykit-characters";
    const std::vector<fs::path> packs = {base / "adventurers-2.0", base / "skeletons-1.1"};
    for (const auto& pack : packs) {
        if (!fs::exists(pack)) continue;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(pack, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (!it->is_regular_file(ec) || it->path().extension() != ".glb") continue;
            std::string generic = it->path().generic_string();
            if (generic.find("/Characters/gltf/") == std::string::npos && generic.find("/characters/gltf/") == std::string::npos) continue;
            out.push_back({it->path().stem().string(), it->path(), pack});
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    return out;
}

fs::path profilePath(const fs::path& root, const CharacterEntry& c) {
    return root / "projects" / "sample-rts" / "characters" / (c.name + ".profile");
}

std::map<std::string,std::string> loadProfile(const fs::path& root, const CharacterEntry& c) {
    std::map<std::string,std::string> out;
    std::ifstream in(profilePath(root,c));
    std::string line;
    while (std::getline(in,line)) {
        auto p=line.find('=');
        if (p==std::string::npos) continue;
        out[line.substr(0,p)] = line.substr(p+1);
    }
    return out;
}

bool saveProfile(const fs::path& root, const CharacterEntry& c, const std::map<std::string,std::string>& map) {
    std::error_code ec;
    fs::create_directories(profilePath(root,c).parent_path(), ec);
    std::ofstream out(profilePath(root,c), std::ios::binary);
    if (!out) return false;
    out << "CNCGZH_CHARACTER_PROFILE_V1\n";
    for (const auto& [action, anim] : map) out << action << '=' << anim << '\n';
    return static_cast<bool>(out);
}

bool clicked(Rectangle r) {
    return IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r);
}

void drawButton(const RectButton& b, bool active=false) {
    Color fill = active ? Color{62,118,168,255} : Color{51,55,62,255};
    if (CheckCollisionPointRec(GetMousePosition(), b.rect)) fill = active ? Color{75,137,191,255} : Color{67,72,80,255};
    DrawRectangleRounded(b.rect,0.12f,5,fill);
    DrawRectangleRoundedLinesEx(b.rect,0.12f,5,1,Color{88,94,104,255});
    int tw=MeasureText(b.text.c_str(),16);
    DrawText(b.text.c_str(),(int)(b.rect.x+(b.rect.width-tw)/2),(int)(b.rect.y+8),16,RAYWHITE);
}

void unloadAnimationSets(std::vector<AnimationSet>& sets) {
    for (auto& s : sets) if (s.animations) UnloadModelAnimations(s.animations, s.count);
    sets.clear();
}

void loadAnimationsFor(const CharacterEntry& c, Model model, std::vector<AnimationSet>& sets, std::vector<AnimationRef>& refs) {
    unloadAnimationSets(sets);
    refs.clear();
    fs::path animRoot = c.packRoot / "Animations" / "gltf" / "Rig_Medium";
    if (!fs::exists(animRoot)) return;
    std::vector<fs::path> sources;
    for (const auto& e : fs::directory_iterator(animRoot)) if (e.is_regular_file() && e.path().extension()==".glb") sources.push_back(e.path());
    std::sort(sources.begin(),sources.end());
    for (const auto& src : sources) {
        int count=0;
        std::string path=pathUtf8(src);
        ModelAnimation* a=LoadModelAnimations(path.c_str(),&count);
        if (!a || count<=0) continue;
        int setIndex=(int)sets.size();
        sets.push_back({a,count,src});
        for (int i=0;i<count;++i) {
            if (!IsModelAnimationValid(model,a[i])) continue;
            refs.push_back({setIndex,i,a[i].name[0] ? a[i].name : (src.stem().string()+" #"+std::to_string(i+1))});
        }
    }
}

ModelAnimation* animationAt(std::vector<AnimationSet>& sets, const std::vector<AnimationRef>& refs, int selected) {
    if (selected<0 || selected>=(int)refs.size()) return nullptr;
    auto r=refs[selected];
    if (r.set<0 || r.set>=(int)sets.size() || r.index<0 || r.index>=sets[r.set].count) return nullptr;
    return &sets[r.set].animations[r.index];
}

Camera3D makeCamera(float yaw, float pitch, float distance) {
    float yr=yaw*DEG2RAD, pr=pitch*DEG2RAD;
    Vector3 target{0.0f,1.0f,0.0f};
    Vector3 pos{
        target.x + distance*std::cos(pr)*std::sin(yr),
        target.y + distance*std::sin(pr),
        target.z + distance*std::cos(pr)*std::cos(yr)
    };
    return Camera3D{pos,target,{0,1,0},45.0f,CAMERA_PERSPECTIVE};
}

} // namespace

int main(int argc, char** argv) {
    fs::path root=findRoot();
    auto characters=scanCharacters(root);
    if (characters.empty()) {
        MessageBoxA(nullptr,"KayKit Adventurers 2.0 / Skeletons 1.1 were not found. Build the project once so the sample assets are prepared.","CnCGZH Character Lab",MB_ICONERROR);
        return 2;
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE|FLAG_MSAA_4X_HINT);
    InitWindow(1500,900,"CnCGZH Character Lab");
    SetTargetFPS(60);

    int characterIndex=0;
    if (argc>1) {
        std::string wanted=argv[1];
        for (int i=0;i<(int)characters.size();++i) if (characters[i].name==wanted || characters[i].model.string()==wanted) { characterIndex=i; break; }
    }

    Model model{};
    bool modelLoaded=false;
    std::vector<AnimationSet> sets;
    std::vector<AnimationRef> animations;
    std::map<std::string,std::string> actionMap;
    int animIndex=0;
    float frame=0.0f;
    float speed=1.0f;
    bool playing=true;
    bool loop=true;
    int charScroll=0, animScroll=0;
    float yaw=35.0f,pitch=20.0f,distance=5.0f;
    Vector2 orbitStart{};
    bool orbiting=false;
    int actionIndex=0;
    const std::vector<std::string> actions={"Idle","Move","Run","Attack","Interact","PickUp","UseItem","Hit","Death","Spawn"};
    std::string status="Ready";

    auto loadCharacter=[&]() {
        if (modelLoaded) UnloadModel(model);
        unloadAnimationSets(sets);
        std::string path=pathUtf8(characters[characterIndex].model);
        model=LoadModel(path.c_str());
        modelLoaded=IsModelValid(model);
        if (modelLoaded) loadAnimationsFor(characters[characterIndex],model,sets,animations);
        actionMap=loadProfile(root,characters[characterIndex]);
        animIndex=0;frame=0;playing=true;
        status=modelLoaded ? (std::to_string(animations.size())+" compatible animations loaded") : "Could not load character model";
    };
    loadCharacter();

    while (!WindowShouldClose()) {
        int w=GetScreenWidth(),h=GetScreenHeight();
        const float leftW=250,rightW=330,topH=58,bottomH=38;
        Rectangle viewport{leftW,topH,(float)w-leftW-rightW,(float)h-topH-bottomH};

        Vector2 mouse=GetMousePosition();
        if (CheckCollisionPointRec(mouse,viewport)) {
            float wheel=GetMouseWheelMove();
            if (wheel!=0) distance=std::clamp(distance-wheel*0.45f,1.5f,15.0f);
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {orbiting=true;orbitStart=mouse;}
            if (orbiting && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                Vector2 d=Vector2Subtract(mouse,orbitStart); orbitStart=mouse;
                yaw-=d.x*0.35f; pitch=std::clamp(pitch+d.y*0.30f,-10.0f,75.0f);
            }
            if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) orbiting=false;
        }

        if (playing && modelLoaded) {
            ModelAnimation* a=animationAt(sets,animations,animIndex);
            if (a && a->keyframeCount>0) {
                frame += GetFrameTime()*30.0f*speed;
                if (frame>=a->keyframeCount) frame=loop ? std::fmod(frame,(float)a->keyframeCount) : (float)a->keyframeCount-1;
                UpdateModelAnimation(model,*a,frame);
            }
        }

        BeginDrawing();
        ClearBackground(Color{21,23,27,255});

        DrawRectangle(0,0,w,(int)topH,Color{28,30,35,255});
        DrawText("CnCGZH Character Lab",18,17,22,RAYWHITE);

        RectButton play{{leftW+16,12,90,34},playing?"Pause":"Play"};
        RectButton prev{{leftW+114,12,42,34},"<"};
        RectButton next{{leftW+162,12,42,34},">"};
        RectButton slower{{leftW+214,12,42,34},"-"};
        RectButton faster{{leftW+262,12,42,34},"+"};
        drawButton(play);drawButton(prev);drawButton(next);drawButton(slower);drawButton(faster);
        if(clicked(play.rect))playing=!playing;
        if(clicked(prev.rect)&&!animations.empty()){animIndex=(animIndex+(int)animations.size()-1)%(int)animations.size();frame=0;}
        if(clicked(next.rect)&&!animations.empty()){animIndex=(animIndex+1)%(int)animations.size();frame=0;}
        if(clicked(slower.rect))speed=std::max(0.1f,speed-0.1f);
        if(clicked(faster.rect))speed=std::min(3.0f,speed+0.1f);
        DrawText(TextFormat("Speed %.1fx",speed),leftW+320,20,17,Color{205,210,218,255});

        DrawRectangle(0,(int)topH,(int)leftW,h-(int)topH-(int)bottomH,Color{31,33,38,255});
        DrawText("CHARACTERS",14,(int)topH+12,16,Color{180,187,198,255});
        int charY=(int)topH+40;
        int visibleChars=std::max(1,(h-(int)topH-(int)bottomH-52)/32);
        if (CheckCollisionPointRec(mouse,{0,topH,leftW,(float)h-topH-bottomH})) {
            int wheel=(int)GetMouseWheelMove();
            if(wheel)charScroll=std::clamp(charScroll-wheel,0,std::max(0,(int)characters.size()-visibleChars));
        }
        for(int row=0;row<visibleChars && row+charScroll<(int)characters.size();++row){
            int idx=row+charScroll;Rectangle r{8,(float)(charY+row*32),leftW-16,28};
            Color c=idx==characterIndex?Color{63,108,151,255}:Color{41,44,50,255};
            if(CheckCollisionPointRec(mouse,r))c=Color{55,60,68,255};
            DrawRectangleRec(r,c);DrawText(characters[idx].name.c_str(),16,(int)r.y+5,16,RAYWHITE);
            if(clicked(r)&&idx!=characterIndex){characterIndex=idx;loadCharacter();}
        }

        DrawRectangle(w-(int)rightW,(int)topH,(int)rightW,h-(int)topH-(int)bottomH,Color{31,33,38,255});
        int rx=w-(int)rightW+12;
        DrawText("ANIMATIONS",rx,(int)topH+12,16,Color{180,187,198,255});
        int animY=(int)topH+38;
        int animPanelH=300;
        int visibleAnims=animPanelH/27;
        if (CheckCollisionPointRec(mouse,{(float)(w-rightW),topH,rightW,(float)animPanelH})) {
            int wheel=(int)GetMouseWheelMove();
            if(wheel)animScroll=std::clamp(animScroll-wheel,0,std::max(0,(int)animations.size()-visibleAnims));
        }
        for(int row=0;row<visibleAnims && row+animScroll<(int)animations.size();++row){
            int idx=row+animScroll;Rectangle r{(float)rx,(float)(animY+row*27),rightW-24,24};
            Color c=idx==animIndex?Color{76,117,72,255}:Color{41,44,50,255};
            if(CheckCollisionPointRec(mouse,r))c=Color{55,60,68,255};
            DrawRectangleRec(r,c);DrawText(animations[idx].name.c_str(),rx+7,(int)r.y+4,15,RAYWHITE);
            if(clicked(r)){animIndex=idx;frame=0;playing=true;}
        }

        int actionY=(int)topH+355;
        DrawText("ACTION MENU",rx,actionY,16,Color{180,187,198,255});
        actionY+=26;
        for(int i=0;i<(int)actions.size();++i){
            Rectangle r{(float)rx,(float)(actionY+i*27),96,23};
            Color c=i==actionIndex?Color{122,88,48,255}:Color{41,44,50,255};
            DrawRectangleRec(r,c);DrawText(actions[i].c_str(),rx+6,(int)r.y+4,14,RAYWHITE);
            if(clicked(r))actionIndex=i;
            auto it=actionMap.find(actions[i]);
            std::string mapped=it==actionMap.end()?"-":it->second;
            DrawText(mapped.c_str(),rx+106,(int)r.y+4,14,Color{192,198,207,255});
        }
        Rectangle assign{(float)rx,(float)(actionY+(int)actions.size()*27+8),rightW-24,32};
        DrawRectangleRounded(assign,0.12f,5,Color{63,108,151,255});
        DrawText("Assign current animation",rx+14,(int)assign.y+8,16,RAYWHITE);
        if(clicked(assign)&&!animations.empty()){
            actionMap[actions[actionIndex]]=animations[animIndex].name;
            if(saveProfile(root,characters[characterIndex],actionMap))status=actions[actionIndex]+" = "+animations[animIndex].name;
            else status="Could not save character profile";
        }

        BeginScissorMode((int)viewport.x,(int)viewport.y,(int)viewport.width,(int)viewport.height);
        Camera3D camera=makeCamera(yaw,pitch,distance);
        BeginMode3D(camera);
        DrawGrid(20,1.0f);
        if(modelLoaded)DrawModel(model,{0,0,0},1.0f,WHITE);
        EndMode3D();
        EndScissorMode();
        DrawRectangleLinesEx(viewport,1,Color{70,75,83,255});

        if(!animations.empty()){
            ModelAnimation* a=animationAt(sets,animations,animIndex);
            std::string label=animations[animIndex].name;
            DrawText(label.c_str(),(int)viewport.x+16,(int)viewport.y+14,20,RAYWHITE);
            if(a)DrawText(TextFormat("Frame %.1f / %d",frame,a->keyframeCount),(int)viewport.x+16,(int)viewport.y+40,15,Color{190,196,205,255});
        }
        DrawText("RMB orbit | Wheel zoom | Select animation on the right",(int)viewport.x+16,(int)(viewport.y+viewport.height-28),15,Color{170,177,187,255});

        DrawRectangle(0,h-(int)bottomH,w,(int)bottomH,Color{26,28,32,255});
        DrawText(status.c_str(),12,h-27,15,Color{205,211,220,255});
        DrawText(("Profile: "+pathUtf8(profilePath(root,characters[characterIndex]))).c_str(),w-620,h-27,14,Color{145,152,162,255});

        EndDrawing();
    }

    unloadAnimationSets(sets);
    if(modelLoaded)UnloadModel(model);
    CloseWindow();
    return 0;
}
