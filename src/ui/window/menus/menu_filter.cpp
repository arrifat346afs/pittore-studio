#include "ui/main_window.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QPointer>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSet>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#include "engine/ai/bg_remove.h"
#include "engine/compute/factory.h"
#include "engine/compute/adjust.h"
#include "engine/core/log.h"
#include "engine/core/tonal_ops.h"
#include "ui/ai_models.h"
#include "ui/canvas_view.h"
#include "ui/contextual_task_bar.h"
#include "ui/export_dialog.h"
#include "ui/icons.h"
#include "ui/layer_style_dialog.h"
#include "ui/options_bar.h"
#include "ui/panels.h"
#include "ui/keymap.h"
#include "ui/spotlight.h"
#include "ui/filter_dialog.h"
#include "engine/filter/filters.h"
#include "ui/preferences_dialog.h"
#include "ui/project_manager.h"
#include "ui/selection_mask.h"
#include "ui/theme.h"
#include "ui/tone_dialogs.h"
#include "ui/tools_panel.h"
#include "ui/workspace.h"
#include "ui/window/shared/window_helpers.h"

namespace pittore::ui {


void MainWindow::buildFilterMenu() {
    QMenu* filter = menuBar()->addMenu(tr("F&ilter"));
    makeAction(filter, tr("Last Filter"), QStringLiteral("Ctrl+F"),
               [this] {
                   if (lastFilterId_.isEmpty()) {
                       state_->setStatusHint(tr("No filter applied yet."));
                       return;
                   }
                   applyFilterOneShot(lastFilterId_, lastFilterParams_);
               });
    makeAction(filter, tr("Convert for Smart Filters"), {},
               [this] {
                   if (lastFilterId_.isEmpty()) {
                       state_->setStatusHint(
                           tr("Open any filter first, then convert."));
                       return;
                   }
                   state_->convertToLiveFilter(lastFilterId_);
               });
    filter->addSeparator();
    makeAction(filter, tr("Neural Filters…"), QString(),
               [this] { filterGalleryDialog(QStringLiteral("Neural")); });
    QMenu* gallery = filter->addMenu(tr("Filter Gallery"));
    makeAction(gallery, tr("Open Gallery…"), QString(),
               [this] { filterGalleryDialog(QStringLiteral("Gallery")); });
    makeAction(filter, tr("Adaptive Wide Angle…"), QStringLiteral("Ctrl+Alt+Shift+A"),
               [this] { filterDialog(QStringLiteral("adaptive_wide_angle")); });
    makeAction(filter, tr("Camera Raw Filter…"), QStringLiteral("Ctrl+Shift+A"),
               [this] { filterDialog(QStringLiteral("camera_raw")); });
    makeAction(filter, tr("Lens Correction…"), QStringLiteral("Ctrl+Shift+R"),
               [this] { filterDialog(QStringLiteral("lens_correction")); });
    makeAction(filter, tr("Liquify…"), QStringLiteral("Ctrl+Shift+X"),
               [this] { liquifyDialog(); });
    makeAction(filter, tr("Color Grading…"), QString(),
               [this] { colorGradeDialog(); });
    makeAction(filter, tr("Vanishing Point…"), QStringLiteral("Ctrl+Alt+V"));
    filter->addSeparator();
    const QVector<QPair<QString, QStringList>> groups{
        {tr("3D"), {tr("Generate Bump Map"), tr("Generate Normal Map")}},
        {tr("Artistic"),
         {tr("Colored Pencil…"), tr("Cutout…"), tr("Dry Brush…"), tr("Film Grain…"),
          tr("Fresco…"), tr("Neon Glow…"), tr("Paint Daubs…"), tr("Palette Knife…"),
          tr("Plastic Wrap…"), tr("Poster Edges…"), tr("Rough Pastels…"),
          tr("Smudge Stick…"), tr("Sponge…"), tr("Underpainting…"), tr("Watercolor…")}},
        {tr("Blur…"),
         {tr("Average…"), tr("Blur…"), tr("Blur More…"), tr("Box Blur…"), tr("Gaussian Blur…"),
          tr("Lens Blur…"), tr("Motion Blur…"), tr("Radial Blur…"), tr("Shape Blur…"),
          tr("Smart Blur…"), tr("Surface Blur…")}},
        {tr("Blur Gallery"),
         {tr("Field Blur…"), tr("Iris Blur…"), tr("Tilt-Shift…"), tr("Path Blur…"),
          tr("Spin Blur…")}},
        {tr("Brush Strokes"),
         {tr("Accented Edges…"), tr("Angled Strokes…"), tr("Crosshatch…"),
          tr("Dark Strokes…"), tr("Ink Outlines…"), tr("Spatter…"),
          tr("Sprayed Strokes…"), tr("Sumi-e…")}},
        {tr("Distort"),
         {tr("Displace…"), tr("Glass…"), tr("Ocean Ripple…"), tr("Pinch…"),
          tr("Polar Coordinates…"), tr("Ripple…"), tr("Shear…"),
          tr("Spherize…"), tr("Twirl…"), tr("Wave…"), tr("ZigZag…")}},
        {tr("Noise"),
         {tr("Add Noise…"), tr("Despeckle…"), tr("Dust & Scratches…"), tr("Median…"),
          tr("Reduce Noise…")}},
        {tr("Pixelate"),
         {tr("Color Halftone…"), tr("Crystallize…"), tr("Facet…"), tr("Fragment…"),
          tr("Mezzotint…"), tr("Mosaic…"), tr("Pointillize…")}},
        {tr("Render"),
         {tr("Flame…"), tr("Picture Frame…"), tr("Tree…"), tr("Clouds…"), tr("Difference Clouds…"),
          tr("Fibers…"), tr("Lens Flare…"), tr("Lighting Effects…")}},
        {tr("Sharpen…"),
         {tr("Shake Reduction…"), tr("Sharpen…"), tr("Sharpen Edges…"), tr("Sharpen More…"),
          tr("Smart Sharpen…"), tr("Unsharp Mask…")}},
        {tr("Stylize"),
         {tr("Diffuse…"), tr("Diffuse Glow…"), tr("Emboss…"), tr("Extrude…"),
          tr("Find Edges…"), tr("Glowing Edges…"), tr("Oil Paint…"),
          tr("Solarize…"), tr("Tiles…"), tr("Trace Contour…"), tr("Wind…")}},
        {tr("Sketch"),
         {tr("Bas Relief…"), tr("Chalk & Charcoal…"), tr("Charcoal…"), tr("Chrome…"),
          tr("Conte Crayon…"), tr("Graphic Pen…"), tr("Halftone Pattern…"),
          tr("Note Paper…"), tr("Photocopy…"), tr("Plaster…"), tr("Reticulation…"),
          tr("Stamp…"), tr("Torn Edges…"), tr("Water Paper…")}},
        {tr("Texture"),
         {tr("Craquelure…"), tr("Grain…"), tr("Mosaic Tiles…"), tr("Patchwork…"),
          tr("Stained Glass…"), tr("Texturizer…")}},
        {tr("Neural"),
         {tr("Neural Colorize…"), tr("Neural Color Transfer…"), tr("Neural Depth Blur…"),
          tr("Face Caricature…"), tr("Harmonization…"), tr("JPEG Artifact Removal…"),
          tr("Landscape Mixer…"), tr("Makeup Transfer…"), tr("Photo Restoration…"),
          tr("Photo to Sketch…"), tr("Sketch to Portrait…"), tr("Skin Smoothing…"),
          tr("Smart Portrait…"), tr("Style Transfer…"), tr("Super Zoom…")}},
        {tr("Video"), {tr("De-Interlace…"), tr("NTSC Colors…")}},
        {tr("Other"),
         {tr("Custom…"), tr("High Pass…"), tr("HSB/HSL…"), tr("Maximum…"), tr("Minimum…"),
          tr("Offset…")}},
    };
    // Every filter opens the same settings popup: entries with parameters
    // get sliders, dropdowns and checkboxes; entries without parameters get
    // a live preview with OK/Cancel. Nothing applies silently on click.
    const QHash<QString, QString> paramFilters{
        {tr("Box Blur…"), QStringLiteral("box_blur")},
        {tr("Gaussian Blur…"), QStringLiteral("gaussian_blur")},
        {tr("Lens Blur…"), QStringLiteral("lens_blur")},
        {tr("Motion Blur…"), QStringLiteral("motion_blur")},
        {tr("Radial Blur…"), QStringLiteral("radial_blur")},
        {tr("Shape Blur…"), QStringLiteral("shape_blur")},
        {tr("Smart Blur…"), QStringLiteral("smart_blur")},
        {tr("Surface Blur…"), QStringLiteral("surface_blur")},
        {tr("Field Blur…"), QStringLiteral("field_blur")},
        {tr("Iris Blur…"), QStringLiteral("iris_blur")},
        {tr("Tilt-Shift…"), QStringLiteral("tilt_shift")},
        {tr("Path Blur…"), QStringLiteral("path_blur")},
        {tr("Spin Blur…"), QStringLiteral("spin_blur")},
        {tr("Displace…"), QStringLiteral("displace")},
        {tr("Glass…"), QStringLiteral("glass")},
        {tr("Ocean Ripple…"), QStringLiteral("ocean_ripple")},
        {tr("Pinch…"), QStringLiteral("pinch")},
        {tr("Polar Coordinates…"), QStringLiteral("polar")},
        {tr("Ripple…"), QStringLiteral("ripple")},
        {tr("Shear…"), QStringLiteral("shear")},
        {tr("Spherize…"), QStringLiteral("spherize")},
        {tr("Twirl…"), QStringLiteral("twirl")},
        {tr("Wave…"), QStringLiteral("wave")},
        {tr("ZigZag…"), QStringLiteral("zigzag")},
        {tr("Add Noise…"), QStringLiteral("add_noise")},
        {tr("Dust & Scratches…"), QStringLiteral("dust_scratches")},
        {tr("Median…"), QStringLiteral("median")},
        {tr("Reduce Noise…"), QStringLiteral("reduce_noise")},
        {tr("Color Halftone…"), QStringLiteral("color_halftone")},
        {tr("Crystallize…"), QStringLiteral("crystallize")},
        {tr("Mezzotint…"), QStringLiteral("mezzotint")},
        {tr("Mosaic…"), QStringLiteral("mosaic")},
        {tr("Pointillize…"), QStringLiteral("pointillize")},
        {tr("Flame…"), QStringLiteral("flame")},
        {tr("Picture Frame…"), QStringLiteral("picture_frame")},
        {tr("Tree…"), QStringLiteral("tree")},
        {tr("Fibers…"), QStringLiteral("fibers")},
        {tr("Lens Flare…"), QStringLiteral("lens_flare")},
        {tr("Lighting Effects…"), QStringLiteral("lighting_effects")},
        {tr("Shake Reduction…"), QStringLiteral("shake_reduction")},
        {tr("Smart Sharpen…"), QStringLiteral("smart_sharpen")},
        {tr("Unsharp Mask…"), QStringLiteral("unsharp_mask")},
        {tr("Diffuse…"), QStringLiteral("diffuse")},
        {tr("Diffuse Glow…"), QStringLiteral("diffuse_glow")},
        {tr("Emboss…"), QStringLiteral("emboss")},
        {tr("Extrude…"), QStringLiteral("extrude")},
        {tr("Glowing Edges…"), QStringLiteral("glowing_edges")},
        {tr("Colored Pencil…"), QStringLiteral("colored_pencil")},
        {tr("Cutout…"), QStringLiteral("cutout")},
        {tr("Dry Brush…"), QStringLiteral("dry_brush")},
        {tr("Film Grain…"), QStringLiteral("film_grain")},
        {tr("Fresco…"), QStringLiteral("fresco")},
        {tr("Neon Glow…"), QStringLiteral("neon_glow")},
        {tr("Paint Daubs…"), QStringLiteral("paint_daubs")},
        {tr("Palette Knife…"), QStringLiteral("palette_knife")},
        {tr("Plastic Wrap…"), QStringLiteral("plastic_wrap")},
        {tr("Poster Edges…"), QStringLiteral("poster_edges")},
        {tr("Rough Pastels…"), QStringLiteral("rough_pastels")},
        {tr("Smudge Stick…"), QStringLiteral("smudge_stick")},
        {tr("Sponge…"), QStringLiteral("sponge")},
        {tr("Underpainting…"), QStringLiteral("underpainting")},
        {tr("Watercolor…"), QStringLiteral("watercolor")},
        {tr("Accented Edges…"), QStringLiteral("accented_edges")},
        {tr("Angled Strokes…"), QStringLiteral("angled_strokes")},
        {tr("Crosshatch…"), QStringLiteral("crosshatch")},
        {tr("Dark Strokes…"), QStringLiteral("dark_strokes")},
        {tr("Ink Outlines…"), QStringLiteral("ink_outlines")},
        {tr("Spatter…"), QStringLiteral("spatter")},
        {tr("Sprayed Strokes…"), QStringLiteral("sprayed_strokes")},
        {tr("Sumi-e…"), QStringLiteral("sumi_e")},
        {tr("Bas Relief…"), QStringLiteral("bas_relief")},
        {tr("Chalk & Charcoal…"), QStringLiteral("chalk_charcoal")},
        {tr("Charcoal…"), QStringLiteral("charcoal")},
        {tr("Chrome…"), QStringLiteral("chrome")},
        {tr("Conte Crayon…"), QStringLiteral("conte_crayon")},
        {tr("Graphic Pen…"), QStringLiteral("graphic_pen")},
        {tr("Halftone Pattern…"), QStringLiteral("halftone_pattern")},
        {tr("Note Paper…"), QStringLiteral("note_paper")},
        {tr("Photocopy…"), QStringLiteral("photocopy")},
        {tr("Plaster…"), QStringLiteral("plaster")},
        {tr("Reticulation…"), QStringLiteral("reticulation")},
        {tr("Stamp…"), QStringLiteral("stamp")},
        {tr("Torn Edges…"), QStringLiteral("torn_edges")},
        {tr("Water Paper…"), QStringLiteral("water_paper")},
        {tr("Craquelure…"), QStringLiteral("craquelure")},
        {tr("Grain…"), QStringLiteral("grain")},
        {tr("Mosaic Tiles…"), QStringLiteral("mosaic_tiles")},
        {tr("Patchwork…"), QStringLiteral("patchwork")},
        {tr("Stained Glass…"), QStringLiteral("stained_glass")},
        {tr("Texturizer…"), QStringLiteral("texturizer")},
        {tr("Neural Colorize…"), QStringLiteral("neural.colorize")},
        {tr("Neural Color Transfer…"), QStringLiteral("neural.color_transfer")},
        {tr("Neural Depth Blur…"), QStringLiteral("neural.depth_blur")},
        {tr("Face Caricature…"), QStringLiteral("neural.face_to_caricature")},
        {tr("Harmonization…"), QStringLiteral("neural.harmonization")},
        {tr("JPEG Artifact Removal…"), QStringLiteral("neural.jpeg_artifacts")},
        {tr("Landscape Mixer…"), QStringLiteral("neural.landscape_mixer")},
        {tr("Makeup Transfer…"), QStringLiteral("neural.makeup_transfer")},
        {tr("Photo Restoration…"), QStringLiteral("neural.photo_restoration")},
        {tr("Photo to Sketch…"), QStringLiteral("neural.photo_to_sketch")},
        {tr("Sketch to Portrait…"), QStringLiteral("neural.sketch_to_portrait")},
        {tr("Skin Smoothing…"), QStringLiteral("neural.skin_smoothing")},
        {tr("Smart Portrait…"), QStringLiteral("neural.smart_portrait")},
        {tr("Style Transfer…"), QStringLiteral("neural.style_transfer")},
        {tr("Super Zoom…"), QStringLiteral("neural.super_zoom")},
        {tr("Oil Paint…"), QStringLiteral("oil_paint")},
        {tr("Tiles…"), QStringLiteral("tiles")},
        {tr("Trace Contour…"), QStringLiteral("trace_contour")},
        {tr("Wind…"), QStringLiteral("wind")},
        {tr("Custom…"), QStringLiteral("custom")},
        {tr("High Pass…"), QStringLiteral("high_pass")},
        {tr("Maximum…"), QStringLiteral("maximum")},
        {tr("Minimum…"), QStringLiteral("minimum")},
        {tr("Offset…"), QStringLiteral("offset")},
        {tr("Average…"), QStringLiteral("average")},
        {tr("Blur…"), QStringLiteral("blur")},
        {tr("Blur More…"), QStringLiteral("blur_more")},
        {tr("Despeckle…"), QStringLiteral("despeckle")},
        {tr("Facet…"), QStringLiteral("facet")},
        {tr("Fragment…"), QStringLiteral("fragment")},
        {tr("Clouds…"), QStringLiteral("clouds")},
        {tr("Difference Clouds…"), QStringLiteral("difference_clouds")},
        {tr("Sharpen…"), QStringLiteral("sharpen")},
        {tr("Sharpen Edges…"), QStringLiteral("sharpen_edges")},
        {tr("Sharpen More…"), QStringLiteral("sharpen_more")},
        {tr("Find Edges…"), QStringLiteral("find_edges")},
        {tr("Solarize…"), QStringLiteral("solarize")},
        {tr("De-Interlace…"), QStringLiteral("deinterlace")},
        {tr("NTSC Colors…"), QStringLiteral("ntsc_colors")},
        {tr("HSB/HSL…"), QStringLiteral("hsb_hsl")},
    };

    for (const auto& group : groups) {
        QMenu* menu = filter->addMenu(group.first);
        for (const QString& entry : group.second) {
            if (paramFilters.contains(entry)) {
                const QString fid = paramFilters.value(entry);
                makeAction(menu, entry, QString(), [this, fid] { filterDialog(fid); });
            } else if (entry == tr("Generate Bump Map")) {
                makeAction(menu, entry, QString(), [this] { filterDialog(QStringLiteral("bump_map")); });
            } else if (entry == tr("Generate Normal Map")) {
                makeAction(menu, entry, QString(), [this] { filterDialog(QStringLiteral("normal_map")); });
            } else {
                makeAction(menu, entry);
            }
        }
    }
    filter->addSeparator();
    makeAction(filter, tr("Browse Filters Online…"));
}

}  // namespace pittore::ui
