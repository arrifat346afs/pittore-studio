#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/compute/backend.h"
#include "engine/compute/brushes/erase/erase.h"
#include "engine/compute/brushes/history/history_dab.h"
#include "engine/compute/brushes/stamp/stamp.h"
#include "engine/compute/factory.h"
#include "engine/core/image.h"
#include "engine/filter/core/filter_detail.h"
#include "engine/filter/filters.h"
#include "test_util.h"

using pittore::Image;
using pittore::RGBAf;
using pittore::compute::BackendType;
using pittore::compute::Buffer;
using pittore::compute::ComputeBackend;

namespace {

Image makeProbe() {
    Image img(48, 36);
    for (std::uint32_t y = 0; y < 36; ++y) {
        for (std::uint32_t x = 0; x < 48; ++x) {
            img.at(x, y) = RGBAf{static_cast<float>(x) / 47.0f,
                                 static_cast<float>(y) / 35.0f,
                                 static_cast<float>((x * 3 + y * 5) % 11) / 10.0f,
                                 static_cast<float>(0.3 + 0.7 * ((x + y) % 2))};
        }
    }
    return img;
}

bool nearImages(const Image& a, const Image& b, float eps) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    for (std::size_t i = 0; i < a.pixel_count(); ++i) {
        for (int c = 0; c < 4; ++c) {
            if (std::fabs(a.data()[i][c] - b.data()[i][c]) > eps) return false;
        }
    }
    return true;
}

struct Bufs {
    std::unique_ptr<Buffer> src;
    std::unique_ptr<Buffer> dst;
};

Bufs makeBufs(ComputeBackend& be, const Image& img) {
    const std::size_t bytes =
        static_cast<std::size_t>(img.width()) * img.height() * sizeof(RGBAf);
    Bufs b{be.make_buffer(bytes), be.make_buffer(bytes)};
    std::memcpy(b.src->host(), img.data(), bytes);
    b.src->upload();
    return b;
}

void pullInto(Bufs& b, Image& img) {
    b.dst->download();
    std::memcpy(img.data(), b.dst->host(),
                static_cast<std::size_t>(img.width()) * img.height() *
                    sizeof(RGBAf));
}

