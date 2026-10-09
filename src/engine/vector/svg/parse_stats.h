#pragma once
// Tree census for perf guards.
namespace pittore::svg {

struct XmlNode;

struct SvgStats {
    int nodes = 0;
    int maxDepth = 0;
    int paths = 0;
    int uses = 0;
};

SvgStats collectStats(const XmlNode& root);

}  // namespace pittore::svg
