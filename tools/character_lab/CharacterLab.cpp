#include <windows.h>
#include <shellapi.h>
#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct CharacterEntry { std::string name; fs::path model; fs::path packRoot; };
struct AnimationSet { ModelAnimation* animations{}; int count{}; };
struct AnimationRef { int set{}; int index{}; std::string name; };

fs::path exePath() {
    std::wstring b(32768,L'\0');
    DWORD n=GetModuleFileNameW(nullptr,b.data(),(DWORD)b.size());
    b.resize(n);
    return fs::path(b);
}

fs::path findRoot() {
    fs::path p=exePath().parent_path();
    for(int i=0;i<9&&!p.empty();++i){
        if(fs::exists(p/"assets"/"sample"/"kaykit-characters")&&fs::exists(p/"projects"))return p;
        if(fs::exists(p/"engine")&&fs::exists(p/"projects"))return p;
        p=p.parent_path();
    }
    return fs::current_path();
}

std::string utf8(const fs::path& p) {
    std::wstring w=p.wstring();
    if(w.empty())return{};
    int n=WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),nullptr,0,nullptr,nullptr);
    std::string s(n,'\0');
    WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),s.data(),n,nullptr,nullptr);
    return s;
}

std::vector<CharacterEntry> scanCharacters(const fs::path& root) {
    std::vector<CharacterEntry> out;
    fs::path base=root/"assets"/"sample"/"kaykit-characters";
    for(const fs::path& pack:{base/"adventurers-2.0",base/"skeletons-1.1"}){
        if(!fs::exists(pack))continue;
        std::error_code ec;
        for(fs::recursive_directory_iterator it(pack,fs::directory_options::skip_permission_denied,ec),end;it!=end;it.increment(ec)){
            if(ec){ec.clear();continue;}
            if(!it->is_regular_file(ec)||it->path().extension()!=L".glb")continue;
            std::string s=it->path().generic_string();
            if(s.find("/Characters/gltf/")==std::string::npos&&s.find("/characters/gltf/")==std::string::npos)continue;
            out.push_back({it->path().stem().string(),it->path(),pack});
        }
    }
    std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.name<b.name;});
    return out;
}

fs::path profilePath(const fs::path& root,const CharacterEntry& c){return root/"projects"/"sample-rts"/"characters"/(c.name+".profile");}

std::map<std::string,std::string> loadProfile(const fs::path& root,const CharacterEntry& c){
    std::map<std::string,std::string> m;std::ifstream in(profilePath(root,c));std::string line;
    while(std::getline(in,line)){auto p=line.find('=');if(p!=std::string::npos&&line.rfind("CNCGZH_",0)!=0)m[line.substr(0,p)]=line.substr(p+1);}
    return m;
}

bool saveProfile(const fs::path& root,const CharacterEntry& c,const std::map<std::string,std::string>& m){
    std::error_code ec;fs::create_directories(profilePath(root,c).parent_path(),ec);
    std::ofstream out(profilePath(root,c),std::ios::binary);if(!out)return false;
    out<<"CNCGZH_CHARACTER_PROFILE_V1\n";for(auto&[a,v]:m)out<<a<<'='<<v<<'\n';return(bool)out;
}

void unloadSets(std::vector<AnimationSet>& sets){for(auto&s:sets)if(s.animations)UnloadModelAnimations(s.animations,s.count);sets.clear();}

void loadAnimations(const CharacterEntry& c,Model model,std::vector<AnimationSet>& sets,std::vector<AnimationRef>& refs){
    unloadSets(sets);refs.clear();
    fs::path dir=c.packRoot/"Animations"/"gltf"/"Rig_Medium";if(!fs::exists(dir))return;
    std::vector<fs::path> files;for(auto&e:fs::directory_iterator(dir))if(e.is_regular_file()&&e.path().extension()==L".glb")files.push_back(e.path());
    std::sort(files.begin(),files.end());
    for(auto&f:files){int count=0;std::string p=utf8(f);ModelAnimation*a=LoadModelAnimations(p.c_str(),&count);if(!a||count<=0)continue;int si=(int)sets.size();sets.push_back({a,count});for(int i=0;i<count;++i)if(IsModelAnimationValid(model,a[i]))refs.push_back({si,i,a[i].name[0]?a[i].name:(f.stem().string()+" #"+std::to_string(i+1))});}
}

ModelAnimation* currentAnimation(std::vector<AnimationSet>& sets,const std::vector<AnimationRef>& refs,int i){
    if(i<0||i>=(int)refs.size())return nullptr;auto r=refs[i];if(r.set<0||r.set>=(int)sets.size()||r.index<0||r.index>=sets[r.set].count)return nullptr;return &sets[r.set].animations[r.index];
}

bool click(Rectangle r){return IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(GetMousePosition(),r);}

