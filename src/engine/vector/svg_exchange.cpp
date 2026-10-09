// DXF/PDF writers + script-extension host + CLI parsing.
#include "engine/vector/svg_exchange.h"

#include <sstream>

namespace pittore::vector {

std::string writeDxf(const std::vector<DxfPolyline>& polylines) {
    std::ostringstream s;
    s << "0\nSECTION\n2\nENTITIES\n";
    for (auto& p : polylines) {
        s << "0\nLWPOLYLINE\n8\n0\n62\n" << p.color << "\n90\n" << p.points.size()
          << "\n70\n" << (p.closed ? 1 : 0) << "\n";
        for (auto [x, y] : p.points) s << "10\n" << x << "\n20\n" << y << "\n";
    }
    s << "0\nENDSEC\n0\nEOF\n";
    return s.str();
}

std::vector<std::uint8_t> writePdf(const std::vector<PdfPage>& pages,
                                   const std::string& title) {
    // Deterministic single-font PDF: Helvetica text, DCT image XObjects,
    // vector path operators and internal link annotations.
    std::string pdf = "%PDF-1.7\n";
    std::vector<size_t> xref;
    std::string kids;
    auto esc = [](const std::string& s) {
        std::string o;
        for (unsigned char c : s) {
            if (c == '(' || c == ')' || c == '\\') {
                o += '\\';
                o += (char)c;
            } else if (c < 32 || c > 126) {
                char b[8];
                snprintf(b, sizeof(b), "\\%03o", c);
                o += b;
            } else {
                o += (char)c;
            }
        }
        return o;
    };
    auto num = [](double v) {
        char b[32];
        snprintf(b, sizeof(b), "%.2f", v);
        return std::string(b);
    };
    struct Staged {
        std::string body;
        bool stream = false;
        std::string streamData = "";
    };
    std::vector<Staged> objs;
    objs.push_back({"<< /Type /Catalog /Pages 2 0 R >>"});  // 1
    objs.push_back({"PAGES"});                              // 2
    objs.push_back({"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"});  // 3
    for (size_t i = 0; i < pages.size(); i++) {
        const PdfPage& pg = pages[i];
        std::string content = pg.content;
        for (auto& t : pg.texts) {
            content += "BT /F1 " + num(t.size) + " Tf " + num(t.x) + " " + num(t.y) +
                       " Td (" + esc(t.text) + ") Tj ET\n";
        }
        for (size_t k = 0; k < pg.images.size(); k++) {
            const PdfImage& im = pg.images[k];
            if (im.jpeg.empty() || im.iw <= 0 || im.ih <= 0) continue;
            content += std::to_string(im.w) + " 0 0 " + std::to_string(im.h) + " " +
                       num(im.x) + " " + num(im.y) + " cm /Im" + std::to_string(k) +
                       " Do\n";
        }
        objs.push_back({"<< /Length " + std::to_string(content.size()) + " >>", true,
                        content});
        int contentId = (int)objs.size();  // 1-based
        std::string xobjs;
        for (size_t k = 0; k < pg.images.size(); k++) {
            const PdfImage& im = pg.images[k];
            if (im.jpeg.empty() || im.iw <= 0 || im.ih <= 0) continue;
            std::string dict = "<< /Type /XObject /Subtype /Image /Width " +
                               std::to_string(im.iw) + " /Height " + std::to_string(im.ih) +
                               " /ColorSpace /DeviceRGB /BitsPerComponent 8 "
                               "/Filter /DCTDecode /Length " +
                               std::to_string(im.jpeg.size()) + " >>";
            objs.push_back({dict, true,
                            std::string((const char*)im.jpeg.data(), im.jpeg.size())});
            int imgId = (int)objs.size();
            xobjs += "/Im" + std::to_string(k) + " " + std::to_string(imgId) + " 0 R ";
        }
        std::string annots;
        for (auto& l : pg.links) {
            objs.push_back({"<< /Type /Annot /Subtype /Link /Rect [" + num(l.x0) +
                            " " + num(l.y0) + " " + num(l.x1) + " " + num(l.y1) +
                            "] /Dest [" + l.dest + "] >>"});
            annots += std::to_string((int)objs.size()) + " 0 R ";
        }
        std::string page = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + num(pg.wPt) +
                           " " + num(pg.hPt) + "] /Contents " + std::to_string(contentId) +
                           " 0 R /Resources << /Font << /F1 3 0 R >>";
        if (!xobjs.empty()) page += " /XObject << " + xobjs + ">>";
        page += " >>";
        if (!annots.empty()) page += " /Annots [" + annots + "]";
        page += " >>";
        objs.push_back({page});
        kids += std::to_string((int)objs.size()) + " 0 R ";
    }
    objs.push_back({"<< /Title (" + esc(title) + ") /Producer (Pittore Studio) >>"});
    for (auto& o : objs) {
        if (o.body == "PAGES") {
            xref.push_back(pdf.size());
            pdf += std::to_string(xref.size()) + " 0 obj\n<< /Type /Pages /Kids [" +
                   kids + "] /Count " + std::to_string(pages.size()) + " >>\nendobj\n";
            continue;
        }
        xref.push_back(pdf.size());
        pdf += std::to_string(xref.size()) + " 0 obj\n" + o.body;
        if (o.stream) pdf += "\nstream\n" + o.streamData + "\nendstream";
        pdf += "\nendobj\n";
    }
    size_t start = pdf.size();
    pdf += "xref\n0 " + std::to_string(xref.size() + 1) + "\n0000000000 65535 f \n";
    char buf[32];
    for (size_t o : xref) {
        snprintf(buf, sizeof(buf), "%010zu 00000 n \n", o);
        pdf += buf;
    }
    pdf += "trailer\n<< /Size " + std::to_string(xref.size() + 1) + " /Root 1 0 R >>\n"
           "startxref\n" +
           std::to_string(start) + "\n%%EOF";
    return std::vector<std::uint8_t>(pdf.begin(), pdf.end());
}

ExtensionDef parseExtension(const std::string& xml, bool& ok) {
    ExtensionDef e;
    ok = false;
    // Root element id, tag-agnostic (any single-root descriptor works).
    {
        size_t p = xml.find('<');
        size_t q = xml.find("id=\"", p == std::string::npos ? 0 : p);
        if (q != std::string::npos) {
            q += 4;
            size_t r = xml.find('"', q);
            e.id = xml.substr(q, r == std::string::npos ? 0 : r - q);
        }
    }
    size_t n0 = xml.find("<name>");
    if (n0 != std::string::npos) {
        size_t n1 = xml.find("</name>", n0);
        e.name = xml.substr(n0 + 6, n1 == std::string::npos ? 0 : n1 - n0 - 6);
    }
    size_t s0 = xml.find("<script>");
    if (s0 != std::string::npos) {
        size_t s1 = xml.find("</script>", s0);
        e.script = xml.substr(s0 + 8, s1 == std::string::npos ? 0 : s1 - s0 - 8);
    }
    // Params: <param name=".." gui-text=".." type="..">default</param>
    size_t p = 0;
    while ((p = xml.find("<param", p)) != std::string::npos) {
        size_t e2 = xml.find('>', p);
        if (e2 == std::string::npos) break;
        std::string head = xml.substr(p, e2 - p);
        auto at = [&](const std::string& k) -> std::string {
            size_t q = head.find(k + "=\"");
            if (q == std::string::npos) return "";
            q += k.size() + 2;
            size_t r = head.find('"', q);
            return head.substr(q, r == std::string::npos ? 0 : r - q);
        };
        size_t c = xml.find("</param>", e2);
        ExtensionParam pr{at("name"), at("gui-text"), at("type"),
                    c == std::string::npos ? "" : xml.substr(e2 + 1, c - e2 - 1)};
        e.params.push_back(pr);
        p = e2 + 1;
    }
    e.rawXml = xml;
    ok = !e.id.empty();
    return e;
}

const ExtensionDef* ExtensionDb::find(const std::string& id) const {
    auto it = byId.find(id);
    return it == byId.end() ? nullptr : &it->second;
}

std::vector<std::string> ExtensionDb::ids() const {
    std::vector<std::string> out;
    for (auto& [k, v] : byId) out.push_back(k);
    return out;
}

ExtensionResult runExtension(const ExtensionDef& ext, const std::string& inSvg,
                             const std::map<std::string, std::string>& args,
                             SubprocessRunner runner, int timeoutMs) {
    std::string cmd = ext.script;
    for (auto& [k, v] : args) {
        cmd += " --" + k + "=\"" + v + "\"";
    }
    if (!runner) return ExtensionResult{false, "", "no runner"};
    return runner(cmd, inSvg, timeoutMs);
}

CliRequest parseCliArgs(const std::vector<std::string>& argv) {
    CliRequest r;
    for (size_t i = 0; i < argv.size(); i++) {
        const std::string& a = argv[i];
        auto val = [&](const std::string& prefix) -> std::string {
            if (a.rfind(prefix, 0) == 0) {
                auto eq = a.find('=');
                if (eq != std::string::npos) return a.substr(eq + 1);
                if (i + 1 < argv.size()) return argv[++i];
            }
            return "";
        };
        if (a == "--help" || a == "-h")
            r.help = true;
        else if (a.rfind("--query-id", 0) == 0)
            r.queryId = val("--query-id");
        else if (a.rfind("--export-plain-svg", 0) == 0)
            r.exportSvg = val("--export-plain-svg");
        else if (a.rfind("--export-dxf", 0) == 0)
            r.exportDxf = val("--export-dxf");
        else if (a.rfind("--export-pdf", 0) == 0)
            r.exportPdf = val("--export-pdf");
        else if (a.rfind("--actions", 0) == 0) {
            std::string list = val("--actions");
            size_t s = 0;
            while (s < list.size()) {
                size_t c = list.find(',', s);
                r.actions.push_back(list.substr(s, c == std::string::npos ? std::string::npos
                                                                         : c - s));
                if (c == std::string::npos) break;
                s = c + 1;
            }
        } else if (!a.empty() && a[0] != '-' && r.input.empty()) {
            r.input = a;
        }
    }
    return r;
}

}  // namespace pittore::vector
