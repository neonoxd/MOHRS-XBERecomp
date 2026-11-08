#pragma once
#include "cpu/x86_ops.h"

// A quick save of the whole machine: guest memory and the kernel's, the renderer's and the sound library's state
// beside it (core/snapshot.h has the parts). It is kept in memory, and written to quicksave/<name>.qsv beside the
// executable in the background, so a later run of the same build can load it too.
//
// The host's call stack cannot be saved, so Save and Load are called by one guest thread at one point it reaches
// with the same host call chain every time, the entry of the title's frame function: site is that function (the
// caller of the code that calls Save or Load), and the chain below it must match the save's.
// A load puts that thread's registers and every byte of guest memory back, so the frame starts again as the saved
// one did. Every other guest thread is stopped in a kernel wait, delay or yield meanwhile, keeps its own stack and
// goes on where it was: a later run must have the same threads at the same addresses, as it does when it boots the
// same way. Guest clocks resume from the saved moment.
namespace snapshot {

enum class Result {
    Done,
    Busy,       // another guest thread did not reach a kernel wait in time
    Missing,    // nothing saved yet, in memory or in the file
    Elsewhere,  // saved from another thread or call depth
    Mismatch,   // this run's threads, host memory or files differ from the save's
    NotYet,     // another guest thread is parked elsewhere than at the save: the game should run on and try again
};

Result Save(X86Context& ctx, const void* site);
Result Load(X86Context& ctx, const void* site);
bool Has();

}  // namespace snapshot
