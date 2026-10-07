#!/usr/bin/env bash
#
# Pittore Studio bootstrap script.
#
#   1. Detects your distro and installs the packages needed to build.
#   2. Configures + builds with Meson/Ninja (GPU backends auto-detected).
#   3. Prints where the binary is; install system-wide separately with
#      `just install` (see the justfile).
#
# Usage:
#   ./build.sh                 one command: install missing deps, build, done.
#                              Asks whether you want release or debug.
#   ./build.sh --no-deps       never install deps (also auto-skipped when all
#                              build tools/libs are detected present)
#   ./build.sh --buildtype=release
#                              force a specific build type, skipping the prompt
#   ./build.sh --buildtype=debug
#                              unoptimised (AF import ~7x slower)
#   ./build.sh --help
#
# At the end, if you're at a terminal, it asks whether to install system-wide
# into /usr. Answering yes runs the install with your password (pkexec when
# you're in a graphical session, sudo otherwise).
#
# Dependency detection probes the same pkg-config modules meson looks up, so
# "all present" means the tree will actually configure. If ./build was
# configured with a compiler wrapper that has since been removed (ccache,
# sccache, ...), the cached absolute path is dead and every compile fails; the
# script notices that and reconfigures the build dir from scratch.
#
set -euo pipefail

# ── output helpers ───────────────────────────────────────────────────────────
if [[ -t 1 ]]; then
    C_RESET=$'\033[0m'; C_BOLD=$'\033[1m'
    C_GREEN=$'\033[32m'; C_YEL=$'\033[33m'; C_RED=$'\033[31m'; C_CYN=$'\033[36m'
else
    C_RESET=; C_BOLD=; C_GREEN=; C_YEL=; C_RED=; C_CYN=
fi
info()  { printf "${C_CYN}::${C_RESET} %s\n" "$*"; }
ok()    { printf "${C_GREEN}✔${C_RESET} %s\n" "$*"; }
warn()  { printf "${C_YEL}⚠${C_RESET} %s\n" "$*"; }
die()   { printf "${C_RED}✖${C_RESET} %s\n" "$*" >&2; exit 1; }

# ── flags ────────────────────────────────────────────────────────────────────
NO_DEPS=0
# Default until choose_buildtype() runs. `--buildtype=` on the command line
# overrides it before the prompt is ever shown.
BUILDTYPE=debugoptimized
BUILDTYPE_OVERRIDE=""
for arg in "$@"; do
    case "$arg" in
        --no-deps)       NO_DEPS=1 ;;
        --buildtype=*)   BUILDTYPE="${arg#*=}"; BUILDTYPE_OVERRIDE="${arg#*=}" ;;
        -h|--help)
            sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) die "unknown argument: $arg (see --help)" ;;
    esac
done

cd "$(dirname "$0")"

# ── privilege escalation (graphical prompt preferred) ────────────────────────
as_root() {
    if command -v pkexec >/dev/null 2>&1 && [[ -n "${WAYLAND_DISPLAY:-}${DISPLAY:-}" ]]; then
        pkexec "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        die "need root privileges but neither pkexec nor sudo is available"
    fi
}

# ── GPU detection ─────────────────────────────────────────────────────────────
# Fills GPU_NAMES with the vendors of present GPUs ("NVIDIA", "AMD", "Intel" —
# a laptop can carry several). Used only to print policy: this script NEVER
# installs vendor GPU toolchains, whichever GPU is present.
GPU_NAMES=()
detect_gpu() {
    GPU_NAMES=()
    command -v lspci >/dev/null 2>&1 || return 0
    local devs
    devs="$(lspci 2>/dev/null | grep -iE 'vga compatible controller|3d controller|display controller' || true)"
    [[ -n "$devs" ]] || return 0
    if grep -qiE 'nvidia|geforce|quadro|tesla' <<<"$devs"; then GPU_NAMES+=(NVIDIA); fi
    if grep -qiE 'advanced micro devices|\bamd\b|radeon|vega' <<<"$devs"; then GPU_NAMES+=(AMD); fi
    if grep -qi 'intel corporation' <<<"$devs"; then GPU_NAMES+=(Intel); fi
}

