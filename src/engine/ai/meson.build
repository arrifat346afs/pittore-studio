# src/engine/ai/meson.build
# On-device segmentation (background removal) through ONNX Runtime. The runtime
# is never bundled with a build — it is either the Arch/`onnxruntime` pacman
# package (auto-detected) or any ONNX Runtime prefix you point at with
#   meson setup build -Donnxruntime-root=/path/to/onnxruntime
# Model *weights* are separate again: they are downloaded at run time into the
# per-user model cache by the in-app AI manager / the `just models` recipe.

cc = meson.get_compiler('cpp')
onnx_feature = get_option('onnxruntime')
onnx_root = get_option('onnxruntime-root')
pittore_onnx_enabled = false
ai_args = []
ai_deps = []

find_onnx = false
if onnx_feature.allowed()
  if onnx_root != ''
    onnx_inc = include_directories(join_paths(onnx_root, 'include'))
    onnx_lib = cc.find_library('onnxruntime',
      dirs : [join_paths(onnx_root, 'lib'), join_paths(onnx_root, 'lib64')],
      required : onnx_feature.enabled())
    if onnx_lib.found()
      find_onnx = true
      ai_deps += declare_dependency(
        dependencies : onnx_lib,
        include_directories : onnx_inc,
        link_args : ['-Wl,-rpath,' + join_paths(onnx_root, 'lib')])
    endif
  else
    onnx_dep = dependency('onnxruntime', required : false)  # pkg-config
    if onnx_dep.found()
      find_onnx = true
      ai_deps += onnx_dep
    else
      onnx_lib = cc.find_library('onnxruntime', required : false)
      if onnx_lib.found()
        foreach inc : ['/usr/include/onnxruntime', '/usr/local/include/onnxruntime', '/usr/include']
          if cc.has_header('onnxruntime_cxx_api.h', args : '-I' + inc)
            find_onnx = true
            ai_deps += declare_dependency(
              dependencies : onnx_lib,
              include_directories : include_directories(inc))
            break
          endif
        endforeach
      endif
    endif
  endif
endif

if not find_onnx and onnx_feature.enabled()
  error('onnxruntime requested (-Donnxruntime=enabled) but no runtime was found.')
endif

if find_onnx
  ai_args += '-DPITTORE_HAS_ONNX=1'
  pittore_onnx_enabled = true
  message('pittore-studio: ONNX Runtime AI backend enabled')
else
  message('pittore-studio: ONNX Runtime not found — AI background removal unavailable '
          + '(install onnxruntime-cuda or set -Donnxruntime-root)')
endif

pittore_ai = static_library('pittore-ai',
  files('session/ai_session.cpp',
        'preprocess/ai_preprocess.cpp',
        'session/ai_runtime.cpp',
        'segment/ai_segment.cpp',
        'encode/ai_encode.cpp',
        'decode/ai_decode.cpp',
        'decode/ai_decode_pub.cpp',
        'select/ai_select.cpp',
        'refine/ai_refine.cpp'),
  include_directories : src_inc,
  cpp_args : ai_args,
  dependencies : ai_deps,
  install : false)

pittore_ai_dep = declare_dependency(
  link_with : pittore_ai,
  include_directories : src_inc,
  dependencies : ai_deps)