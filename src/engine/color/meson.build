# src/engine/color/meson.build
# Color management: soft-proofing over LittleCMS2. Host-side only (pixels in,
# pixels out) — no compute-backend involvement by design. LCMS2 is optional:
# without it the module compiles to a stub reporting unavailable (see
# proof.cpp), so builds on systems without lcms2 keep working.

lcms_dep = dependency('lcms2', version : '>= 2.9', required : false)
color_args = []
color_deps = []
if lcms_dep.found()
  color_deps += lcms_dep
  color_args += '-DPITTORE_LCMS2'
  message('pittore-studio: LCMS2 soft-proofing enabled')
else
  message('pittore-studio: LCMS2 not found — soft-proofing unavailable '
          + '(install lcms2)')
endif

pittore_color = static_library('pittore-color',
  files('proof.cpp', 'convert.cpp', 'icc_convert.cpp', 'separate.cpp'),
  include_directories : src_inc,
  cpp_args : color_args,
  dependencies : color_deps,
  install : false)

pittore_color_dep = declare_dependency(
  link_with : pittore_color,
  include_directories : src_inc,
  dependencies : color_deps,
  compile_args : color_args)
