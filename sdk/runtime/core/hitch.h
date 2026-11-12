#pragma once
// Hitch recorder (XBE_HITCH): timed host events in a ring of the last seconds. A present that comes late logs what
// ran in its frame; while a present is overdue the guest threads' stacks are sampled; F8 writes the last seconds to
// logs\hitch_<time>.txt. See docs/debugging.md.
#include <cstdarg>
#include <cstdint>

namespace hitch {

enum class Kind : uint8_t {
    Frame,       // present to present on the render thread
    TitleFrame,  // the title's present to present
    Shader,      // a shader compiled
    Pipeline,    // a pipeline state made
    Texture,     // a texture resource made
    Target,      // a render target resource made
    Upload,      // texture bytes converted and uploaded
    Buffer,      // an upload buffer of its own (too big for the frame's ring)
    FileOpen,
    FileRead,
    TitleWait,   // the title waited for the render thread
    Swap,        // the swap chain's Present call
    Fence,       // the render thread waited for the GPU
    ReadBack,    // GPU pixels brought back for the CPU
    Stack,       // a guest thread's stack sampled while a present was overdue
    Precompile,  // a shader compiled on a background thread (PrecompileVertexProgram)
    Count
};

// XBE_HITCH set: 1 (or anything below 2) picks the slow frame threshold from the recent frames, a larger number is
// the threshold in milliseconds
bool On();
int64_t Now();  // steady clock, ns

// the calling thread's name in the record (render, audio); guest threads are named by their id
void NameThread(const char* name);

void Record(Kind k, int64_t start, int64_t end, const char* fmt, ...) __attribute__((format(printf, 4, 5)));

// times its scope, recorded at the end when it took at least the kind's minimum; Text names it
class Scope {
public:
    explicit Scope(Kind k) : k_(k), t_(On() ? Now() : 0) {}
    ~Scope();
    void Text(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Kind k_;
    int64_t t_;
    char text_[112] = {};
};

void Present();       // render thread, after each swap chain Present
void TitlePresent();  // the title's present
void Mark();          // the mark key: writes the last seconds to a file

}  // namespace hitch
