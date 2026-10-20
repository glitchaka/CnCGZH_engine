#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "ImGuizmo.h"
#include "StudioModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace fs = std::filesystem;
using namespace DirectX;
using namespace cncstudio;

namespace {

HWND g_hwnd = nullptr;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_mainRTV = nullptr;

struct Vertex { XMFLOAT3 pos; };
struct SceneConstants { XMFLOAT4X4 worldViewProj; XMFLOAT4 color; };

void releaseMainTarget() {
    if (g_mainRTV) { g_mainRTV->Release(); g_mainRTV = nullptr; }
}

void createMainTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_mainRTV);
        backBuffer->Release();
    }
}

bool createDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL requested[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL obtained{};
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        requested, ARRAYSIZE(requested), D3D11_SDK_VERSION,
        &sd, &g_swapChain, &g_device, &obtained, &g_context);
    if (FAILED(hr)) return false;
    createMainTarget();
    return g_mainRTV != nullptr;
}

void destroyDevice() {
    releaseMainTarget();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
LRESULT WINAPI wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_device && wParam != SIZE_MINIMIZED) {
            releaseMainTarget();
            g_swapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            createMainTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

fs::path executablePath() {
    std::wstring buf(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    buf.resize(n);
    return fs::path(buf);
}

fs::path findProjectRoot() {
    fs::path p = executablePath().parent_path();
    for (int i = 0; i < 10 && !p.empty(); ++i) {
        if (fs::exists(p / "upstream" / "ea-generals-zero-hour")) return p;
        p = p.parent_path();
    }
    p = fs::current_path();
    for (int i = 0; i < 6 && !p.empty(); ++i) {
        if (fs::exists(p / "upstream" / "ea-generals-zero-hour")) return p;
        p = p.parent_path();
    }
    return fs::current_path();
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string readText(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

class ApiCatalog {
public:
    void scan(const fs::path& root) {
        symbols.clear();
        fs::path base = root / "upstream" / "ea-generals-zero-hour";
        if (!fs::exists(base)) return;
        static const std::regex fnRe(R"(([A-Za-z_~][A-Za-z0-9_:~<>]*)\s*\(([^;{}]{0,260})\)\s*(?:const\s*)?(?:=\s*0\s*)?;)");
        std::error_code ec;
        std::size_t fileCount = 0;
        for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (!it->is_regular_file(ec) || lower(it->path().extension().string()) != ".h") continue;
            if (++fileCount > 2400) break;
            std::string text = readText(it->path());
            if (text.empty()) continue;
            std::string generic = it->path().generic_string();
            std::string library = "GameEngine";
            auto pos = generic.find("/Code/");
            if (pos != std::string::npos) {
                auto begin = pos + 6;
                auto slash = generic.find('/', begin);
                if (slash != std::string::npos) library = generic.substr(begin, slash - begin);
            }
            std::string rel = fs::relative(it->path(), base, ec).generic_string();
            std::sregex_iterator mi(text.begin(), text.end(), fnRe), me;
            int perHeader = 0;
            for (; mi != me && perHeader < 120; ++mi, ++perHeader) {
                std::string signature = mi->str(0);
                signature.erase(std::remove(signature.begin(), signature.end(), '\r'), signature.end());
                std::replace(signature.begin(), signature.end(), '\n', ' ');
                while (signature.find("  ") != std::string::npos) signature.replace(signature.find("  "), 2, " ");
                ApiSymbol s;
                s.library = library;
                s.header = rel;
                s.name = (*mi)[1].str();
                s.signature = signature;
                s.callableAdapter = false;
                symbols.push_back(std::move(s));
                if (symbols.size() >= 30000) return;
            }
        }
        std::sort(symbols.begin(), symbols.end(), [](const auto& a, const auto& b) {
            if (a.library != b.library) return a.library < b.library;
            if (a.header != b.header) return a.header < b.header;
            return a.name < b.name;
        });
    }

    std::vector<ApiSymbol> symbols;
};

struct NodeTemplate {
    std::string type;
    std::string title;
    std::string category;
    std::vector<std::pair<std::string, PinType>> inputs;
    std::vector<std::pair<std::string, PinType>> outputs;
};

class NodeRegistry {
public:
    NodeRegistry() {
        add("event.start", "Al iniciar", "Eventos", {}, {{"Flujo", PinType::Flow}});
        add("event.update", "Cada fotograma", "Eventos", {}, {{"Flujo", PinType::Flow}, {"dt", PinType::Number}});
        add("event.click", "Al seleccionar", "Eventos", {}, {{"Flujo", PinType::Flow}});
        add("flow.branch", "Condición", "Flujo", {{"Flujo", PinType::Flow}, {"Condición", PinType::Bool}}, {{"Sí", PinType::Flow}, {"No", PinType::Flow}});
        add("flow.wait", "Esperar", "Flujo", {{"Flujo", PinType::Flow}, {"Segundos", PinType::Number}}, {{"Flujo", PinType::Flow}});
        add("object.move", "Mover objeto", "Objeto", {{"Flujo", PinType::Flow}, {"Objeto", PinType::Object}, {"Posición", PinType::Vector3}}, {{"Flujo", PinType::Flow}});
        add("object.rotate", "Rotar objeto", "Objeto", {{"Flujo", PinType::Flow}, {"Objeto", PinType::Object}, {"Rotación", PinType::Vector3}}, {{"Flujo", PinType::Flow}});
        add("object.scale", "Escalar objeto", "Objeto", {{"Flujo", PinType::Flow}, {"Objeto", PinType::Object}, {"Escala", PinType::Vector3}}, {{"Flujo", PinType::Flow}});
        add("object.visible", "Mostrar / ocultar", "Objeto", {{"Flujo", PinType::Flow}, {"Objeto", PinType::Object}, {"Visible", PinType::Bool}}, {{"Flujo", PinType::Flow}});
        add("object.destroy", "Eliminar objeto", "Objeto", {{"Flujo", PinType::Flow}, {"Objeto", PinType::Object}}, {{"Flujo", PinType::Flow}});
        add("camera.activate", "Activar cámara", "Cámara", {{"Flujo", PinType::Flow}, {"Cámara", PinType::Object}}, {{"Flujo", PinType::Flow}});
        add("camera.fov", "Cambiar FOV", "Cámara", {{"Flujo", PinType::Flow}, {"Cámara", PinType::Object}, {"FOV", PinType::Number}}, {{"Flujo", PinType::Flow}});
        add("math.add", "Sumar", "Matemática", {{"A", PinType::Number}, {"B", PinType::Number}}, {{"Resultado", PinType::Number}});
        add("math.compare", "Comparar", "Matemática", {{"A", PinType::Number}, {"B", PinType::Number}}, {{"A > B", PinType::Bool}});
        add("value.number", "Número", "Valores", {}, {{"Valor", PinType::Number}});
        add("value.text", "Texto", "Valores", {}, {{"Valor", PinType::Text}});
        add("value.vector3", "Vector 3", "Valores", {}, {{"Valor", PinType::Vector3}});
        add("value.self", "Este objeto", "Valores", {}, {{"Objeto", PinType::Object}});
    }

    const std::vector<NodeTemplate>& all() const { return templates; }

private:
    void add(std::string type, std::string title, std::string category,
             std::vector<std::pair<std::string, PinType>> in,
             std::vector<std::pair<std::string, PinType>> out) {
        templates.push_back({std::move(type), std::move(title), std::move(category), std::move(in), std::move(out)});
    }
    std::vector<NodeTemplate> templates;
};

class ViewportRenderer {
public:
    ~ViewportRenderer() { shutdown(); }

    bool initialize() {
        static const char* vsSource = R"(
cbuffer SceneCB : register(b0) { float4x4 worldViewProj; float4 tint; };
struct VSIn { float3 pos : POSITION; };
struct VSOut { float4 pos : SV_POSITION; };
VSOut main(VSIn v) { VSOut o; o.pos = mul(float4(v.pos,1), worldViewProj); return o; }
)";
        static const char* psSource = R"(
cbuffer SceneCB : register(b0) { float4x4 worldViewProj; float4 tint; };
float4 main() : SV_Target { return tint; }
)";
        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* psBlob = nullptr;
        ID3DBlob* err = nullptr;
        HRESULT hr = D3DCompile(vsSource, strlen(vsSource), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &err);
        if (FAILED(hr)) { if (err) err->Release(); return false; }
        hr = D3DCompile(psSource, strlen(psSource), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &err);
        if (FAILED(hr)) { vsBlob->Release(); if (err) err->Release(); return false; }
        g_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs);
        g_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps);
        D3D11_INPUT_ELEMENT_DESC il[] = {{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0}};
        g_device->CreateInputLayout(il, 1, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &layout);
        vsBlob->Release(); psBlob->Release(); if (err) err->Release();

        D3D11_BUFFER_DESC cbd{};
        cbd.ByteWidth = sizeof(SceneConstants);
        cbd.Usage = D3D11_USAGE_DYNAMIC;
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g_device->CreateBuffer(&cbd, nullptr, &constantBuffer);

        std::array<Vertex,8> cube = {{ Vertex{XMFLOAT3{-.5f,-.5f,-.5f}}, Vertex{XMFLOAT3{.5f,-.5f,-.5f}}, Vertex{XMFLOAT3{.5f,.5f,-.5f}}, Vertex{XMFLOAT3{-.5f,.5f,-.5f}}, Vertex{XMFLOAT3{-.5f,-.5f,.5f}}, Vertex{XMFLOAT3{.5f,-.5f,.5f}}, Vertex{XMFLOAT3{.5f,.5f,.5f}}, Vertex{XMFLOAT3{-.5f,.5f,.5f}} }};
        std::array<std::uint16_t,36> indices = {{0,1,2,0,2,3,4,6,5,4,7,6,0,4,5,0,5,1,3,2,6,3,6,7,1,5,6,1,6,2,0,3,7,0,7,4}};
        makeImmutableBuffer(cube.data(), sizeof(cube), D3D11_BIND_VERTEX_BUFFER, &cubeVB);
        makeImmutableBuffer(indices.data(), sizeof(indices), D3D11_BIND_INDEX_BUFFER, &cubeIB);

        std::vector<Vertex> grid;
        constexpr int half = 20;
        for (int i=-half; i<=half; ++i) {
            grid.push_back({XMFLOAT3((float)i,0,(float)-half)}); grid.push_back({XMFLOAT3((float)i,0,(float)half)});
            grid.push_back({XMFLOAT3((float)-half,0,(float)i)}); grid.push_back({XMFLOAT3((float)half,0,(float)i)});
        }
        gridVertexCount = static_cast<UINT>(grid.size());
        makeImmutableBuffer(grid.data(), static_cast<UINT>(grid.size()*sizeof(Vertex)), D3D11_BIND_VERTEX_BUFFER, &gridVB);
        return vs && ps && layout && constantBuffer && cubeVB && cubeIB && gridVB;
    }

    void ensureTarget(int w, int h) {
        w = std::max(w, 16); h = std::max(h, 16);
        if (w == width && h == height && srv) return;
        releaseTarget(); width = w; height = h;
        D3D11_TEXTURE2D_DESC td{};
        td.Width=w; td.Height=h; td.MipLevels=1; td.ArraySize=1; td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        ID3D11Texture2D* tex=nullptr;
        if (SUCCEEDED(g_device->CreateTexture2D(&td,nullptr,&tex))) {
            g_device->CreateRenderTargetView(tex,nullptr,&rtv);
            g_device->CreateShaderResourceView(tex,nullptr,&srv);
            tex->Release();
        }
        D3D11_TEXTURE2D_DESC dd=td; dd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT; dd.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        ID3D11Texture2D* depthTex=nullptr;
        if (SUCCEEDED(g_device->CreateTexture2D(&dd,nullptr,&depthTex))) { g_device->CreateDepthStencilView(depthTex,nullptr,&dsv); depthTex->Release(); }
    }

    void render(const std::vector<SceneObject>& objects, std::uint64_t selectedId, XMMATRIX view, XMMATRIX proj) {
        if (!rtv || !dsv) return;
        const float clear[4]{0.045f,0.052f,0.065f,1.0f};
        g_context->OMSetRenderTargets(1,&rtv,dsv);
        g_context->ClearRenderTargetView(rtv,clear);
        g_context->ClearDepthStencilView(dsv,D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1.0f,0);
        D3D11_VIEWPORT vp{0,0,(float)width,(float)height,0,1};
        g_context->RSSetViewports(1,&vp);
        g_context->IASetInputLayout(layout);
        g_context->VSSetShader(vs,nullptr,0); g_context->PSSetShader(ps,nullptr,0);
        g_context->VSSetConstantBuffers(0,1,&constantBuffer); g_context->PSSetConstantBuffers(0,1,&constantBuffer);

        drawGrid(view,proj);
        for (const auto& o : objects) {
            if (!o.visible) continue;
            if (o.kind == ObjectKind::Camera) drawCamera(o, o.id==selectedId, view,proj);
            else drawObject(o, o.id==selectedId, view,proj);
        }
    }

    ID3D11ShaderResourceView* texture() const { return srv; }
    int targetWidth() const { return width; }
    int targetHeight() const { return height; }

    void shutdown() {
        releaseTarget();
        release(vs); release(ps); release(layout); release(constantBuffer); release(cubeVB); release(cubeIB); release(gridVB);
    }

