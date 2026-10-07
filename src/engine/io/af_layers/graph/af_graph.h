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
namespace pittore::io {
namespace af_detail {

// ---------------------------------------------------------------------------
// Object graph ("Pers"/"DocR"/…): the tagged tree the document half of the
// format is built from. Classes carry a chain of (tag, version) types and a
// flat list of (field tag, value) members; the parser keeps enough wire detail
// alongside them for the emitter to write a node back unchanged.
// ---------------------------------------------------------------------------
struct Node;

struct Value {
    enum class K : std::uint8_t {
        Null, U8, U16, U32, U64, I8, I16, I32, I64, F32, F64,
        VecI, VecF, VecD, Bool, Str, Enum, Flags, Blob, Struct, Embedded, Class, Array
    };
    K k = K::Null;
    std::uint64_t u = 0;    // integer payload / bool / flags / enum id
    std::uint32_t ver = 0;  // enum version
    double f = 0.0;         // scalar float
    Node* cls = nullptr;    // Class reference
    std::string s;          // Str / Embedded name
    std::vector<std::int32_t> vi;
    std::vector<float> vf;
    std::vector<double> vd;
    std::vector<std::uint8_t> bytes;  // Blob / Struct / Curve
    std::vector<Value> arr;
    // Exact type byte as read (array bit included for arrays). 0 means
    // "fresh" (builder-made): the emitter picks the canonical wire form.
    std::uint8_t wire = 0;
    // 0x32 tag/id: element tag/id for single 0x32 classes, shared header
    // for 0x32 arrays (kept on the array value).
    std::uint32_t refTag = 0;
    std::uint16_t refId = 0;
};

struct Node {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> types;  // (tag, version)
    std::vector<std::pair<std::uint32_t, Value>> fields;
    std::uint32_t id = 0;
    // 0x31 type-section replay: stream-order sections with their field
    // ranges, plus the closing marker (1 = root tag, 2 = plain close).
    // Empty for 0x30/0x32 nodes and the document root.
    struct Section {
        std::uint32_t tag = 0;
        std::uint16_t ver = 0;
        std::size_t begin = 0;
        std::size_t end = 0;
        bool root = false;
    };
    std::vector<Section> sections;
    std::uint8_t endMark = 1;
};

struct Graph {
    std::vector<std::unique_ptr<Node>> nodes;
    // Document header, retained for re-emit (extra word iff fileVersion == 2).
    std::uint16_t fileVersion = 1;
    std::uint32_t headerExtra = 0;
    Node* root() const { return nodes.empty() ? nullptr : nodes[0].get(); }
    const Value* field(const Node* n, const char* name) const {
        if (!n) return nullptr;
        const std::uint32_t t = tag4(name);
        for (const auto& f : n->fields)
            if (f.first == t) return &f.second;
        return nullptr;
    }
    Node* child(const Node* n, const char* name) const {
        const Value* v = field(n, name);
        return (v && v->k == Value::K::Class) ? v->cls : nullptr;
    }
    std::vector<Node*> children(const Node* n, const char* name) const {
        std::vector<Node*> out;
        const Value* v = field(n, name);
        if (v && v->k == Value::K::Array)
            for (const Value& item : v->arr)
                if (item.k == Value::K::Class && item.cls) out.push_back(item.cls);
        return out;
    }
    std::uint32_t typeTag(const Node* n) const {
        return (n && !n->types.empty()) ? n->types[0].first : 0;
    }
};

// Object-graph documents open with the literal bytes 00 FF 4B 53, i.e. the
// same little-endian packing the container's `tag` uses — not the reversed
// class/field convention.
constexpr std::uint32_t kTagDoc = letag4("\x00\xff\x4b\x53");  // 0x534bff00

float f32FromBits(std::uint32_t b);
double f64FromBits(std::uint64_t b);
Graph parseGraph(const std::vector<std::uint8_t>& bytes);
std::optional<std::uint16_t> enumOf(const Graph& g, const Node* n, const char* name);
std::optional<std::pair<std::uint16_t, std::uint16_t>> enumVerOf(const Graph& g, const Node* n,
                                                                const char* name);
std::int32_t i32Of(const Graph& g, const Node* n, const char* name, std::int32_t dflt = 0);
std::optional<float> f32Of(const Graph& g, const Node* n, const char* name);
std::optional<bool> boolOf(const Graph& g, const Node* n, const char* name);
std::string strOf(const Graph& g, const Node* n, const char* name);
const std::vector<double>* f64s(const Graph& g, const Node* n, const char* name);
std::string blendName(std::uint16_t id, std::uint16_t version);

}  // namespace af_detail
}  // namespace pittore::io
