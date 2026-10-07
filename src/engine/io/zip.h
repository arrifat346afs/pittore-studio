#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pittore::io {

// One file in a ZIP.
struct ZipEntry {
    std::string name;                  // UTF-8 path inside the archive
    std::vector<std::uint8_t> data;
    std::uint16_t method = 8;          // 0 = stored, 8 = deflate
};

// Unpacks all entries. Nullopt on bad data. Dup names keep the last.
std::optional<std::vector<ZipEntry>> zipRead(const std::vector<std::uint8_t>& archive);

// Packs entries (deflate unless method is 0). Nullopt on zlib failure.
std::optional<std::vector<std::uint8_t>> zipWrite(const std::vector<ZipEntry>& entries);

// Finds entry by name. Nullptr if missing.
const ZipEntry* zipFind(const std::vector<ZipEntry>& entries, const std::string& name);

}  // namespace pittore::io