private:
    template <typename T> void release(T*& p) { if(p){p->Release();p=nullptr;} }
    void releaseTarget(){ release(srv); release(rtv); release(dsv); width=height=0; }
    void makeImmutableBuffer(const void* data, UINT bytes, UINT bind, ID3D11Buffer** out) {
        D3D11_BUFFER_DESC bd{}; bd.ByteWidth=bytes; bd.Usage=D3D11_USAGE_IMMUTABLE; bd.BindFlags=bind;
        D3D11_SUBRESOURCE_DATA init{}; init.pSysMem=data; g_device->CreateBuffer(&bd,&init,out);
    }

    XMMATRIX objectWorld(const SceneObject& o) const {
        const auto& p=o.transform.position; const auto& r=o.transform.rotation; const auto& s=o.transform.scale;
        return XMMatrixScaling(s[0],s[1],s[2]) * XMMatrixRotationRollPitchYaw(XMConvertToRadians(r[0]),XMConvertToRadians(r[1]),XMConvertToRadians(r[2])) * XMMatrixTranslation(p[0],p[1],p[2]);
    }

    void constants(XMMATRIX m, XMFLOAT4 c) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(g_context->Map(constantBuffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return;
        auto* cb=reinterpret_cast<SceneConstants*>(mapped.pData);
        XMStoreFloat4x4(&cb->worldViewProj, XMMatrixTranspose(m)); cb->color=c;
        g_context->Unmap(constantBuffer,0);
    }

    void drawObject(const SceneObject& o, bool selected, XMMATRIX view, XMMATRIX proj) {
        UINT stride=sizeof(Vertex), offset=0; g_context->IASetVertexBuffers(0,1,&cubeVB,&stride,&offset); g_context->IASetIndexBuffer(cubeIB,DXGI_FORMAT_R16_UINT,0);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        XMFLOAT4 col = selected ? XMFLOAT4(.25f,.85f,.45f,1) : (o.kind==ObjectKind::Light ? XMFLOAT4(1,.8f,.25f,1) : XMFLOAT4(.43f,.58f,.72f,1));
        constants(objectWorld(o)*view*proj,col); g_context->DrawIndexed(36,0,0);
    }

    void drawGrid(XMMATRIX view,XMMATRIX proj) {
        UINT stride=sizeof(Vertex),offset=0;g_context->IASetVertexBuffers(0,1,&gridVB,&stride,&offset);g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        constants(XMMatrixIdentity()*view*proj,XMFLOAT4(.16f,.19f,.23f,1));g_context->Draw(gridVertexCount,0);
    }

    void drawCamera(const SceneObject& o,bool selected,XMMATRIX view,XMMATRIX proj) {
        SceneObject body=o; body.transform.scale={0.35f,0.25f,0.45f}; drawObject(body,selected,view,proj);
        float aspect=16.0f/9.0f; float n=.55f; float f=1.8f; float t=std::tan(XMConvertToRadians(o.camera.fovDegrees*.5f));
        float nh=n*t,nw=nh*aspect,fh=f*t,fw=fh*aspect;
        std::array<XMFLOAT3,8> p={XMFLOAT3{-nw,-nh,n},XMFLOAT3{nw,-nh,n},XMFLOAT3{nw,nh,n},XMFLOAT3{-nw,nh,n},XMFLOAT3{-fw,-fh,f},XMFLOAT3{fw,-fh,f},XMFLOAT3{fw,fh,f},XMFLOAT3{-fw,fh,f}};
        int e[][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
        std::vector<Vertex> lines; lines.reserve(24); for(auto& x:e){lines.push_back({p[x[0]]});lines.push_back({p[x[1]]});}
        ID3D11Buffer* vb=nullptr; makeImmutableBuffer(lines.data(),(UINT)(lines.size()*sizeof(Vertex)),D3D11_BIND_VERTEX_BUFFER,&vb);
        if(vb){UINT stride=sizeof(Vertex),offset=0;g_context->IASetVertexBuffers(0,1,&vb,&stride,&offset);g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);constants(objectWorld(o)*view*proj,selected?XMFLOAT4(.25f,.85f,.45f,1):XMFLOAT4(.92f,.63f,.2f,1));g_context->Draw((UINT)lines.size(),0);vb->Release();}
    }

    ID3D11VertexShader* vs=nullptr; ID3D11PixelShader* ps=nullptr; ID3D11InputLayout* layout=nullptr; ID3D11Buffer* constantBuffer=nullptr;
    ID3D11Buffer* cubeVB=nullptr; ID3D11Buffer* cubeIB=nullptr; ID3D11Buffer* gridVB=nullptr; UINT gridVertexCount=0;
    ID3D11RenderTargetView* rtv=nullptr; ID3D11ShaderResourceView* srv=nullptr; ID3D11DepthStencilView* dsv=nullptr; int width=0,height=0;
};

