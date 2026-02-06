//
// Created by vastrakai on 6/29/2024.
//
// (����������� ������ � ������� ��������, ����������� ������)
//

#include "D3DHook.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <kiero.hpp>
#include <Features/Events/RenderEvent.hpp>
#include <Features/Events/WindowResizeEvent.hpp>
#include <SDK/Minecraft/Rendering/bgfx_context.hpp>
#include <SDK/Minecraft/Rendering/GuiData.hpp>
#include <Utils/FontHelper.hpp>
#include <Utils/ProcUtils.hpp>
#include <Utils/Resource.hpp>
#include <Utils/Resources.hpp>
#include <Utils/MiscUtils/D2D.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>
#include <Utils/MiscUtils/RenderUtils.hpp>
#include <winrt/base.h>
#include <mutex>

// skidded (i do not care :trollcat:)

using winrt::com_ptr;

typedef HRESULT(__thiscall* presentD3D)(IDXGISwapChain3*, UINT, UINT);
presentD3D oPresent;
typedef HRESULT(__thiscall* resizeBuffers)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
resizeBuffers oResizeBuffers;

static com_ptr<ID3D11Device> gDevice11;
static com_ptr<ID3D11DeviceContext> gContext11;
static com_ptr<ID3D12Device> gDevice12;
static std::vector<com_ptr<ID3D11Texture2D>> mBackBuffer11Tex = std::vector<com_ptr<ID3D11Texture2D>>();
static std::vector<com_ptr<ID3D11RenderTargetView>> mBackBuffer11Rtv =
std::vector<com_ptr<ID3D11RenderTargetView>>();

static com_ptr<ID3D11DeviceContext> gDevice_context11;
static com_ptr<ID3D11On12Device> gDevice11on12;

static bool imGuiInitialized = false;

static HWND wnd = NULL;
static WNDPROC oWndProc = nullptr;
static WNDPROC gHookedWndProc = nullptr;
static WNDPROC gOriginalWndProc = nullptr;

static IDXGISwapChain3* gSwapChain = nullptr;

static bool alreadyRunningD3D11 = false;
static bool d3dInitImGui = false;

#define BUFFER_COUNT 3

struct LoadedResource {
    com_ptr<ID3D11ShaderResourceView> srv;
    int width;
    int height;
};

static std::unordered_map<std::string, LoadedResource> g_loadedResources;
static std::mutex g_loadedResourcesMutex;
static bool gCreatedImGuiContext = false;

bool D3DHook::loadTextureFromEmbeddedResource(const char* resourceName, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height)
{
    std::lock_guard<std::mutex> guard(g_loadedResourcesMutex);
    if (!resourceName || !out_srv || !out_width || !out_height) {
        spdlog::error("Invalid arguments to loadTextureFromEmbeddedResource");
        return false;
    }
    if (!gDevice11) {
        spdlog::error("D3D11 device not initialized for resource load");
        return false;
    }

    std::string rname(resourceName);
    auto it = g_loadedResources.find(rname);
    if (it != g_loadedResources.end())
    {
        if (it->second.srv) {
            *out_srv = it->second.srv.get();
            *out_width = it->second.width;
            *out_height = it->second.height;
            return true;
        }
    }

    Resource resource = ResourceLoader::Resources[resourceName];
    if (resource.data() == nullptr)
    {
        spdlog::error("Failed to load embedded resource: {0}", resourceName);
        return false;
    }
    int image_width = 0;
    int image_height = 0;
    unsigned char* image_data = stbi_load_from_memory(static_cast<stbi_uc const*>(resource.data2()), (int)resource.size(), &image_width, &image_height, nullptr, 4);
    if (image_data == NULL)
    {
        spdlog::error("Failed to load embedded image: {0}", resourceName);
        return false;
    };

    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = image_width;
    desc.Height = image_height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    ID3D11Texture2D* pTexture = NULL;
    D3D11_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = image_data;
    subResource.SysMemPitch = desc.Width * 4;
    subResource.SysMemSlicePitch = 0;

    HRESULT hr = gDevice11->CreateTexture2D(&desc, &subResource, &pTexture);
    if (FAILED(hr) || !pTexture) {
        spdlog::error("CreateTexture2D failed for resource {0} hr={1}", resourceName, hr);
        stbi_image_free(image_data);
        return false;
    }

    // Create texture view into local com_ptr
    com_ptr<ID3D11ShaderResourceView> srvLocal;
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = gDevice11->CreateShaderResourceView(pTexture, &srvDesc, srvLocal.put());
    pTexture->Release();
    pTexture = nullptr;

    if (FAILED(hr) || !srvLocal) {
        spdlog::error("CreateShaderResourceView failed for resource {0} hr={1}", resourceName, hr);
        stbi_image_free(image_data);
        return false;
    }

    *out_srv = srvLocal.get();
    *out_width = image_width;
    *out_height = image_height;
    stbi_image_free(image_data);
    g_loadedResources[rname] = { srvLocal, image_width, image_height };

    return true;
}


