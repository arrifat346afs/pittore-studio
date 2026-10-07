# src/engine/text/meson.build
# Text layout and rasterization built on the system font stack: fontconfig
# finds faces, HarfBuzz shapes them and FreeType rasterizes the outlines. The
# Affinity codec re-sets imported text through this engine.

harfbuzz_dep = dependency('harfbuzz', required : true)
freetype_dep = dependency('freetype2', required : true)
fontconfig_dep = dependency('fontconfig', required : true)

text_deps = [harfbuzz_dep, freetype_dep, fontconfig_dep]

pittore_text = static_library('pittore-text',
  files('face/text_face.cpp',
        'shape/text_shape.cpp',
        'raster/text_raster.cpp',
        'layout/text_layout.cpp',
        'family/text_family.cpp'),
  include_directories : src_inc,
  dependencies : text_deps,
  install : false)

pittore_text_dep = declare_dependency(
  link_with : pittore_text,
  include_directories : src_inc,
  dependencies : text_deps)