class StudioApp {
public:
    explicit StudioApp(fs::path root) : root(std::move(root)) {
        api.scan(this->root);
        scanAssets();
        addObject(ObjectKind::Camera,"Cámara principal").camera.active=true;
        auto& cube=addObject(ObjectKind::Mesh,"Objeto"); cube.transform.position={0,0.5f,0};
    }

    bool initialize() { return renderer.initialize(); }

    void frame() {
        ImGuiIO& io=ImGui::GetIO();
        ImGui::DockSpaceOverViewport(0,ImGui::GetMainViewport(),ImGuiDockNodeFlags_PassthruCentralNode);
        menuBar();
        hierarchyPanel();
        assetPanel();
        inspectorPanel();
        apiPanel();
        viewportPanel();
        scriptPanel();
        statusPanel();
    }

private:
    SceneObject& addObject(ObjectKind kind, std::string name) {
        SceneObject o; o.id=nextId++; o.kind=kind; o.name=std::move(name);
        if(kind==ObjectKind::Camera) o.camera.fovDegrees=60;
        objects.push_back(std::move(o)); selectedId=objects.back().id; return objects.back();
    }

    SceneObject* selected() {
        auto it=std::find_if(objects.begin(),objects.end(),[&](auto& o){return o.id==selectedId;}); return it==objects.end()?nullptr:&*it;
    }