bool D3DHook::createTextureFromFile(const char* filename, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height)
{
    if (!filename || !out_srv || !out_width || !out_height) {
        spdlog::error("Invalid argument(s) provided to CreateTextureFromFile.");
        return false;
    }

    int width, height, channels;
    uint8_t* data = stbi_load(filename, &width, &height, &channels, STBI_rgb_alpha);
    if (!data) {
        spdlog::error("Failed to load image from file: {0}", filename);
        return false;
    }

    bool ok = createTextureFromData(data, width, height, out_srv);
    if (!ok) {
        spdlog::error("Failed to create texture from file data: {0}", filename);
        stbi_image_free(data);
        return false;
    }

    stbi_image_free(data);

    *out_width = width;
    *out_height = height;

    return true;
}

bool D3DHook::createTextureFromData(const uint8_t* data, int width, int height, ID3D11ShaderResourceView** out_srv)
{
    ID3D11Device* device = gDevice11.get();

    if (!device || !data || width <= 0 || height <= 0 || !out_srv) {
        spdlog::error("Invalid argument(s) provided to CreateTextureFromData.");
        return false;
    }

    // Define the texture description
    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = static_cast<UINT>(width);
    textureDesc.Height = static_cast<UINT>(height);
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // assuming it's 32-bit RGBA format
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    textureDesc.CPUAccessFlags = 0;
    textureDesc.MiscFlags = 0;

    // Define the subresource data
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data;
    initData.SysMemPitch = static_cast<UINT>(width * 4);
    initData.SysMemSlicePitch = 0;

    // Create the texture
    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&textureDesc, &initData, &texture);
    if (FAILED(hr) || !texture) {
        spdlog::error("Failed to create texture from data. hr={0}", hr);
        if (texture) {
            texture->Release();
            texture = nullptr;
        }
        return false;
    }

    // Create the shader resource view into a local com_ptr
    com_ptr<ID3D11ShaderResourceView> srvLocal;
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = textureDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    hr = device->CreateShaderResourceView(texture, &srvDesc, srvLocal.put());
    texture->Release();
    texture = nullptr;

    if (FAILED(hr) || !srvLocal) {
        spdlog::error("Failed to create shader resource view. hr={0}", hr);
        return false;
    }
    *out_srv = srvLocal.detach();

    return true;
}

