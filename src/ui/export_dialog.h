#pragma once
#include <QColor>
#include <QDialog>
#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;

namespace pittore::ui {

class AppState;
class DocumentItem;

// Everything the Export dialog collects, handed back to the caller on accept().
// Nothing here is encoder-specific: writeExportImage() below turns this into an
// actual image + file write.
struct ExportSettings {
    QString format = QStringLiteral("png");   // lowercase suffix, no dot
    QString presetId;                         // built-in preset id, or empty
    int quality = 92;                         // 1-100, lossy formats only
    // Palettized export (PNG-8 / GIF): median-cut quantize to paletteColors
    // entries; paletteDither runs Floyd-Steinberg error diffusion.
    bool palettized = false;
    int paletteColors = 256;                  // 2-256
    bool paletteDither = true;
    // "Limit file size" (PNG-8 / GIF): the writer steps the palette down
    // (then shrinks the dimensions) until the encoded file fits maxFileSizeMB.
    bool limitFileSize = false;
    double maxFileSizeMB = 1.5;
    bool dither = false;                      // legacy global flag (see below)
    bool interlace = false;                   // progressive scan (JPEG only)

    QSize pixelSize;         // target size AFTER scale is applied
    int dpi = 72;
    bool lockAspect = true;
    // 0 = bicubic (smooth), 1 = Lanczos, 2 = nearest, 3 = bilinear.
    int resampleMode = 0;

    double scalePercent = 100.0;   // 100/150/200/300/400 from the scale chips
    QString scaleSuffix;           // "", "@1.5x", "@2x"...

    QString colorProfile = QStringLiteral("sRGB IEC61966-2.1");
    bool embedIccProfile = true;

    // The "Advanced" toggles. hideHiddenLayers=false temporarily reveals
    // hidden layers for the export render, then restores them.
    bool hideHiddenLayers = true;
    // "Use document format" (doc), "rgb8", "rgba8", "gray8", "rgb16", "gray16".
    QString pixelFormat = QStringLiteral("doc");
    // CMYK delivery (TIFF): set by the export entry point when "Use document
    // format" follows a CMYK-tagged document — the encoder separates
    // appearance pixels into ink planes and embeds `icc`. The dialog and
    // presets never set it; an explicit rgb/gray pixel format overrides.
    bool cmyk = false;
    QByteArray icc;
    bool matteEnabled = false;
    QColor matte = QColor(255, 255, 255);
    bool embedMetadata = true;    // Software tag + resolution text chunks
    bool includeBleed = false;    // shown disabled: no bleed model yet

    // PNG HDR (Rec.2020): transfer 0 = SDR, 1 = PQ (ST 2084), 2 = HLG;
    // primaries 0 = sRGB/Rec.709, 1 = BT.2020. HDR presets fix primaries to
    // BT.2020 and write a cICP chunk so players tag the file correctly.
    int transferFunction = 0;
    int primaries = 0;
    bool fullRange = true;

    // JPEG extras. convertClipsToPaths / gainMap are shown disabled: layers
    // carry no vector clip paths and there is no HDR merge source to build a
    // gain map from yet.
    bool progressive = false;   // mirrors interlace for the JPEG panel
    bool convertClipsToPaths = false;
    bool gainMap = false;

    // TIFF extras.affinityLayers is shown disabled: layered TIFF needs the
    // .af container work, not the flat encoder.
    // compression: 0 = none, 1 = LZW, 2 = ZIP (deflate).
    int tiffCompression = 1;
    bool affinityLayers = false;

    // SVG extras. rasterize: 0 = unsupported properties (vector where the
    // document has geometry, PNG/JPEG fallback otherwise), 1 = everything
    // (single flattened bitmap inside the SVG).
    int svgRasterize = 0;
    bool svgUseDocRes = true;
    int svgRasterDpi = 300;
    bool svgDownsample = false;
    int svgAboveDpi = 375;
    // Off by default so plain exports keep lossless PNG embeds (and the
    // existing raster-SVG tests); the small-size preset enables it.
    bool svgAllowJpeg = false;
    int svgJpegQuality = 85;
    int svgDecimalPlaces = 3;
    bool svgSetViewbox = true;
    bool svgLineBreaks = true;