    void menuBar() {
        if(!ImGui::BeginMainMenuBar()) return;
        if(ImGui::BeginMenu("Archivo")){ if(ImGui::MenuItem("Guardar escena","Ctrl+S")) saveScene(); if(ImGui::MenuItem("Salir")) PostMessageW(g_hwnd,WM_CLOSE,0,0); ImGui::EndMenu(); }
        if(ImGui::BeginMenu("Crear")){ if(ImGui::MenuItem("Objeto vacío")) addObject(ObjectKind::Empty,"Objeto vacío"); if(ImGui::MenuItem("Malla")) addObject(ObjectKind::Mesh,"Malla"); if(ImGui::MenuItem("Cámara")) addObject(ObjectKind::Camera,"Cámara"); if(ImGui::MenuItem("Luz")) addObject(ObjectKind::Light,"Luz"); ImGui::EndMenu(); }
        ImGui::Separator(); ImGui::TextDisabled("CnCGZH Studio v2");
        ImGui::EndMainMenuBar();
    }

    void hierarchyPanel() {
        ImGui::Begin("Escena");
        if(ImGui::Button("+ Objeto")) addObject(ObjectKind::Empty,"Objeto vacío"); ImGui::SameLine(); if(ImGui::Button("+ Cámara")) addObject(ObjectKind::Camera,"Cámara");
        ImGui::Separator();
        for(auto& o:objects){
            const char* icon=o.kind==ObjectKind::Camera?"[CAM] ":o.kind==ObjectKind::Light?"[LUZ] ":o.kind==ObjectKind::Mesh?"[MALLA] ":"[OBJ] ";
            std::string label=std::string(icon)+o.name+"##"+std::to_string(o.id);
            if(ImGui::Selectable(label.c_str(),o.id==selectedId)) selectedId=o.id;
        }
        ImGui::End();
    }

