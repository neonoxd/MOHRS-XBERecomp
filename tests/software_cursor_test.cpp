// Build with -DUNICODE -D_UNICODE. Link with software_cursor.cpp, ImGui's four .cpp files, the build's cursor.rc.res,
// and ole32.lib windowscodecs.lib user32.lib. No game, GPU or window is needed.
#include "common/software_cursor.h"
#include "core/log.h"

#include <cstdio>
#include <cstdlib>
#include <imgui.h>

namespace input { bool Playing() { return false; } }
namespace game { bool MenuMouseCursorVisible() { return false; } }
namespace app { bool Widescreen() { return true; } }
namespace logx { void Write(Level, const char*, ...) {} }

void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main() {
    ImGui::CreateContext();
    Check(!game::cursor::Ready(), "cursor starts unavailable");
    game::cursor::Init(nullptr);
    Check(game::cursor::Ready(), "embedded PNG decoded and added to atlas");
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    const ImFontAtlasCustomRect* cursor = nullptr;
    for (const auto& rect : atlas->CustomRects)
        if (rect.Width == 128 && rect.Height == 128) cursor = &rect;
    Check(cursor && cursor->IsPacked(), "PNG has a packed 128x128 atlas rectangle");
    Check(atlas->TexPixelsUseColors, "atlas advertises colour pixels");
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    atlas->GetTexDataAsRGBA32(&pixels, &width, &height);  // same request as the DX12 uploader
    Check(pixels && width > 0 && height > 0, "RGBA upload pixels available");
    auto sample = [&](int x, int y, int r, int g, int b, int a) {
        const auto* p = pixels + ((cursor->Y + y) * width + cursor->X + x) * 4;
        Check(p[0] == r && p[1] == g && p[2] == b && p[3] == a, "PNG colour/alpha preserved at upload");
    };
    sample(0, 0, 255, 255, 255, 0);
    sample(20, 6, 182, 150, 106, 255);
    sample(40, 40, 155, 47, 39, 253);
    sample(60, 50, 203, 173, 145, 252);
    sample(90, 100, 78, 67, 56, 252);
    sample(127, 127, 255, 255, 255, 0);
    Check(atlas->Fonts.Size && atlas->Fonts[0]->FindGlyph('F'), "FPS font remains available");
    ImVec2 uv0, uv1;
    atlas->CalcCustomRectUV(cursor, &uv0, &uv1);
    Check(uv0.x >= 0 && uv0.y >= 0 && uv1.x <= 1 && uv1.y <= 1 &&
          uv1.x > uv0.x && uv1.y > uv0.y, "cursor UVs are valid");
    game::cursor::Draw();  // no focused game window: no drawing or input consumption
    ImGui::DestroyContext();
    std::puts("software cursor: embedded PNG, RGBA upload, transparency and font coexistence passed");
}
