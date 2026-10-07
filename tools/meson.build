# tools/meson.build — developer utilities (built, not tested).
#
# run_tone_blend16 exercises the real engine transfer
# (pittore::compute::applyToneBlend) on two same-size photos from the
# command line. I/O is 16-bit native-endian raw RGB (magick `rgb:`), e.g.:
#   magick tree.png -depth 16 rgb:tree.raw
#   magick sunset.jpg -resize 3000x1987! -depth 16 rgb:sunset.raw
#   ./run_tone_blend16 tree.raw sunset.raw 3000 1987 out.raw
#   magick -size 3000x1987 -depth 16 rgb:out.raw out.png

tone_tool = executable('run_tone_blend16', 'run_tone_blend16.cpp',
  dependencies : [pittore_compute_dep],
  include_directories : src_inc)
