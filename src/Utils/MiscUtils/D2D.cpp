//
// Created by vastrakai on 6/29/2024.
//

#include "D2D.hpp"

#include <algorithm>
#include <winrt/base.h>
#include <spdlog/spdlog.h>

struct BlurCallbackData;
static winrt::com_ptr<ID2D1Factory3> d2dFactory = nullptr;
static winrt::com_ptr<ID2D1Device> d2dDevice = nullptr;

static winrt::com_ptr<ID2D1Effect> blurEffect = nullptr;
static winrt::com_ptr<ID2D1Bitmap1> sourceBitmap = nullptr;

static winrt::com_ptr<ID2D1DeviceContext> d2dDeviceContext = nullptr;
static winrt::com_ptr<ID2D1SolidColorBrush> brush = nullptr;

bool initD2D = false;
static ID2D1Bitmap* cachedBitmap = nullptr;
static ID2D1ImageBrush* cachedBrush = nullptr;
static ID2D1RoundedRectangleGeometry* cachedClipRectGeo = nullptr;
static bool requestFlush = false;
static std::vector<winrt::com_ptr<ID2D1Bitmap>> cachedGhostBitmaps = {};
static D2D1_SIZE_U cachedGhostBitmapSize = { 0, 0 };
static bool d2dInDraw = false;

//Liro, if you're reading this, try optimizing your blur better.
//Liro, if you're reading this, try optimizing your blur better.
//Liro, if you're reading this, try optimizing your blur better.
//Liro, if you're reading this, try optimizing your blur better.
//Liro, if you're reading this, try optimizing your blur better.
//Liro, if you're reading this, try optimizing your blur better.
//Liro, if you're reading this, try optimizing your blur better.
//Liro, if you're reading this, try optimizing your blur better.
//Liro, if you're reading this, try optimizing your blur better.
//Liro, if you're reading this, try optimizing your blur better.

float dpi = 0.0f;

template <typename T>
static void EraseCallbackPtr(std::vector<std::shared_ptr<T>>& vec, T* ptr)
{
    if (!ptr) {
        return;
    }
    for (auto it = vec.begin(); it != vec.end(); ++it) {
        if (it->get() == ptr) {
            vec.erase(it);
            return;
        }
    }
}

template <typename T>
struct EraseCallbackOnExit
{
    std::vector<std::shared_ptr<T>>& vec;
    T* ptr;
    ~EraseCallbackOnExit() { EraseCallbackPtr(vec, ptr); }
};

void D2D::init(IDXGISwapChain* pSwapChain, ID3D11Device* pDevice)
{
    if (initD2D) return;
    if (!pDevice) return;

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory.put()))) {
        return;
    }

    winrt::com_ptr<IDXGIDevice> dxgiDevice;
    if (FAILED(pDevice->QueryInterface(dxgiDevice.put()))) {
        d2dFactory = nullptr;
        return;
    }

    if (FAILED(d2dFactory->CreateDevice(dxgiDevice.get(), d2dDevice.put()))) {
        d2dFactory = nullptr;
        return;
    }

    if (FAILED(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dDeviceContext.put()))) {
        d2dDevice = nullptr;
        d2dFactory = nullptr;
        return;
    }

    if (FAILED(d2dDeviceContext->CreateEffect(CLSID_D2D1GaussianBlur, blurEffect.put()))) {
        d2dDeviceContext = nullptr;
        d2dDevice = nullptr;
        d2dFactory = nullptr;
        return;
    }

    initD2D = true;
}

void D2D::shutdown()
{
    if (!initD2D) {
        return;
    }

    blurCallbacks.clear();
    ghostCallbacks.clear();

    if (d2dInDraw && d2dDeviceContext) {
        d2dDeviceContext->EndDraw();
        d2dDeviceContext->SetTarget(nullptr);
    }
    d2dInDraw = false;
    cachedGhostBitmaps.clear();
    cachedGhostBitmapSize = { 0, 0 };
    if (cachedBitmap != nullptr) {
        cachedBitmap->Release();
        cachedBitmap = nullptr;
    }
    if (cachedBrush != nullptr) {
        cachedBrush->Release();
        cachedBrush = nullptr;
    }
    if (cachedClipRectGeo != nullptr) {
        cachedClipRectGeo->Release();
        cachedClipRectGeo = nullptr;
    }
    d2dFactory = nullptr;
    d2dDevice = nullptr;
    d2dDeviceContext = nullptr;
    brush = nullptr;
    blurEffect = nullptr;
    sourceBitmap = nullptr;
    initD2D = false;
    spdlog::info("Shutdown D2D.");
}

