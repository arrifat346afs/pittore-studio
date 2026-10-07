// Liquify displacement mesh: identity, displacement, falloff, relax, subgrid
// extraction, bounds clamping and the transparent-outside contract.
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "engine/compute/factory.h"
#include "engine/compute/warp.h"
#include "engine/core/pixel.h"
#include "test_util.h"

using pittore::RGBAf;
using namespace pittore::compute;

namespace {

// A deterministic opaque test image: red ramps with x, green with y.
std::vector<RGBAf> makeImage(std::uint32_t w, std::uint32_t h) {
    std::vector<RGBAf> img(static_cast<std::size_t>(w) * h);
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x)
            img[static_cast<std::size_t>(y) * w + x] = RGBAf{
                static_cast<float>(x) / w, static_cast<float>(y) / h, 0.25f, 1.0f};
    return img;
}

}  // namespace

void test_warp() {
    // 1. Grid geometry + identity.
    {
        const WarpMesh m = make_warp_mesh(64, 40);
        CHECK_EQ(m.left, 0);
        CHECK_EQ(m.top, 0);
        CHECK_EQ(m.right, 64);
        CHECK_EQ(m.bottom, 40);
        CHECK_EQ(m.cols, static_cast<std::uint32_t>(std::ceil(64 / kWarpCell)) + 1);
        CHECK_EQ(m.rows, static_cast<std::uint32_t>(std::ceil(40 / kWarpCell)) + 1);
        CHECK_EQ(m.offsets.size(), static_cast<std::size_t>(m.cols) * m.rows * 2);
        CHECK(m.identity());
    }

    // 2. Identity mesh reproduces the source in-region and leaves the rest alone.
    {
        constexpr std::uint32_t w = 48, h = 48;
        const std::vector<RGBAf> src = makeImage(w, h);
        std::vector<RGBAf> dst = src;
        const WarpMesh mesh = make_warp_mesh(w, h);
        const WarpSubgrid grid = warp_subgrid(mesh, 8, 8, 40, 40);
        warp_into_host(dst.data(), src.data(), w, h, grid, 8, 8, 40, 40);
        for (std::uint32_t y = 0; y < h; ++y)
            for (std::uint32_t x = 0; x < w; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * w + x;
                const bool in = x >= 8 && x < 40 && y >= 8 && y < 40;
                if (in) {
                    CHECK_NEAR(dst[i].r, src[i].r, 1e-5f);
                    CHECK_NEAR(dst[i].g, src[i].g, 1e-5f);
                } else {
                    CHECK_NEAR(dst[i].r, src[i].r, 0.0f);
                }
            }
    }

    // 3. A uniform displacement shifts colour: every point fetches from +2 px.
    //    Past the right edge the fetch falls outside the layer and is
    //    transparent, so x = w-2 and x = w-1 go clear.
    {
        constexpr std::uint32_t w = 32, h = 16;
        const std::vector<RGBAf> src = makeImage(w, h);
        std::vector<RGBAf> dst = src;
        WarpMesh mesh = make_warp_mesh(w, h);
        for (std::size_t k = 0; k < mesh.offsets.size(); k += 2) {
            mesh.offsets[k] = 2.0f;      // dx = +2 (fetch to the right)
            mesh.offsets[k + 1] = 0.0f;
        }
        const WarpSubgrid grid = warp_subgrid(mesh, 0, 0, w, h);
        warp_into_host(dst.data(), src.data(), w, h, grid, 0, 0, w, h);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x + 2 < w; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * w + x;
                const std::size_t s = static_cast<std::size_t>(y) * w + x + 2;
                CHECK_NEAR(dst[i].r, src[s].r, 1e-5f);
                CHECK_NEAR(dst[i].g, src[s].g, 1e-5f);
            }
            CHECK_NEAR(dst[static_cast<std::size_t>(y) * w + (w - 2)].a, 0.0f, 1e-6f);
        }
    }

    // 4. Brush falloff: the centre vertex moves fully, the rim not at all, and
    //    vertices outside the radius are untouched.
    {
        WarpMesh mesh = make_warp_mesh(128, 128);
        // Forward-warp style push: drag each in-range vertex toward the centre
        // by the smoothstep weight.
        warp_mesh_for_each_near(mesh, 64.0f, 64.0f, 32.0f,
                                [](float& dx, float& dy, float wt, float rx,
                                   float ry) {
                                    dx -= rx * wt;
                                    dy -= ry * wt;
                                });
        CHECK(!mesh.identity());
        const auto at = [&mesh](int c, int r, int k) {
            return mesh.offsets[static_cast<std::size_t>(r) * mesh.cols * 2 +
                                static_cast<std::size_t>(c) * 2 + k];
        };
        // Vertex just right of the brush centre (68,64): c = 17, r = 16 moves
        // toward the centre. At the exact centre there is no direction to drag,
        // so (16,16) stays put by construction.
        CHECK(std::abs(at(17, 16, 0)) > 1e-3f);
        // Vertex at x = 64 + 32 = 96, y = 64: c = 24, r = 16 is at the rim
        // (d == radius), so the brush skips it entirely.
        CHECK_NEAR(at(24, 16, 0), 0.0f, 1e-6f);
        CHECK_NEAR(at(24, 16, 1), 0.0f, 1e-6f);
        // A vertex far outside the radius stays zero.
        CHECK_NEAR(at(0, 0, 0), 0.0f, 1e-6f);
    }

    // 5. Reconstruct relaxes an offset toward zero, exactly to zero at the
    //    brush centre.
    {
        WarpMesh mesh = make_warp_mesh(64, 64);
        for (std::size_t k = 0; k < mesh.offsets.size(); k += 2) {
            mesh.offsets[k] = 5.0f;
            mesh.offsets[k + 1] = -3.0f;
        }
        warp_mesh_relax(mesh, 32.0f, 32.0f, 16.0f, 1.0f);
        const auto at = [&mesh](int c, int r, int k) {
            return mesh.offsets[static_cast<std::size_t>(r) * mesh.cols * 2 +
                                static_cast<std::size_t>(c) * 2 + k];
        };
        // Nearest vertex to (32,32) is c = r = 8; a full relax zeroes it.
        CHECK_NEAR(at(8, 8, 0), 0.0f, 1e-6f);
        CHECK_NEAR(at(8, 8, 1), 0.0f, 1e-6f);
        // Far outside the brush: untouched.
        CHECK_NEAR(at(0, 0, 0), 5.0f, 1e-6f);
    }

    // 6. Subgrid sampling is bit-identical to full-mesh sampling over the
    //    region: this is what lets the CPU and GPU backends share one code path.
    {
        constexpr std::uint32_t w = 96, h = 80;
        WarpMesh mesh = make_warp_mesh(w, h);
        warp_mesh_for_each_near(mesh, 40.0f, 36.0f, 28.0f,
                                [](float& dx, float& dy, float wt, float rx,
                                   float ry) {
                                    dx -= rx * wt * 0.9f;
                                    dy -= ry * wt * 0.9f;
                                });
        const int x0 = 10, y0 = 6, x1 = 70, y1 = 60;
        const WarpSubgrid grid = warp_subgrid(mesh, x0, y0, x1, y1);
        CHECK(!grid.empty());
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                float fdx = 0, fdy = 0, gdx = 0, gdy = 0;
                warp_mesh_sample(mesh, x + 0.5f, y + 0.5f, fdx, fdy);
                warp_subgrid_sample(grid, x + 0.5f, y + 0.5f, gdx, gdy);
                CHECK_NEAR(gdx, fdx, 0.0f);
                CHECK_NEAR(gdy, fdy, 0.0f);
            }

        // And the subgrid render equals the full-mesh render.
        std::vector<RGBAf> a(static_cast<std::size_t>(w) * h);
        std::vector<RGBAf> b = a;
        const std::vector<RGBAf> src = makeImage(w, h);
        warp_into_host(a.data(), src.data(), w, h, mesh, x0, y0, x1, y1);
        warp_into_host(b.data(), src.data(), w, h, grid, x0, y0, x1, y1);
        for (std::size_t i = 0; i < a.size(); ++i) {
            CHECK_NEAR(b[i].r, a[i].r, 0.0f);
            CHECK_NEAR(b[i].g, a[i].g, 0.0f);
            CHECK_NEAR(b[i].a, a[i].a, 0.0f);
        }
    }

    // 7. Regression: a dab rect never runs past the layer edge. The mesh's last
    //    vertex sits at ceil(w/cell)*cell, past w, so a naive bound would let
    //    the render write x = w (a heap overflow).
    {
        const WarpMesh mesh = make_warp_mesh(100, 100);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        warp_dab_rect(mesh, 50.0f, 50.0f, 1000.0f, x0, y0, x1, y1);
        CHECK(x0 >= 0 && y0 >= 0);
        CHECK(x1 <= 100 && y1 <= 100);
    }

    // 8. Degenerate/empty mesh is a no-op.
    {
        std::vector<RGBAf> img = makeImage(16, 16);
        const std::vector<RGBAf> before = img;
        const WarpMesh empty;  // cols/rows == 0
        warp_into_host(img.data(), before.data(), 16, 16, empty, 0, 0, 16, 16);
        for (std::size_t i = 0; i < img.size(); ++i)
            CHECK_NEAR(img[i].r, before[i].r, 0.0f);
    }

    // 9. Backend path agrees with the host helper (CPU parity), and uploads only
    //    the region: pixels outside the rect keep their value.
    {
        auto backend = pittore::compute::make_default_backend();
        constexpr std::uint32_t w = 64, h = 64;
        const std::vector<RGBAf> src = makeImage(w, h);
        WarpMesh mesh = make_warp_mesh(w, h);
        warp_mesh_for_each_near(mesh, 30.0f, 30.0f, 20.0f,
                                [](float& dx, float& dy, float wt, float rx,
                                   float ry) {
                                    dx -= rx * wt;
                                    dy -= ry * wt;
                                });
        const int x0 = 8, y0 = 8, x1 = 56, y1 = 56;
        const WarpSubgrid grid = warp_subgrid(mesh, x0, y0, x1, y1);

        std::vector<RGBAf> expected = src;
        warp_into_host(expected.data(), src.data(), w, h, grid, x0, y0, x1, y1);

        auto sb = backend->make_buffer(src.size() * sizeof(RGBAf));
        auto db = backend->make_buffer(src.size() * sizeof(RGBAf));
        std::memcpy(sb->host(), src.data(), sb->size());
        std::memcpy(db->host(), src.data(), db->size());
        sb->upload();
        db->upload();
        backend->warp(*db, *sb, w, h, grid, x0, y0, x1, y1);
        const RGBAf* out = static_cast<const RGBAf*>(db->host());
        for (std::uint32_t y = 0; y < h; ++y)
            for (std::uint32_t x = 0; x < w; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * w + x;
                const bool in = x >= x0 && x < x1 && y >= y0 && y < y1;
                if (in) {
                    CHECK_NEAR(out[i].r, expected[i].r, 1e-6f);
                    CHECK_NEAR(out[i].a, expected[i].a, 1e-6f);
                } else {
                    CHECK_NEAR(out[i].r, src[i].r, 0.0f);  // untouched
                }
            }
    }
    {
        WarpMesh mesh = make_warp_mesh(64, 64);
        for (std::size_t k = 0; k < mesh.offsets.size(); k += 2) {
            mesh.offsets[k] = 5.0f;
            mesh.offsets[k + 1] = -3.0f;
        }
        warp_mesh_smooth(mesh, 32.0f, 32.0f, 16.0f, 1.0f);
        const auto at = [&mesh](int c, int r, int k) {
            return mesh.offsets[static_cast<std::size_t>(r) * mesh.cols * 2 +
                                static_cast<std::size_t>(c) * 2 + k];
        };
        CHECK_NEAR(at(8, 8, 0), 5.0f, 1e-5f);
        CHECK_NEAR(at(8, 8, 1), -3.0f, 1e-5f);
        CHECK_NEAR(at(0, 0, 0), 5.0f, 1e-5f);
    }
    {
        WarpMesh mesh = make_warp_mesh(64, 64);
        mesh.offsets[static_cast<std::size_t>(8) * mesh.cols * 2 +
                     static_cast<std::size_t>(8) * 2] = 10.0f;
        warp_mesh_smooth(mesh, 32.0f, 32.0f, 20.0f, 1.0f);
        const auto at = [&mesh](int c, int r, int k) {
            return mesh.offsets[static_cast<std::size_t>(r) * mesh.cols * 2 +
                                static_cast<std::size_t>(c) * 2 + k];
        };
        CHECK(at(8, 8, 0) < 10.0f);
        CHECK(at(8, 8, 0) > 0.0f);
        CHECK(at(9, 8, 0) > 0.0f);
        CHECK_NEAR(at(0, 0, 0), 0.0f, 1e-6f);
    }
    {
        WarpMesh mesh = make_warp_mesh(64, 64);
        for (std::size_t k = 0; k < mesh.offsets.size(); k += 2) {
            mesh.offsets[k] = 4.0f;
            mesh.offsets[k + 1] = 2.0f;
        }
        for (int r = 10; r <= 14; ++r) {
            for (int c = 10; c <= 14; ++c) {
                mesh.offsets[(std::size_t(r) * mesh.cols + std::size_t(c)) * 2] = 0.0f;
                mesh.offsets[(std::size_t(r) * mesh.cols + std::size_t(c)) * 2 + 1] = 0.0f;
            }
        }
        warp_mesh_clone(mesh, 16.0f, 16.0f, 48.0f, 48.0f, 12.0f, 1.0f);
        const auto at = [&mesh](int c, int r, int k) {
            return mesh.offsets[static_cast<std::size_t>(r) * mesh.cols * 2 +
                                static_cast<std::size_t>(c) * 2 + k];
        };
        CHECK_NEAR(at(12, 12, 0), 4.0f, 1e-4f);
        CHECK_NEAR(at(12, 12, 1), 2.0f, 1e-4f);
        WarpMesh flat = make_warp_mesh(64, 64);
        warp_mesh_clone(flat, 16.0f, 16.0f, 48.0f, 48.0f, 12.0f, 1.0f);
        CHECK(flat.identity());
    }
    {
        WarpMesh a = make_warp_mesh(48, 48);
        WarpMesh b = make_warp_mesh(48, 48);
        warp_mesh_for_each_near(a, 24.0f, 24.0f, 16.0f,
                                [](float& dx, float& dy, float wt, float, float) {
                                    dx += wt;
                                    dy -= wt;
                                });
        warp_mesh_for_each_near_shaped(b, 24.0f, 24.0f, 16.0f, 1.0f,
                                       [](float& dx, float& dy, float wt, float, float) {
                                           dx += wt;
                                           dy -= wt;
                                       });
        CHECK(a.offsets.size() == b.offsets.size());
        for (std::size_t k = 0; k < a.offsets.size(); ++k)
            CHECK_NEAR(a.offsets[k], b.offsets[k], 0.0f);
        WarpMesh hard = make_warp_mesh(48, 48);
        WarpMesh soft = make_warp_mesh(48, 48);
        warp_mesh_for_each_near_shaped(hard, 24.0f, 24.0f, 16.0f, 0.2f,
                                       [](float& dx, float&, float wt, float, float) {
                                           dx += wt;
                                       });
        warp_mesh_for_each_near_shaped(soft, 24.0f, 24.0f, 16.0f, 3.0f,
                                       [](float& dx, float&, float wt, float, float) {
                                           dx += wt;
                                       });
        const auto hat = [&hard](int c, int r) {
            return hard.offsets[static_cast<std::size_t>(r) * hard.cols * 2 +
                                static_cast<std::size_t>(c) * 2];
        };
        const auto sat = [&soft](int c, int r) {
            return soft.offsets[static_cast<std::size_t>(r) * soft.cols * 2 +
                                static_cast<std::size_t>(c) * 2];
        };
        CHECK(hat(9, 6) > sat(9, 6));
        CHECK(hat(6, 6) >= sat(6, 6));
    }
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "pittore_warp_test";
        fs::create_directories(dir);
        const std::string good = (dir / "mesh.iflq").string();
        const std::string missing = (dir / "nope.iflq").string();
        const std::string corrupt = (dir / "bad.iflq").string();
        WarpMesh mesh = make_warp_mesh(40, 28);
        mesh.offsets[0] = 1.5f;
        mesh.offsets[1] = -0.5f;
        mesh.offsets[mesh.offsets.size() - 1] = 2.25f;
        CHECK(warp_mesh_write(good, mesh));
        WarpMesh back;
        CHECK(warp_mesh_read(good, back));
        CHECK_EQ(back.left, mesh.left);
        CHECK_EQ(back.top, mesh.top);
        CHECK_EQ(back.right, mesh.right);
        CHECK_EQ(back.bottom, mesh.bottom);
        CHECK_EQ(back.cols, mesh.cols);
        CHECK_EQ(back.rows, mesh.rows);
        CHECK(back.offsets.size() == mesh.offsets.size());
        for (std::size_t k = 0; k < mesh.offsets.size(); ++k)
            CHECK_NEAR(back.offsets[k], mesh.offsets[k], 0.0f);
        CHECK(!warp_mesh_read(missing, back));
        {
            std::ofstream f(corrupt, std::ios::binary | std::ios::trunc);
            f << "garbage-bytes-here";
        }
        CHECK(!warp_mesh_read(corrupt, back));
        fs::remove_all(dir);
    }

}

#ifndef PITTORE_TEST_NO_MAIN
TEST_MAIN_CALL(test_warp)
#endif
