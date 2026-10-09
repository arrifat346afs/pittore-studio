// Raw XML reader. Single pass, keeps unknown nodes.
#include "engine/vector/svg/xml_reader.h"

#include <cctype>

namespace pittore::svg {
namespace {

// Skip blanks.
void skipWs(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace((unsigned char)s[i])) {
        ++i;
    }
}

// True for tag and attribute name bytes.
bool isName(int c) {
    return std::isalnum((unsigned char)c) || c == '-' || c == '_' ||
           c == ':' || c == '.';
}

// Decode the 5 predefined entities plus numeric refs.
std::string decodeEnt(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        if (in[i] != '&') {
            out += in[i++];
            continue;
        }
        const size_t semi = in.find(';', i);
        if (semi == std::string::npos) {
            out += in[i++];
            continue;
        }
        const std::string ent = in.substr(i + 1, semi - i - 1);
        if (ent == "amp") {
            out += '&';
        } else if (ent == "lt") {
            out += '<';
        } else if (ent == "gt") {
            out += '>';
        } else if (ent == "quot") {
            out += '"';
        } else if (ent == "apos") {
            out += '\'';
        } else if (!ent.empty() && ent[0] == '#') {
            unsigned v = 0;
            const bool hex = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X');
            try {
                v = (unsigned)std::stoul(hex ? ent.substr(2) : ent.substr(1),
                                        nullptr, hex ? 16 : 10);
            } catch (...) {
                v = 0;
            }
            if (v < 0x80) {
                out += (char)v;
            } else if (v < 0x800) {
                out += (char)(0xC0 | (v >> 6));
                out += (char)(0x80 | (v & 0x3F));
            } else {
                out += (char)(0xE0 | (v >> 12));
                out += (char)(0x80 | ((v >> 6) & 0x3F));
                out += (char)(0x80 | (v & 0x3F));
            }
        } else {
            out += in.substr(i, semi - i + 1);
        }
        i = semi + 1;
    }
    return out;
}

// Append non-blank text to the open node.
void pushText(const std::string& t,
              std::vector<std::shared_ptr<XmlNode>>& stack) {
    if (stack.empty() || t.empty()) {
        return;
    }
    bool blank = true;
    for (char c : t) {
        if (!std::isspace((unsigned char)c)) {
            blank = false;
            break;
        }
    }
    if (!blank) {
        stack.back()->text += decodeEnt(t);
    }
}

// Attach a finished node to its parent or root slot.
void attach(std::shared_ptr<XmlNode> done,
            std::vector<std::shared_ptr<XmlNode>>& stack,
            std::shared_ptr<XmlNode>& root) {
    if (stack.empty()) {
        if (!root) {
            root = done;
        }
        return;
    }
    done->parent = stack.back().get();
    stack.back()->children.push_back(done);
}

}  // namespace

XmlRead readXml(const std::string& xml) {
    XmlRead out;
    std::vector<std::shared_ptr<XmlNode>> stack;
    size_t i = 0;
    while (i < xml.size()) {
        if (xml[i] != '<') {
            const size_t lt = xml.find('<', i);
            pushText(xml.substr(i, lt == std::string::npos ? std::string::npos
                                                           : lt - i),
                     stack);
            i = lt == std::string::npos ? xml.size() : lt;
            continue;
        }
        // Skip comments.
        if (xml.compare(i, 4, "<!--") == 0) {
            const size_t end = xml.find("-->", i + 4);
            if (end == std::string::npos) {
                out.error = "unterminated comment";
                return out;
            }
            i = end + 3;
            continue;
        }
        // Skip processing instructions.
        if (xml.compare(i, 2, "<?") == 0) {
            const size_t end = xml.find("?>", i + 2);
            i = end == std::string::npos ? xml.size() : end + 2;
            continue;
        }
        // Skip doctype.
        if (xml.compare(i, 9, "<!DOCTYPE") == 0) {
            const size_t end = xml.find('>', i + 9);
            i = end == std::string::npos ? xml.size() : end + 1;
            continue;
        }
        // Keep CDATA as text.
        if (xml.compare(i, 9, "<![CDATA[") == 0) {
            const size_t end = xml.find("]]>", i + 9);
            pushText(xml.substr(i + 9, end == std::string::npos
                                             ? std::string::npos
                                             : end - i - 9),
                     stack);
            i = end == std::string::npos ? xml.size() : end + 3;
            continue;
        }
        const bool close = i + 1 < xml.size() && xml[i + 1] == '/';
        size_t j = i + (close ? 2 : 1);
        skipWs(xml, j);
        const size_t n0 = j;
        while (j < xml.size() && isName(xml[j])) {
            ++j;
        }
        const std::string tag = xml.substr(n0, j - n0);
        if (tag.empty()) {
            out.error = "bad tag";
            return out;
        }
        if (close) {
            const size_t gt = xml.find('>', j);
            if (gt == std::string::npos) {
                out.error = "unterminated close tag";
                return out;
            }
            if (!stack.empty() && stack.back()->tag == tag) {
                auto done = stack.back();
                stack.pop_back();
                attach(done, stack, out.root);
            }
            i = gt + 1;
            continue;
        }
        auto el = std::make_shared<XmlNode>();
        el->tag = tag;
        bool selfClose = false;
        while (j < xml.size()) {
            skipWs(xml, j);
            if (j >= xml.size()) {
                break;
            }
            if (xml[j] == '>') {
                ++j;
                break;
            }
            if (xml.compare(j, 2, "/>") == 0) {
                selfClose = true;
                j += 2;
                break;
            }
            const size_t a0 = j;
            while (j < xml.size() && isName(xml[j])) {
                ++j;
            }
            const std::string attr = xml.substr(a0, j - a0);
            skipWs(xml, j);
            std::string val;
            if (j < xml.size() && xml[j] == '=') {
                ++j;
                skipWs(xml, j);
                if (j < xml.size() && (xml[j] == '"' || xml[j] == '\'')) {
                    const char q = xml[j++];
                    const size_t v0 = j;
                    const size_t ve = xml.find(q, j);
                    if (ve == std::string::npos) {
                        out.error = "unterminated attribute";
                        return out;
                    }
                    val = decodeEnt(xml.substr(v0, ve - v0));
                    j = ve + 1;
                } else {
                    // Bare value ends at space or tag end.
                    const size_t v0 = j;
                    while (j < xml.size() && !std::isspace((unsigned char)xml[j]) &&
                           xml[j] != '>' && xml[j] != '/') {
                        ++j;
                    }
                    val = xml.substr(v0, j - v0);
                }
            }
            if (!attr.empty()) {
                el->attrs[attr] = val;
            }
        }
        if (selfClose) {
            if (stack.empty()) {
                if (!out.root) {
                    out.root = el;
                } else {
                    // Extra top-level node: wrap so nothing is lost.
                    auto wrap = std::make_shared<XmlNode>();
                    wrap->tag = "svg";
                    out.root->parent = wrap.get();
                    wrap->children.push_back(out.root);
                    el->parent = wrap.get();
                    wrap->children.push_back(el);
                    out.root = wrap;
                }
            } else {
                attach(el, stack, out.root);
            }
            i = j;
            continue;
        }
        stack.push_back(el);
        i = j;
    }
    // Flush unclosed nodes in order.
    while (!stack.empty()) {
        auto done = stack.back();
        stack.pop_back();
        if (stack.empty()) {
            if (!out.root) {
                out.root = done;
            }
        } else {
            attach(done, stack, out.root);
        }
    }
    out.ok = out.root != nullptr;
    return out;
}

}  // namespace pittore::svg
