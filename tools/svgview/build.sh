#!/bin/sh
# Build the svgview audit tool against the prebuilt static libraries.
# Deliberately standalone: does not touch the main meson build.
set -e
cd "$(dirname "$0")"
ROOT=../..
B=$ROOT/build

test -f "$B/src/ui/libpittore-ui.a" || {
    echo "error: prebuilt libraries missing - run 'ninja -C build' once first" >&2
    exit 1
}

QT_CFLAGS=$(pkg-config --cflags Qt6Widgets Qt6Gui Qt6Core)

g++ -std=c++20 -O1 -fPIC -w \
    -I"$ROOT/src" -I"$ROOT/Reference/nanosvg/src" $QT_CFLAGS \
    main.cpp -o svgview \
    "$B/src/ui/libpittore-ui.a" \
    "$B/src/engine/compute/libpittore-compute.a" \
    "$B/src/engine/ai/libpittore-ai.a" \
    "$B/src/engine/io/libpittore-io.a" \
    "$B/src/engine/render/libpittore-render.a" \
    "$B/src/engine/vector/libpittore-vector.a" \
    "$B/src/engine/text/libpittore-text.a" \
    "$B/src/engine/color/libpittore-color.a" \
    -Wl,-rpath,/opt/cuda/lib64 -Wl,-rpath,/opt/rocm/lib \
    /opt/cuda/lib64/libcudart.so /opt/rocm/lib/libamdhip64.so \
    /usr/lib/libQt6Widgets.so /usr/lib/libQt6Gui.so /usr/lib/libQt6Core.so \
    /usr/lib/libQt6Network.so /usr/lib/libQt6Svg.so \
    /usr/lib/libonnxruntime.so.1.29.0 /usr/lib/libz.so /usr/lib/libharfbuzz.so \
    /usr/lib/libfreetype.so /usr/lib/libfontconfig.so /usr/lib/liblcms2.so \
    /usr/lib/libwebp.so /usr/lib/libtiff.so /usr/lib/libjpeg.so \
    /usr/lib/libzstd.so /usr/lib/libtomlplusplus.so /usr/lib/libqrencode.so \
    -lm

echo "built: $(pwd)/svgview"
