# src/engine/compute/meson.build
# The compute engine: interface + CPU reference backend always; CUDA and HIP
# backends joined in when (and only when) their toolchains are present.

fs = import('fs')
cc = meson.get_compiler('cpp')

compute_sources = files(
  'blend.cpp',
  'cpu_backend.cpp',
  'layer_mask.cpp',
  'warp.cpp',
  'factory.cpp',
  'brushes/dab/dab.cpp',
  'brushes/erase/erase.cpp',
  'brushes/stamp/stamp.cpp',
  'brushes/smudge/smudge.cpp',
  'brushes/loaders/loaders.cpp',
  'brushes/tone/tone.cpp',
  'brushes/flood/flood.cpp',
  'brushes/replace/replace.cpp',
  'brushes/heal/heal.cpp',
  'brushes/clone/clone.cpp',
  'brushes/composite/composite.cpp',
  'tone_blend.cpp',
)
compute_args = []
compute_deps = []

# --- CUDA backend (NVIDIA) ------------------------------------------------
cuda_feature = get_option('backend-cuda')
pittore_cuda_enabled = false
if cuda_feature.allowed()
  nvcc = find_program('nvcc', required : cuda_feature.enabled())
  if nvcc.found()
    add_languages('cuda', required : true, native : false)
    compute_sources += files('cuda_backend.cu')
    compute_args += '-DPITTORE_HAS_CUDA=1'
    pittore_cuda_enabled = true

    cuda_root = get_option('cuda-root')
    if cuda_root == ''
      cuda_root = fs.parent(fs.parent(nvcc.full_path()))
    endif
    cuda_lib_dir = join_paths(cuda_root, 'lib64')
    cudart = cc.find_library('cudart', dirs : [cuda_lib_dir], required : true)
    compute_deps += declare_dependency(
      dependencies : cudart,
      link_args : ['-Wl,-rpath,' + cuda_lib_dir])
    message('pittore-studio: CUDA backend enabled  (toolkit @ ' + cuda_root + ')')
  endif
else
  message('pittore-studio: CUDA backend disabled')
endif

# --- HIP backend (AMD) ------------------------------------------------------
# Meson has no "hip" language (add_languages('hip') errors on 1.11+), so the
# kernels + runtime are compiled directly by hipcc through a custom_target.
# The resulting object is archived into libpittore-compute, so every consumer
# (app, test_hip, test-all) picks it up through pittore_compute_dep.
hip_feature = get_option('backend-hip')
pittore_hip_enabled = false
if hip_feature.allowed()
  hip_root = get_option('hip-root')
  if hip_root != ''
    hipcc = find_program(join_paths(hip_root, 'bin', 'hipcc'),
                         required : hip_feature.enabled())
  else
    hipcc = find_program('hipcc', required : hip_feature.enabled())
  endif
  if hipcc.found()
    if hip_root == ''
      hip_root = fs.parent(fs.parent(hipcc.full_path()))
    endif
    hip_obj = custom_target('hip_backend_obj',
      input : files('hip_backend.cpp'),
      output : 'hip_backend.hip.o',
      command : [hipcc, '-c',
                 '-DPITTORE_HAS_HIP=1',
                 '-std=c++20',
                 '-I' + meson.project_source_root() + '/src',
                 '-I' + join_paths(hip_root, 'include'),
                 '@INPUT@',
                 '-o', '@OUTPUT@'],
      build_by_default : true)
    compute_sources += hip_obj
    compute_args += '-DPITTORE_HAS_HIP=1'
    pittore_hip_enabled = true

    hip_lib_dir = join_paths(hip_root, 'lib')
    hip_rt = cc.find_library('amdhip64', dirs : [hip_lib_dir], required : true)
    compute_deps += declare_dependency(
      dependencies : hip_rt,
      link_args : ['-Wl,-rpath,' + hip_lib_dir])
    message('pittore-studio: HIP backend enabled  (ROCm @ ' + hip_root + ')')
  endif
else
  message('pittore-studio: HIP backend disabled')
endif

pittore_compute = static_library('pittore-compute',
  compute_sources,
  include_directories : src_inc,
  cpp_args : compute_args,
  # `-arch=native`: nvcc otherwise defaults to sm_75 (CUDA 13), which the
  # driver JIT-compiles to this GPU's sm_120 on first launch — slower code
  # plus a compile stall. Native SASS also lets ptxas schedule for Blackwell.
  cuda_args : ['--expt-relaxed-constexpr', '-DPITTORE_HAS_CUDA=1',
               '-arch=native'],
  dependencies : compute_deps,
  install : false)

pittore_compute_dep = declare_dependency(
  link_with : pittore_compute,
  include_directories : src_inc,
  dependencies : compute_deps)