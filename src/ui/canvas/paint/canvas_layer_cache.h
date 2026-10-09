#pragma once
// Per-layer view cache: display-density rasters of vector layers, so
// pan/zoom frames blit instead of rebuilding paths.
//
// Each entry is pinned by a content key (pixel/filter stamps, placement,
// opacity) plus a quantized zoom bucket. Structural edits shift indices,
// which only mismatches keys and re-bakes - always correct, just not
// optimal. Entries for closed documents age out through the entry cap, so
// no document-lifetime hook is needed.
//
// GUI thread only: entries are read and written during paint.
#include <QImage>
#include <QRectF>

#include <cstdint>
#include <chrono>
#include <functional>
#include <unordered_map>

#include "ui/canvas/paint/canvas_crisp.h"

class QPainter;

namespace pittore::ui {

struct DocumentItem;

class LayerViewCache {
public:
    // Bake kinds share one budget; the kind rides the lookup key.
    static constexpr int kCrisp = 0;    // filled vector draw
    static constexpr int kOutline = 1;  // hairline contour

    static LayerViewCache& instance();

    // Cached raster for the layer, or null on any miss (unknown layer,
    // stale key, inadmissible content). The pointer is only valid until
    // the next store() call; blit from it immediately.
    const QImage* find(const DocumentItem& doc, int layerIndex, int kind,
                       double zoomBucket, QRectF* docBox);
    // Bake an entry. Refused silently when the content is not worth
    // caching (plain pixel layers already blit; absurd sizes never fit).
    void store(const DocumentItem& doc, int layerIndex, int kind,
               double zoomBucket, QImage img, const QRectF& docBox);
    void setBudgetBytes(qint64 bytes);
    qint64 usageBytes() const { return usedBytes_; }

    void paintCachedCrisp(QPainter& p, const DocumentItem& doc, int index,
                          const QRectF& docBox, double zoomEff,
                          CrispStats* stats = nullptr,
                          std::chrono::steady_clock::time_point deadline =
                              std::chrono::steady_clock::time_point::max());

private:
    LayerViewCache() = default;
    struct Key {
        const DocumentItem* doc = nullptr;
        int index = -1;
        int kind = 0;
        quint64 content = 0;
        double bucket = 0.0;
        bool operator==(const Key& o) const {
            return doc == o.doc && index == o.index && kind == o.kind &&
                   content == o.content && bucket == o.bucket;
        }
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept {
            std::size_t h = std::hash<const void*>{}(k.doc);
            h ^= std::hash<int>{}(k.index) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<int>{}(k.kind) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<quint64>{}(k.content) + 0x9e3779b9u + (h << 6) +
                 (h >> 2);
            h ^= std::hash<double>{}(k.bucket) + 0x9e3779b9u + (h << 6) +
                 (h >> 2);
            return h;
        }
    };
    struct Entry {
        QImage img;
        QRectF box;
        quint64 tick = 0;
    };
    quint64 contentKey(const DocumentItem& doc, int index) const;
    // Cache weight of the layer's vector content (0 = do not cache).
    double cacheWeight(const DocumentItem& doc, int index) const;
    void evict();

    std::unordered_map<Key, Entry, KeyHash> entries_;
    qint64 budgetBytes_ = 256ll * 1024 * 1024;
    qint64 usedBytes_ = 0;
    quint64 tick_ = 0;
};

}  // namespace pittore::ui
