# AGENTS.md

Instructions for AI coding agents working in this repository. Humans may read it too. It is the source of truth for build commands, layout, conventions, and legal constraints.

## Project

Pittore Studio is a layer-based photo editor for Linux, written in C++20 on Qt 6. It covers raster painting and retouching, a vector persona, text, adjustment layers, filters, and an ICC-aware colour pipeline. Rendering uses GPU compute (CUDA or HIP) with a CPU fallback. The CPU path is the reference implementation.

Status: alpha. Linux only. The build refuses to configure on other platforms.

License: MIT (see `LICENSE`).

## Commands

```sh
./build.sh                                   # bootstrap a machine, configure, compile
meson setup build                            # configure
ninja -C build                               # compile
./build/src/app/painter                      # run
just test                                    # build + run the full suite
ninja -C build && meson test -C build --print-errorlogs   # same, without just
meson test -C build <name> --print-errorlogs # single test
```

Optional: `just models` downloads AI models (multi-GB, never bundle them). `just install` installs system-wide.

Meson options: `backend-cuda`, `backend-hip`, `cuda-root`, `hip-root`, `onnxruntime`, `onnxruntime-root`, `app`. Backends and ONNX Runtime are auto-detected. Do not make the build depend on a GPU toolchain being present.

## Layout

```
src/engine/    core types, compute kernels, IO codecs, filters, render, text, colour, vector, AI
src/ui/        Qt interface: panels, canvas, tools, dialogs, settings
src/app/       entry point
tests/         unit, codec, GPU-parity, and headless UI tests
tools/         developer utilities
docs/          feature list, build guide, vector engine notes
packaging/     desktop file, metainfo, MIME types, icons
```

Dependency direction is one-way: `ui` depends on `engine`, never the reverse. Do not include Qt headers from `src/engine/`.

## Working rules

1. Diagnose and fix. Reproduce the failure, find the root cause, and fix it. Do not paper over a failing test or loosen an assertion to get green.
2. Keep `just test` green. Run it before declaring any task complete. Report the actual result, including failures you did not fix.
3. Deliver complete files. When changing a file, return the whole file unless a patch is explicitly requested.
4. One responsibility per file. Prefer a new small file over growing an existing large one. Several existing files are already over 50 KB; do not add to them.
5. Match existing style in the surrounding code (naming, namespaces, error handling, comment density). Do not reformat unrelated code.
6. No speculative features, no drive-by refactors. Keep diffs scoped to the task.
7. Output is clean and professional: no emoji in code, comments, commit messages, UI strings, or docs.
8. Be terse in explanations. State what changed, why, and what was verified.

## Correctness requirements

- CPU/GPU parity: every compute kernel must have the CPU and GPU backends agree. Add or update the parity test when touching a kernel. The CPU path defines correct behaviour.
- Colour: respect embedded ICC profiles and the working-space mismatch policy. Do not silently treat unmanaged pixels as sRGB.
- Undo: any user-visible document mutation must go through the history system.
- Tests that depend on sample files not in the repo must skip themselves when the file is absent. Never commit large binary fixtures.
- File formats (`.psc` native, `.ifp` legacy, PSD, XCF, Affinity): preserve unknown data on round trip where the format allows. Never discard user content silently; report what was baked or dropped.

## Legal and provenance rules (hard constraints)

This is an MIT project. Contamination by copyleft or proprietary code is the most serious class of defect an agent can introduce.

- Do not copy, translate, or closely paraphrase source code from GPL/LGPL/AGPL projects (including Krita, Inkscape, GIMP, darktable, LibreOffice) or from proprietary software. Reading their documented behaviour and public format specifications is fine; reproducing their code is not.
- Permissively licensed code (MIT, BSD, Apache-2.0, ISC, Zlib) may be adapted only if the license notice and attribution are added to the repository's third-party notices file in the same change. Record the source URL and commit.
- Implement file formats from public specifications and observed behaviour. If a format has no public spec, do not extend reverse-engineered support without asking the maintainer first. This applies in particular to the Affinity (`.af`, `.afphoto`, `.afdesign`, `.afpub`) reader and writer.
- Never embed bytes from third-party-authored documents (templates, sample files, ICC profiles, fonts, brushes, patterns) in source or resources unless their license permits redistribution and it is recorded.
- AI models are downloaded on demand and never bundled. Before adding a model, confirm its license permits the project's intended use and record it. Non-commercial licenses (for example BRIA RMBG) are not acceptable defaults.
- Numeric constants, filter parameters, and algorithms should be original fits or from published papers, cited in a comment. Comments of the form "not copied from any other app" are claims; only write them if true.
- Product names such as Photoshop, Illustrator, Affinity, and Adobe may appear descriptively (for example, "opens PSD files"). Do not use them in the app name, icon, or marketing strings, and do not imply endorsement.
- Do not clone, vendor, or fetch other editors' source trees into this repository or its build.

If a task cannot be done without crossing one of these lines, stop and say so.

## Dependencies

Qt 6 (LGPL; link dynamically), HarfBuzz, FreeType, Little CMS, optional ONNX Runtime, optional CUDA or HIP. These come from the system, not from the repository. Lucide icons are bundled under ISC (`src/ui/icons/lucide/LICENSE.lucide`).

Do not add a dependency without maintainer approval. Any new dependency needs: license, why a system library or existing code is insufficient, and an entry in the third-party notices file.

## Git and commits

- Small, focused commits with an imperative subject line under 72 characters.
- Do not commit build output, logs, model files, `.bak` files, or machine-specific paths.
- Do not rewrite published history.

## When unsure

Ask the maintainer instead of guessing, particularly for: file-format semantics, anything touching licensing, public API or project-format changes, and anything that would change rendered output.
