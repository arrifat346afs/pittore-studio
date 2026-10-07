#pragma once

#include "ui/app_state.h"

// Free helpers shared by the app_state translation units. This file is
// internal to src/ui: nothing outside the state split includes it.
//
// The helpers used to live at the top of app_state.cpp. They are declared
// here so that file and the files split out of it can all call them.

namespace pittore::ui {

bool isPaintable(const LayerItem& l);

bool layerSelectionMask(const DocumentItem& d, const LayerItem& l,
                        pittore::compute::SelectionMask& out,
                        const pittore::Image* targetImage = nullptr,
                        QPointF targetOffset = QPointF(),
                        double targetScaleX = -1.0, double targetScaleY = -1.0);

void ensureLayerPixels(DocumentItem& d, LayerItem& l);
// Flat relief plane matching the layer's pixel dims (no-op without pixels).
void ensureLayerHeight(LayerItem& l);
// Relief plane for a target of w*h, or null when absent or stale-sized.
float* heightPlaneFor(LayerItem& l, std::uint32_t w, std::uint32_t h);
int parentGroupIndex(const DocumentItem& d, int index);

const pittore::Image* effectiveMaskImage(const QSize& docSize,
                                          const LayerItem& l);

void maskTransformFor(const LayerItem& l, QPointF& offset, double& scaleX,
                      double& scaleY);

QRectF maskBoundsFor(const DocumentItem& d, const LayerItem& l);

void transformLinkedMaskPixels(
    LayerItem& l, const pittore::Image& oldPixels,
    const std::function<pittore::Image(const pittore::Image&)>& op);

void syncLinkedMaskPlacement(LayerItem& l);

bool ensureLayerMask(DocumentItem& d, LayerItem& l, float coverage);

float maskBrushValue(const QColor& color);

QImage qImageFromImage(const pittore::Image& img);

std::optional<pittore::text::TextRaster> rasterizeTextItem(const TextItem& t);

void setAdjustmentDefaults(LayerItem& l);

bool renderLiveText(LayerItem& l);

QString textLabel(const QString& text);

bool gpuCompositeWindow(const DocumentItem& d, const LayerItem& l,
                        std::uint32_t cx0,
                        std::uint32_t cy0, std::uint32_t cx1,
                        std::uint32_t cy1, std::uint32_t* rx0,
                        std::uint32_t* ry0, std::uint32_t* rx1,
                        std::uint32_t* ry1);

void blitRGBAfToPremul(const pittore::RGBAf* src, uchar* dst,
                       std::uint32_t x0, std::uint32_t y0, std::uint32_t rw,
                       std::uint32_t rh);

pittore::render::LayerStyle scaledStyle(const pittore::render::LayerStyle& s,
                                         double r);

void ensureLayerStyle(const LayerItem& l);

ProjectFileData projectDataFromDocument(const DocumentItem& doc);

// Estimated per-document working set (pixel layers + engine staging + the
// composite QImage). Used to enforce the preferences RAM limit. Exact bytes
// are not critical — this is a budget guard, and such limits are coarse.
std::size_t documentWorkingSetBytes(const QSize& size);

// Last panel index of the subtree rooted at `start`. A group header owns every
// following row with deeper indent; a leaf's token is just itself. Used for all
// stack surgery so a moved group travels with its children.
int layerTokenEnd(const QVector<LayerItem>& layers, int start);

// The rows to move as a unit: the selection, expanded so each selected group
// carries its whole subtree. Returns the sorted unit; `base` receives the
// shallowest indent among them.
QVector<int> layerMoveUnit(const DocumentItem* d, int* base = nullptr);

}  // namespace pittore::ui