# One-line policy summary depending on which GPUs are present.
gpu_policy() {
    local has_nv=0 has_amd=0 g
    for g in "${GPU_NAMES[@]}"; do
        [[ "$g" == "NVIDIA" ]] && has_nv=1
        [[ "$g" == "AMD" ]] && has_amd=1
    done
    if [[ $has_nv -eq 1 && $has_amd -eq 1 ]]; then
        info "GPU: NVIDIA + AMD — using each toolchain only if present, never installing."
    elif [[ $has_nv -eq 1 ]]; then
        info "GPU: NVIDIA — never installing AMD/HIP."
    elif [[ $has_amd -eq 1 ]]; then
        info "GPU: AMD — never installing NVIDIA/CUDA."
    else
        info "GPU: no NVIDIA/AMD GPU detected — CPU fallback build."
    fi
}

# ── distro detection ─────────────────────────────────────────────────────────
DISTRO_ID=""
PKG=()
TOOL_PKGS=""
detect_distro() {
    [[ -r /etc/os-release ]] || die "cannot detect distro: /etc/os-release missing"
    . /etc/os-release
    DISTRO_ID="${ID:-}"
    # PKG is the package-manager argv; TOOL_PKGS the toolchain installed when
    # one of meson/ninja/c++/git/pkg-config is absent.
    case "$DISTRO_ID" in
        arch)
            PKG=(pacman --noconfirm -S)
            TOOL_PKGS="base-devel meson ninja git pkgconf" ;;
        fedora)
            PKG=(dnf -y install)
            TOOL_PKGS="gcc-c++ meson ninja-build git pkgconf" ;;
        debian|ubuntu|pikaos)
            PKG=(apt-get -y install)
            TOOL_PKGS="build-essential meson ninja-build git pkg-config" ;;
        opensuse*)
            PKG=(zypper --non-interactive install)
            TOOL_PKGS="gcc-c++ meson ninja git pkgconf" ;;
        *) die "unsupported distro '$DISTRO_ID' (supported: arch, fedora, debian, ubuntu, pikaos, opensuse)" ;;
    esac
}

# ── dependency requirements ──────────────────────────────────────────────────
# These are the pkg-config module names the *.meson.build files ask for.
# REQ_MODULES gate the app and engine; if one is missing the tree either fails
# to configure or silently drops the whole Qt app. OPT_MODULES are declared
# `required: false` upstream, so meson tolerates their absence — but they gate
# real functionality (WebP/TIFF/JPEG/Zstd decode, the vector QR tool), so we
# still install them and only downgrade to a warning.
#
# HIP/ROCm tools are deliberately NOT here. build.sh never installs anything
# AMD-related — HIP is an optional backend that is only compiled in when hipcc
# is already present on the machine (see detect_backends below).
REQ_MODULES=(
    zlib harfbuzz freetype2 fontconfig tomlplusplus
    Qt6Widgets Qt6Network Qt6Svg
)
OPT_MODULES=(
    lcms2 libwebp libtiff-4 libjpeg libzstd libqrencode
)

