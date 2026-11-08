// gpu::Tracked: the pieces written since the last TakeMarked, as runs, and nothing else
// cmd /c "tools\dev.bat clang++ -std=c++20 -O2 -Isdk/runtime tests/state_tracked_test.cpp -o re/state_tracked_test.exe"
#include <array>
#include <cstdio>
#include <random>
#include <vector>

#include "gpu/state.h"

static int failures = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            failures++;                                             \
        }                                                           \
    } while (0)

template <typename A>
std::vector<bool> Taken(const A& a) {
    std::vector<bool> got(A::size(), false);
    size_t last_end = 0;
    bool first = true;
    a.TakeMarked([&](size_t i, size_t n) {
        CHECK(n > 0);
        CHECK(i + n <= A::size());
        // runs come in order and never overlap (they may touch where a mark word ends)
        CHECK(first || i >= last_end);
        first = false;
        last_end = i + n;
        for (size_t k = i; k < i + n; k++) got[k] = true;
    });
    return got;
}

template <typename A>
void Expect(const A& a, const std::vector<size_t>& written) {
    constexpr size_t per = A::kPiece / sizeof(a[0]);
    std::vector<bool> want(A::size(), false);
    for (size_t w : written)
        for (size_t k = w / per * per; k < std::min(A::size(), w / per * per + per); k++) want[k] = true;
    CHECK(Taken(a) == want);
    // taken: nothing is left
    CHECK(Taken(a) == std::vector<bool>(A::size(), false));
}

int main() {
    {
        gpu::Tracked<uint32_t, 0x800> s;  // 128 pieces, two words
        std::vector<bool> all = Taken(s);
        CHECK(all == std::vector<bool>(s.size(), true));  // new: everything marked
        CHECK(Taken(s) == std::vector<bool>(s.size(), false));
        s.set(0, 1);
        s.set(5, 2);  // same piece
        Expect(s, {0});
        CHECK(s[0] == 1 && s[5] == 2);
        // a run across the word boundary: pieces 63 and 64
        s.set(63 * 16, 3);
        s.set(64 * 16 + 15, 4);
        Expect(s, {63 * 16, 64 * 16});
        s.set(0x7FF, 5);  // last piece
        s.set(62 * 16, 6);
        Expect(s, {0x7FF, 62 * 16});
        s.edit(100) = 7;
        CHECK(s[100] == 7);
        Expect(s, {100});
    }
    {
        gpu::Tracked<std::array<float, 4>, 192> v;  // 48 pieces, one partial word
        Taken(v);
        v.set(191, {1, 2, 3, 4});
        v.edit(3)[2] = 9;
        Expect(v, {191, 3});
        CHECK(v[3][2] == 9 && v[191][3] == 4);
        std::memcpy(v.edit(4).data(), v[191].data(), 16);
        Expect(v, {4});
    }
    {
        // random writes against a plain bitmap
        std::mt19937 rng(7);
        gpu::Tracked<uint32_t, 0x800> s;
        Taken(s);
        for (int round = 0; round < 2000; round++) {
            std::vector<size_t> written;
            int n = int(rng() % 40);
            for (int k = 0; k < n; k++) {
                size_t i = rng() % s.size();
                if (rng() % 3 == 0) i = (rng() % 4) * 512 + rng() % 64;  // clusters near word edges
                s.set(i, uint32_t(round));
                written.push_back(i);
            }
            Expect(s, written);
        }
    }
    {
        // copies keep their marks, a reset marks everything
        gpu::State a;
        Taken(a.simple);
        Taken(a.vsc);
        a.simple.set(10, 1);
        gpu::State b = a;
        Expect(b.simple, {10});
        a = gpu::State{};
        CHECK(Taken(a.simple) == std::vector<bool>(a.simple.size(), true));
    }
    std::printf(failures ? "%d failures\n" : "state_tracked_test: all passed\n", failures);
    return failures ? 1 : 0;
}