    void inspectorPanel() {
        ImGui::Begin("Inspector");
        SceneObject* o=selected();
        if(!o){ImGui::TextDisabled("Selecciona un objeto.");ImGui::End();return;}
        char name[256]{}; strncpy_s(name,o->name.c_str(),_TRUNCATE); if(ImGui::InputText("Nombre",name,sizeof(name))) o->name=name;
        ImGui::Checkbox("Visible",&o->visible);
        if(ImGui::CollapsingHeader("Transformación",ImGuiTreeNodeFlags_DefaultOpen)){
            ImGui::DragFloat3("Posición",o->transform.position.data(),.05f); ImGui::DragFloat3("Rotación",o->transform.rotation.data(),.5f); ImGui::DragFloat3("Escala",o->transform.scale.data(),.02f,.01f,100);
        }
        if(o->kind==ObjectKind::Camera&&ImGui::CollapsingHeader("Cámara",ImGuiTreeNodeFlags_DefaultOpen)){
            if(ImGui::Checkbox("Cámara activa",&o->camera.active)&&o->camera.active){for(auto& other:objects)if(other.id!=o->id&&other.kind==ObjectKind::Camera)other.camera.active=false;}
            ImGui::SliderFloat("FOV",&o->camera.fovDegrees,10,140,"%.0f°"); ImGui::DragFloat("Plano cercano",&o->camera.nearPlane,.01f,.01f,50); ImGui::DragFloat("Plano lejano",&o->camera.farPlane,1,1,10000);
            if(ImGui::Button("Mirar desde esta cámara")){ editorTarget={o->transform.position[0],o->transform.position[1],o->transform.position[2]}; editorDistance=5; }
        }
        if(o->kind==ObjectKind::Light&&ImGui::CollapsingHeader("Luz",ImGuiTreeNodeFlags_DefaultOpen)){ImGui::ColorEdit3("Color",o->light.color.data());ImGui::DragFloat("Intensidad",&o->light.intensity,.05f,0,100);}
        if(!o->assetPath.empty()){ImGui::SeparatorText("Recurso");ImGui::TextWrapped("%s",o->assetPath.generic_string().c_str());}
        ImGui::Separator();
        if(ImGui::Button("Duplicar")){SceneObject copy=*o;copy.id=nextId++;copy.name+=" copia";copy.transform.position[0]+=.75f;objects.push_back(std::move(copy));selectedId=objects.back().id;}
        ImGui::SameLine(); if(ImGui::Button("Eliminar")){auto id=o->id;objects.erase(std::remove_if(objects.begin(),objects.end(),[&](auto& x){return x.id==id;}),objects.end());selectedId=objects.empty()?0:objects.front().id;}
        ImGui::End();
    }

    void scanAssets() {
        assets.clear(); std::error_code ec;
        std::vector<fs::path> bases={root/"assets",root/"upstream"/"ea-generals-zero-hour"};
        for(auto& base:bases){if(!fs::exists(base))continue;for(fs::recursive_directory_iterator it(base,fs::directory_options::skip_permission_denied,ec),end;it!=end;it.increment(ec)){if(ec){ec.clear();continue;}if(!it->is_regular_file(ec))continue;std::string ext=lower(it->path().extension().string());if(ext==".w3d"||ext==".obj"||ext==".fbx"||ext==".gltf"||ext==".glb"||ext==".dds"||ext==".tga"||ext==".ini"||ext==".map")assets.push_back(it->path());if(assets.size()>=12000)return;}}
    }

    void assetPanel() {
        ImGui::Begin("Recursos");
        ImGui::InputTextWithHint("##assetSearch","Buscar recursos...",assetSearch,sizeof(assetSearch));
        std::string needle=lower(assetSearch); int shown=0;
        for(const auto& p:assets){std::string label=p.filename().string();if(!needle.empty()&&lower(label).find(needle)==std::string::npos)continue;if(++shown>600)break;ImGui::Selectable(label.c_str());if(ImGui::BeginDragDropSource()){std::string path=p.generic_string();ImGui::SetDragDropPayload("CNC_ASSET",path.c_str(),path.size()+1);ImGui::Text("%s",label.c_str());ImGui::EndDragDropSource();}if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",p.generic_string().c_str());}
        if(shown>=600)ImGui::TextDisabled("Mostrando los primeros 600 resultados. Usa el buscador.");
        ImGui::End();
    }

    void apiPanel() {
        ImGui::Begin("Biblioteca del motor");
        ImGui::Text("Símbolos descubiertos: %zu",api.symbols.size());
        ImGui::InputTextWithHint("##apiSearch","Buscar función, clase o cabecera...",apiSearch,sizeof(apiSearch));
        ImGui::SameLine(); ImGui::Checkbox("Solo adaptadores",&onlyAdapters);
        std::string q=lower(apiSearch); int shown=0;
        for(const auto& s:api.symbols){if(onlyAdapters&&!s.callableAdapter)continue;std::string hay=lower(s.name+" "+s.signature+" "+s.header+" "+s.library);if(!q.empty()&&hay.find(q)==std::string::npos)continue;if(++shown>350)break;ImGui::PushID(&s);if(ImGui::TreeNodeEx(s.name.c_str(),ImGuiTreeNodeFlags_SpanAvailWidth)){ImGui::TextDisabled("%s | %s",s.library.c_str(),s.header.c_str());ImGui::TextWrapped("%s",s.signature.c_str());if(s.callableAdapter)ImGui::TextColored({.3f,.9f,.5f,1},"Disponible como nodo ejecutable");else ImGui::TextColored({.9f,.65f,.25f,1},"Catalogado desde C++; requiere adaptador antes de ejecutarse.");if(ImGui::Button("Crear nodo de referencia")){if(auto* o=selected())addApiReferenceNode(o->script,s);}ImGui::TreePop();}ImGui::PopID();}
        ImGui::End();
    }

    void viewportPanel() {
        ImGui::Begin("Viewport",nullptr,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        if(ImGui::Button("Seleccionar [Q]")) gizmoOp=ImGuizmo::TRANSLATE; ImGui::SameLine(); if(ImGui::Button("Mover [W]"))gizmoOp=ImGuizmo::TRANSLATE;ImGui::SameLine();if(ImGui::Button("Rotar [E]"))gizmoOp=ImGuizmo::ROTATE;ImGui::SameLine();if(ImGui::Button("Escalar [R]"))gizmoOp=ImGuizmo::SCALE;
        ImVec2 avail=ImGui::GetContentRegionAvail(); int vw=std::max(16,(int)avail.x),vh=std::max(16,(int)avail.y); renderer.ensureTarget(vw,vh);
        XMMATRIX view=editorView(); XMMATRIX proj=XMMatrixPerspectiveFovLH(XMConvertToRadians(60.0f),(float)vw/(float)vh,.05f,1000.0f); renderer.render(objects,selectedId,view,proj);
        ImVec2 imagePos=ImGui::GetCursorScreenPos(); ImGui::Image((ImTextureID)renderer.texture(),{(float)vw,(float)vh}); bool hovered=ImGui::IsItemHovered();
        if(ImGui::BeginDragDropTarget()){if(const ImGuiPayload* payload=ImGui::AcceptDragDropPayload("CNC_ASSET")){std::string path((const char*)payload->Data);auto& o=addObject(ObjectKind::Mesh,fs::path(path).stem().string());o.assetPath=path;o.transform.position={editorTarget.x,editorTarget.y,editorTarget.z};}ImGui::EndDragDropTarget();}
        if(hovered){auto& io=ImGui::GetIO();if(ImGui::IsMouseDragging(ImGuiMouseButton_Right)){editorYaw-=io.MouseDelta.x*.25f;editorPitch=std::clamp(editorPitch-io.MouseDelta.y*.25f,-80.0f,80.0f);}if(io.MouseWheel!=0)editorDistance=std::clamp(editorDistance-io.MouseWheel*.7f,1.0f,150.0f);if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)&&!ImGuizmo::IsOver())pickObject(imagePos,{(float)vw,(float)vh},view,proj);}
        manipulateSelected(imagePos,{(float)vw,(float)vh},view,proj);
        ImGui::End();
    }

