#pragma once
// Layered Affinity export: the inverse of AppState::openAfLayers.
//
// buildAfLayersDoc converts the live panel model (index 0 = top) into file
// order (bottom -> top) with group opacity/visibility unfolded (the importer
// refolds them), raw native pixels and doc-space masks. Everything the .af
// graph cannot carry natively (adjustments, live text, vector art, effects)
// bakes into the pixels; the bake list is returned alongside for the export
// dialog to report. AppState::exportAfLayers wraps it with the template,
// thumbnail and encoding; the caller writes the bytes to disk.
#include <QString>

#include <string>
#include <vector>

#include "engine/io/af_layers.h"

namespace pittore::ui {

class DocumentItem;

// Panel -> file order AfLayersDoc plus bake notes ("Curves baked", ...).
struct AfExportDoc {
    pittore::io::AfLayersDoc doc;
    std::vector<std::string> baked;
};

AfExportDoc buildAfLayersDoc(DocumentItem& doc);

}  // namespace pittore::io