void button(Rectangle r,const char* text,bool active=false){
    Color c=active?Color{62,118,168,255}:Color{49,53,60,255};if(CheckCollisionPointRec(GetMousePosition(),r))c=Color{70,76,86,255};
    DrawRectangleRec(r,c);DrawRectangleLinesEx(r,1,Color{86,92,103,255});int tw=MeasureText(text,16);DrawText(text,(int)(r.x+(r.width-tw)/2),(int)r.y+8,16,RAYWHITE);
}

Camera3D cameraFor(float yaw,float pitch,float distance){
    float yr=yaw*DEG2RAD,pr=pitch*DEG2RAD;Vector3 target{0,1,0};
    Vector3 pos{distance*std::cos(pr)*std::sin(yr),1+distance*std::sin(pr),distance*std::cos(pr)*std::cos(yr)};
    return Camera3D{pos,target,{0,1,0},45,CAMERA_PERSPECTIVE};
}

} // namespace

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    fs::path root=findRoot();auto chars=scanCharacters(root);
    if(chars.empty()){MessageBoxW(nullptr,L"No se encontraron KayKit Adventurers 2.0 y Skeletons 1.1. Compila el proyecto una vez para preparar los recursos de muestra.",L"CnCGZH Laboratorio de personajes",MB_ICONERROR);return 2;}

    SetConfigFlags(FLAG_WINDOW_RESIZABLE|FLAG_MSAA_4X_HINT);InitWindow(1500,900,"CnCGZH Laboratorio de personajes");SetTargetFPS(60);

    int charIndex=0,animIndex=0,actionIndex=0,charScroll=0,animScroll=0;float frame=0,speed=1,yaw=35,pitch=18,distance=5;bool playing=true,loop=true,orbit=false;Vector2 last{};
    const std::vector<std::string> actions={"Idle","Move","Run","Attack","Interact","PickUp","UseItem","Hit","Death","Spawn"};
    const std::vector<std::string> actionLabels={"Reposo","Mover","Correr","Atacar","Interactuar","Recoger","Usar objeto","Recibir golpe","Morir","Aparecer"};
    Model model{};bool modelLoaded=false;std::vector<AnimationSet>sets;std::vector<AnimationRef>refs;std::map<std::string,std::string>mapping;std::string status="Listo";

    auto loadCharacter=[&](){
        if(modelLoaded)UnloadModel(model);unloadSets(sets);refs.clear();std::string p=utf8(chars[charIndex].model);model=LoadModel(p.c_str());modelLoaded=model.meshCount>0;
        if(modelLoaded)loadAnimations(chars[charIndex],model,sets,refs);mapping=loadProfile(root,chars[charIndex]);animIndex=0;frame=0;playing=true;animScroll=0;
        status=modelLoaded?std::to_string(refs.size())+" animaciones cargadas":"No se pudo cargar el modelo";
    };loadCharacter();

    while(!WindowShouldClose()){
        int w=GetScreenWidth(),h=GetScreenHeight();float left=250,right=350,top=58,bottom=34;Rectangle view{left,top,(float)w-left-right,(float)h-top-bottom};Vector2 mouse=GetMousePosition();
        if(CheckCollisionPointRec(mouse,view)){float wh=GetMouseWheelMove();if(wh)distance=std::clamp(distance-wh*.4f,1.5f,15.0f);if(IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)){orbit=true;last=mouse;}if(orbit&&IsMouseButtonDown(MOUSE_BUTTON_RIGHT)){yaw-=(mouse.x-last.x)*.35f;pitch=std::clamp(pitch+(mouse.y-last.y)*.3f,-8.0f,75.0f);last=mouse;}if(IsMouseButtonReleased(MOUSE_BUTTON_RIGHT))orbit=false;}
        if(playing&&modelLoaded){if(auto*a=currentAnimation(sets,refs,animIndex)){frame+=GetFrameTime()*30.0f*speed;if(a->keyframeCount>0&&frame>=a->keyframeCount)frame=loop?std::fmod(frame,(float)a->keyframeCount):(float)a->keyframeCount-1;UpdateModelAnimation(model,*a,frame);}}

        BeginDrawing();ClearBackground(Color{20,22,26,255});DrawRectangle(0,0,w,(int)top,Color{28,30,35,255});DrawText("CnCGZH Laboratorio de personajes",16,17,22,RAYWHITE);
        Rectangle bp{left+16,12,86,34},bprev{left+110,12,40,34},bnext{left+156,12,40,34},bminus{left+206,12,40,34},bplus{left+252,12,40,34},bloop{left+300,12,72,34};
        button(bp,playing?"Pausa":"Reproducir");button(bprev,"<");button(bnext,">");button(bminus,"-");button(bplus,"+");button(bloop,loop?"Bucle":"Una vez",loop);
        if(click(bp))playing=!playing;if(click(bprev)&&!refs.empty()){animIndex=(animIndex+(int)refs.size()-1)%(int)refs.size();frame=0;}if(click(bnext)&&!refs.empty()){animIndex=(animIndex+1)%(int)refs.size();frame=0;}if(click(bminus))speed=std::max(.1f,speed-.1f);if(click(bplus))speed=std::min(3.0f,speed+.1f);if(click(bloop))loop=!loop;
        DrawText(TextFormat("%.1fx",speed),(int)left+386,21,16,Color{205,211,220,255});

        DrawRectangle(0,(int)top,(int)left,h-(int)top-(int)bottom,Color{31,33,38,255});DrawText("PERSONAJES",12,(int)top+12,16,Color{180,187,198,255});int cy=(int)top+40;int visible=std::max(1,(h-(int)top-(int)bottom-50)/30);
        if(CheckCollisionPointRec(mouse,{0,top,left,(float)h-top-bottom})){int wh=(int)GetMouseWheelMove();if(wh)charScroll=std::clamp(charScroll-wh,0,std::max(0,(int)chars.size()-visible));}
        for(int row=0;row<visible&&row+charScroll<(int)chars.size();++row){int idx=row+charScroll;Rectangle r{8,(float)(cy+row*30),left-16,26};DrawRectangleRec(r,idx==charIndex?Color{61,107,151,255}:Color{42,45,51,255});DrawText(chars[idx].name.c_str(),14,(int)r.y+5,15,RAYWHITE);if(click(r)&&idx!=charIndex){charIndex=idx;loadCharacter();}}

        DrawRectangle(w-(int)right,(int)top,(int)right,h-(int)top-(int)bottom,Color{31,33,38,255});int rx=w-(int)right+12;DrawText("ANIMACIONES",rx,(int)top+12,16,Color{180,187,198,255});int ay=(int)top+40,animVisible=10;
        if(CheckCollisionPointRec(mouse,{(float)(w-right),top,right,300})){int wh=(int)GetMouseWheelMove();if(wh)animScroll=std::clamp(animScroll-wh,0,std::max(0,(int)refs.size()-animVisible));}
        for(int row=0;row<animVisible&&row+animScroll<(int)refs.size();++row){int idx=row+animScroll;Rectangle r{(float)rx,(float)(ay+row*27),right-24,23};DrawRectangleRec(r,idx==animIndex?Color{75,116,72,255}:Color{42,45,51,255});DrawText(refs[idx].name.c_str(),rx+6,(int)r.y+4,14,RAYWHITE);if(click(r)){animIndex=idx;frame=0;playing=true;}}

        int actionY=(int)top+330;DrawText("ACCIONES",rx,actionY,16,Color{180,187,198,255});actionY+=26;
        for(int i=0;i<(int)actions.size();++i){Rectangle r{(float)rx,(float)(actionY+i*26),92,22};DrawRectangleRec(r,i==actionIndex?Color{122,87,47,255}:Color{42,45,51,255});DrawText(actionLabels[i].c_str(),rx+6,(int)r.y+4,13,RAYWHITE);if(click(r))actionIndex=i;auto it=mapping.find(actions[i]);DrawText((it==mapping.end()?"-":it->second).c_str(),rx+102,(int)r.y+4,13,Color{195,201,210,255});}
        Rectangle assign{(float)rx,(float)(actionY+(int)actions.size()*26+8),right-24,32};button(assign,"Asignar animación actual");if(click(assign)&&!refs.empty()){mapping[actions[actionIndex]]=refs[animIndex].name;status=saveProfile(root,chars[charIndex],mapping)?actions[actionIndex]+" = "+refs[animIndex].name:"No se pudo guardar el perfil";}

        BeginScissorMode((int)view.x,(int)view.y,(int)view.width,(int)view.height);BeginMode3D(cameraFor(yaw,pitch,distance));DrawGrid(20,1.0f);if(modelLoaded)DrawModel(model,{0,0,0},1.0f,WHITE);EndMode3D();EndScissorMode();DrawRectangleLinesEx(view,1,Color{74,80,89,255});
        if(!refs.empty()){DrawText(refs[animIndex].name.c_str(),(int)view.x+14,(int)view.y+14,20,RAYWHITE);if(auto*a=currentAnimation(sets,refs,animIndex))DrawText(TextFormat("Fotograma %.1f / %d",frame,a->keyframeCount),(int)view.x+14,(int)view.y+41,15,Color{190,196,205,255});}
        DrawText("Botón derecho: orbitar | Rueda: zoom",(int)view.x+14,(int)(view.y+view.height-26),15,Color{170,177,187,255});

        DrawRectangle(0,h-(int)bottom,w,(int)bottom,Color{27,29,33,255});DrawText(status.c_str(),12,h-23,14,Color{205,211,220,255});DrawText(("Perfil: "+utf8(profilePath(root,chars[charIndex]))).c_str(),w-600,h-23,13,Color{145,152,162,255});EndDrawing();
    }

    unloadSets(sets);if(modelLoaded)UnloadModel(model);CloseWindow();return 0;
}
