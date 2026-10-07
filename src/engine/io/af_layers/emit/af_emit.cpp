#include "engine/io/af_layers/emit/af_emit.h"

#include "engine/io/af_layers/container/af_archive.h"

#include <stdexcept>

namespace pittore::io {
namespace af_detail {

namespace {
[[noreturn]] void efail(const std::string& m) { throw AfError(m); }
struct DepthGuard {
    int& d;
    explicit DepthGuard(int& d) : d(d) {
        if (++d > 500) efail("emit nesting too deep");
    }
    ~DepthGuard() { --d; }
};
}  // namespace

std::uint8_t Emitter::canonical(const Value& v) {
    switch (v.k) {
        case Value::K::U8: return 0x01;
        case Value::K::U16: return 0x02;
        case Value::K::U32: return 0x03;
        case Value::K::U64: return 0x04;
        case Value::K::I8: return 0x05;
        case Value::K::I16: return 0x06;
        case Value::K::I32: return 0x07;
        case Value::K::I64: return 0x08;
        case Value::K::F32: return 0x09;
        case Value::K::F64: return 0x0a;
        case Value::K::VecI: {
            const std::size_t n = v.vi.size();
            if (n < 2 || n > 6) efail("VecI width out of range for emit");
            return static_cast<std::uint8_t>(0x15 + (n - 2));
        }
        case Value::K::VecF: {
            const std::size_t n = v.vf.size();
            if (n < 2 || n > 6) efail("VecF width out of range for emit");
            return static_cast<std::uint8_t>(0x1f + (n - 2));
        }
        case Value::K::VecD: {
            const std::size_t n = v.vd.size();
            if (n < 2 || n > 6) efail("VecD width out of range for emit");
            return static_cast<std::uint8_t>(0x24 + (n - 2));
        }
        case Value::K::Bool: return 0x29;
        case Value::K::Enum: return 0x2a;
        case Value::K::Str: return 0x2b;
        case Value::K::Blob: return 0x2d;
        case Value::K::Struct: {
            const std::size_t n = v.bytes.size();
            if (n < 1 || n > 64) efail("Struct size out of range for emit");
            return static_cast<std::uint8_t>(0x34 + n);
        }
        case Value::K::Embedded: return 0x33;
        case Value::K::Flags: return 0x75;
        case Value::K::Class: return 0x31;
        case Value::K::Array: return 0x31;  // array-of-class default; fixed up by caller
        case Value::K::Null: return 0x31;   // null class slot
    }
    efail("unemittable value");
}

void Emitter::scalar(const Value& v, std::uint8_t base) {
    switch (base) {
        case 0x01: u8(static_cast<std::uint8_t>(v.u & 0xff)); return;
        case 0x02: u16(static_cast<std::uint16_t>(v.u & 0xffff)); return;
        case 0x03: case 0x2f: case 0x34:
            u32(static_cast<std::uint32_t>(v.u & 0xffffffffu));
            return;
        case 0x04: u64(v.u); return;
        case 0x05: u8(static_cast<std::uint8_t>(v.u & 0xff)); return;
        case 0x06: u16(static_cast<std::uint16_t>(v.u & 0xffff)); return;
        case 0x07: u32(static_cast<std::uint32_t>(v.u & 0xffffffffu)); return;
        case 0x08: u64(v.u); return;
        case 0x09: f32(static_cast<float>(v.f)); return;
        case 0x0a: f64(v.f); return;
        case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: {
            const std::size_t n = std::size_t(base - 0x15) + 2;
            if (v.vi.size() != n) efail("VecI width mismatch on emit");
            for (std::size_t i = 0; i < n; ++i) u32(static_cast<std::uint32_t>(v.vi[i]));
            return;
        }
        case 0x1f: case 0x20: case 0x21: case 0x22: case 0x23: {
            const std::size_t n = std::size_t(base - 0x1f) + 2;
            if (v.vf.size() != n) efail("VecF width mismatch on emit");
            for (std::size_t i = 0; i < n; ++i) f32(v.vf[i]);
            return;
        }
        case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: {
            const std::size_t n = std::size_t(base - 0x24) + 2;
            if (v.vd.size() != n) efail("VecD width mismatch on emit");
            for (std::size_t i = 0; i < n; ++i) f64(v.vd[i]);
            return;
        }
        case 0x29: u8(v.u ? 1 : 0); return;
        case 0x2a:
            u16(static_cast<std::uint16_t>(v.u & 0xffff));
            u16(static_cast<std::uint16_t>(v.ver & 0xffff));
            return;
        case 0x2b: case 0x2e: {
            if (v.s.size() > kMaxArray) efail("string too large for emit");
            u32(static_cast<std::uint32_t>(v.s.size()));
            out.insert(out.end(), v.s.begin(), v.s.end());
            return;
        }
        case 0x2d:
            if (v.bytes.size() > kMaxArray) efail("blob too large for emit");
            u32(static_cast<std::uint32_t>(v.bytes.size()));
            bytes(v.bytes);
            return;
        case 0x2c: {
            // Curve record(s): non-array Struct values read through 0x2c.
            if (v.bytes.size() != 12 && v.bytes.size() != 16 && v.bytes.size() != 18 &&
                v.bytes.size() != 24 && v.bytes.size() != 32)
                efail("curve record size invalid on emit");
            u16(static_cast<std::uint16_t>(v.bytes.size()));
            bytes(v.bytes);
            return;
        }
        case 0x33:
            u32(static_cast<std::uint32_t>(v.u & 0xffffffffu));
            if (v.s.size() > kMaxArray) efail("embedded name too large for emit");
            u32(static_cast<std::uint32_t>(v.s.size()));
            out.insert(out.end(), v.s.begin(), v.s.end());
            return;
        case 0x75: {
            // Minimal byte count for the bits (at least one byte).
            std::uint64_t bits = v.u;
            std::size_t count = 0;
            std::uint64_t t = bits;
            do {
                ++count;
                t >>= 8;
            } while (t != 0 && count < 8);
            u16(static_cast<std::uint16_t>(v.ver & 0xffff));
            u8(static_cast<std::uint8_t>(count));
            for (std::size_t i = 0; i < count; ++i)
                u8(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xff));
            return;
        }
        default:
            if (base >= 0x35 && base <= 0x74) {
                const std::size_t n = std::size_t(base - 0x34);
                if (v.bytes.size() != n) efail("Struct size mismatch on emit");
                bytes(v.bytes);
                return;
            }
            efail("unknown scalar wire type on emit");
    }
}

void Emitter::classValue(const Value& v) {
    // Level-1 type byte is written by the caller (fieldList) or omitted
    // for array elements; here only the flag/body follows.
    if (v.k != Value::K::Class) efail("classValue on non-class");
    DepthGuard dg(depth);
    const std::uint8_t base = v.wire ? (v.wire & 0x7f) : 0x31;
    if (v.cls == nullptr) {
        // Null slot: flag 0 in either class form.
        if (base == 0x30) efail("null 0x30 class has no encoding");
        u8(0);
        return;
    }
    if (base == 0x30) {
        nodeUntagged(v.cls);
        return;
    }
    if (base == 0x32) {
        u8(1);
        u32(v.refTag);
        u16(v.refId);
        nodeTaggedFields(v.cls);
        return;
    }
    // 0x31 definition or link.
    auto it = ids.find(v.cls);
    if (it != ids.end()) {
        u8(2);
        u32(it->second);
        return;
    }
    u8(1);
    const std::uint32_t id = nextId++;
    ids.emplace(v.cls, id);
    u32(id);
    nodeDef(v.cls);
}

void Emitter::arrayValue(const Value& v) {
    if (v.k != Value::K::Array) efail("arrayValue on non-array");
    if (++depth > 200) efail("emit nesting too deep");
    if (v.arr.size() > kMaxArray) efail("array too large for emit");
    // Element form: explicit wire, else uniform element kind.
    std::uint8_t base = 0;
    if (v.wire) {
        base = v.wire & 0x7f;
    } else if (!v.arr.empty()) {
        base = canonical(v.arr[0]) & 0x7f;
        if (base == 0x31 && v.arr[0].k == Value::K::Class) base = 0x31;
    } else {
        efail("fresh empty array needs an explicit wire type");
    }
    if (base == 0x29) {
        // Bit-packed bools.
        u32(static_cast<std::uint32_t>(v.arr.size()));
        std::uint8_t acc = 0;
        int bit = 0;
        for (const Value& e : v.arr) {
            if (e.u) acc |= static_cast<std::uint8_t>(1u << bit);
            if (++bit == 8) {
                u8(acc);
                acc = 0;
                bit = 0;
            }
        }
        if (bit != 0) u8(acc);
        --depth;
        return;
    }
    if (base == 0x2a) {
        u32(static_cast<std::uint32_t>(v.arr.size()));
        u16(v.arr.empty() ? 0 : static_cast<std::uint16_t>(v.arr[0].ver & 0xffff));
        for (const Value& e : v.arr) u16(static_cast<std::uint16_t>(e.u & 0xffff));
        --depth;
        return;
    }
    if (base == 0x2b || base == 0x2e) {
        // Total of the string payloads (empty arrays record 0).
        std::uint64_t total = 0;
        for (const Value& e : v.arr) total += 4 + e.s.size();
        if (total > kMaxArray) efail("string array too large for emit");
        u32(static_cast<std::uint32_t>(total));
        u32(static_cast<std::uint32_t>(v.arr.size()));
        for (const Value& e : v.arr) scalar(e, base);
        --depth;
        return;
    }
    if (base == 0x2c) {
        u32(static_cast<std::uint32_t>(v.arr.size()));
        if (v.arr.empty()) efail("fresh empty curve array needs a record size");
        const std::size_t rec = v.arr[0].bytes.size();
        u16(static_cast<std::uint16_t>(rec));
        for (const Value& e : v.arr) {
            if (e.bytes.size() != rec) efail("ragged curve array on emit");
            bytes(e.bytes);
        }
        --depth;
        return;
    }
    if (base == 0x30 || base == 0x31 || base == 0x32) {
        u32(static_cast<std::uint32_t>(v.arr.size()));
        if (base == 0x32) {
            // Hoisted shared header (template-kept arrays).
            u32(v.refTag);
            u16(v.refId);
            for (const Value& e : v.arr) {
                if (e.k != Value::K::Class || !e.cls) efail("null in shared class array");
                u8(1);
                nodeTaggedFields(e.cls);
            }
            --depth;
            return;
        }
        for (const Value& e : v.arr) classValue(e);
        --depth;
        return;
    }
    // Plain scalar arrays: count + items.
    u32(static_cast<std::uint32_t>(v.arr.size()));
    for (const Value& e : v.arr) scalar(e, base);
    --depth;
}

void Emitter::value(const Value& v) {
    valueHead(v);
    valueBody(v);
}

void Emitter::valueHead(const Value& v) {
    if (v.k == Value::K::Array) {
        std::uint8_t base = 0;
        if (v.wire) {
            base = v.wire & 0x7f;
        } else if (!v.arr.empty()) {
            base = canonical(v.arr[0]) & 0x7f;
        } else {
            efail("fresh empty array needs an explicit wire type");
        }
        u8(static_cast<std::uint8_t>(base | 0x80));
        return;
    }
    if (v.k == Value::K::Class) {
        u8(v.wire ? (v.wire & 0x7f) : 0x31);
        return;
    }
    u8(v.wire ? (v.wire & 0x7f) : canonical(v));
}

void Emitter::valueBody(const Value& v) {
    if (v.k == Value::K::Array) {
        arrayValue(v);
        return;
    }
    if (v.k == Value::K::Class) {
        classValue(v);
        return;
    }
    scalar(v, v.wire ? (v.wire & 0x7f) : canonical(v));
}

void Emitter::nodeDef(const Node* n) {
    // 0x31 section replay: stream-order sections (each with its own field
    // list + terminator), then the closing marker, then the trailing
    // fields + terminator. Fresh nodes carry empty sections with all
    // fields trailing.
    for (const auto& s : n->sections) {
        if (s.root) {
            // Root sections carry no field list of their own; the trailing
            // fields follow the marker directly.
            u8(1);
            tag(s.tag);
            continue;
        }
        u8(0);
        tag(s.tag);
        u16(s.ver);
        for (std::size_t i = s.begin; i < s.end && i < n->fields.size(); ++i)
            field(n->fields[i].first, n->fields[i].second);
        u8(0);
    }
    if (n->sections.empty() || !n->sections.back().root) {
        // No trailing root section: close with the recorded marker.
        u8(n->endMark == 2 ? 2 : 1);
        if (n->endMark != 2 && !n->types.empty()) tag(n->types.back().first);
    }
    const std::size_t t0 = n->sections.empty() ? 0 : n->sections.back().end;
    for (std::size_t i = t0; i < n->fields.size(); ++i)
        field(n->fields[i].first, n->fields[i].second);
    u8(0);
}

void Emitter::nodeTaggedFields(const Node* n) {
    fieldList(n->fields);
}

void Emitter::nodeUntagged(const Node* n) {
    fieldListUntagged(n->fields);
}

void Emitter::fieldList(const std::vector<std::pair<std::uint32_t, Value>>& fields) {
    for (const auto& f : fields) field(f.first, f.second);
    u8(0);
}

void Emitter::field(std::uint32_t fieldTag, const Value& v) {
    valueHead(v);
    // Tagged field lists carry a tag on every field (the parser reads one
    // unconditionally when withTags; its needsTag check only validates
    // untagged contexts).
    tag(fieldTag);
    valueBody(v);
}

void Emitter::fieldListUntagged(const std::vector<std::pair<std::uint32_t, Value>>& fields) {
    for (const auto& f : fields) value(f.second);
    u8(0);
}

void Emitter::graphDoc(const Graph& g) {
    const Node* root = g.root();
    if (!root) efail("no root for emit");
    if (root->types.empty()) efail("root has no type for emit");
    // Magic bytes 00 FF 4B 53.
    u8(0x00);
    u8(0xff);
    u8(0x4b);
    u8(0x53);
    u16(g.fileVersion);
    tag(root->types[0].first);
    u16(static_cast<std::uint16_t>(root->types[0].second & 0xffff));
    if (g.fileVersion == 2) u32(g.headerExtra);
    fieldList(root->fields);
}

void emitGraph(const Graph& g, std::vector<std::uint8_t>& out) {
    Emitter e;
    e.graphDoc(g);
    out = std::move(e.out);
}

}  // namespace af_detail
}  // namespace pittore::io