HRESULT D3DHook::present(IDXGISwapChain3* swapChain, UINT syncInterval, UINT flags)
{
    if (Solstice::mRequestEject)
    {
        return oPresent(swapChain, syncInterval, flags);
    }
    gSwapChain = swapChain;
    static bool once = false;
    if (!once)
    {
        gDevice11 = nullptr;
        gDevice12 = nullptr;
        if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(gDevice11.put()))) && gDevice11) {
            spdlog::info("[D3D] D3D11 Device acquired");
            alreadyRunningD3D11 = true;
            static bool msgwarn = false;
            if (!msgwarn)
            {
                NotifyUtils::notify("WARNING! Solstice is currently running in D3D11 mode. \nThis is NOT recommended and may cause instability.\nI strongly advise you to use D3D12 for the time being.", 15.f, Notification::Type::Warning);
                msgwarn = true;
            }
        }

        if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(gDevice12.put()))) && gDevice12) {
            spdlog::info("[D3D] D3D12 Device acquired");

            if (Solstice::Prefs->mFallbackToD3D11)
            {
                ID3D12Device* bad_device = nullptr;
                if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&bad_device))) && bad_device)
                {
                    spdlog::warn("Removing D3D12 device [preferred fallback]");
                    winrt::com_ptr<ID3D12Device5> bad_device5;
                    if (SUCCEEDED(bad_device->QueryInterface(IID_PPV_ARGS(bad_device5.put()))) && bad_device5) {
                        bad_device5->RemoveDevice();
                    }
                    bad_device->Release();
                    return oPresent(swapChain, syncInterval, flags);
                }

            }

            UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED;

            auto bgfxCtx = bgfx_context::get();
            if (!bgfxCtx) {
                return oPresent(swapChain, syncInterval, flags);
            }
            auto renderCtx = bgfxCtx->getRenderContext();
            if (!renderCtx) {
                return oPresent(swapChain, syncInterval, flags);
            }
            auto rendererCtxD3D12 = reinterpret_cast<bgfx_d3d12_RendererContextD3D12*>(renderCtx);
            ID3D12CommandQueue* pacq = rendererCtxD3D12 ? rendererCtxD3D12->getCommandQueue() : nullptr;
            if (!pacq) {
                return oPresent(swapChain, syncInterval, flags);
            }

            winrt::com_ptr<ID3D12CommandQueue> queueRef;
            queueRef.copy_from(pacq);
            IUnknown* queues[] = { queueRef.get() };

            HRESULT hr = D3D11On12CreateDevice(
                gDevice12.get(), // the D3D12 device
                deviceFlags, // flags for the D3D11 device
                nullptr, // array of feature levels (null is default, which is what the game uses i assume)
                0, // number of feature levels in that array
                queues, // array of command queues
                1, // number of command queues in that array
                0, // node mask (0 is a magic number i actually don't know what it does)
                gDevice11.put(), // the D3D11 device we get back
                gDevice_context11.put(), // the D3D11 device context we get back
                nullptr // the feature level we get back (we don't care)
            );
            if (FAILED(hr) || !gDevice11 || !gDevice_context11) {
                spdlog::error("D3D11On12CreateDevice failed hr={0}", hr);
                return oPresent(swapChain, syncInterval, flags);
            }
        }

        once = true;
    }

    if (forceFallback && !alreadyRunningD3D11)
    {
        spdlog::warn("Forcing fallback to D3D11");

        D2D::shutdown();
        shutdownImGui();
        imGuiInitialized = false;
        d3dInitImGui = false;

        mBackBuffer11Rtv.clear();
        mBackBuffer11Tex.clear();
        if (gContext11) gContext11->Flush();

        gDevice11on12 = nullptr;
        // ���������� ������ D3D12 ����������
        ID3D12Device* bad_device = nullptr;
        if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&bad_device))) && bad_device)
        {
            spdlog::warn("Removing D3D12 device [user requested fallback]");
            winrt::com_ptr<ID3D12Device5> bad_device5;
            if (SUCCEEDED(bad_device->QueryInterface(IID_PPV_ARGS(bad_device5.put()))) && bad_device5) {
                bad_device5->RemoveDevice();
            }
            bad_device->Release();
        }
        else {
            spdlog::error("Failed to get D3D12 device");
        }

        forceFallback = false;
        once = false;

        return oPresent(swapChain, syncInterval, flags);
    }

    if (FrameTransforms)
        while (FrameTransforms->size() > transformDelay)
        {
            RenderUtils::transform = FrameTransforms->front();
            FrameTransforms->pop();
        }
    else
    {
        spdlog::error("FrameTransforms is null");
    }

    int count = alreadyRunningD3D11 ? 1 : BUFFER_COUNT;

    if (!d3dInitImGui) {
        mBackBuffer11Tex.resize(count);
        mBackBuffer11Rtv.resize(count);
        if (alreadyRunningD3D11) {
            gDevice11->GetImmediateContext(gContext11.put());
            DXGI_SWAP_CHAIN_DESC sd;
            swapChain->GetDesc(&sd);
            wnd = sd.OutputWindow;
            swapChain->GetBuffer(0, IID_PPV_ARGS(mBackBuffer11Tex.at(0).put()));
            gDevice11->CreateRenderTargetView(mBackBuffer11Tex.at(0).get(), NULL, mBackBuffer11Rtv.at(0).put());
        }
        else {
            gDevice11->GetImmediateContext(gContext11.put());
            DXGI_SWAP_CHAIN_DESC sd;
            HRESULT hrDesc = swapChain->GetDesc(&sd);
            if (FAILED(hrDesc)) {
                spdlog::error("SwapChain GetDesc failed hr={0}", hrDesc);
                d3dInitImGui = false;
                mBackBuffer11Rtv.clear();
                mBackBuffer11Tex.clear();
                return oPresent(swapChain, syncInterval, flags);
            }
            wnd = sd.OutputWindow;

            D3D11_RESOURCE_FLAGS backBuffer11Flags = {};
            backBuffer11Flags.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            HRESULT hrOn12 = gDevice11->QueryInterface(IID_PPV_ARGS(gDevice11on12.put()));
            if (FAILED(hrOn12) || !gDevice11on12) {
                spdlog::error("QueryInterface(ID3D11On12Device) failed hr={0}", hrOn12);
                d3dInitImGui = false;
                mBackBuffer11Rtv.clear();
                mBackBuffer11Tex.clear();
                return oPresent(swapChain, syncInterval, flags);
            }
            for (int i = 0; i < BUFFER_COUNT; i++) {
                winrt::com_ptr<ID3D12Resource> buffer = nullptr;

                HRESULT hrBuf = swapChain->GetBuffer(i, IID_PPV_ARGS(buffer.put()));
                if (FAILED(hrBuf) || !buffer) {
                    spdlog::error("SwapChain GetBuffer({0}) failed hr={1}", i, hrBuf);
                    d3dInitImGui = false;
                    mBackBuffer11Rtv.clear();
                    mBackBuffer11Tex.clear();
                    return oPresent(swapChain, syncInterval, flags);
                }

                HRESULT hrWrap = gDevice11on12->CreateWrappedResource(buffer.get(), &backBuffer11Flags,
                    D3D12_RESOURCE_STATE_PRESENT,
                    D3D12_RESOURCE_STATE_PRESENT,
                    IID_PPV_ARGS(mBackBuffer11Tex.at(i).put()));
                if (FAILED(hrWrap) || !mBackBuffer11Tex.at(i)) {
                    spdlog::error("CreateWrappedResource failed hr={0}", hrWrap);
                    d3dInitImGui = false;
                    mBackBuffer11Rtv.clear();
                    mBackBuffer11Tex.clear();
                    return oPresent(swapChain, syncInterval, flags);
                }

                D3D11_TEXTURE2D_DESC texDesc;
                mBackBuffer11Tex.at(i)->GetDesc(&texDesc);
                D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
                rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                rtvDesc.Format = texDesc.Format;
                rtvDesc.Texture2D.MipSlice = 0;

                HRESULT hrRtv = gDevice11->CreateRenderTargetView(mBackBuffer11Tex.at(i).get(), nullptr, mBackBuffer11Rtv.at(i).put());
                if (FAILED(hrRtv) || !mBackBuffer11Rtv.at(i)) {
                    spdlog::error("CreateRenderTargetView failed hr={0}", hrRtv);
                    d3dInitImGui = false;
                    mBackBuffer11Rtv.clear();
                    mBackBuffer11Tex.clear();
                    return oPresent(swapChain, syncInterval, flags);
                }
            }
            if (oWndProc && wnd && !gOriginalWndProc) {
                // SetWindowLongPtr ���������� ���������� proc � ���������
                gOriginalWndProc = (WNDPROC)SetWindowLongPtr(wnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
                spdlog::info("WndProc hooked, original saved");
            }
        }
        d3dInitImGui = true;
    }

    if (!wnd) {
        wnd = ProcUtils::getMinecraftWindow();
    }
    float dpi = wnd ? (float)GetDpiForWindow(wnd) : 96.0f;
    UINT index = reinterpret_cast<IDXGISwapChain3*>(swapChain)->GetCurrentBackBufferIndex();

    index = alreadyRunningD3D11 ? 0 : index;

    initImGui(gDevice11.get(), gContext11.get());
    D2D::init(swapChain, gDevice11.get());

    static ImVec2 lastWindowSize = ImGui::GetIO().DisplaySize;
    ImVec2 windowSize = ImGui::GetIO().DisplaySize;
    if (auto ci = ClientInstance::get())
    {
        if (auto gui = ci->getGuiData())
        {
            windowSize = ImVec2(gui->mResolution.x, gui->mResolution.y);
        }
    }

    if (lastWindowSize.x != windowSize.x || lastWindowSize.y != windowSize.y) {
        auto holder = nes::make_holder<WindowResizeEvent>(windowSize.x, windowSize.y);
        gFeatureManager->mDispatcher->trigger(holder);
    }

    lastWindowSize = windowSize;

    winrt::com_ptr<IDXGISurface> surface;
    if (index < (UINT)mBackBuffer11Tex.size()) {
        ID3D11Resource* resource = mBackBuffer11Tex.at(index).get();
        const bool needAcquire = !alreadyRunningD3D11 && gDevice11on12 && resource;
        struct WrappedResourcesGuard
        {
            ID3D11On12Device* device = nullptr;
            ID3D11Resource* resource = nullptr;
            ID3D11DeviceContext* ctx = nullptr;
            bool active = false;
            WrappedResourcesGuard(ID3D11On12Device* d, ID3D11Resource* r, ID3D11DeviceContext* c, bool a)
                : device(d), resource(r), ctx(c), active(a)
            {
                if (active && device && resource) {
                    device->AcquireWrappedResources(&resource, 1);
                }
            }
            ~WrappedResourcesGuard()
            {
                if (active && device && resource) {
                    device->ReleaseWrappedResources(&resource, 1);
                    if (ctx) ctx->Flush();
                }
            }
        } guard(gDevice11on12.get(), resource, gContext11.get(), needAcquire);

        mBackBuffer11Tex.at(index).try_as(surface);
        D2D::beginRender(surface.get(), dpi);

        igNewFrame();

        auto holder = nes::make_holder<RenderEvent>();
        gFeatureManager->mDispatcher->trigger(holder);

        igEndFrame();

        auto thing = mBackBuffer11Rtv.at(index).get();
        gContext11->OMSetRenderTargets(1, &thing, NULL);

        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        D2D::endRender();
    }
    else {
        spdlog::error("Present index out of range: {0} (buffer count {1})", index, mBackBuffer11Tex.size());
    }

    return oPresent(swapChain, syncInterval, flags);
}