    XMMATRIX editorView() const {
        float yaw=XMConvertToRadians(editorYaw),pitch=XMConvertToRadians(editorPitch);XMVECTOR target=XMVectorSet(editorTarget.x,editorTarget.y,editorTarget.z,1);XMVECTOR offset=XMVectorSet(editorDistance*std::cos(pitch)*std::sin(yaw),editorDistance*std::sin(pitch),-editorDistance*std::cos(pitch)*std::cos(yaw),0);return XMMatrixLookAtLH(target+offset,target,XMVectorSet(0,1,0,0));
    }

    void manipulateSelected(ImVec2 pos,ImVec2 size,XMMATRIX view,XMMATRIX proj) {
        auto* o=selected();if(!o)return;float matrix[16];ImGuizmo::RecomposeMatrixFromComponents(o->transform.position.data(),o->transform.rotation.data(),o->transform.scale.data(),matrix);
        XMFLOAT4X4 vf,pf;XMStoreFloat4x4(&vf,XMMatrixTranspose(view));XMStoreFloat4x4(&pf,XMMatrixTranspose(proj));
        ImGuizmo::SetDrawlist();ImGuizmo::SetRect(pos.x,pos.y,size.x,size.y);ImGuizmo::SetOrthographic(false);if(ImGuizmo::Manipulate(&vf.m[0][0],&pf.m[0][0],gizmoOp,ImGuizmo::WORLD,matrix)){ImGuizmo::DecomposeMatrixToComponents(matrix,o->transform.position.data(),o->transform.rotation.data(),o->transform.scale.data());}
    }

    void pickObject(ImVec2 imagePos,ImVec2 size,XMMATRIX view,XMMATRIX proj) {
        ImVec2 m=ImGui::GetMousePos();float x=m.x-imagePos.x,y=m.y-imagePos.y;if(x<0||y<0||x>=size.x||y>=size.y)return;
        XMVECTOR nearP=XMVector3Unproject(XMVectorSet(x,y,0,1),0,0,size.x,size.y,0,1,proj,view,XMMatrixIdentity());XMVECTOR farP=XMVector3Unproject(XMVectorSet(x,y,1,1),0,0,size.x,size.y,0,1,proj,view,XMMatrixIdentity());XMVECTOR dir=XMVector3Normalize(farP-nearP);
        float best=1e30f;std::uint64_t bestId=0;
        for(auto& o:objects){XMMATRIX world=XMMatrixScaling(o.transform.scale[0],o.transform.scale[1],o.transform.scale[2])*XMMatrixRotationRollPitchYaw(XMConvertToRadians(o.transform.rotation[0]),XMConvertToRadians(o.transform.rotation[1]),XMConvertToRadians(o.transform.rotation[2]))*XMMatrixTranslation(o.transform.position[0],o.transform.position[1],o.transform.position[2]);XMMATRIX inv=XMMatrixInverse(nullptr,world);XMVECTOR ro=XMVector3TransformCoord(nearP,inv),rd=XMVector3Normalize(XMVector3TransformNormal(dir,inv));XMFLOAT3 O,D;XMStoreFloat3(&O,ro);XMStoreFloat3(&D,rd);float tmin=0,tmax=1e30f;bool hit=true;for(int a=0;a<3;a++){float oo=(&O.x)[a],dd=(&D.x)[a];if(std::abs(dd)<1e-6f){if(oo<-.5f||oo>.5f){hit=false;break;}}else{float t1=(-.5f-oo)/dd,t2=(.5f-oo)/dd;if(t1>t2)std::swap(t1,t2);tmin=std::max(tmin,t1);tmax=std::min(tmax,t2);if(tmin>tmax){hit=false;break;}}}if(hit&&tmin<best){best=tmin;bestId=o.id;}}
        if(bestId)selectedId=bestId;
    }

