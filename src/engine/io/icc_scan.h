#pragma once
// Container-level embedded ICC extraction (clean-room implementation).
//
// The decoded image's color space is the first place the import policy looks
// for an embedded profile, but the toolkit only surfaces a profile it can
// model itself: a CMYK profile (or any blob it cannot parse) leaves the
// QImage color space invalid, the import then sees "nothing embedded", and
// the mismatch dialog never fires. The profile is still right there in the
// file, so read it from where writers put it:
//   JPEG  APP2 "ICC_PROFILE" segments (possibly split across several),
//   PNG    the iCCP chunk (zlib-compressed, must precede IDAT),
//   WebP   the ICCP chunk announced by VP8X.
// Fully bounds-checked, never throws; hostile/truncated files yield false.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pittore::io {

// Fills *out with the embedded profile when the buffer starts with a
// container that carries one and every part of it is present; false otherwise.
bool containerIccProfile(const std::uint8_t* p, std::size_t n,
                         std::vector<std::uint8_t>* out);

// Description of the profile embedded in the file at path, or "" when the
// file carries none, is unreadable, or the blob has no readable 'desc' tag.
// Only the file header is read: both specs keep the profile ahead of the
// pixel data, so opening a huge photo costs one small read.
std::string containerIccProfileName(const char* path);

}  // namespace pittore::io