void D2D::beginRender(IDXGISurface* surface, float fxdpi)
{
    if (!initD2D || !d2dDeviceContext || !surface) {
        return;
    }
    (void)fxdpi;

    if (d2dInDraw) {
        d2dDeviceContext->EndDraw();
        d2dDeviceContext->SetTarget(nullptr);
        d2dInDraw = false;
    }

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));
    HRESULT hr = d2dDeviceContext->CreateBitmapFromDxgiSurface(surface, &bitmapProperties, sourceBitmap.put());

    if (FAILED(hr)) {
        spdlog::error("Failed to create bitmap from DXGI surface");
        return;
    }

    d2dDeviceContext->SetTarget(sourceBitmap.get());
    d2dDeviceContext->BeginDraw();
    d2dInDraw = true;
}

void D2D::ghostFrameCallback(const ImDrawList* parent_list, const ImDrawCmd* cmd)
{
    auto data = (GhostCallbackData*)cmd->UserCallbackData;
    if (data == nullptr || !initD2D || !d2dDeviceContext || !sourceBitmap) {
        return;
    }
    EraseCallbackOnExit<GhostCallbackData> eraseOnExit{ ghostCallbacks, data };

    ImGuiIO& io = ImGui::GetIO();
    auto displaySize = io.DisplaySize;
    auto size = sourceBitmap->GetPixelSize();
    auto rect = D2D1::RectU(0, 0, size.width, size.height);
    auto destPoint = D2D1::Point2U(0, 0);
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(sourceBitmap->GetPixelFormat());

    if (cachedGhostBitmapSize.width != size.width || cachedGhostBitmapSize.height != size.height) {
        cachedGhostBitmaps.clear();
        cachedGhostBitmapSize = size;
    }

    int maxFrames = data->maxFrames;
    if (maxFrames <= 0) {
        cachedGhostBitmaps.clear();
        cachedGhostBitmapSize = size;
        return;
    }
    while (cachedGhostBitmaps.size() >= (size_t)maxFrames) {
        cachedGhostBitmaps.erase(cachedGhostBitmaps.begin());
    }
    winrt::com_ptr<ID2D1Bitmap> ghostBitmap;
    d2dDeviceContext->CreateBitmap(size, props, ghostBitmap.put());
    if (!ghostBitmap) {
        return;
    }
    ghostBitmap->CopyFromBitmap(&destPoint, sourceBitmap.get(), &rect);
    cachedGhostBitmaps.push_back(ghostBitmap);

    float alpha = 0.3f * data->strength;
    for (auto& ghostFrame : cachedGhostBitmaps) {
        d2dDeviceContext->DrawBitmap(ghostFrame.get(), D2D1::RectF(0, 0, displaySize.x, displaySize.y), alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        alpha *= data->strength;
    }
}
void D2D::addGhostFrame(ImDrawList* drawList, int maxFrames, float strength)
{
    if (!initD2D) {
        return;
    }

    auto uniqueData = std::make_shared<GhostCallbackData>(strength, maxFrames);
    auto data = uniqueData.get();
    ghostCallbacks.push_back(uniqueData);
    drawList->AddCallback(ghostFrameCallback, data);

}

void D2D::endRender()
{
    blurCallbacks.clear();
    ghostCallbacks.clear();

    if (!initD2D || !d2dDeviceContext) {
        sourceBitmap = nullptr;
    }

    if (d2dDeviceContext && d2dInDraw) {
        d2dDeviceContext->EndDraw();
        d2dInDraw = false;
    }
    if (d2dDeviceContext) {
        d2dDeviceContext->SetTarget(nullptr);
    }
    sourceBitmap = nullptr;
    if (cachedBitmap != nullptr) {
        cachedBitmap->Release();
        cachedBitmap = nullptr;
    }
    if (cachedBrush != nullptr) {
        cachedBrush->Release();
        cachedBrush = nullptr;
    }
    if (cachedClipRectGeo != nullptr) {
        cachedClipRectGeo->Release();
        cachedClipRectGeo = nullptr;
    }

}




void D2D::blurCallback(const ImDrawList* parent_list, const ImDrawCmd* cmd) {
    auto data = (BlurCallbackData*)cmd->UserCallbackData;
    if (data == nullptr || !initD2D || !d2dDeviceContext || !sourceBitmap || !blurEffect) {
        return;
    }
    EraseCallbackOnExit<BlurCallbackData> eraseOnExit{ blurCallbacks, data };

    ImVec4 clipRect = data->clipRect.has_value() ? *data->clipRect : cmd->ClipRect;
    winrt::com_ptr<ID2D1Bitmap> targetBitmap;
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(sourceBitmap->GetPixelFormat());
    d2dDeviceContext->CreateBitmap(sourceBitmap->GetPixelSize(), props, targetBitmap.put());
    if (!targetBitmap) {
        return;
    }
    auto destPoint = D2D1::Point2U(0, 0);
    auto size = sourceBitmap->GetPixelSize();
    auto rect = D2D1::RectU(0, 0, size.width, size.height);
    targetBitmap->CopyFromBitmap(&destPoint, sourceBitmap.get(), &rect);
    D2D1_RECT_F screenRectF = D2D1::RectF(0, 0, (float)size.width, (float)size.height);
    D2D1_RECT_F clipRectD2D = D2D1::RectF(
        clipRect.x,
        clipRect.y,
        clipRect.z,
        clipRect.w
    );
    D2D1_ROUNDED_RECT clipRectRounded = D2D1::RoundedRect(clipRectD2D, data->rounding, data->rounding);
    blurEffect->SetInput(0, targetBitmap.get());
    blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, data->strength);
    blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_SPEED);
    winrt::com_ptr<ID2D1Image> outImage;
    blurEffect->GetOutput(outImage.put());
    if (!outImage) {
        return;
    }
    winrt::com_ptr<ID2D1ImageBrush> outImageBrush;
    D2D1_IMAGE_BRUSH_PROPERTIES outImage_props = D2D1::ImageBrushProperties(screenRectF);
    d2dDeviceContext->CreateImageBrush(
        outImage.get(),
        outImage_props,
        outImageBrush.put()
    );
    if (!outImageBrush) {
        return;
    }
    outImageBrush->SetOpacity(std::clamp(data->opacity, 0.0f, 1.0f));

    winrt::com_ptr<ID2D1RoundedRectangleGeometry> clipRectGeo;
    d2dFactory->CreateRoundedRectangleGeometry(clipRectRounded, clipRectGeo.put());
    if (!clipRectGeo) {
        return;
    }
    d2dDeviceContext->FillGeometry(clipRectGeo.get(), outImageBrush.get());
    d2dDeviceContext->Flush();

}