# Which package ships each module's .pc file. These are real repo names, not the
# obvious transliteration: on Fedora there is no zlib-devel (the zlib module
# comes from zlib-ng-compat-devel) and no libqrencode-devel (it is
# qrencode-devel). Guessing here is what makes the install fail on a clean
# machine — the package simply does not exist.
declare -A PKG_FEDORA=(
    [zlib]=zlib-ng-compat-devel
    [lcms2]=lcms2-devel
    [harfbuzz]=harfbuzz-devel
    [freetype2]=freetype-devel
    [fontconfig]=fontconfig-devel
    [tomlplusplus]=tomlplusplus-devel
    [Qt6Widgets]=qt6-qtbase-devel
    [Qt6Network]=qt6-qtbase-devel
    [Qt6Svg]=qt6-qtsvg-devel
    [libwebp]=libwebp-devel
    [libtiff-4]=libtiff-devel
    [libjpeg]=libjpeg-turbo-devel
    [libzstd]=libzstd-devel
    [libqrencode]=qrencode-devel
)
declare -A PKG_ARCH=(
    [zlib]=zlib
    [lcms2]=lcms2
    [harfbuzz]=harfbuzz
    [freetype2]=freetype2
    [fontconfig]=fontconfig
    [tomlplusplus]=tomlplusplus
    [Qt6Widgets]=qt6-base
    [Qt6Network]=qt6-base
    [Qt6Svg]=qt6-svg
    [libwebp]=libwebp
    [libtiff-4]=tiff
    [libjpeg]=libjpeg-turbo
    [libzstd]=zstd
    [libqrencode]=libqrencode
)
declare -A PKG_DEBIAN=(
    [zlib]=zlib1g-dev
    [lcms2]=liblcms2-dev
    [harfbuzz]=libharfbuzz-dev
    [freetype2]=libfreetype-dev
    [fontconfig]=libfontconfig-dev
    [tomlplusplus]=libtomlplusplus-dev
    [Qt6Widgets]=qt6-base-dev
    [Qt6Network]=qt6-base-dev
    [Qt6Svg]=libqt6svg6-dev
    [libwebp]=libwebp-dev
    [libtiff-4]=libtiff-dev
    [libjpeg]=libjpeg-dev
    [libzstd]=libzstd-dev
    [libqrencode]=libqrencode-dev
)
declare -A PKG_OPENSUSE=(
    [zlib]=zlib-devel
    [lcms2]=lcms2-devel
    [harfbuzz]=harfbuzz-devel
    [freetype2]=freetype2-devel
    [fontconfig]=fontconfig-devel
    [tomlplusplus]=tomlplusplus-devel
    [Qt6Widgets]=qt6-base-devel
    [Qt6Network]=qt6-base-devel
    [Qt6Svg]=libqt6-qt6svg-devel
    [libwebp]=libwebp-devel
    [libtiff-4]=libtiff-devel
    [libjpeg]=libjpeg8-devel
    [libzstd]=libzstd-devel
    [libqrencode]=libqrencode-devel
)

# Package backing a module on the current distro (empty when we don't know one).
pkg_for_module() {
    case "$DISTRO_ID" in
        arch)                 printf '%s\n' "${PKG_ARCH[$1]:-}" ;;
        fedora)               printf '%s\n' "${PKG_FEDORA[$1]:-}" ;;
        debian|ubuntu|pikaos) printf '%s\n' "${PKG_DEBIAN[$1]:-}" ;;
        opensuse*)            printf '%s\n' "${PKG_OPENSUSE[$1]:-}" ;;
    esac
}

# ── dependency presence check ────────────────────────────────────────────────
# Detection uses the very same probe meson uses (`pkg-config --exists MODULE`),
# so a module reported present here is a module meson will resolve. Anything
# weaker reports "all present" on a machine that cannot configure — which is how
# a stale build dir ends up getting blamed for a missing library.
MISSING_TOOLS=()
MISSING_REQ=()
MISSING_OPT=()
detect_missing_deps() {
    MISSING_TOOLS=(); MISSING_REQ=(); MISSING_OPT=()

    command -v meson >/dev/null 2>&1            || MISSING_TOOLS+=(meson)
    command -v ninja >/dev/null 2>&1            || MISSING_TOOLS+=(ninja)
    command -v c++ >/dev/null 2>&1              || MISSING_TOOLS+=(C++-compiler)
    command -v git >/dev/null 2>&1              || MISSING_TOOLS+=(git)
    if ! command -v pkg-config >/dev/null 2>&1 && ! command -v pkgconf >/dev/null 2>&1; then
        MISSING_TOOLS+=(pkg-config)
    fi

    # With pkg-config itself absent every module probe fails, which is correct:
    # the install below then pulls in pkg-config together with the libraries.
    local m
    for m in "${REQ_MODULES[@]}"; do
        pkg-config --exists "$m" 2>/dev/null || MISSING_REQ+=("$m")
    done
    for m in "${OPT_MODULES[@]}"; do
        pkg-config --exists "$m" 2>/dev/null || MISSING_OPT+=("$m")
    done

    # CUDA and HIP are optional: their toolchains only decide whether the
    # corresponding backend is built at all, so neither is a hard build
    # dependency and neither is ever installed by this script.
}

