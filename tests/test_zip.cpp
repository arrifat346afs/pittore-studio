// ZIP reader/writer round-trip: deflate + store entries, corruption handling.
#include "engine/io/zip.h"

#include <cstdio>
#include <random>
#include <string>

using pittore::io::ZipEntry;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (cond) return;
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
}

std::vector<std::uint8_t> bytesOf(const std::string& s) {
    return {s.begin(), s.end()};
}

}  // namespace

int main() {
    using pittore::io::zipFind;
    using pittore::io::zipRead;
    using pittore::io::zipWrite;

    // --- deflate round-trip with a compressible payload -------------------
    std::string big;
    for (int i = 0; i < 4096; ++i) big += "The quick brown fox jumps over the lazy dog. ";
    std::string random;
    std::mt19937 gen(42);
    for (int i = 0; i < 8192; ++i) random += static_cast<char>(gen() & 0xff);

    std::vector<ZipEntry> entries;
    entries.push_back({"hello.txt", bytesOf("Hello, Pittore!"), 8});
    entries.push_back({"data.bin", {random.begin(), random.end()}, 8});
    entries.push_back({"stored.txt", bytesOf("plain stored entry"), 0});

    auto archive = zipWrite(entries);
    check(archive.has_value(), "zipWrite succeeds");
    if (!archive) return 1;

    auto back = zipRead(*archive);
    check(back.has_value(), "zipRead succeeds on written archive");
    if (!back) return 1;
    check(back->size() == 3, "all three entries survive");
    const ZipEntry* hello = zipFind(*back, "hello.txt");
    check(hello != nullptr, "zipFind finds hello.txt");
    check(hello && hello->data == bytesOf("Hello, Pittore!"), "hello.txt content intact");
    const ZipEntry* data = zipFind(*back, "data.bin");
    check(data && data->data == std::vector<std::uint8_t>(random.begin(), random.end()),
          "data.bin content intact");
    const ZipEntry* stored = zipFind(*back, "stored.txt");
    check(stored && stored->data == bytesOf("plain stored entry"), "stored entry content intact");

    // --- corruption is rejected, not crashed on ----------------------------
    auto corrupt = *archive;
    if (!corrupt.empty()) corrupt[0] ^= 0xff;
    auto bad = zipRead(corrupt);
    check(!bad.has_value(), "corrupted signature → zipRead fails");
    std::vector<std::uint8_t> tiny = {0x50, 0x4b};
    check(!zipRead(tiny).has_value(), "truncated archive → zipRead fails");

    // --- empty archive round-trip ------------------------------------------
    auto emptyArc = zipWrite({});
    check(emptyArc.has_value(), "zipWrite of empty entry list succeeds");
    auto emptyBack = zipRead(*emptyArc);
    check(emptyBack && emptyBack->empty(), "empty archive reads back empty");

    if (failures == 0) std::printf("OK: zip round-trip, corruption guard, empty archive\n");
    return failures == 0 ? 0 : 1;
}