bool D2D::addBlur(ImDrawList* drawList, float strength, std::optional<ImVec4> clipRect, float rounding)
{
    if (!initD2D) {
        return false;
    }

    if (strength == 0)
        return false;

    auto uniqueData = std::make_shared<BlurCallbackData>(strength, rounding, clipRect);
    auto data = uniqueData.get();
    blurCallbacks.push_back(uniqueData);
    drawList->AddCallback(blurCallback, data);
    return true;
}

bool D2D::addBlurAlpha(ImDrawList* drawList, float strength, float opacity, std::optional<ImVec4> clipRect, float rounding)
{
    if (!initD2D) {
        return false;
    }

    if (strength == 0 || opacity <= 0.0f) {
        return false;
    }

    auto uniqueData = std::make_shared<BlurCallbackData>(strength, opacity, rounding, clipRect);
    auto data = uniqueData.get();
    blurCallbacks.push_back(uniqueData);
    drawList->AddCallback(blurCallback, data);
    return true;
}

template <typename T>
void SafeRelease(T** ptr) {
    if (*ptr) {
        (*ptr)->Release();
        *ptr = nullptr;
    }
}

void D2D::blurCallbackOptimized(const ImDrawList* parent_list, const ImDrawCmd* cmd) {
    auto data = (BlurCallbackData*)cmd->UserCallbackData;
    if (data == nullptr || !initD2D || !d2dDeviceContext || !sourceBitmap || !blurEffect) {
        return;
    }
    EraseCallbackOnExit<BlurCallbackData> eraseOnExit{ blurCallbacks, data };

    ImVec4 clipRect = data->clipRect.has_value() ? *data->clipRect : cmd->ClipRect;


    D2D1_SIZE_U bitmapSize = sourceBitmap->GetPixelSize();
    if (cachedBitmap == nullptr || cachedBitmap->GetPixelSize().width != bitmapSize.width || cachedBitmap->GetPixelSize().height != bitmapSize.height) {
        SafeRelease(&cachedBitmap);
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(sourceBitmap->GetPixelFormat());
        d2dDeviceContext->CreateBitmap(bitmapSize, props, &cachedBitmap);
    }
    if (cachedBitmap == nullptr) {
        return;
    }
    {
        auto destPoint = D2D1::Point2U(0, 0);
        auto rect = D2D1::RectU(0, 0, bitmapSize.width, bitmapSize.height);
        cachedBitmap->CopyFromBitmap(&destPoint, sourceBitmap.get(), &rect);
    }
    static ImVec4 cachedClipRect;
    static float cachedRounding = -1.0f;
    if (cachedClipRectGeo == nullptr || cachedClipRect != clipRect || cachedRounding != data->rounding) {
        SafeRelease(&cachedClipRectGeo);
        cachedClipRect = clipRect;
        cachedRounding = data->rounding;

        D2D1_RECT_F clipRectD2D = D2D1::RectF(clipRect.x, clipRect.y, clipRect.z, clipRect.w);
        D2D1_ROUNDED_RECT clipRectRounded = D2D1::RoundedRect(clipRectD2D, data->rounding, data->rounding);
        d2dFactory->CreateRoundedRectangleGeometry(clipRectRounded, &cachedClipRectGeo);
    }
    if (cachedClipRectGeo == nullptr) {
        return;
    }
    static float cachedStrength = -1.0f;
    static D2D1_SIZE_U cachedBrushSize = { 0, 0 };
    const bool needBrushUpdate = (cachedBrush == nullptr) || (cachedStrength != data->strength) ||
        (cachedBrushSize.width != bitmapSize.width) || (cachedBrushSize.height != bitmapSize.height);
    if (needBrushUpdate) {
        SafeRelease(&cachedBrush);
        winrt::com_ptr<ID2D1Image> outImage;
        blurEffect->SetInput(0, cachedBitmap);
        blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, data->strength);
        blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
        blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_SPEED);
        blurEffect->GetOutput(outImage.put());
        if (!outImage) {
            return;
        }

        D2D1_IMAGE_BRUSH_PROPERTIES brushProps = D2D1::ImageBrushProperties(
            D2D1::RectF(0, 0, (float)bitmapSize.width, (float)bitmapSize.height)
        );
        d2dDeviceContext->CreateImageBrush(outImage.get(), brushProps, &cachedBrush);
        if (cachedBrush == nullptr) {
            return;
        }
        cachedStrength = data->strength;
        cachedBrushSize = bitmapSize;
    }
    if (cachedBrush == nullptr) {
        return;
    }

    winrt::com_ptr<ID2D1Image> originalTarget;
    d2dDeviceContext->GetTarget(originalTarget.put());

    d2dDeviceContext->SetTarget(sourceBitmap.get());
    cachedBrush->SetOpacity(std::clamp(data->opacity, 0.0f, 1.0f));
    d2dDeviceContext->FillGeometry(cachedClipRectGeo, cachedBrush);
    d2dDeviceContext->SetTarget(originalTarget.get());
    d2dDeviceContext->Flush();
}


bool D2D::addBlurOptimized(ImDrawList* drawList, float strength, std::optional<ImVec4> clipRect, float rounding)
{
    if (!initD2D) {
        return false;
    }

    if (strength == 0) {
        return false;
    }

    auto uniqueData = std::make_shared<BlurCallbackData>(strength, rounding, clipRect);
    auto data = uniqueData.get();
    blurCallbacks.push_back(uniqueData);
    drawList->AddCallback(blurCallbackOptimized, data);
    return true;
}

bool D2D::addBlurOptimizedAlpha(ImDrawList* drawList, float strength, float opacity, std::optional<ImVec4> clipRect, float rounding)
{
    if (!initD2D) {
        return false;
    }

    if (strength == 0 || opacity <= 0.0f) {
        return false;
    }

    auto uniqueData = std::make_shared<BlurCallbackData>(strength, opacity, rounding, clipRect);
    auto data = uniqueData.get();
    blurCallbacks.push_back(uniqueData);
    drawList->AddCallback(blurCallbackOptimized, data);
    return true;
}
