#include "common/feng.h"

#include <algorithm>
#include <cstring>

namespace feng {

namespace {

constexpr uint32_t Tag4(const char (&t)[5]) {
    return uint32_t(uint8_t(t[0])) | uint32_t(uint8_t(t[1])) << 8 | uint32_t(uint8_t(t[2])) << 16 |
           uint32_t(uint8_t(t[3])) << 24;
}
constexpr uint32_t kPackage = 0xE76E4546;  // "FEn" 0xE7
constexpr uint32_t kHeader = Tag4("PkHd");
constexpr uint32_t kObjects = 0xCC6A624F;  // "Obj" 0xCC
constexpr uint32_t kObject = 0xEA624F46;   // "FOb" 0xEA
constexpr uint32_t kButtons = Tag4("Butn");
constexpr uint32_t kDesc = Tag4("ObjD");
constexpr uint32_t kScript = Tag4("Scrp");
constexpr uint32_t kMessages = Tag4("MsgR");
constexpr uint32_t kAnswers = Tag4("PkgR");
constexpr uint32_t kTargets = Tag4("Targ");
constexpr uint32_t kEditor = Tag4("EDat");
constexpr uint32_t kSetButton = 0x100;  // a message answer making the button with GUID Ru current
constexpr size_t kPosition = 28;        // in SA, after the colour and the pivot
constexpr size_t kEventSize = 12;       // a script event (EV): message, target GUID, time

uint32_t Get32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
uint32_t Get32(const std::vector<uint8_t>& d, size_t at) { return at + 4 <= d.size() ? Get32(d.data() + at) : 0; }
void Put32(std::vector<uint8_t>& d, size_t at, uint32_t v) {
    if (at + 4 <= d.size()) std::memcpy(d.data() + at, &v, 4);
}
void Append32(std::vector<uint8_t>& d, uint32_t v) {
    uint8_t b[4];
    std::memcpy(b, &v, 4);
    d.insert(d.end(), b, b + 4);
}
void Pad(std::vector<uint8_t>& d) {
    while (d.size() % 4) d.push_back(0);
}
std::vector<uint8_t> Ascii(std::string_view s) {
    std::vector<uint8_t> d(s.begin(), s.end());
    d.push_back(0);
    Pad(d);
    return d;
}
std::vector<uint8_t> U32(uint32_t v) {
    std::vector<uint8_t> d;
    Append32(d, v);
    return d;
}

bool HasFields(uint32_t tag) {
    return tag == kDesc || tag == kScript || tag == kMessages || tag == kAnswers || tag == kTargets || tag == kEditor;
}

bool ParseFields(const uint8_t* p, size_t size, std::vector<Field>& out) {
    size_t at = 0;
    while (at < size) {
        if (size - at < 4) return false;
        uint16_t tag, n;
        std::memcpy(&tag, p + at, 2);
        std::memcpy(&n, p + at + 2, 2);
        if (at + 4 + n > size) return false;
        out.push_back({tag, std::vector<uint8_t>(p + at + 4, p + at + 4 + n)});
        at += 4 + n;
    }
    return true;
}

// a run of chunks: tag, size, data
bool ParseChunks(const uint8_t* p, size_t size, std::vector<Chunk>& out) {
    size_t at = 0;
    while (at < size) {
        if (size - at < 8) return false;
        Chunk c;
        c.tag = Get32(p + at);
        uint32_t n = Get32(p + at + 4);
        if (n > size - at - 8) return false;
        const uint8_t* d = p + at + 8;
        if (HasFields(c.tag)) {
            c.has_fields = true;
            if (!ParseFields(d, n, c.fields)) return false;
        } else {
            c.raw.assign(d, d + n);
        }
        out.push_back(std::move(c));
        at += 8 + n;
    }
    return true;
}

void WriteChunk(std::vector<uint8_t>& out, uint32_t tag, const std::vector<uint8_t>& data) {
    Append32(out, tag);
    Append32(out, uint32_t(data.size()));
    out.insert(out.end(), data.begin(), data.end());
}

std::vector<uint8_t> ChunkData(const Chunk& c) {
    if (!c.has_fields) return c.raw;
    std::vector<uint8_t> d;
    for (const Field& f : c.fields) {
        uint16_t n = uint16_t(f.data.size());
        d.push_back(uint8_t(f.tag));
        d.push_back(uint8_t(f.tag >> 8));
        d.push_back(uint8_t(n));
        d.push_back(uint8_t(n >> 8));
        d.insert(d.end(), f.data.begin(), f.data.end());
    }
    return d;
}

void Set(Chunk& c, uint16_t tag, std::vector<uint8_t> data) {
    if (Field* f = c.Find(tag)) {
        f->data = std::move(data);
    } else {
        c.fields.push_back({tag, std::move(data)});
    }
}

// message answers (MsgR, PkgR): Mi the message, MC the answers, each Ri the kind, Ru or Rs its argument, Rt
void ReplaceSetButton(Chunk& c, uint32_t from, uint32_t to) {
    uint32_t kind = 0;
    for (Field& f : c.fields) {
        if (f.tag == Tag2("Ri")) kind = Get32(f.data, 0);
        if (f.tag == Tag2("Ru") && kind == kSetButton && Get32(f.data, 0) == from) Put32(f.data, 0, to);
    }
}

}  // namespace

uint32_t Hash(std::string_view text) {
    uint32_t h = 0xFFFFFFFF;
    for (char ch : text) {
        uint8_t c = uint8_t(ch);
        if (c >= 'a' && c <= 'z') c -= 0x20;
        h = h * 33 + c;
    }
    return h;
}

Field* Chunk::Find(uint16_t tag) {
    for (Field& f : fields)
        if (f.tag == tag) return &f;
    return nullptr;
}

const Field* Chunk::Find(uint16_t tag) const {
    for (const Field& f : fields)
        if (f.tag == tag) return &f;
    return nullptr;
}

// OP: GUID, name hash, flags, resource index
std::string Object::Name() const {
    const Field* f = desc.Find(Tag2("On"));
    if (!f) return {};
    const char* name = reinterpret_cast<const char*>(f->data.data());
    return std::string(name, strnlen(name, f->data.size()));
}

uint32_t Object::Guid() const {
    const Field* f = desc.Find(Tag2("OP"));
    return f ? Get32(f->data, 0) : 0;
}

uint32_t Object::Flags() const {
    const Field* f = desc.Find(Tag2("OP"));
    return f ? Get32(f->data, 8) : 0;
}

uint32_t Object::Parent() const {
    const Field* f = desc.Find(Tag2("PA"));
    return f ? Get32(f->data, 0) : 0;
}

float Object::X() const {
    const Field* f = desc.Find(Tag2("SA"));
    float v = 0.0f;
    if (f && f->data.size() >= kPosition + 8) std::memcpy(&v, f->data.data() + kPosition, 4);
    return v;
}

float Object::Y() const {
    const Field* f = desc.Find(Tag2("SA"));
    float v = 0.0f;
    if (f && f->data.size() >= kPosition + 8) std::memcpy(&v, f->data.data() + kPosition + 4, 4);
    return v;
}

void Object::SetName(std::string_view name) {
    Set(desc, Tag2("On"), Ascii(name));
    if (Field* f = desc.Find(Tag2("OP"))) Put32(f->data, 4, Hash(name));
}

void Object::SetGuid(uint32_t guid) {
    if (Field* f = desc.Find(Tag2("OP"))) Put32(f->data, 0, guid);
}

void Object::SetParent(uint32_t guid) {
    if (Field* f = desc.Find(Tag2("PA"))) Put32(f->data, 0, guid);
}

void Object::Move(float dx, float dy) {
    Field* f = desc.Find(Tag2("SA"));
    if (!f || f->data.size() < kPosition + 8) return;
    float x, y;
    std::memcpy(&x, f->data.data() + kPosition, 4);
    std::memcpy(&y, f->data.data() + kPosition + 4, 4);
    x += dx;
    y += dy;
    std::memcpy(f->data.data() + kPosition, &x, 4);
    std::memcpy(f->data.data() + kPosition + 4, &y, 4);
}

// St the UTF-16 text, Sb the characters its buffer holds, SL the label and SH its hash
void Object::SetText(std::string_view text, std::string_view label) {
    std::vector<uint8_t> wide;
    for (char c : text) {
        wide.push_back(uint8_t(c));
        wide.push_back(0);
    }
    wide.push_back(0);
    wide.push_back(0);
    Pad(wide);
    Set(desc, Tag2("St"), std::move(wide));
    if (Field* f = desc.Find(Tag2("Sb"))) Put32(f->data, 0, std::max<uint32_t>(Get32(f->data, 0), uint32_t(text.size() + 1)));
    Set(desc, Tag2("SL"), Ascii(label));
    Set(desc, Tag2("SH"), U32(Hash(label)));
}

void Object::SetPathFile(std::string_view file) {
    for (Chunk& c : chunks) {
        if (c.tag != kMessages) continue;
        for (Field& f : c.fields) {
            if (f.tag != Tag2("Rs")) continue;
            const char* text = reinterpret_cast<const char*>(f.data.data());
            std::string path(text, strnlen(text, f.data.size()));
            size_t slash = path.find_last_of("\\/");
            path.replace(slash == std::string::npos ? 0 : slash + 1, std::string::npos, file);
            f.data = Ascii(path);
        }
    }
}

void Object::ReplaceTargets(uint32_t from, uint32_t to) {
    for (Chunk& c : chunks) {
        if (c.tag == kMessages) ReplaceSetButton(c, from, to);
        if (c.tag != kScript) continue;
        for (Field& f : c.fields) {
            if (f.tag != Tag2("EV")) continue;
            for (size_t at = 0; at + kEventSize <= f.data.size(); at += kEventSize)
                if (Get32(f.data, at + 4) == from) Put32(f.data, at + 4, to);
        }
    }
}

std::optional<Package> Package::Parse(const uint8_t* data, size_t size) {
    if (size < 8 || Get32(data) != kPackage) return std::nullopt;
    uint32_t n = Get32(data + 4);
    if (n > size - 8) return std::nullopt;
    Package p;
    if (!ParseChunks(data + 8, n, p.top_)) return std::nullopt;
    auto objects = std::find_if(p.top_.begin(), p.top_.end(), [](const Chunk& c) { return c.tag == kObjects; });
    if (objects == p.top_.end() || p.top_.empty() || p.top_[0].tag != kHeader) return std::nullopt;
    std::vector<Chunk> inside;
    if (!ParseChunks(objects->raw.data(), objects->raw.size(), inside)) return std::nullopt;
    for (const Chunk& c : inside) {
        if (c.tag == kButtons) continue;  // written again from the objects
        if (c.tag != kObject) return std::nullopt;
        std::vector<Chunk> parts;
        if (!ParseChunks(c.raw.data(), c.raw.size(), parts) || parts.empty() || parts[0].tag != kDesc) return std::nullopt;
        Object o;
        o.desc = std::move(parts[0]);
        o.chunks.assign(std::make_move_iterator(parts.begin() + 1), std::make_move_iterator(parts.end()));
        p.objects.push_back(std::move(o));
    }
    p.objects_tag_ = objects->tag;
    p.objects_at_ = size_t(objects - p.top_.begin());
    p.top_.erase(objects);
    return p;
}

std::vector<uint8_t> Package::Write() const {
    // the button count sizes the button map the reader fills (0x88540)
    std::vector<uint8_t> list;
    uint32_t buttons = 0, top = 0;
    for (const Object& o : objects) {
        buttons += o.IsButton();
        top += o.Parent() == 0;
    }
    WriteChunk(list, kButtons, U32(buttons));
    for (const Object& o : objects) {
        std::vector<uint8_t> d;
        WriteChunk(d, o.desc.tag, ChunkData(o.desc));
        for (const Chunk& c : o.chunks) WriteChunk(d, c.tag, ChunkData(c));
        WriteChunk(list, kObject, d);
    }
    std::vector<uint8_t> body;
    for (size_t i = 0; i <= top_.size(); i++) {
        if (i == objects_at_) WriteChunk(body, objects_tag_, list);
        if (i == top_.size()) break;
        const Chunk& c = top_[i];
        std::vector<uint8_t> d = ChunkData(c);
        if (c.tag == kHeader) Put32(d, 12, top);  // version, 0, resources, top level objects, name and path sizes
        if (c.tag == kTargets) {
            // Tc the lists, then each Mt
            Chunk t = c;
            uint32_t lists = 0;
            for (const Field& f : t.fields) lists += f.tag == Tag2("Mt");
            if (Field* f = t.Find(Tag2("Tc"))) Put32(f->data, 0, lists);
            d = ChunkData(t);
        }
        if (c.tag == kEditor) {
            // EG the editor's next GUID
            Chunk e = c;
            if (Field* f = e.Find(Tag2("EG")); f && f->data.size() == 4) Put32(f->data, 0, std::max(Get32(f->data, 0), NextGuid()));
            d = ChunkData(e);
        }
        WriteChunk(body, c.tag, d);
    }
    std::vector<uint8_t> out;
    WriteChunk(out, kPackage, body);
    return out;
}

// PkHd: version, 0, resources, top level objects, name size, path size, the name and the path
std::string Package::Name() const {
    const std::vector<uint8_t>& d = top_[0].raw;
    if (d.size() < 24) return {};
    const char* name = reinterpret_cast<const char*>(d.data() + 24);
    return std::string(name, strnlen(name, d.size() - 24));
}

std::string Package::Path() const {
    const std::vector<uint8_t>& d = top_[0].raw;
    size_t at = 24 + Get32(d, 16);
    if (d.size() < 24 || at >= d.size()) return {};
    const char* path = reinterpret_cast<const char*>(d.data() + at);
    return std::string(path, strnlen(path, d.size() - at));
}

void Package::SetName(std::string_view name, std::string_view path) {
    std::vector<uint8_t>& d = top_[0].raw;
    d.resize(24);
    Put32(d, 16, uint32_t(name.size() + 1));
    Put32(d, 20, uint32_t(path.size() + 1));
    d.insert(d.end(), name.begin(), name.end());
    d.push_back(0);
    d.insert(d.end(), path.begin(), path.end());
    d.push_back(0);
    Pad(d);
}

Object* Package::Find(std::string_view name) {
    size_t i = Index(name);
    return i < objects.size() ? &objects[i] : nullptr;
}

const Object* Package::Find(std::string_view name) const {
    size_t i = Index(name);
    return i < objects.size() ? &objects[i] : nullptr;
}

size_t Package::Index(std::string_view name) const {
    for (size_t i = 0; i < objects.size(); i++)
        if (objects[i].Name() == name) return i;
    return objects.size();
}

void Package::Remove(std::string_view name) {
    size_t i = Index(name);
    if (i == objects.size()) return;
    std::vector<uint32_t> gone{objects[i].Guid()};
    objects.erase(objects.begin() + i);
    // children follow their group
    while (i < objects.size() && std::find(gone.begin(), gone.end(), objects[i].Parent()) != gone.end()) {
        gone.push_back(objects[i].Guid());
        objects.erase(objects.begin() + i);
    }
    for (uint32_t g : gone) RemoveTarget(g);
}

uint32_t Package::NextGuid() const {
    uint32_t next = 1;
    for (const Object& o : objects) next = std::max(next, o.Guid() + 1);
    return next;
}

void Package::ReplaceTargets(uint32_t from, uint32_t to) {
    for (Chunk& c : top_)
        if (c.tag == kAnswers) ReplaceSetButton(c, from, to);
    for (Object& o : objects) o.ReplaceTargets(from, to);
}

// Mt: the message, then the GUIDs it goes to
void Package::AddTarget(uint32_t like, uint32_t guid) {
    for (Chunk& c : top_) {
        if (c.tag != kTargets) continue;
        for (Field& f : c.fields) {
            if (f.tag != Tag2("Mt")) continue;
            bool has = false;
            for (size_t at = 4; at + 4 <= f.data.size(); at += 4) has = has || Get32(f.data, at) == like;
            if (has) Append32(f.data, guid);
        }
    }
}

void Package::RemoveTarget(uint32_t guid) {
    for (Chunk& c : top_) {
        if (c.tag != kTargets) continue;
        for (Field& f : c.fields) {
            if (f.tag != Tag2("Mt")) continue;
            for (size_t at = 4; at + 4 <= f.data.size();) {
                if (Get32(f.data, at) == guid) {
                    f.data.erase(f.data.begin() + at, f.data.begin() + at + 4);
                } else {
                    at += 4;
                }
            }
        }
    }
}

}  // namespace feng