HRESULT D3DHook::resizeBuffers(IDXGISwapChain3* swapChain, UINT bufferCount, UINT width, UINT height,
    DXGI_FORMAT newFormat, UINT swapChainFlags)
{
    if (d3dInitImGui)
    {

        // release all the stuff we created
        D2D::shutdown();
        shutdownImGui();

        mBackBuffer11Rtv.clear();
        mBackBuffer11Tex.clear();
        if (gContext11) gContext11->Flush();

        d3dInitImGui = false;

    }

    return oResizeBuffers(swapChain, bufferCount, width, height, newFormat, swapChainFlags);
}

void D3DHook::initImGui(ID3D11Device* device, ID3D11DeviceContext* deviceContext)
{
    if (imGuiInitialized) return;
    if (!ImGui::GetCurrentContext()) {
        ImGui::CreateContext();
        gCreatedImGuiContext = true;
    }

    FontHelper::load();
    static bool onc = false;
    if (!onc)
    {
        auto res = &ResourceLoader::Resources["skinblinker.txt"];

        FileUtils::writeResourceToFile(res, FileUtils::getSolsticeDir() + "BlinkerSkins\\README.txt");

        onc = true;
    }

    ImGui_ImplWin32_Init(ProcUtils::getMinecraftWindow());
    ImGui_ImplDX11_Init(device, deviceContext);

    ImGuiIO& io = ImGui::GetIO();
    if (auto ci = ClientInstance::get())
    {
        if (auto gui = ci->getGuiData())
        {
            io.DisplaySize = ImVec2(gui->mResolution.x, gui->mResolution.y);
        }
    }

    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScrollbarSize = 0.0f;
        style.Colors[ImGuiCol_ScrollbarBg].w = 0.0f;
        style.Colors[ImGuiCol_ScrollbarGrab].w = 0.0f;
        style.Colors[ImGuiCol_ScrollbarGrabHovered].w = 0.0f;
        style.Colors[ImGuiCol_ScrollbarGrabActive].w = 0.0f;
    }

    imGuiInitialized = true;
}