    ScriptNode makeNode(const NodeTemplate& t,float x,float y) {
        ScriptNode n;n.id=nextNodeId++;n.type=t.type;n.title=t.title;n.category=t.category;n.x=x;n.y=y;for(auto& p:t.inputs)n.inputs.push_back({nextPinId++,p.first,p.second});for(auto& p:t.outputs)n.outputs.push_back({nextPinId++,p.first,p.second});return n;
    }

    void addApiReferenceNode(ScriptGraph& g,const ApiSymbol& s){ScriptNode n;n.id=nextNodeId++;n.type="cpp.reference";n.title=s.name;n.category=s.library;n.x=50;n.y=50;n.properties["signature"]=s.signature;n.properties["header"]=s.header;n.inputs.push_back({nextPinId++,"Flujo",PinType::Flow});n.outputs.push_back({nextPinId++,"Flujo",PinType::Flow});g.nodes.push_back(std::move(n));}

    void scriptPanel() {
        ImGui::Begin("Script del objeto");auto* o=selected();if(!o){ImGui::TextDisabled("Selecciona un objeto para editar su script.");ImGui::End();return;}ImGui::Text("Anidado en: %s",o->name.c_str());ImGui::Separator();
        ImGui::BeginChild("palette",{210,0},true);ImGui::TextUnformatted("NODOS");ImGui::InputTextWithHint("##nodeSearch","Buscar...",nodeSearch,sizeof(nodeSearch));std::string q=lower(nodeSearch);for(const auto& t:nodes.all()){if(!q.empty()&&lower(t.title+" "+t.category).find(q)==std::string::npos)continue;if(ImGui::Selectable((t.category+" / "+t.title).c_str()))o->script.nodes.push_back(makeNode(t,70+(float)o->script.nodes.size()*18,70+(float)o->script.nodes.size()*14));}ImGui::EndChild();ImGui::SameLine();
        ImGui::BeginChild("graph",{0,0},true,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);drawGraph(o->script);ImGui::EndChild();ImGui::End();
    }

