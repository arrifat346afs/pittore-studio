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

#include "engine/io/af_layers/emit/af_emit.h"
#include "engine/io/af_layers/graph/af_graph.h"

namespace pittore::io {

// Forward declarations (full types in engine/io/af_layers.h, included by
// af_write.cpp; kept out here to avoid a header cycle).
struct AfLayersDoc;

namespace af_detail {

// ---------------------------------------------------------------------------
// Layered Affinity writer: template-derived export.
//
// A .af document holds a good deal more than a layer stack, so instead of
// inventing every node the format allows, export starts from a minimal
// template graph (a Pers root, a DocN and one spread), patches the canvas
// geometry, swaps the spread's layer stack for fresh nodes built from an
// AfLayersDoc, serializes with the byte-exact emitter, and packs a fresh
// v12 container (single #FT4 savepoint, zstd entries, thumbnail).
// Adjustments/text/shapes arrive pre-baked as rasters; the caller reports
// that. Only Rstr/Grup/MRst nodes are authored; everything else rides the
// template verbatim. The template is supplied by the caller — nothing
// third-party is bundled in the engine.
// ---------------------------------------------------------------------------

// One container payload to write.
struct OutEntry {
    std::string name;
    std::vector<std::uint8_t> payload;  // bytes after #Fil
    std::uint64_t plainSize = 0;        // decompressed size
    std::uint8_t comp = 0;              // container compression byte
    std::uint32_t crc = 0;              // crc32 of the decompressed bytes
};

// Fresh graph-node builders (all values carry explicit wire bytes).
struct Builder {
    Graph* g = nullptr;
    std::uint64_t rng = 0;

    explicit Builder(Graph* g, std::uint64_t seed) : g(g), rng(seed) {}
    std::uint64_t rand();

    Node* node31(const std::vector<std::pair<std::uint32_t, std::uint16_t>>& chain);
    Node* nodeClosed(std::uint32_t tag, std::uint16_t ver);
    Node* nodeTagged(std::uint32_t tag, std::uint16_t ver);

    static Value vI32(std::int32_t v);
    static Value vU8(std::uint8_t v);
    static Value vU32(std::uint32_t v);
    static Value vF32(float v);
    static Value vF64(double v);
    static Value vBool(bool v);
    static Value vEnum(std::uint16_t id, std::uint16_t ver);
    static Value vStr(const std::string& s);
    static Value vVecI(const std::vector<std::int32_t>& v);
    static Value vVecD(const std::vector<double>& v);
    static Value vNull31();
    static Value vClass(Node* n);  // fresh 0x31 reference
    static Value vArray(std::uint8_t wire);  // fresh empty array
    static Value vEmbedded(std::uint32_t tag, const std::string& name);

    static void put(Node* n, const char* name, Value v);
    static void putArr(Node* n, const char* name, std::uint8_t wire,
                       std::vector<Value> items);
};

// Split an 8-bit RGBA bitmap into planar 256-tile payloads. Each stored
// tile appends a d/N entry; statuses follow the file convention
// (1 empty, 2 solid-FF, 4 stored).
struct TiledPlanes {
    // Stored-tile entries, in scan order.
    std::vector<OutEntry> entries;
    // Per-plane (channels × tiles) status bytes.
    std::vector<std::vector<std::uint8_t>> statuses;
    // Per-plane Blck nodes (fresh, in the target graph) for status-4 tiles.
    std::vector<std::vector<Node*>> blocks;
    int gridW = 0;
    int gridH = 0;
};

// Encode one RGBA8 bitmap (w*h*4 bytes) into planar tiles, appending
// container entries for stored tiles. `entryBase` is the next d/N index
// (updated past the entries taken).
TiledPlanes tileRgba8(Graph& g, const std::uint8_t* px, std::uint32_t w,
                      std::uint32_t h, int& entryBase);
// Single-channel variant (masks): coverage bytes, format-6 layout.
TiledPlanes tileMask8(Graph& g, const std::uint8_t* px, std::uint32_t w,
                      std::uint32_t h, int& entryBase);

// Fresh bitmap node (DyBm) referencing tiled planes. Format 0 = RGBA8
// (4 planes), 6 = single channel. No mip levels (recomputed caches).
Node* bitmapNode(Graph& g, Builder& b, int format, std::uint32_t w,
                 std::uint32_t h, const TiledPlanes& tiled);

// Evict a spread composite-cache bitmap in place (dimensions + all-empty
// status grid, no pixel data).
void evictBitmap(Node* dybm, std::uint32_t w, std::uint32_t h);

// Minimal PNG encoder (RGBA8, Sub-filtered) for the thumbnail block.
std::vector<std::uint8_t> encodePngRgba8(const std::uint8_t* px, std::uint32_t w,
                                         std::uint32_t h);

// Fresh v12 container: single #FT4 savepoint, compressed entries,
// thumbnail block when `thumbPng` is non-empty.
std::vector<std::uint8_t> writeContainer(const std::vector<OutEntry>& entries,
                                         const std::vector<std::uint8_t>& thumbPng,
                                         std::uint64_t creationDate);

// Inverse of blendName(): our blend name to the (id, version) pair.
// Returns nullopt for modes with no Affinity equivalent.
std::optional<std::pair<std::uint16_t, std::uint16_t>> blendEnum(
    const std::string& name);

}  // namespace af_detail

// Thumbnail + title bundled for export (the file browser preview).
struct AfThumb {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;  // width*height*4 straight RGBA8
};

struct AfEncodeResult {
    std::vector<std::uint8_t> bytes;
    // Human-readable notes for dropped content (unknown blends, layers
    // without pixels). The caller surfaces these next to the export.
    std::vector<std::string> skipped;
};

// Fresh export template graph: a Pers root holding one DocN, one spread and
// an empty layer stack — exactly what afEncodeLayers() patches (DfSz, SprB,
// MiID, Chld) plus the optional CSel. Built in code so no third-party
// document ships with the engine.
std::vector<std::uint8_t> afBuildTemplateDoc();

// Layered Affinity export (.af/.afphoto): patch a minimal template graph
// (its doc.dat bytes) with the canvas + a fresh raster/group layer stack
// built from `doc`, then pack a fresh v12 container. Text, adjustments and
// effects arrive pre-baked as rasters (AfLayer pixels); the caller notes
// that. `templateDocDat` is the plaintext doc.dat of a minimal document —
// pass afBuildTemplateDoc() for the built-in one; tests may pass a
// fixture's doc.dat to exercise a real document's structure.
std::optional<AfEncodeResult> afEncodeLayers(
    const AfLayersDoc& doc, const std::vector<std::uint8_t>& templateDocDat,
    const AfThumb& thumb, const std::string& title, std::uint64_t creationDate,
    std::string* error = nullptr);

}  // namespace pittore::io
