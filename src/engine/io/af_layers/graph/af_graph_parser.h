#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/io/af_layers.h"
#include "engine/render/layer_style.h"
#include "engine/text/text_engine.h"
#include "engine/vector/vector_art.h"
#include "engine/vector/vector_shape.h"

#include "engine/io/af_layers/container/af_archive.h"
#include "engine/io/af_layers/graph/af_graph.h"
namespace pittore::io {
namespace af_detail {

struct GraphParser {
    Cursor c;
    Graph g;
    std::unordered_map<std::uint32_t, Node*> byId;
    int depth = 0;

    Node* newNode() {
        g.nodes.push_back(std::make_unique<Node>());
        return g.nodes.back().get();
    }

    template <class F>
    Value scalars(bool array, F read) {
        if (!array) return read();
        const std::uint32_t count = c.u32();
        if (count > c.remaining() || count > kMaxArray) fail("array count exceeds input");
        Value v;
        v.k = Value::K::Array;
        v.arr.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) v.arr.push_back(read());
        return v;
    }

    Value value(std::uint8_t ty, bool array) {
        switch (ty) {
            case 0x01:
                return scalars(array, [&] { Value v; v.k = Value::K::U8; v.u = c.u8(); return v; });
            case 0x02:
                return scalars(array, [&] { Value v; v.k = Value::K::U16; v.u = c.u16(); return v; });
            case 0x03:
            case 0x2f:
            case 0x34:
                return scalars(array, [&] { Value v; v.k = Value::K::U32; v.u = c.u32(); return v; });
            case 0x04:
                return scalars(array, [&] { Value v; v.k = Value::K::U64; v.u = c.u64(); return v; });
            case 0x05:
                return scalars(array, [&] { Value v; v.k = Value::K::I8; v.u = c.u8(); return v; });
            case 0x06:
                return scalars(array, [&] { Value v; v.k = Value::K::I16; v.u = c.u16(); return v; });
            case 0x07:
                return scalars(array, [&] { Value v; v.k = Value::K::I32; v.u = c.u32(); return v; });
            case 0x08:
                return scalars(array, [&] { Value v; v.k = Value::K::I64; v.u = c.u64(); return v; });
            case 0x09:
                return scalars(array, [&] {
                    Value v; v.k = Value::K::F32; v.f = f32FromBits(c.u32()); return v;
                });
            case 0x0a:
                return scalars(array, [&] {
                    Value v; v.k = Value::K::F64; v.f = f64FromBits(c.u64()); return v;
                });
            case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: {
                const std::size_t n = std::size_t(ty - 0x15) + 2;
                return scalars(array, [&] {
                    Value v; v.k = Value::K::VecI; v.vi.reserve(n);
                    for (std::size_t i = 0; i < n; ++i)
                        v.vi.push_back(static_cast<std::int32_t>(c.u32()));
                    return v;
                });
            }
            case 0x1f: case 0x20: case 0x21: case 0x22: case 0x23: {
                const std::size_t n = std::size_t(ty - 0x1f) + 2;
                return scalars(array, [&] {
                    Value v; v.k = Value::K::VecF; v.vf.reserve(n);
                    for (std::size_t i = 0; i < n; ++i) v.vf.push_back(f32FromBits(c.u32()));
                    return v;
                });
            }
            case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: {
                const std::size_t n = std::size_t(ty - 0x24) + 2;
                return scalars(array, [&] {
                    Value v; v.k = Value::K::VecD; v.vd.reserve(n);
                    for (std::size_t i = 0; i < n; ++i) v.vd.push_back(f64FromBits(c.u64()));
                    return v;
                });
            }
            case 0x29: return bools(array);
            case 0x2a: return enums(array);
            case 0x2b: case 0x2e: return strings(array);
            case 0x2c: return curves(array);
            case 0x2d: {
                if (array) fail("binary arrays do not occur");
                const std::uint32_t sz = c.u32();
                Value v; v.k = Value::K::Blob;
                const std::uint8_t* p = c.take(sz);
                v.bytes.assign(p, p + sz);
                return v;
            }
            case 0x30: case 0x31: case 0x32: return classes(ty, array);
            case 0x33: {
                if (array) fail("embedded-data arrays do not occur");
                const std::uint32_t dataTag = c.u32();
                const std::uint32_t sz = c.u32();
                Value v; v.k = Value::K::Embedded; v.u = dataTag;
                const std::uint8_t* p = c.take(sz);
                v.s.assign(reinterpret_cast<const char*>(p), sz);
                return v;
            }
            case 0x75: {
                if (array) fail("flag-set arrays do not occur");
                const std::uint16_t version = c.u16();
                const std::uint8_t count = c.u8();
                if (count > 8) fail("flag set wider than 64 bits");
                const std::uint8_t* p = c.take(count);
                std::uint64_t bits = 0;
                for (int i = count - 1; i >= 0; --i) bits = (bits << 8) | p[i];
                Value v; v.k = Value::K::Flags; v.ver = version; v.u = bits;
                return v;
            }
            default:
                if (ty >= 0x35 && ty <= 0x74) {
                    const std::size_t n = std::size_t(ty - 0x34);
                    return scalars(array, [&] {
                        Value v; v.k = Value::K::Struct;
                        const std::uint8_t* p = c.take(n);
                        v.bytes.assign(p, p + n);
                        return v;
                    });
                }
                fail("unknown field type");
        }
    }