install_deps() {
    local pkgs=() m p q
    if [[ ${#MISSING_TOOLS[@]} -gt 0 ]]; then
        # shellcheck disable=SC2206  # deliberate splitting: $TOOL_PKGS is a list
        pkgs+=($TOOL_PKGS)
    fi
    for m in "${MISSING_REQ[@]}" "${MISSING_OPT[@]}"; do
        p="$(pkg_for_module "$m")"
        if [[ -n "$p" ]]; then
            pkgs+=("$p")
        else
            warn "no package known for module '$m' on $DISTRO_ID — install it manually"
        fi
    done

    # Dedupe while preserving order: Qt6Widgets and Qt6Network both come from
    # the one qt6-base package, and re-installing is wasteful on pacman.
    local -A seen=()
    local -a want=()
    if [[ ${#pkgs[@]} -gt 0 ]]; then
        for q in "${pkgs[@]}"; do
            [[ -n "$q" && -z "${seen[$q]:-}" ]] || continue
            seen[$q]=1
            want+=("$q")
        done
    fi

    if [[ ${#want[@]} -eq 0 ]]; then
        ok "Nothing to install."
        return 0
    fi

    info "Installing build dependencies via ${PKG[0]}:"
    printf '  %s\n' "${want[@]}"
    as_root "${PKG[@]}" "${want[@]}"
    ok "Dependencies installed."

    # Re-probe: the install only helps if it actually fixed what was missing,
    # and meson fails immediately afterwards if it did not.
    detect_missing_deps
    if [[ ${#MISSING_REQ[@]} -gt 0 ]]; then
        warn "still missing after install: ${MISSING_REQ[*]}"
        die "cannot continue: meson requires those modules to configure"
    fi
    if [[ ${#MISSING_OPT[@]} -gt 0 ]]; then
        warn "optional modules unavailable (continuing with reduced features): ${MISSING_OPT[*]}"
    fi
}

# ── backend auto-detection ────────────────────────────────────────────────────
BACKEND_OPTS=()
detect_backends() {
    BACKEND_OPTS=()
    if command -v nvcc >/dev/null 2>&1; then
        local v; v="$(nvcc --version | awk '/release/ {print $6}')"
        info "Found nvcc ${v:-} -> enabling CUDA backend"
        BACKEND_OPTS+=("-Dbackend-cuda=enabled")
    else
        info "nvcc not found -> CUDA backend disabled (CPU fallback used)"
        BACKEND_OPTS+=("-Dbackend-cuda=disabled")
    fi
    # HIP is never installed by this script — only compiled in when hipcc is
    # already on the machine. Arch keeps ROCm under /opt/rocm (not on PATH),
    # so spot that layout too and pass its root to Meson via -Dhip-root.
    local hipcc_bin=""
    if command -v hipcc >/dev/null 2>&1; then
        hipcc_bin="$(command -v hipcc)"
    elif [[ -x "${ROCM_PATH:-/opt/rocm}/bin/hipcc" ]]; then
        hipcc_bin="${ROCM_PATH:-/opt/rocm}/bin/hipcc"
        BACKEND_OPTS+=("-Dhip-root=${ROCM_PATH:-/opt/rocm}")
    fi
    if [[ -n "$hipcc_bin" ]]; then
        info "Found hipcc ($hipcc_bin) -> enabling HIP backend"
        BACKEND_OPTS+=("-Dbackend-hip=enabled")
    else
        info "hipcc not found -> HIP backend disabled (CPU fallback used)"
        BACKEND_OPTS+=("-Dbackend-hip=disabled")
    fi
}

# ── build ─────────────────────────────────────────────────────────────────────
BUILD_DIR="build"

# Absolute programs recorded in the build dir's cached compiler data that no
# longer exist. Meson resolves the compiler once and stores the resolved argv,
# so a build dir configured while a wrapper (ccache, sccache, distcc) was
# installed keeps that wrapper's absolute path forever. Uninstall the wrapper
# and `meson setup --reconfigure` happily reuses the dead path: configure
# succeeds, then every single compile dies with exit 127 and
# "/usr/sbin/ccache: No such file or directory" — a wall of errors that looks
# like a broken toolchain but is really a poisoned build directory.
dead_compiler_paths() {
    local info="$BUILD_DIR/meson-info/intro-compilers.json"
    [[ -f "$info" ]] || return 0
    # exelist/linker_exelist are the only fields holding absolute paths; bare
    # names like "c++" are resolved against PATH at compile time and are fine.
    local p
    while IFS= read -r p; do
        [[ -n "$p" && ! -x "$p" ]] && printf '%s\n' "$p"
    done < <(grep -A6 -E '"(exe|compiler|linker)_?list"' "$info" 2>/dev/null \
             | grep -oE '"/[^"]+"' | tr -d '"' | sort -u)
    return 0
}

# Reconfigure from scratch when the cached compiler is unusable. A fresh setup
# re-probes the toolchain, so this both fixes the dead-wrapper case and picks up
# a compiler that was upgraded underneath the build dir.
heal_build_dir_if_stale() {
    local dead p
    dead="$(dead_compiler_paths)"
    [[ -n "$dead" ]] || return 0
    warn "Build dir was configured with a compiler wrapper that is gone:"
    while IFS= read -r p; do
        [[ -n "$p" ]] && printf '  %s\n' "$p"
    done <<<"$dead"
    warn "Reconfiguring $BUILD_DIR from scratch (incremental state is lost)."
    rm -rf "$BUILD_DIR"
}

build() {
    detect_backends
    heal_build_dir_if_stale
    info "Building (buildtype=$BUILDTYPE)..."
    if [[ -f "$BUILD_DIR/build.ninja" ]]; then
        meson setup --reconfigure "$BUILD_DIR" -Dbuildtype="$BUILDTYPE" "${BACKEND_OPTS[@]}"
    else
        meson setup "$BUILD_DIR" --prefix=/usr -Dbuildtype="$BUILDTYPE" "${BACKEND_OPTS[@]}"
    fi
    ninja -C "$BUILD_DIR"
    ok "Build finished."
}

# ── interactive: release vs debug ─────────────────────────────────────────────
# One question, answered with 1 or 2 (Enter picks the default). The names sound
# technical; the explanation is not, because the trade-off matters more than
# the label.
choose_buildtype() {
    # A --buildtype= flag on the command line means the caller already decided;
    # don't second-guess them with a prompt.
    if [[ -n "${BUILDTYPE_OVERRIDE:-}" ]]; then
        return 0
    fi
    # Nothing to ask if this script isn't talking to a terminal (piped, ssh'd,
    # scripted) — fall back to the default instead of blocking on a read.
    [[ -t 0 ]] || return 0

    echo
    echo "How would you like to build?"
    echo
    echo "  ${C_BOLD}1${C_RESET}  Release (recommended)"
    echo "       Turns on full compiler optimisation. The app runs fast and the"
    echo "       binary is ready to hand to anyone. You lose some troubleshooting"
    echo "       hints and can't easily attach a debugger mid-run."
    echo
    echo "  ${C_BOLD}2${C_RESET}  Debug"
    echo "       Keeps debug symbols and extra runtime checks so you can step"
    echo "       through the code when something breaks. Slower — the Affinity"
    echo "       File Format import is about 7x slower in this mode. Still much"
    echo "       faster than a pure -O0 build, which you can get with"
    echo "       ./build.sh --buildtype=debug"
    echo
    while true; do
        printf "Enter 1 or 2 (default 1): "
        read -r choice || break
        case "${choice:-1}" in
            1) BUILDTYPE=release; break ;;
            2) BUILDTYPE=debugoptimized; break ;;
            *) echo "Please type 1 or 2." ;;
        esac
    done
    echo
}

# ── optional: install system-wide at the end ──────────────────────────────────
# The build is already done at this point. Asking here, after you have a
# working binary, means a no is a legitimate answer — you may just want to run
# it from the build directory.
ask_install() {
    [[ -f "$BUILD_DIR/build.ninja" ]] || return 0
    # Non-interactive runs get the default (no install) without blocking.
    if [[ -t 0 ]]; then
        echo
        echo "The app is built at ${BUILD_DIR}/src/app/painter."
        echo
        printf "Install it system-wide now? It goes to /usr, so it lands on your"
        printf " PATH and shows up in your application launcher. [y/N] "
        read -r go || true
        case "${go:-}" in
            y|Y|yes|YES|Yes)
                info "Installing to /usr (you'll be asked for your password)..."
                as_root meson install -C "$BUILD_DIR" --no-rebuild
                ok "Installed. Run it with: painter"
                return 0
                ;;
        esac
    fi
    info "Not installed. Do it later with:  sudo just install"
}

# ── main ──────────────────────────────────────────────────────────────────────
detect_distro
detect_gpu
choose_buildtype

# Decide whether dependency installation is needed before printing the banner,
# so the summary reflects the actual state. --no-deps stays as a hard override.
if [[ $NO_DEPS -eq 1 ]]; then
    INSTALL_DEPS=0
else
    detect_missing_deps
    if [[ ${#MISSING_TOOLS[@]} -eq 0 && ${#MISSING_REQ[@]} -eq 0 && ${#MISSING_OPT[@]} -eq 0 ]]; then
        INSTALL_DEPS=0
    else
        INSTALL_DEPS=1
    fi
fi

echo
echo "${C_BOLD}Pittore Studio${C_RESET} -- bootstrap"
echo "  distro:        ${DISTRO_ID}"
echo "  gpu:           ${GPU_NAMES[*]:-not probed (lspci missing)}"
echo "  build type:    ${BUILDTYPE}"
if [[ $NO_DEPS -eq 1 ]]; then
    echo "  install deps:  no (--no-deps)"
elif [[ $INSTALL_DEPS -eq 1 ]]; then
    echo "  install deps:  yes"
    # if/fi, not `[[ ]] && echo`: under `set -e` a false test as the last
    # command in the script would exit.
    if [[ ${#MISSING_TOOLS[@]} -gt 0 ]]; then
        echo "    tools:        ${MISSING_TOOLS[*]}"
    fi
    if [[ ${#MISSING_REQ[@]} -gt 0 ]]; then
        echo "    required:     ${MISSING_REQ[*]}"
    fi
    if [[ ${#MISSING_OPT[@]} -gt 0 ]]; then
        echo "    optional:     ${MISSING_OPT[*]}"
    fi
else
    echo "  install deps:  no (all present)"
fi
echo

gpu_policy

if [[ $INSTALL_DEPS -eq 1 ]]; then
    install_deps
fi

build

# Show backend summary
echo
info "Backend summary:"
if [[ -f "$BUILD_DIR/build.ninja" ]]; then
    grep -E 'backend-(cuda|hip)' "$BUILD_DIR/build.ninja" | head -4 | while read -r line; do
        printf "  %s\n" "$line"
    done
fi

# Show the test-all binary location
if [[ -f "$BUILD_DIR/tests/test-all" ]]; then
    ok "Single test binary: $BUILD_DIR/tests/test-all"
    info "Run with:  ./$BUILD_DIR/tests/test-all"
    info "Ship with:  scp ./$BUILD_DIR/tests/test-all user@friend:~/"
fi

info "Done. Install system-wide later with:  sudo just install"

ask_install

ok "All done."