    bool keepMetadata = false;
    bool stripGpsLocation = true;

    // -1 = whole canvas, -2 = current selection, >=0 = single layer index
    // (matching DocumentItem::layers). A layer target is rendered on its own
    // over transparency and cropped to its own bounds.
    int exportTarget = -1;

    // 0 = ask each time, 1 = same folder as document, 2 = choose folder…
    int exportLocation = 0;
};

// File ▸ Export ▸ Export As… — the export persona (live preview + a
// slice/artboard/layer picker on the left) merged with a conventional Export As
// settings stack (format, image size, resample, colour space, metadata, scale)
// on the right.
//
// The dialog only *collects* settings; MainWindow::exportActiveDocument() reads
// the accepted ExportSettings, renders the final image with writeExportImage()
// and writes it through saveImageFile() — the same function every other export
// path already goes through.
class ExportDialog final : public QDialog {
    Q_OBJECT

  public:
    ExportDialog(AppState* state, DocumentItem* doc, QWidget* parent = nullptr);

    ExportSettings settings() const { return settings_; }

  private:
    QWidget* buildLeft();
    QWidget* buildRight();

    void seedWidgets();
    ExportSettings collect() const;
    void refreshPreview();
    void refreshFormatDependentFields();
    void refreshEstimatedSize();
    void syncHeightFromWidth();
    void syncWidthFromHeight();
    void applyScaleChip(double percent, const QString& suffix);
    void applyTargetSize();
    void applyPreset(int index);
    // conventional preset handling: rebuild the preset combo for the current
    // format, apply a built-in preset id to the widgets, and drop back to
    // Custom on any manual edit.
    void rebuildPresetList();
    void applyPresetId(const QString& id);
    void markCustom();
    void syncMatteButton();
    // Push one ExportSettings through every widget (used by presets).
    void showSettings(const ExportSettings& s);
    void savePreset();
    void loadPresets();
    void persistPresets();

    int currentTarget() const;
    // Native pixel size of the selected target (canvas, selection or layer),
    // used to seed the size fields and the aspect lock.
    QSize targetNaturalSize() const;

    AppState* state_ = nullptr;
    DocumentItem* doc_ = nullptr;
    ExportSettings settings_;
    bool loading_ = false;   // suppress feedback loops while seeding widgets

    // Left: preview + export-target list.
    QLabel* preview_ = nullptr;
    QListWidget* targetList_ = nullptr;
    QVector<int> targetValues_;   // list row → ExportSettings::exportTarget

    // Right: format & preset.
    QComboBox* formatCombo_ = nullptr;
    QComboBox* presetCombo_ = nullptr;

    // Right: file settings.
    QFormLayout* fileForm_ = nullptr;
    QWidget* qualityRow_ = nullptr;
    QSlider* qualitySlider_ = nullptr;
    QLabel* qualityValue_ = nullptr;
    QCheckBox* ditherCheck_ = nullptr;
    QCheckBox* interlaceCheck_ = nullptr;

    // Right: image size.
    QSpinBox* widthSpin_ = nullptr;
    QSpinBox* heightSpin_ = nullptr;
    QSpinBox* dpiSpin_ = nullptr;
    QComboBox* resampleCombo_ = nullptr;
    QCheckBox* lockAspectCheck_ = nullptr;

    // Right: color space.
    QComboBox* colorSpaceCombo_ = nullptr;
    QCheckBox* embedIccCheck_ = nullptr;

