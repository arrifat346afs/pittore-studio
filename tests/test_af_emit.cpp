// test_af_emit.cpp — graph serializer fidelity: parse every Affinity
// fixture/probe doc.dat and re-emit it. Byte-exact round trips prove the
// emitter reproduces Affinity's own wire layout, which is what makes
// template-derived exports structurally valid.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "af_probe.h"
#include "engine/io/af_layers/container/af_archive.h"
#include "engine/io/af_layers/emit/af_emit.h"
#include "engine/io/af_layers/graph/af_graph.h"
#include "engine/io/af_layers/graph/af_graph_parser.h"
#include "test_util.h"

namespace {

namespace fs = std::filesystem;
using pittore::io::af_detail::Archive;
using pittore::io::af_detail::emitGraph;
using pittore::io::af_detail::parseGraph;

int tested = 0;
int exact = 0;

void runFile(const std::string& path) {
    auto bytes = pittore::probe::readFile(path);
    if (!bytes) return;
    Archive ar;
    try {
        ar = Archive::parse(*bytes);
    } catch (const std::exception& e) {
        std::printf("  %-28s parse: %s\n", fs::path(path).filename().c_str(), e.what());
        return;
    }
    const auto* de = ar.head("doc.dat");
    if (!de) return;
    std::vector<std::uint8_t> doc;
    try {
        doc = ar.extract(*de);
    } catch (const std::exception& e) {
        std::printf("  %-28s extract: %s\n", fs::path(path).filename().c_str(), e.what());
        return;
    }
    ++tested;
    try {
        auto g = parseGraph(doc);
        std::vector<std::uint8_t> back;
        emitGraph(g, back);
        if (back == doc) {
            ++exact;
        } else {
            std::size_t at = 0;
            while (at < back.size() && at < doc.size() && back[at] == doc[at]) ++at;
            std::printf("  %-28s DIFF at %zu (orig %zu, emit %zu)\n",
                        fs::path(path).filename().c_str(), at, doc.size(), back.size());
            CHECK(false);
        }
    } catch (const std::exception& e) {
        std::printf("  %-28s emit: %s\n", fs::path(path).filename().c_str(), e.what());
        CHECK(false);
    }
}

void run() {
    const std::string probe = pittore::probe::probeDir();
    const std::string aff = pittore::probe::afDesignDir();
    if (probe.empty() && aff.empty()) {
        std::printf("  no fixture root (PITTORE_AF_PROBE_DIR / "
                    "PITTORE_AFDESIGN_DIR unset)\n");
        return;
    }
    for (const std::string& dir : {aff, probe}) {
        if (dir.empty()) continue;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!e.is_regular_file()) continue;
            const std::string ext = e.path().extension().string();
            if (ext != ".af" && ext != ".afdesign" && ext != ".afphoto" &&
                ext != ".afpub")
                continue;
            runFile(e.path().string());
        }
    }
    std::printf("  emit fidelity: %d/%d byte-exact\n", exact, tested);
    if (tested == 0) {
        std::printf("  no fixtures found — skipped\n");
        return;
    }
    CHECK(tested > 100);
    CHECK(exact == tested);
}

}  // namespace

TEST_MAIN_CALL(run)
