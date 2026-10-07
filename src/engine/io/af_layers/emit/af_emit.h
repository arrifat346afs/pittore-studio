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

#include "engine/io/af_layers/graph/af_graph.h"

namespace pittore::io {
namespace af_detail {

// ---------------------------------------------------------------------------
// Object-graph serializer: the inverse of GraphParser. Parsed values carry
// their exact wire byte (Value::wire), so template subtrees re-emit
// byte-identically; builder-made values (wire == 0) use canonical forms
// (0x31 classes, 0xb1 class arrays). Shared 0x31 definitions are emitted
// once (fresh DFS-order ids) and linked (flag 2) afterwards, mirroring how
// the files are laid out.
// ---------------------------------------------------------------------------
struct Emitter {
    std::vector<std::uint8_t> out;
    std::unordered_map<const Node*, std::uint32_t> ids;
    std::uint32_t nextId = 0;
    int depth = 0;

    void u8(std::uint8_t v) { out.push_back(v); }
    void u16(std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v & 0xff));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
    }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
    }
    void u64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i)
            out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
    }
    void f32(float v) {
        std::uint32_t b = 0;
        std::memcpy(&b, &v, 4);
        u32(b);
    }
    void f64(double v) {
        std::uint64_t b = 0;
        std::memcpy(&b, &v, 8);
        u64(b);
    }
    void bytes(const std::uint8_t* p, std::size_t n) {
        out.insert(out.end(), p, p + n);
    }
    void bytes(const std::vector<std::uint8_t>& v) {
        out.insert(out.end(), v.begin(), v.end());
    }
    void tag(std::uint32_t t) { u32(t); }

    // Canonical base wire type for a fresh (wire == 0) value.
    static std::uint8_t canonical(const Value& v);

    void value(const Value& v);
    // Level-1 type byte (arrays carry it themselves downstream).
    void valueHead(const Value& v);
    // Everything after the level-1 byte.
    void valueBody(const Value& v);
    void scalar(const Value& v, std::uint8_t base);
    void classValue(const Value& v);
    void arrayValue(const Value& v);
    // 0x31 body: section replay + closing marker + trailing fields + 0x00.
    // (Flag byte and id are written by classValue.)
    void nodeDef(const Node* n);
    void nodeTaggedFields(const Node* n);
    void nodeUntagged(const Node* n);
    // A fields(true) stream: tagged values + 0x00 terminator.
    void fieldList(const std::vector<std::pair<std::uint32_t, Value>>& fields);
    // One tagged field: type byte + tag (when the wire form takes one).
    void field(std::uint32_t fieldTag, const Value& v);
    // A fields(false) stream: untagged values + 0x00 terminator.
    void fieldListUntagged(const std::vector<std::pair<std::uint32_t, Value>>& fields);
    void graphDoc(const Graph& g);
};

void emitGraph(const Graph& g, std::vector<std::uint8_t>& out);

}  // namespace af_detail
}  // namespace pittore::io