    void drawGraph(ScriptGraph& graph) {
        ImDrawList* dl=ImGui::GetWindowDrawList();ImVec2 origin=ImGui::GetCursorScreenPos();ImVec2 region=ImGui::GetContentRegionAvail();dl->AddRectFilled(origin,{origin.x+region.x,origin.y+region.y},IM_COL32(24,27,32,255));float step=32;for(float x=std::fmod(graphPan.x,step);x<region.x;x+=step)dl->AddLine({origin.x+x,origin.y},{origin.x+x,origin.y+region.y},IM_COL32(42,45,52,160));for(float y=std::fmod(graphPan.y,step);y<region.y;y+=step)dl->AddLine({origin.x,origin.y+y},{origin.x+region.x,origin.y+y},IM_COL32(42,45,52,160));
        if(ImGui::IsWindowHovered()&&ImGui::IsMouseDragging(ImGuiMouseButton_Middle)){graphPan.x+=ImGui::GetIO().MouseDelta.x;graphPan.y+=ImGui::GetIO().MouseDelta.y;}
        std::unordered_map<std::uint64_t,ImVec2> pinPos;
        for(auto& n:graph.nodes){ImVec2 p={origin.x+graphPan.x+n.x,origin.y+graphPan.y+n.y};ImVec2 sz={210,44+22.0f*(float)std::max(n.inputs.size(),n.outputs.size())};ImGui::SetCursorScreenPos(p);ImGui::PushID((int)n.id);ImGui::InvisibleButton("node",sz);if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(ImGuiMouseButton_Left)){n.x+=ImGui::GetIO().MouseDelta.x;n.y+=ImGui::GetIO().MouseDelta.y;p={origin.x+graphPan.x+n.x,origin.y+graphPan.y+n.y};}dl->AddRectFilled(p,{p.x+sz.x,p.y+sz.y},IM_COL32(45,49,57,255),6);dl->AddRectFilled(p,{p.x+sz.x,p.y+28},IM_COL32(65,81,105,255),6,ImDrawFlags_RoundCornersTop);dl->AddText({p.x+9,p.y+6},IM_COL32_WHITE,n.title.c_str());
            for(size_t i=0;i<n.inputs.size();++i){ImVec2 pp={p.x+8,p.y+42+(float)i*22};pinPos[n.inputs[i].id]=pp;dl->AddCircleFilled(pp,5,IM_COL32(100,180,230,255));dl->AddText({pp.x+10,pp.y-7},IM_COL32(210,215,225,255),n.inputs[i].name.c_str());}
            for(size_t i=0;i<n.outputs.size();++i){ImVec2 pp={p.x+sz.x-8,p.y+42+(float)i*22};pinPos[n.outputs[i].id]=pp;dl->AddCircleFilled(pp,5,IM_COL32(235,170,80,255));float tw=ImGui::CalcTextSize(n.outputs[i].name.c_str()).x;dl->AddText({pp.x-10-tw,pp.y-7},IM_COL32(210,215,225,255),n.outputs[i].name.c_str());}
            if(ImGui::BeginPopupContextItem("nodeMenu")){if(ImGui::MenuItem("Eliminar nodo")){deleteNodeId=n.id;}ImGui::EndPopup();}ImGui::PopID();}
        for(auto& l:graph.links){auto a=pinPos.find(l.fromPin),b=pinPos.find(l.toPin);if(a!=pinPos.end()&&b!=pinPos.end()){ImVec2 p1=a->second,p2=b->second;dl->AddBezierCubic(p1,{p1.x+65,p1.y},{p2.x-65,p2.y},p2,IM_COL32(180,190,205,255),2.5f);}}
        if(deleteNodeId){std::vector<std::uint64_t> pins;for(auto& n:graph.nodes)if(n.id==deleteNodeId){for(auto&p:n.inputs)pins.push_back(p.id);for(auto&p:n.outputs)pins.push_back(p.id);}graph.links.erase(std::remove_if(graph.links.begin(),graph.links.end(),[&](auto& l){return std::find(pins.begin(),pins.end(),l.fromPin)!=pins.end()||std::find(pins.begin(),pins.end(),l.toPin)!=pins.end();}),graph.links.end());graph.nodes.erase(std::remove_if(graph.nodes.begin(),graph.nodes.end(),[&](auto& n){return n.id==deleteNodeId;}),graph.nodes.end());deleteNodeId=0;}
    }

    void statusPanel(){ImGui::Begin("Estado");ImGui::Text("Proyecto: %s",root.generic_string().c_str());ImGui::SameLine();ImGui::TextDisabled("| %zu objetos | %zu símbolos C++ catalogados",objects.size(),api.symbols.size());ImGui::End();}

    void saveScene(){fs::path dir=root/"projects";std::error_code ec;fs::create_directories(dir,ec);std::ofstream out(dir/"studio-v2.scene",std::ios::binary);if(!out)return;out<<"CNCGZH_STUDIO_SCENE_V2\n";for(auto& o:objects){out<<"OBJECT|"<<o.id<<'|'<<(int)o.kind<<'|'<<o.name<<'|'<<o.transform.position[0]<<'|'<<o.transform.position[1]<<'|'<<o.transform.position[2]<<'|'<<o.transform.rotation[0]<<'|'<<o.transform.rotation[1]<<'|'<<o.transform.rotation[2]<<'|'<<o.transform.scale[0]<<'|'<<o.transform.scale[1]<<'|'<<o.transform.scale[2]<<'|'<<o.assetPath.generic_string()<<"\n";for(auto& n:o.script.nodes)out<<"NODE|"<<o.id<<'|'<<n.id<<'|'<<n.type<<'|'<<n.title<<'|'<<n.x<<'|'<<n.y<<"\n";}}

    fs::path root;std::vector<SceneObject> objects;std::vector<fs::path> assets;std::uint64_t selectedId=0,nextId=1,nextNodeId=1,nextPinId=1,deleteNodeId=0;ViewportRenderer renderer;ApiCatalog api;NodeRegistry nodes;ImGuizmo::OPERATION gizmoOp=ImGuizmo::TRANSLATE;XMFLOAT3 editorTarget{0,0,0};float editorYaw=35,editorPitch=25,editorDistance=12;ImVec2 graphPan{20,20};char assetSearch[128]{};char apiSearch[160]{};char nodeSearch[96]{};bool onlyAdapters=false;
};

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance,HINSTANCE,PWSTR,int){
    WNDCLASSEXW wc{sizeof(wc),CS_CLASSDC,wndProc,0,0,hInstance,nullptr,nullptr,nullptr,nullptr,L"CnCGZHStudioV2",nullptr};RegisterClassExW(&wc);
    g_hwnd=CreateWindowW(wc.lpszClassName,L"CnCGZH Studio v2 — Editor visual",WS_OVERLAPPEDWINDOW,100,80,1680,980,nullptr,nullptr,wc.hInstance,nullptr);
    if(!createDevice(g_hwnd)){UnregisterClassW(wc.lpszClassName,wc.hInstance);return 1;}
    ShowWindow(g_hwnd,SW_SHOWDEFAULT);UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();ImGui::CreateContext();ImGuiIO& io=ImGui::GetIO();io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_DockingEnable;io.IniFilename="CnCGZHStudioV2.ini";ImGui::StyleColorsDark();ImGuiStyle& style=ImGui::GetStyle();style.WindowRounding=5;style.FrameRounding=4;style.TabRounding=4;
    ImGui_ImplWin32_Init(g_hwnd);ImGui_ImplDX11_Init(g_device,g_context);

    StudioApp app(findProjectRoot());if(!app.initialize()){MessageBoxW(g_hwnd,L"No se pudo inicializar el renderer del viewport.",L"CnCGZH Studio v2",MB_ICONERROR);}
    bool done=false;while(!done){MSG msg;while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessage(&msg);if(msg.message==WM_QUIT)done=true;}if(done)break;ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();ImGuizmo::BeginFrame();app.frame();ImGui::Render();const float clear[4]{.07f,.075f,.085f,1};g_context->OMSetRenderTargets(1,&g_mainRTV,nullptr);g_context->ClearRenderTargetView(g_mainRTV,clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());g_swapChain->Present(1,0);}

    ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext();destroyDevice();DestroyWindow(g_hwnd);UnregisterClassW(wc.lpszClassName,wc.hInstance);return 0;
}
