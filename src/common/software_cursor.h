#pragma once

namespace game::cursor {

void Init(void* hwnd);  // before the renderer uploads ImGui's font atlas
bool Ready();
void Draw();           // inside the ImGui frame; does not consume menu input

}  // namespace game::cursor