void D3DHook::shutdownImGui()
{
    if (imGuiInitialized) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();

        imGuiInitialized = false;
    }
}

void D3DHook::igNewFrame()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScrollbarSize = 0.0f;
        style.Colors[ImGuiCol_ScrollbarBg].w = 0.0f;
        style.Colors[ImGuiCol_ScrollbarGrab].w = 0.0f;
        style.Colors[ImGuiCol_ScrollbarGrabHovered].w = 0.0f;
        style.Colors[ImGuiCol_ScrollbarGrabActive].w = 0.0f;
    }

    ImGuiIO& io = ImGui::GetIO();
    if (auto ci = ClientInstance::get())
    {
        if (auto gui = ci->getGuiData())
        {
            io.DisplaySize = ImVec2(gui->mResolution.x, gui->mResolution.y);
        }
    }

    MathUtils::fov = RenderUtils::transform.mFov;
    if (auto ci = ClientInstance::get())
    {
        if (auto gui = ci->getGuiData())
        {
            MathUtils::displaySize = gui->mResolution;
        }
    }
    MathUtils::origin = RenderUtils::transform.mOrigin;
}

void D3DHook::igEndFrame()
{
    ImGui::EndFrame();
    ImGui::Render();
};

void D3DHook::init()
{
    mName = "D3DHook";
    s_init();
}

