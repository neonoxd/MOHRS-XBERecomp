#include "common/software_cursor.h"

#include <algorithm>
#include <cstring>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <imgui.h>

#include "app/app.h"
#include "common/mouse_keyboard.h"
#include "common/menu_mouse.h"
#include "core/log.h"

namespace game::cursor {
namespace {

HWND g_hwnd = nullptr;
int g_rect = -1;

bool ReadPixels(std::vector<unsigned char>& pixels, UINT& width, UINT& height) {
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(module, L"MOHRS_CURSOR", RT_RCDATA);
    if (!resource) return false;
    DWORD bytes = SizeofResource(module, resource);
    auto data = static_cast<BYTE*>(LockResource(LoadResource(module, resource)));
    if (!data || !bytes) return false;

    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    if (SUCCEEDED(hr)) hr = wic->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromMemory(data, bytes);
    if (SUCCEEDED(hr)) hr = wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = wic->CreateFormatConverter(&converter);
    // ImGui's renderer expects straight RGBA, preserving the PNG's colours and transparent edges.
    if (SUCCEEDED(hr)) hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) hr = converter->GetSize(&width, &height);
    if (FAILED(hr) || !width || !height || width > 1024 || height > 1024) return false;
    pixels.resize(size_t(width) * height * 4);
    return SUCCEEDED(converter->CopyPixels(nullptr, width * 4, UINT(pixels.size()), pixels.data()));
}

}  // namespace

void Init(void* hwnd) {
    g_hwnd = static_cast<HWND>(hwnd);
    std::vector<unsigned char> pixels;
    UINT width = 0, height = 0;
    HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool loaded = ReadPixels(pixels, width, height);
    if (SUCCEEDED(apartment)) CoUninitialize();
    if (!loaded) {
        LOGW("software cursor: embedded cursor.png could not be decoded; using system arrow");
        return;
    }

    // Share the existing overlay texture: no extra GPU resources or upload path are needed.
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    int index = atlas->AddCustomRectRegular(int(width), int(height));
    unsigned char* texture = nullptr;
    int texture_width = 0, texture_height = 0;
    atlas->GetTexDataAsRGBA32(&texture, &texture_width, &texture_height);
    const ImFontAtlasCustomRect* rect = atlas->GetCustomRectByIndex(index);
    if (!texture || !rect->IsPacked()) {
        LOGW("software cursor: overlay atlas could not be built; using system arrow");
        return;
    }
    for (UINT y = 0; y < height; ++y)
        std::memcpy(texture + (size_t(rect->Y + y) * texture_width + rect->X) * 4,
                    pixels.data() + size_t(y) * width * 4, size_t(width) * 4);
    atlas->TexPixelsUseColors = true;
    g_rect = index;
    POINT point;
    if (GetForegroundWindow() == g_hwnd && GetCursorPos(&point) && WindowFromPoint(point) == g_hwnd)
        SetCursor(nullptr);  // no duplicate system arrow when the pointer is stationary at startup
    LOGR("software cursor: embedded cursor.png, %ux%u", width, height);
}

bool Ready() { return g_rect >= 0; }

void Draw() {
    if (!Ready() || !MenuMouseCursorVisible() || input::Playing() || GetForegroundWindow() != g_hwnd) return;
    POINT point;
    RECT client;
    if (!GetCursorPos(&point) || WindowFromPoint(point) != g_hwnd ||
        !ScreenToClient(g_hwnd, &point) || !GetClientRect(g_hwnd, &client)) return;
    if (point.x < 0 || point.y < 0 || point.x >= client.right || point.y >= client.bottom) return;

    const ImGuiIO& io = ImGui::GetIO();
    if (client.right <= 0 || client.bottom <= 0 || io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return;
    // Use the same client coordinates as menu hit testing, independent of the guest picture's bars/stretch.
    ImVec2 at(float(point.x) * io.DisplaySize.x / float(client.right),
              float(point.y) * io.DisplaySize.y / float(client.bottom));
    float aspect = app::Widescreen() ? 16.0f / 9.0f : 4.0f / 3.0f;
    float picture_height = std::min(io.DisplaySize.y, io.DisplaySize.x / aspect);
    // 24px at a 720px picture height. The arrow tip is (20, 6) in the supplied 128px PNG.
    float size = std::max(12.0f, picture_height / 30.0f);
    ImVec2 origin(at.x - size * (20.0f / 128.0f), at.y - size * (6.0f / 128.0f));
    ImFontAtlas* atlas = io.Fonts;
    const ImFontAtlasCustomRect* rect = atlas->GetCustomRectByIndex(g_rect);
    ImVec2 uv0, uv1;
    atlas->CalcCustomRectUV(rect, &uv0, &uv1);
    ImGui::GetForegroundDrawList()->AddImage(atlas->TexID, origin,
        ImVec2(origin.x + size, origin.y + size), uv0, uv1);
}

}  // namespace game::cursor