    Value bools(bool array) {
        if (!array) {
            Value v; v.k = Value::K::Bool; v.u = c.u8() != 0;
            return v;
        }
        const std::uint32_t count = c.u32();
        if (count > kMaxArray) fail("boolean array is over the element limit");
        const std::uint8_t* bytes = c.take((std::size_t(count) + 7) / 8);
        Value v; v.k = Value::K::Array; v.arr.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            Value b; b.k = Value::K::Bool;
            b.u = (bytes[i / 8] >> (i % 8)) & 1;
            v.arr.push_back(b);
        }
        return v;
    }

    Value enums(bool array) {
        if (!array) {
            Value v; v.k = Value::K::Enum; v.u = c.u16(); v.ver = c.u16();
            return v;
        }
        const std::uint32_t count = c.u32();
        const std::uint16_t version = c.u16();
        if (count > c.remaining() || count > kMaxArray) fail("enum count exceeds input");
        Value v; v.k = Value::K::Array; v.arr.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            Value e; e.k = Value::K::Enum; e.u = c.u16(); e.ver = version;
            v.arr.push_back(e);
        }
        return v;
    }

    Value readString() {
        const std::uint32_t len = c.u32();
        Value v; v.k = Value::K::Str;
        const std::uint8_t* p = c.take(len);
        v.s.assign(reinterpret_cast<const char*>(p), len);
        return v;
    }

    Value strings(bool array) {
        if (!array) return readString();
        c.u32();  // total size
        const std::uint32_t count = c.u32();
        if (count > c.remaining() || count > kMaxArray) fail("string count exceeds input");
        Value v; v.k = Value::K::Array; v.arr.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) v.arr.push_back(readString());
        return v;
    }

    Value curves(bool array) {
        const std::uint32_t count = array ? c.u32() : 1;
        const std::uint16_t size = c.u16();
        if (!(size == 12 || size == 16 || size == 18 || size == 24 || size == 32))
            fail("unknown curve record size");
        if (count > kMaxArray) fail("curve count exceeds limit");
        std::vector<Value> items;
        items.reserve(std::min<std::uint32_t>(count, 256));
        for (std::uint32_t i = 0; i < count; ++i) {
            Value v; v.k = Value::K::Struct;
            const std::uint8_t* p = c.take(size);
            v.bytes.assign(p, p + size);
            items.push_back(std::move(v));
        }
        if (!array) return items.empty() ? Value{} : std::move(items.front());
        Value v; v.k = Value::K::Array; v.arr = std::move(items);
        return v;
    }

    Value classes(std::uint8_t ty, bool array) {
        std::uint32_t count = 1;
        bool haveShared = false;
        std::uint32_t sharedTag = 0, sharedId = 0;
        if (array) {
            count = c.u32();
            if (ty == 0x32) {
                sharedTag = c.u32();
                sharedId = c.u16();
                haveShared = true;
            }
            if (count > c.remaining() || count > kMaxArray) fail("class count exceeds input");
        }
        std::vector<Value> items;
        items.reserve(std::min<std::uint32_t>(count, 1024));
        for (std::uint32_t i = 0; i < count; ++i) {
            Node* n = nullptr;
            std::uint32_t et = 0;
            std::uint16_t ev = 0;
            if (ty == 0x30) n = classUntagged();
            else if (ty == 0x31) n = classShared();
            else n = classTagged(haveShared, sharedTag, sharedId, &et, &ev);
            Value v; v.k = Value::K::Class; v.cls = n; v.wire = ty;
            if (ty == 0x32) {
                v.refTag = haveShared ? sharedTag : et;
                v.refId = static_cast<std::uint16_t>(haveShared ? sharedId : ev);
            }
            items.push_back(std::move(v));
        }
        if (!array) return items.empty() ? Value{} : std::move(items.front());
        Value v; v.k = Value::K::Array; v.arr = std::move(items);
        v.wire = static_cast<std::uint8_t>(ty | 0x80);
        if (ty == 0x32 && haveShared) {
            v.refTag = sharedTag;
            v.refId = static_cast<std::uint16_t>(sharedId);
        }
        return v;
    }

    std::vector<std::pair<std::uint32_t, Value>> fields(bool withTags) {
        if (++depth > 200) fail("class nesting too deep");
        std::vector<std::pair<std::uint32_t, Value>> out;
        while (true) {
            const std::uint8_t typeByte = c.u8();
            const bool array = (typeByte & 0x80) != 0;
            const std::uint8_t ty = typeByte & 0x7f;
            if (ty == 0x00) {
                --depth;
                return out;
            }
            const bool needsTag = (ty >= 0x30 && ty <= 0x33) || ty == 0x75;
            if (needsTag && !withTags) fail("untagged class field");
            const std::uint32_t fieldTag = withTags ? c.u32() : 0;
            out.emplace_back(fieldTag, value(ty, array));
            // Retain the exact wire form for byte-exact re-emit.
            out.back().second.wire = typeByte;
        }
    }

    Node* classUntagged() {
        auto f = fields(false);
        Node* n = newNode();
        n->fields = std::move(f);
        return n;
    }

    Node* classShared() {
        const std::uint8_t flag = c.u8();
        if (flag == 0) return nullptr;
        if (flag == 1) {
            const std::uint32_t id = c.u32();
            if (byId.count(id)) fail("object id defined twice");
            Node* n = newNode();
            n->id = id;
            byId[id] = n;
            while (true) {
                const std::uint8_t s = c.u8();
                if (s == 0) {
                    const std::uint32_t t = c.u32();
                    const std::uint16_t v = c.u16();
                    const std::size_t begin = n->fields.size();
                    auto f = fields(true);
                    n->types.emplace_back(t, v);
                    n->fields.insert(n->fields.end(), f.begin(), f.end());
                    n->sections.push_back({t, v, begin, n->fields.size(), false});
                } else if (s == 1) {
                    const std::uint32_t t = c.u32();
                    n->types.emplace_back(t, 0);
                    n->sections.push_back(
                        {t, 0, n->fields.size(), n->fields.size(), true});
                    n->endMark = 1;
                    break;
                } else if (s == 2) {
                    n->endMark = 2;
                    break;
                } else {
                    fail("unknown type-section flag");
                }
            }
            auto f = fields(true);
            n->fields.insert(n->fields.end(), f.begin(), f.end());
            return n;
        }
        if (flag == 2) {
            const std::uint32_t id = c.u32();
            auto it = byId.find(id);
            if (it == byId.end()) fail("link to undefined object id");
            return it->second;
        }
        fail("unknown class flag");
    }

    Node* classTagged(bool haveShared, std::uint32_t sharedTag, std::uint32_t sharedId,
                      std::uint32_t* tagOut = nullptr, std::uint16_t* verOut = nullptr) {
        const std::uint8_t flag = c.u8();
        if (flag == 0) return nullptr;
        if (flag == 1) {
            std::uint32_t t = sharedTag, v = sharedId;
            if (!haveShared) {
                t = c.u32();
                v = c.u16();
            }
            if (tagOut) *tagOut = t;
            if (verOut) *verOut = static_cast<std::uint16_t>(v);
            Node* n = newNode();
            n->types.emplace_back(t, v);
            n->fields = fields(true);
            return n;
        }
        fail("unknown tagged-class flag");
    }
};


}  // namespace af_detail
}  // namespace pittore::io
