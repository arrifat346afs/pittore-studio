# src/engine/render/meson.build
# Toolkit-free CPU render helpers shared by the codecs and the editor. The
# layer-style renderer turns a straight-alpha RGBA8 layer plus its parameters
# into the styled raster (drop/inner shadow, outer/inner glow, colour and
# gradient overlay, stroke, bevel, layer blur).

pittore_render = static_library('pittore-render',
  files('style/shared/style_plane.cpp',
        'style/blend/style_blend.cpp',
        'style/blur/style_blur.cpp',
        'style/sdf/style_sdf.cpp',
        'style/fx/style_fx.cpp',
        'style/apply/layer_style_apply.cpp'),
  include_directories : src_inc,
  install : false)

pittore_render_dep = declare_dependency(
  link_with : pittore_render,
  include_directories : src_inc)
