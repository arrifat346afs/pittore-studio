#pragma once
// SVG import bridge: normalize incoming SVG through the toolkit-free DOM
// (use/symbol expansion, stylesheet inlining) before the Qt importer parses
// it. Keeps svg_parts.cpp untouched while filling the use/clone, symbol,
// CSS-stylesheet and marker/pattern-reference gaps.
#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QVector>

#include "engine/vector/vector_art.h"

namespace pittore::ui {

// Expand <use>/<symbol>, inline <style> rules as presentation attributes, and
// resolve url(#id) fill/stroke against defs so downstream parsing sees flat
// geometry. Returns the original bytes when parsing fails (fail-open).
QByteArray svgNormalizeForImport(const QByteArray& xml);

// Inject the built-in marker + pattern defs when the document references
// mk-* / pat-* ids but carries no <defs> for them (round-trips files the
// editor itself wrote).
QByteArray svgEnsureBuiltinDefs(const QByteArray& xml);

// Live XML editing of retained vector geometry: apply one `attr=value` pair
// (d, fill, stroke, stroke-width, opacity, fill-rule, marker-*, clip-path,
// mask, filter, pattern fill url) onto `node`. False + *error on bad values;
// unknown attributes are ignored (never fail the edit).
bool applyXmlEditToArt(pittore::vector::ArtNode& node, const QString& attr,
                       const QString& value, QString* error);

// Serialize one retained node back to an SVG element line for the XML tree.
QString artNodeToXmlLine(const pittore::vector::ArtNode& node);

// Multipage: layers named "Page*" (case-insensitive) with vector geometry
// act as page frames; without any, the whole canvas is the single page.
// Frames are document-space rects of the art bounds.
class AppState;
struct DocumentItem;
QVector<QRectF> pageFrames(const DocumentItem& doc);
// One PdfPage per frame with the intersecting vector art (paths + text runs
// + placed rasters as JPEG XObjects). Links span pages by frame order.
bool exportPagesPdf(AppState* state, int docIndex, const QString& path,
                    QString* error);

}  // namespace pittore::ui