void D3DHook::s_init()
{
    Solstice::console->info("Initializing D3DHook");
    // Attempt to init on D3D12
    if (kiero::init(kiero::RenderType::D3D12) == kiero::Status::Success)
    {
        Solstice::console->info("Initialized kiero [D3D12]");
        kiero::bind(140, reinterpret_cast<void**>(&oPresent), reinterpret_cast<void*>(present));
        kiero::bind(145, reinterpret_cast<void**>(&oResizeBuffers), reinterpret_cast<void*>(resizeBuffers));
        return;
        // Else, attempt to init on D3D11
    }
    else if (kiero::init(kiero::RenderType::D3D11) == kiero::Status::Success)
    {
        Solstice::console->info("Initialized kiero [D3D11]");
        kiero::bind(8, reinterpret_cast<void**>(&oPresent), reinterpret_cast<void*>(present));
        kiero::bind(13, reinterpret_cast<void**>(&oResizeBuffers), reinterpret_cast<void*>(resizeBuffers));
        return;
    }
    Solstice::console->error("Failed to initialize kiero");
    MessageBoxA(NULL, "Failed to initialize kiero", "Solstice", MB_OK | MB_ICONERROR);
}

void D3DHook::shutdown()
{
    s_shutdown();
}

void D3DHook::s_shutdown()
{
    Solstice::console->info("Shutting down D3DHook");

    // ���������� frame transforms
    FrameTransforms.reset();

    // Unbind kiero (��������� ��������, ���� ���� �� ���������������)
    kiero::unbind(8);
    kiero::unbind(13);
    kiero::unbind(140);
    kiero::unbind(145);
    kiero::shutdown();

    // �������������� ��������� ImGui ���� ����
    shutdownImGui();
    if (gCreatedImGuiContext && ImGui::GetCurrentContext()) {
        ImGui::DestroyContext();
        gCreatedImGuiContext = false;
    }

    // D2D shutdown �� ������ ������
    D2D::shutdown();

    // ������� ���� ����������� �������� (SRV)
    if (!g_loadedResources.empty()) {
        for (auto& kv : g_loadedResources) {
            // com_ptr ������������� ������� Release ��� ����������
            kv.second.srv = nullptr;
        }
        g_loadedResources.clear();
    }

    // ������� RTV/Texture ������� (com_ptr ��������� ������)
    mBackBuffer11Rtv.clear();
    mBackBuffer11Tex.clear();

    if (gContext11) {
        gContext11->Flush();
    }

    // �������������� ������������� WndProc (���� �� ���������)
    if (gOriginalWndProc && wnd) {
        SetWindowLongPtr(wnd, GWLP_WNDPROC, (LONG_PTR)gOriginalWndProc);
        gOriginalWndProc = nullptr;
    }

    // ���������� com_ptr ����������/����������
    gDevice11on12 = nullptr;
    gDevice_context11 = nullptr;
    gContext11 = nullptr;
    gDevice11 = nullptr;
    gDevice12 = nullptr;

    alreadyRunningD3D11 = false;
    d3dInitImGui = false;
    imGuiInitialized = false;
}
