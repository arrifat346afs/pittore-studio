#pragma once
// Interchange + automation: DXF/PDF writers, script-extension host, CLI verbs.
//
// A script-extension descriptor (name/id, script command, parameters) runs through a
// Python subprocess, and the command line exposes `--export-* / --query-* /
// --actions` verbs. PDF export here is a minimal vector writer (paths/text/
// raster) with internal-link annotations; DXF is the R12 polyline subset CAD
// tools accept.
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace pittore::vector {

// --- DXF ------------------------------------------------------------------
struct DxfPolyline {
    std::vector<std::pair<double, double>> points;
    bool closed = false;
    int color = 7;  // ACI 1..9
};
std::string writeDxf(const std::vector<DxfPolyline>& polylines);

// --- Minimal PDF ------------------------------------------------------------
struct PdfLink {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // rect in points, bottom-origin
    std::string dest;  // named destination or page label
};
// WinAnsi text run (Helvetica base-14; non-latin bytes escape as octal).
struct PdfText {
    double x = 0, y = 0, size = 12.0;
    std::string text;
};
// Raster image as a DCT (JPEG) XObject stretched over x/y/w/h.
struct PdfImage {
    double x = 0, y = 0, w = 0, h = 0;
    std::vector<std::uint8_t> jpeg;
    int iw = 0, ih = 0;
};
struct PdfPage {
    double wPt = 595, hPt = 842;  // A4 default
    std::string content;          // content-stream fragment (paths/text)
    std::vector<PdfLink> links;
    std::vector<PdfText> texts;
    std::vector<PdfImage> images;
};
std::vector<std::uint8_t> writePdf(const std::vector<PdfPage>& pages,
                                   const std::string& title = "");

// --- Extension host ----------------------------------------------------------
// One parsed script-extension entry (format subset: name/id, script command,
// params with gui-text/type/default).
struct ExtensionParam {
    std::string name;
    std::string guiText;
    std::string type;  // "string", "int", "float", "bool", "enum", ...
    std::string def;
};
struct ExtensionDef {
    std::string id;
    std::string name;
    std::string script;                  // interpreter + script path
    std::vector<ExtensionParam> params;
    std::string rawXml;                  // round-trip
};
ExtensionDef parseExtension(const std::string& xml, bool& ok);

// Registry: id -> extension, with effect/input/output/template categories.
struct ExtensionDb {
    std::map<std::string, ExtensionDef> byId;
    void add(ExtensionDef e) { byId[e.id] = std::move(e); }
    const ExtensionDef* find(const std::string& id) const;
    std::vector<std::string> ids() const;
};

// Run one extension as a subprocess: feeds SVG on stdin, reads SVG on stdout.
// Returns stdout (or stderr text on failure with ok=false). Timeout in ms.
struct ExtensionResult {
    bool ok = false;
    std::string outSvg;
    std::string log;
};
using SubprocessRunner =
    std::function<ExtensionResult(const std::string& command, const std::string& stdinSvg,
                                  int timeoutMs)>;
ExtensionResult runExtension(const ExtensionDef& ext, const std::string& inSvg,
                             const std::map<std::string, std::string>& args,
                             SubprocessRunner runner, int timeoutMs = 30000);

// --- CLI verbs ----------------------------------------------------------------
// `--query-id`, `--export-plain-svg`, `--export-dxf`, `--export-pdf`,
// `--actions=` list. Parsed without toolkit deps so headless tests drive them.
struct CliRequest {
    std::string input;
    std::string queryId;
    std::string exportSvg;
    std::string exportDxf;
    std::string exportPdf;
    std::vector<std::string> actions;
    bool help = false;
};
CliRequest parseCliArgs(const std::vector<std::string>& argv);

}  // namespace pittore::vector
