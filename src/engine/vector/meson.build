# src/engine/vector/meson.build
# Vector scene model + SVG parser, and the path builder / scanline rasterizer
# that shapes and text paint through. Header-only consumers link this lib;
# the rasterizer kernels (engine/compute) include the headers through src_inc
# and keep their own device-side scene POD mirrors.

pittore_vector = static_library('pittore-vector',
  files('svg_parse.cpp', 'path.cpp', 'vector_shape.cpp', 'vector_art.cpp',
        'boolean.cpp', 'svg_dom.cpp', 'clone.cpp', 'marker.cpp', 'pattern.cpp',
        'mesh.cpp', 'clip_mask.cpp', 'filter_fe.cpp', 'snap.cpp', 'grids.cpp',
        'transform_ops.cpp', 'path_ops.cpp', 'spiro.cpp', 'calligraphy.cpp',
        'connector.cpp', 'trace.cpp', 'box3d.cpp', 'spray.cpp', 'text_flow.cpp',
        'palette.cpp', 'svg_exchange.cpp', 'lpe/lpe.cpp', 'lpe/lpe_drag.cpp', 'lpe/lpe_offset.cpp',
        'lpe/lpe_deform.cpp', 'lpe/lpe_generate.cpp', 'lpe/lpe_convert.cpp'),
  include_directories : src_inc,
  install : false)

pittore_vector_dep = declare_dependency(
  link_with : pittore_vector,
  include_directories : src_inc)