void testBackend(ComputeBackend& be) {
    {
        Image probe = makeProbe();
        for (int r : {1, 3, 7, 16}) {
            Image ref = probe.clone();
            Image got = probe.clone();
            {
                Image tmp(probe.width(), probe.height());
                pittore::filter::detail::boxBlurInto(ref, tmp, r);
                std::memcpy(ref.data(), tmp.data(),
                            ref.pixel_count() * sizeof(RGBAf));
            }
            Bufs b = makeBufs(be, probe);
            be.box_blur(*b.src, *b.dst, probe.width(), probe.height(), r);
            pullInto(b, got);
            CHECK(nearImages(ref, got, 2e-4f));
        }
    }
    {
        Image probe = makeProbe();
        for (int r : {1, 2, 5}) {
            Image ref = probe.clone();
            pittore::filter::detail::medianR(ref, r);
            Image got = probe.clone();
            Bufs b = makeBufs(be, probe);
            be.median_radius(*b.src, *b.dst, probe.width(), probe.height(), r);
            pullInto(b, got);
            CHECK(nearImages(ref, got, 1e-5f));
        }
    }
    {
        Image probe = makeProbe();
        Image ref = probe.clone();
        pittore::filter::detail::unsharpF(ref, 0.8, 2, 0.05);
        Image got = probe.clone();
        Bufs b = makeBufs(be, probe);
        be.unsharp_box(*b.src, *b.dst, probe.width(), probe.height(), 0.8f, 2,
                       0.05f);
        pullInto(b, got);
        CHECK(nearImages(ref, got, 2e-4f));
    }
    {
        Image probe = makeProbe();
        Image ref = probe.clone();
        pittore::filter::applyFilter(ref, "motion_blur", {30.0, 12.0});
        Image got = probe.clone();
        Bufs b = makeBufs(be, probe);
        be.motion_blur(*b.src, *b.dst, probe.width(), probe.height(), 30.0f,
                       12);
        pullInto(b, got);
        CHECK(nearImages(ref, got, 2e-4f));
    }
    {
        Image probe = makeProbe();
        Image ref = probe.clone();
        pittore::filter::applyFilter(ref, "lens_blur", {8.0, 3.0, 30.0, 200.0, 20.0});
        Image got = probe.clone();
        Bufs b = makeBufs(be, probe);
        be.lens_blur(*b.src, *b.dst, probe.width(), probe.height(), 8, 15, 0.3f,
                     200.0f / 255.0f, 0.4f);
        pullInto(b, got);
        CHECK(nearImages(ref, got, 2e-3f));
    }
    {
        // bg_erase (Background Eraser, discontiguous): backend kernel vs
        // host core, bit-for-bit at 1e-5 incl. protect + tol-0 gate.
        // Ragged sizes exercise the bbox clamp.
        for (const auto [W, H] : {std::pair{48u, 36u}, std::pair{51u, 29u}}) {
            Image probe(W, H);
            for (std::uint32_t y = 0; y < H; ++y)
                for (std::uint32_t x = 0; x < W; ++x)
                    probe.at(x, y) = RGBAf{
                        static_cast<float>(x) / float(W),
                        static_cast<float>(y) / float(H),
                        static_cast<float>((x * 3 + y * 5) % 11) / 10.0f,
                        1.0f};
            for (int cfg = 0; cfg < 3; ++cfg) {
                const RGBAf sample{0.5f, 0.5f, 0.5f, 1.0f};
                const RGBAf fg{1.0f, 0.0f, 0.0f, 1.0f};
                const float tol = cfg == 2 ? 0.0f : 0.25f;
                const bool protect = cfg == 1;
                Image ref = probe.clone();
                int rb[4] = {0, 0, 0, 0};
                pittore::compute::background_erase_dab_host(
                    ref.data(), W, H, float(W) / 2.0f, float(H) / 2.0f,
                    10.0f, 0.8f, 1.0f, sample, tol, protect, fg, rb);
                Bufs b = makeBufs(be, probe);
                int gb[4] = {0, 0, 0, 0};
                be.bg_erase(*b.src, W, H, float(W) / 2.0f, float(H) / 2.0f,
                            10.0f, 0.8f, 1.0f, sample, tol, protect, fg,
                            gb);
                // Device kernels write device memory; pull it back.
                b.src->download();
                Image got(W, H);
                std::memcpy(got.data(), b.src->host(),
                            static_cast<std::size_t>(W) * H * sizeof(RGBAf));
                CHECK(nearImages(ref, got, 1e-5f));
                CHECK(rb[0] == gb[0] && rb[1] == gb[1] && rb[2] == gb[2] &&
                      rb[3] == gb[3]);
            }
        }
    }
    {
        // pattern_stamp: backend kernel vs host core at 1e-5, two tiles,
        // two origins (aligned + travelled).
        for (const auto [W, H] : {std::pair{48u, 36u}, std::pair{51u, 29u}}) {
            Image probe(W, H);
            for (std::uint32_t y = 0; y < H; ++y)
                for (std::uint32_t x = 0; x < W; ++x)
                    probe.at(x, y) = RGBAf{1.0f, 1.0f, 1.0f, 1.0f};
            for (int pid = 0; pid < 2; ++pid) {
                pittore::compute::PatternTile tile;
                tile.id = pid;
                tile.px = pittore::compute::make_pattern_tile(pid).px;
                for (float ox : {0.0f, 5.0f}) {
                    Image ref = probe.clone();
                    int rb[4] = {0, 0, 0, 0};
                    pittore::compute::pattern_stamp_dab_host(
                        ref.data(), W, H, float(W) / 2.0f, float(H) / 2.0f,
                        10.0f, 0.8f, 1.0f, tile, ox, 0.0f, rb);
                    Bufs b = makeBufs(be, probe);
                    auto tileBuf = be.make_buffer(64u * 64u * sizeof(RGBAf));
                    std::memcpy(tileBuf->host(), tile.px.data(),
                                64u * 64u * sizeof(RGBAf));
                    tileBuf->upload();
                    int gb[4] = {0, 0, 0, 0};
                    be.pattern_stamp(*b.src, W, H, float(W) / 2.0f,
                                     float(H) / 2.0f, 10.0f, 0.8f, 1.0f,
                                     *tileBuf, ox, 0.0f, gb);
                    b.src->download();
                    Image got(W, H);
                    std::memcpy(got.data(), b.src->host(),
                                static_cast<std::size_t>(W) * H *
                                    sizeof(RGBAf));
                    CHECK(nearImages(ref, got, 1e-5f));
                    CHECK(rb[0] == gb[0] && rb[1] == gb[1] &&
                          rb[2] == gb[2] && rb[3] == gb[3]);
                }
            }
        }
    }
    {
        // history_dab: backend kernel vs host core at 1e-5 (source-over
        // snapshot texels; float round-trip, never bit-exact by design).
        for (const auto [W, H] : {std::pair{48u, 36u}, std::pair{51u, 29u}}) {
            Image dstImg(W, H), srcImg(W, H);
            for (std::uint32_t y = 0; y < H; ++y)
                for (std::uint32_t x = 0; x < W; ++x) {
                    dstImg.at(x, y) = RGBAf{1.0f, 0.0f, 0.0f, 1.0f};
                    srcImg.at(x, y) = RGBAf{
                        static_cast<float>(x) / float(W),
                        static_cast<float>(y) / float(H), 0.5f, 1.0f};
                }
            Image ref = dstImg.clone();
            int rb[4] = {0, 0, 0, 0};
            pittore::compute::history_brush_dab_host(
                ref.data(), srcImg.data(), W, H, float(W) / 2.0f,
                float(H) / 2.0f, 10.0f, 0.8f, 1.0f, rb);
            Bufs b = makeBufs(be, dstImg);
            auto srcBuf = be.make_buffer(static_cast<std::size_t>(W) * H *
                                         sizeof(RGBAf));
            std::memcpy(srcBuf->host(), srcImg.data(),
                        static_cast<std::size_t>(W) * H * sizeof(RGBAf));
            srcBuf->upload();
            int gb[4] = {0, 0, 0, 0};
            be.history_dab(*b.src, W, H, float(W) / 2.0f, float(H) / 2.0f,
                           10.0f, 0.8f, 1.0f, *srcBuf, gb);
            b.src->download();
            Image got(W, H);
            std::memcpy(got.data(), b.src->host(),
                        static_cast<std::size_t>(W) * H * sizeof(RGBAf));
            CHECK(nearImages(ref, got, 1e-5f));
            CHECK(rb[0] == gb[0] && rb[1] == gb[1] && rb[2] == gb[2] &&
                  rb[3] == gb[3]);
        }
    }
    {
        Image probe = makeProbe();
        Image ref = probe.clone();
        pittore::filter::detail::gaussBlur(ref, 4.0);
        Image got = probe.clone();
        Bufs b = makeBufs(be, probe);
        auto tmp = be.make_buffer(static_cast<std::size_t>(probe.width()) *
                                  probe.height() * sizeof(RGBAf));
        auto dst2 = be.make_buffer(static_cast<std::size_t>(probe.width()) *
                                   probe.height() * sizeof(RGBAf));
        std::memcpy(b.src->host(), probe.data(),
                    static_cast<std::size_t>(probe.width()) * probe.height() *
                        sizeof(RGBAf));
        b.src->upload();
        be.box_blur(*b.src, *tmp, probe.width(), probe.height(), 4);
        be.box_blur(*tmp, *dst2, probe.width(), probe.height(), 4);
        be.box_blur(*dst2, *tmp, probe.width(), probe.height(), 4);
        tmp->download();
        std::memcpy(got.data(), tmp->host(),
                    static_cast<std::size_t>(probe.width()) * probe.height() *
                        sizeof(RGBAf));
        CHECK(nearImages(ref, got, 2e-4f));
    }
}

}  // namespace

static void testFilterGpu() {
    {
        auto cpu = pittore::compute::make_backend(BackendType::CPU);
        CHECK(cpu != nullptr);
        if (cpu) testBackend(*cpu);
    }
    {
        auto cuda = pittore::compute::make_backend(BackendType::CUDA);
        if (cuda) testBackend(*cuda);
    }
    {
        auto hip = pittore::compute::make_backend(BackendType::HIP);
        if (hip) testBackend(*hip);
    }
}

#ifndef PITTORE_TEST_NO_MAIN
TEST_MAIN_CALL(testFilterGpu)
#endif