    // Right: Advanced (conventional behaviour, per-format rows).
    QWidget* advGenericRow_ = nullptr;
    QWidget* pixelFormatRow_ = nullptr;
    QWidget* matteRow_ = nullptr;
    QCheckBox* hideHiddenCheck_ = nullptr;
    QComboBox* pixelFormatCombo_ = nullptr;
    QPushButton* matteButton_ = nullptr;
    QColor matteColor_ = QColor(255, 255, 255);
    bool matteTransparent_ = true;
    QCheckBox* embedMetadataCheck_ = nullptr;
    QCheckBox* includeBleedCheck_ = nullptr;
    // Per-format Advanced groups (whole blocks show/hide by format).
    // (Generic/pixel/matte containers are declared above.)
    // PNG HDR rows.
    QWidget* hdrRow_ = nullptr;
    QComboBox* transferCombo_ = nullptr;
    QComboBox* primariesCombo_ = nullptr;
    QCheckBox* fullRangeCheck_ = nullptr;
    // Palette rows (PNG-8 / GIF).
    QWidget* paletteRow_ = nullptr;
    QCheckBox* palettizedCheck_ = nullptr;
    QComboBox* paletteCombo_ = nullptr;
    QComboBox* paletteColorsCombo_ = nullptr;
    QCheckBox* paletteDitherCheck_ = nullptr;
    QCheckBox* limitSizeCheck_ = nullptr;
    QDoubleSpinBox* maxSizeSpin_ = nullptr;
    // JPEG rows (progressive reuses the File Settings checkbox).
    QWidget* jpegRow_ = nullptr;
    QCheckBox* clipsToPathsCheck_ = nullptr;
    QCheckBox* gainMapCheck_ = nullptr;
    // TIFF rows.
    QWidget* tiffRow_ = nullptr;
    QCheckBox* affinityLayersCheck_ = nullptr;
    QComboBox* tiffCompressionCombo_ = nullptr;
    // SVG rows.
    QWidget* svgRow_ = nullptr;
    QComboBox* svgRasterizeCombo_ = nullptr;
    QCheckBox* svgUseDocResCheck_ = nullptr;
    QSpinBox* svgRasterDpiSpin_ = nullptr;
    QCheckBox* svgDownsampleCheck_ = nullptr;
    QComboBox* svgAboveDpiCombo_ = nullptr;
    QCheckBox* svgAllowJpegCheck_ = nullptr;
    QComboBox* svgQualityCombo_ = nullptr;
    QComboBox* svgDecimalCombo_ = nullptr;
    QCheckBox* svgTextCurvesCheck_ = nullptr;
    QCheckBox* svgLongerSpansCheck_ = nullptr;
    QCheckBox* svgRelativeCheck_ = nullptr;
    QCheckBox* svgHexCheck_ = nullptr;
    QCheckBox* svgFlattenCheck_ = nullptr;
    QCheckBox* svgTileCheck_ = nullptr;
    QCheckBox* svgViewboxCheck_ = nullptr;
    QCheckBox* svgBreaksCheck_ = nullptr;

    // Right: metadata.
    QCheckBox* keepMetadataCheck_ = nullptr;
    QCheckBox* stripGpsCheck_ = nullptr;

    // Right: scale + batch.
    QVector<QPushButton*> scaleChips_;
    QComboBox* exportLocationCombo_ = nullptr;

    // Footer.
    QLabel* estimateLabel_ = nullptr;

    double aspect_ = 1.0;   // width/height, recomputed whenever the doc size loads

    // Named presets the user saved: persisted as JSON beside Settings.toml.
    struct SavedPreset {
        QString name;
        ExportSettings s;
    };
    QVector<SavedPreset> presets_;
};

// Build the final export image from `source`: crop to `crop` (in source pixels;
// empty = the whole image), resample to settings.pixelSize with its
// resampleMode, stamp the DPI, convert/embed the colour profile, then write it
// with the chosen quality + progressive flag. Returns false with *error set on
// failure.
bool writeExportImage(const QImage& source, const QRect& crop,
                      const ExportSettings& settings, const QString& path,
                      QString* error, QString* report = nullptr);

// Write the document as SVG. Layers that carry retained vector geometry (SVG
// imports) are emitted as real <path>/<rect>/<circle>/gradients; every other
// layer is embedded as a base64 <image> (PNG, or JPEG when the settings allow
// it), so the result is vector wherever the document actually has geometry.
// Honours the export target (canvas, selection or a single layer), the output
// size (as the SVG viewport), per-layer blend/opacity, the SVG rasterize mode,
// downsampling and number formatting. Returns false with *error set on failure.
bool writeExportVector(DocumentItem& doc, const ExportSettings& settings,
                       const QString& path, QString* error);

// Default file stem for an export: the document title (or the .psc basename)
// plus the scale chip's suffix, without an extension.
QString exportBaseName(const DocumentItem& doc, const ExportSettings& settings);

}  // namespace pittore::ui
