#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// EA's FEng menu packages (.fng in the shell and pause screen archives), read and written on the host so the menus
// can gain screens and buttons before the game's package reader (0x99AB0) sees them. A package is chunks with a four
// character tag and a 32-bit size; objects, scripts and message lists hold fields with a two character tag and a
// 16-bit size, each padded to four bytes.
namespace feng {

// the game's name hash (0x8D960): object, script and message names, upper case
uint32_t Hash(std::string_view text);

constexpr uint16_t Tag2(const char (&t)[3]) { return uint16_t(uint8_t(t[0]) | uint8_t(t[1]) << 8); }

struct Field {
    uint16_t tag;
    std::vector<uint8_t> data;
};

// a chunk of an object, its description (ObjD), a script (Scrp) or messages it answers (MsgR), or of the package
struct Chunk {
    uint32_t tag;
    std::vector<uint8_t> raw;  // when it has no fields
    std::vector<Field> fields;
    bool has_fields = false;

    Field* Find(uint16_t tag);
    const Field* Find(uint16_t tag) const;
};

struct Object {
    Chunk desc;                   // ObjD
    std::vector<Chunk> chunks;    // Scrp and MsgR, in file order

    std::string Name() const;
    uint32_t Guid() const;
    uint32_t Flags() const;
    uint32_t Parent() const;  // the group's GUID, 0 at the top
    bool IsButton() const { return (Flags() & 0x10000000) != 0; }
    float X() const;
    float Y() const;

    void SetName(std::string_view name);  // its hash too, which the game finds it by
    void SetGuid(uint32_t guid);
    void SetParent(uint32_t guid);
    void Move(float dx, float dy);
    // a string object's text and the label the localisation looks up; an unknown label keeps the text
    void SetText(std::string_view text, std::string_view label);
    // the overlay a button's messages open: every message path (Rs) names file in its folder instead
    void SetPathFile(std::string_view file);
    void ReplaceTargets(uint32_t from, uint32_t to);  // script events sent to an object
};

class Package {
public:
    static std::optional<Package> Parse(const uint8_t* data, size_t size);
    std::vector<uint8_t> Write() const;

    std::string Name() const;
    std::string Path() const;  // where the editor saved it
    void SetName(std::string_view name, std::string_view path);

    std::vector<Object> objects;  // in file order, a group's children after it

    Object* Find(std::string_view name);
    const Object* Find(std::string_view name) const;
    size_t Index(std::string_view name) const;  // objects.size() if none
    // the object and its children, children's children too
    void Remove(std::string_view name);
    uint32_t NextGuid() const;

    // the package's own message answers (PkgR) and script events: a button set current by GUID
    void ReplaceTargets(uint32_t from, uint32_t to);
    // the message target lists (Targ): add next to every list holding like, or drop from all
    void AddTarget(uint32_t like, uint32_t guid);
    void RemoveTarget(uint32_t guid);

private:
    std::vector<Chunk> top_;  // PkHd, TypS, ... in file order; the object chunk is rebuilt from objects
    uint32_t objects_tag_ = 0;
    size_t objects_at_ = 0;
};

}  // namespace feng
