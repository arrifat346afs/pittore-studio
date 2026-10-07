#include "ui/export_dialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QColorSpace>
#include <QComboBox>
#include <QBuffer>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTransform>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "ui/app_state.h"
#include "ui/project_manager.h"
#include "ui/settings.h"
#include "ui/theme.h"
#include "ui/export/shared/export_helpers.h"

namespace pittore::ui {

using namespace export_detail;


ExportDialog::ExportDialog(AppState* state, DocumentItem* doc, QWidget* parent)
    : QDialog(parent), state_(state), doc_(doc) {
    setWindowTitle(tr("Export"));
    resize(1020, 660);

    settings_.pixelSize = doc_->size;
    settings_.dpi = doc_->dpi > 0 ? doc_->dpi : 72;
    settings_.colorProfile = doc_->profile;
    // Settings > Export seeds the sticky defaults; per-export edits stay
    // local to the dialog and never write back to Settings.toml.
    {
        const AppSettings& prefs = state_->settings();
        if (!prefs.exportFormat.isEmpty()) settings_.format = prefs.exportFormat;
        settings_.quality = qBound(1, prefs.exportQuality, 100);
        settings_.exportLocation = qBound(0, prefs.exportLocation, 2);
        settings_.embedIccProfile = prefs.exportEmbedIcc;
    }
    aspect_ = doc_->size.height() > 0
                  ? double(doc_->size.width()) / double(doc_->size.height())
                  : 1.0;

    auto* body = new QHBoxLayout;
    body->addWidget(buildLeft(), 6);
    body->addWidget(buildRight(), 5);

    estimateLabel_ = new QLabel(this);
    estimateLabel_->setStyleSheet(dimStyle(state_));

    auto* buttons = new QDialogButtonBox(this);
    auto* saveNamed = buttons->addButton(tr("Save Preset"), QDialogButtonBox::ResetRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    auto* exportBtn = buttons->addButton(tr("Export"), QDialogButtonBox::AcceptRole);
    exportBtn->setDefault(true);
    connect(saveNamed, &QPushButton::clicked, this, [this] { savePreset(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* footer = new QHBoxLayout;
    footer->addWidget(estimateLabel_, 1);
    footer->addWidget(buttons);

    auto* root = new QVBoxLayout(this);
    root->addLayout(body, 1);
    root->addLayout(footer);

    // Stable object names so tests (and any future scripting) can drive the
    // controls without hunting through the layout.
    preview_->setObjectName(QStringLiteral("export.preview"));
    targetList_->setObjectName(QStringLiteral("export.targets"));
    formatCombo_->setObjectName(QStringLiteral("export.format"));
    presetCombo_->setObjectName(QStringLiteral("export.preset"));
    qualityRow_->setObjectName(QStringLiteral("export.qualityRow"));
    qualitySlider_->setObjectName(QStringLiteral("export.quality"));
    ditherCheck_->setObjectName(QStringLiteral("export.dither"));
    interlaceCheck_->setObjectName(QStringLiteral("export.interlace"));
    widthSpin_->setObjectName(QStringLiteral("export.width"));
    heightSpin_->setObjectName(QStringLiteral("export.height"));
    dpiSpin_->setObjectName(QStringLiteral("export.dpi"));
    resampleCombo_->setObjectName(QStringLiteral("export.resample"));
    lockAspectCheck_->setObjectName(QStringLiteral("export.lockAspect"));
    colorSpaceCombo_->setObjectName(QStringLiteral("export.colorSpace"));
    embedIccCheck_->setObjectName(QStringLiteral("export.embedIcc"));
    keepMetadataCheck_->setObjectName(QStringLiteral("export.keepMetadata"));
    stripGpsCheck_->setObjectName(QStringLiteral("export.stripGps"));
    hideHiddenCheck_->setObjectName(QStringLiteral("export.hideHidden"));
    pixelFormatCombo_->setObjectName(QStringLiteral("export.pixelFormat"));
    matteButton_->setObjectName(QStringLiteral("export.matte"));
    embedMetadataCheck_->setObjectName(QStringLiteral("export.embedMetadata"));
    includeBleedCheck_->setObjectName(QStringLiteral("export.includeBleed"));
    transferCombo_->setObjectName(QStringLiteral("export.transfer"));
    primariesCombo_->setObjectName(QStringLiteral("export.primaries"));
    fullRangeCheck_->setObjectName(QStringLiteral("export.fullRange"));
    palettizedCheck_->setObjectName(QStringLiteral("export.palettized"));
    paletteCombo_->setObjectName(QStringLiteral("export.palette"));
    paletteColorsCombo_->setObjectName(QStringLiteral("export.paletteColors"));
    paletteDitherCheck_->setObjectName(QStringLiteral("export.paletteDither"));
    limitSizeCheck_->setObjectName(QStringLiteral("export.limitSize"));
    maxSizeSpin_->setObjectName(QStringLiteral("export.maxSize"));
    clipsToPathsCheck_->setObjectName(QStringLiteral("export.clipsToPaths"));
    gainMapCheck_->setObjectName(QStringLiteral("export.gainMap"));
    affinityLayersCheck_->setObjectName(QStringLiteral("export.affinityLayers"));
    tiffCompressionCombo_->setObjectName(QStringLiteral("export.tiffCompression"));
    svgRasterizeCombo_->setObjectName(QStringLiteral("export.svgRasterize"));
    svgUseDocResCheck_->setObjectName(QStringLiteral("export.svgUseDocRes"));
    svgRasterDpiSpin_->setObjectName(QStringLiteral("export.svgRasterDpi"));
    svgDownsampleCheck_->setObjectName(QStringLiteral("export.svgDownsample"));
    svgAboveDpiCombo_->setObjectName(QStringLiteral("export.svgAboveDpi"));
    svgAllowJpegCheck_->setObjectName(QStringLiteral("export.svgAllowJpeg"));
    svgQualityCombo_->setObjectName(QStringLiteral("export.svgQuality"));
    svgDecimalCombo_->setObjectName(QStringLiteral("export.svgDecimals"));
    svgTextCurvesCheck_->setObjectName(QStringLiteral("export.svgTextCurves"));
    svgLongerSpansCheck_->setObjectName(QStringLiteral("export.svgLongerSpans"));
    svgRelativeCheck_->setObjectName(QStringLiteral("export.svgRelative"));
    svgHexCheck_->setObjectName(QStringLiteral("export.svgHex"));
    svgFlattenCheck_->setObjectName(QStringLiteral("export.svgFlatten"));
    svgTileCheck_->setObjectName(QStringLiteral("export.svgTile"));
    svgViewboxCheck_->setObjectName(QStringLiteral("export.svgViewbox"));
    svgBreaksCheck_->setObjectName(QStringLiteral("export.svgBreaks"));
    exportLocationCombo_->setObjectName(QStringLiteral("export.location"));
    estimateLabel_->setObjectName(QStringLiteral("export.estimate"));

    loadPresets();
    rebuildPresetList();

    seedWidgets();
    refreshFormatDependentFields();
    refreshPreview();
    refreshEstimatedSize();

    connect(this, &QDialog::accepted, this, [this] { settings_ = collect(); });
}


QWidget* ExportDialog::buildLeft() {
    auto* panel = new QWidget(this);
    auto* col = new QVBoxLayout(panel);
    col->setContentsMargins(0, 0, 0, 0);

    preview_ = new QLabel(panel);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumHeight(320);
    preview_->setFrameShape(QFrame::Box);
    col->addWidget(preview_, 1);

    auto* zoomRow = new QHBoxLayout;
    auto* zoomLabel = new QLabel(tr("Preview — fits the current export target"), panel);
    zoomLabel->setStyleSheet(dimStyle(state_));
    zoomRow->addWidget(zoomLabel);
    zoomRow->addStretch(1);
    col->addLayout(zoomRow);

    auto* targetGroup = new QGroupBox(tr("Export Target"), panel);
    auto* targetLayout = new QVBoxLayout(targetGroup);
    targetList_ = new QListWidget(targetGroup);
    targetList_->addItem(tr("Whole Canvas — %1 × %2")
                             .arg(doc_->size.width())
                             .arg(doc_->size.height()));
    targetValues_ << -1;
    if (!doc_->selection.isEmpty()) {
        const QRect r = doc_->selection.toAlignedRect();
        targetList_->addItem(tr("Current Selection — %1 × %2").arg(r.width()).arg(r.height()));
        targetValues_ << -2;
    }
    for (int i = 0; i < doc_->layers.size(); ++i) {
        const LayerItem& layer = doc_->layers[i];
        if (layer.kind != LayerItem::Kind::Pixel) continue;
        targetList_->addItem(tr("Layer — %1").arg(layer.name));
        targetValues_ << i;
    }
    targetList_->setCurrentRow(0);
    targetList_->setMaximumHeight(160);
    targetLayout->addWidget(targetList_);
    col->addWidget(targetGroup);

    // Picking a target re-seeds the size fields from that target's native
    // bounds, so exporting a layer or a selection starts at its own size
    // rather than the whole canvas (the export persona does the same).
    connect(targetList_, &QListWidget::currentRowChanged, this, [this](int) {
        applyTargetSize();
        refreshPreview();
    });

    return panel;
}


QWidget* ExportDialog::buildRight() {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* page = new QWidget;
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(4, 0, 4, 0);
    col->setSpacing(12);

    // -- Format & Preset -----------------------------------------------------
    auto* formatGroup = new QGroupBox(tr("Format && Preset"), page);
    auto* formatForm = new QFormLayout(formatGroup);
    formatCombo_ = new QComboBox(formatGroup);
    for (const QString& f : availableExportFormats())
        formatCombo_->addItem(f.toUpper(), f);
    formatCombo_->setCurrentIndex(qMax(0, formatCombo_->findData(QStringLiteral("png"))));
    presetCombo_ = new QComboBox(formatGroup);
    presetCombo_->addItem(tr("Custom"), QStringLiteral("custom"));
    formatForm->addRow(tr("File Format"), formatCombo_);
    formatForm->addRow(tr("Preset"), presetCombo_);
    col->addWidget(formatGroup);

    // -- File Settings --------------------------------------------------------
    auto* fileGroup = new QGroupBox(tr("File Settings"), page);
    fileForm_ = new QFormLayout(fileGroup);
    qualitySlider_ = new QSlider(Qt::Horizontal, fileGroup);
    qualitySlider_->setRange(1, 100);
    qualitySlider_->setValue(settings_.quality);
    qualityValue_ = new QLabel(fileGroup);
    qualityValue_->setMinimumWidth(28);
    auto* qualityRow = new QHBoxLayout;
    qualityRow->setContentsMargins(0, 0, 0, 0);
    qualityRow->addWidget(qualitySlider_, 1);
    qualityRow->addWidget(qualityValue_);
    qualityRow_ = new QWidget(fileGroup);
    qualityRow_->setLayout(qualityRow);
    fileForm_->addRow(tr("Quality"), qualityRow_);
    ditherCheck_ = new QCheckBox(tr("Dither (reduces banding on gradients)"), fileGroup);
    ditherCheck_->setChecked(settings_.dither);
    // Dithering only matters when quantising to a lower bit depth. The document
    // composite is 8-bit RGBA and every encoder here writes 8-bit or wider, so
    // there is nothing to dither — shown disabled rather than as a dead toggle.
    ditherCheck_->setEnabled(false);
    ditherCheck_->setToolTip(tr("Dithering applies when reducing to a lower bit "
                                "depth; this 8-bit pipeline has nothing to dither."));
    interlaceCheck_ = new QCheckBox(tr("Progressive / Interlaced"), fileGroup);
    interlaceCheck_->setChecked(settings_.interlace);
    fileForm_->addRow(QString(), ditherCheck_);
    fileForm_->addRow(QString(), interlaceCheck_);
    col->addWidget(fileGroup);

    // -- Image Size -----------------------------------------------------------
    auto* sizeGroup = new QGroupBox(tr("Image Size"), page);
    auto* sizeForm = new QFormLayout(sizeGroup);
    widthSpin_ = new QSpinBox(sizeGroup);
    widthSpin_->setRange(1, 65535);
    widthSpin_->setSuffix(tr(" px"));
    widthSpin_->setValue(doc_->size.width());
    heightSpin_ = new QSpinBox(sizeGroup);
    heightSpin_->setRange(1, 65535);
    heightSpin_->setSuffix(tr(" px"));
    heightSpin_->setValue(doc_->size.height());
    auto* dims = new QHBoxLayout;
    dims->setContentsMargins(0, 0, 0, 0);
    dims->addWidget(widthSpin_);
    dims->addWidget(new QLabel(QStringLiteral("×"), sizeGroup));
    dims->addWidget(heightSpin_);
    auto* dimsWidget = new QWidget(sizeGroup);
    dimsWidget->setLayout(dims);
    sizeForm->addRow(tr("Dimensions"), dimsWidget);
    dpiSpin_ = new QSpinBox(sizeGroup);
    dpiSpin_->setRange(1, 2400);
    dpiSpin_->setValue(settings_.dpi);
    dpiSpin_->setSuffix(tr(" ppi"));
    sizeForm->addRow(tr("Resolution"), dpiSpin_);
    resampleCombo_ = new QComboBox(sizeGroup);
    resampleCombo_->addItems({tr("Bicubic (smooth)"), tr("Lanczos"),
                              tr("Nearest Neighbor"), tr("Bilinear")});
    sizeForm->addRow(tr("Resample"), resampleCombo_);
    lockAspectCheck_ = new QCheckBox(tr("Lock aspect ratio"), sizeGroup);
    lockAspectCheck_->setChecked(settings_.lockAspect);
    sizeForm->addRow(QString(), lockAspectCheck_);
    col->addWidget(sizeGroup);

    connect(widthSpin_, &QSpinBox::valueChanged, this, [this](int) {
        if (!loading_) {
            for (QPushButton* chip : scaleChips_) chip->setChecked(false);
            syncHeightFromWidth();
            markCustom();
        }
        refreshEstimatedSize();
    });
    connect(heightSpin_, &QSpinBox::valueChanged, this, [this](int) {
        if (!loading_) {
            for (QPushButton* chip : scaleChips_) chip->setChecked(false);
            syncWidthFromHeight();
            markCustom();
        }
        refreshEstimatedSize();
    });

    // -- Color Space ----------------------------------------------------------
    auto* colorGroup = new QGroupBox(tr("Color Space"), page);
    auto* colorForm = new QFormLayout(colorGroup);
    colorSpaceCombo_ = new QComboBox(colorGroup);
    colorSpaceCombo_->addItems({QStringLiteral("sRGB IEC61966-2.1"),
                                QStringLiteral("Adobe RGB (1998)"),
                                QStringLiteral("Display P3"),
                                tr("Don't Convert")});
    colorForm->addRow(tr("Convert to"), colorSpaceCombo_);
    embedIccCheck_ = new QCheckBox(tr("Embed ICC color profile"), colorGroup);
    embedIccCheck_->setChecked(settings_.embedIccProfile);
    colorForm->addRow(QString(), embedIccCheck_);
    connect(colorSpaceCombo_, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshFormatDependentFields(); });
    col->addWidget(colorGroup);

    // -- Advanced (conventional behaviour, per-format groups) --------------------------
    auto* advGroup = new QGroupBox(tr("Advanced"), page);
    auto* advCol = new QVBoxLayout(advGroup);
    advCol->setContentsMargins(0, 0, 0, 0);
    advCol->setSpacing(12);

    advGenericRow_ = new QWidget(advGroup);
    {
        auto* form = new QFormLayout(advGenericRow_);
        form->setContentsMargins(0, 0, 0, 0);
        hideHiddenCheck_ =
            new QCheckBox(tr("Don't export hidden layers"), advGenericRow_);
        hideHiddenCheck_->setChecked(settings_.hideHiddenLayers);
        form->addRow(QString(), hideHiddenCheck_);
        embedMetadataCheck_ =
            new QCheckBox(tr("Embed Metadata"), advGenericRow_);
        embedMetadataCheck_->setChecked(settings_.embedMetadata);
        embedMetadataCheck_->setToolTip(
            tr("Writes Software and resolution text tags; off strips them."));
        form->addRow(QString(), embedMetadataCheck_);
        includeBleedCheck_ =
            new QCheckBox(tr("Include bleed"), advGenericRow_);
        includeBleedCheck_->setChecked(false);
        includeBleedCheck_->setEnabled(false);
        includeBleedCheck_->setToolTip(tr("Documents carry no bleed box yet, "
                                          "so there is nothing to include."));
        form->addRow(QString(), includeBleedCheck_);
    }
    advCol->addWidget(advGenericRow_);

    pixelFormatRow_ = new QWidget(advGroup);
    {
        auto* form = new QFormLayout(pixelFormatRow_);
        form->setContentsMargins(0, 0, 0, 0);
        pixelFormatCombo_ = new QComboBox(pixelFormatRow_);
        pixelFormatCombo_->addItem(tr("Use document format"),
                                   QStringLiteral("doc"));
        pixelFormatCombo_->addItem(tr("RGB 8-bit"), QStringLiteral("rgb8"));
        pixelFormatCombo_->addItem(tr("RGBA 8-bit"), QStringLiteral("rgba8"));
        pixelFormatCombo_->addItem(tr("Grayscale 8-bit"),
                                   QStringLiteral("gray8"));
        pixelFormatCombo_->addItem(tr("RGB 16-bit"), QStringLiteral("rgb16"));
        pixelFormatCombo_->addItem(tr("Grayscale 16-bit"),
                                   QStringLiteral("gray16"));
        form->addRow(tr("Pixel Format"), pixelFormatCombo_);
    }
    advCol->addWidget(pixelFormatRow_);

    matteRow_ = new QWidget(advGroup);
    {
        auto* form = new QFormLayout(matteRow_);
        form->setContentsMargins(0, 0, 0, 0);
        auto* matteRow = new QWidget(matteRow_);
        auto* matteLay = new QHBoxLayout(matteRow);
        matteLay->setContentsMargins(0, 0, 0, 0);
        matteButton_ = new QPushButton(matteRow);
        matteButton_->setMinimumWidth(120);
        auto* matteClear = new QPushButton(tr("None"), matteRow);
        matteClear->setToolTip(tr("Transparent matte: keep the alpha channel."));
        matteLay->addWidget(matteButton_, 1);
        matteLay->addWidget(matteClear);
        form->addRow(tr("Matte"), matteRow);
        connect(matteButton_, &QPushButton::clicked, this, [this] {
            QColor picked = QColorDialog::getColor(
                matteTransparent_ ? QColor(255, 255, 255) : matteColor_, this,
                tr("Matte Color"));
            if (!picked.isValid()) return;
            matteColor_ = picked;
            matteTransparent_ = false;
            syncMatteButton();
            markCustom();
            refreshEstimatedSize();
        });
        connect(matteClear, &QPushButton::clicked, this, [this] {
            matteTransparent_ = true;
            syncMatteButton();
            markCustom();
            refreshEstimatedSize();
        });
    }
    advCol->addWidget(matteRow_);

    // PNG HDR rows: transfer function, primaries, range.
    hdrRow_ = new QWidget(advGroup);
    {
        auto* form = new QFormLayout(hdrRow_);
        form->setContentsMargins(0, 0, 0, 0);
        transferCombo_ = new QComboBox(hdrRow_);
        transferCombo_->addItems({tr("SDR"), QStringLiteral("PQ"),
                                  QStringLiteral("HLG")});
        primariesCombo_ = new QComboBox(hdrRow_);
        primariesCombo_->addItems({tr("Rec.709"), QStringLiteral("BT.2020")});
        fullRangeCheck_ = new QCheckBox(tr("Full range"), hdrRow_);
        fullRangeCheck_->setChecked(true);
        form->addRow(tr("Transfer function"), transferCombo_);
        form->addRow(tr("Primaries"), primariesCombo_);
        form->addRow(QString(), fullRangeCheck_);
    }
    hdrRow_->setObjectName(QStringLiteral("export.hdrRow"));
    advCol->addWidget(hdrRow_);

    // Palette rows (PNG-8 / GIF).
    palettizedCheck_ = new QCheckBox(tr("Palettized"), advGroup);
    paletteCombo_ = new QComboBox(advGroup);
    paletteCombo_->addItem(tr("Automatic"));
    paletteCombo_->setEnabled(false);
    paletteCombo_->setToolTip(tr("Only automatic palettes are generated."));
    paletteColorsCombo_ = new QComboBox(advGroup);
    for (int c : {2, 4, 8, 16, 32, 64, 128, 256})
        paletteColorsCombo_->addItem(QString::number(c), c);
    paletteColorsCombo_->setCurrentIndex(
        paletteColorsCombo_->findData(256));
    paletteDitherCheck_ = new QCheckBox(tr("Dither"), advGroup);
    paletteDitherCheck_->setChecked(true);
    paletteDitherCheck_->setToolTip(
        tr("Floyd-Steinberg diffusion when reducing to the palette."));
    limitSizeCheck_ = new QCheckBox(tr("Limit file size"), advGroup);
    limitSizeCheck_->setChecked(false);
    limitSizeCheck_->setToolTip(
        tr("Auto-step the palette down (then shrink) until the file fits."));
    maxSizeSpin_ = new QDoubleSpinBox(advGroup);
    maxSizeSpin_->setRange(0.1, 100.0);
    maxSizeSpin_->setDecimals(1);
    maxSizeSpin_->setSingleStep(0.1);
    maxSizeSpin_->setValue(1.5);
    maxSizeSpin_->setSuffix(tr(" MB"));
    maxSizeSpin_->setEnabled(false);
    paletteRow_ = new QWidget(advGroup);
    auto* palLay = new QFormLayout(paletteRow_);
    palLay->setContentsMargins(0, 0, 0, 0);
    palLay->addRow(QString(), palettizedCheck_);
    palLay->addRow(tr("Palette"), paletteCombo_);
    palLay->addRow(tr("Colors"), paletteColorsCombo_);
    palLay->addRow(QString(), paletteDitherCheck_);
    palLay->addRow(QString(), limitSizeCheck_);
    palLay->addRow(tr("Max size"), maxSizeSpin_);
    advCol->addWidget(paletteRow_);
    paletteRow_->setObjectName(QStringLiteral("export.paletteRow"));

    // JPEG rows.
    jpegRow_ = new QWidget(advGroup);
    {
        auto* form = new QFormLayout(jpegRow_);
        form->setContentsMargins(0, 0, 0, 0);
        clipsToPathsCheck_ =
            new QCheckBox(tr("Convert clips to paths"), jpegRow_);
        clipsToPathsCheck_->setEnabled(false);
        clipsToPathsCheck_->setToolTip(
            tr("Layers carry no vector clip paths to convert yet."));
        gainMapCheck_ =
            new QCheckBox(tr("Embed HDR image as gain map"), jpegRow_);
        gainMapCheck_->setEnabled(false);
        gainMapCheck_->setToolTip(
            tr("Needs an HDR merge source; single-image documents have none."));
        form->addRow(QString(), clipsToPathsCheck_);
        form->addRow(QString(), gainMapCheck_);
    }
    advCol->addWidget(jpegRow_);

    // TIFF rows.
    tiffRow_ = new QWidget(advGroup);
    {
        auto* form = new QFormLayout(tiffRow_);
        form->setContentsMargins(0, 0, 0, 0);
        affinityLayersCheck_ =
            new QCheckBox(tr("Save Affinity layers"), tiffRow_);
        affinityLayersCheck_->setEnabled(false);
        affinityLayersCheck_->setToolTip(
            tr("Layered TIFF needs the .af container work; exports are flat."));
        tiffCompressionCombo_ = new QComboBox(tiffRow_);
        tiffCompressionCombo_->addItems({tr("None"), tr("LZW"), tr("ZIP")});
        tiffCompressionCombo_->setCurrentIndex(1);
        form->addRow(QString(), affinityLayersCheck_);
        form->addRow(tr("Compression"), tiffCompressionCombo_);
    }
    advCol->addWidget(tiffRow_);

    // SVG rows.
    svgRow_ = new QWidget(advGroup);
    {
        auto* form = new QFormLayout(svgRow_);
        form->setContentsMargins(0, 0, 0, 0);
        svgRasterizeCombo_ = new QComboBox(svgRow_);
        svgRasterizeCombo_->addItems({tr("Unsupported properties"),
                                      tr("Everything")});
        svgUseDocResCheck_ =
            new QCheckBox(tr("Use document resolution"), svgRow_);
        svgUseDocResCheck_->setChecked(true);
        svgRasterDpiSpin_ = new QSpinBox(svgRow_);
        svgRasterDpiSpin_->setRange(72, 1200);
        svgRasterDpiSpin_->setValue(300);
        svgDownsampleCheck_ =
            new QCheckBox(tr("Downsample images"), svgRow_);
        svgAboveDpiCombo_ = new QComboBox(svgRow_);
        for (int d : {72, 150, 300, 375, 600})
            svgAboveDpiCombo_->addItem(tr("Above %1 DPI").arg(d), d);
        svgAboveDpiCombo_->setCurrentIndex(svgAboveDpiCombo_->findData(375));
        svgAllowJpegCheck_ =
            new QCheckBox(tr("Allow JPEG compression"), svgRow_);
        svgQualityCombo_ = new QComboBox(svgRow_);
        for (int q : {50, 60, 70, 80, 85, 90, 95, 100})
            svgQualityCombo_->addItem(QString::number(q), q);
        svgQualityCombo_->setCurrentIndex(svgQualityCombo_->findData(85));
        svgDecimalCombo_ = new QComboBox(svgRow_);
        for (int d = 0; d <= 6; ++d)
            svgDecimalCombo_->addItem(QString::number(d), d);
        svgDecimalCombo_->setCurrentIndex(svgDecimalCombo_->findData(3));
        svgTextCurvesCheck_ =
            new QCheckBox(tr("Export text as curves"), svgRow_);
        svgTextCurvesCheck_->setEnabled(false);
        svgTextCurvesCheck_->setToolTip(
            tr("Glyph outlines are not retained by the text engine yet."));
        svgLongerSpansCheck_ =
            new QCheckBox(tr("Longer text spans"), svgRow_);
        svgLongerSpansCheck_->setEnabled(false);
        svgLongerSpansCheck_->setToolTip(
            tr("No text-span model in the serializer yet."));
        svgRelativeCheck_ =
            new QCheckBox(tr("Relative coordinates"), svgRow_);
        svgRelativeCheck_->setEnabled(false);
        svgRelativeCheck_->setToolTip(
            tr("The serializer writes absolute coordinates."));
        svgHexCheck_ = new QCheckBox(tr("Use hex colors"), svgRow_);
        svgHexCheck_->setChecked(true);
        svgHexCheck_->setEnabled(false);
        svgHexCheck_->setToolTip(tr("Colors are always written as hex."));
        svgFlattenCheck_ =
            new QCheckBox(tr("Flatten transforms"), svgRow_);
        svgFlattenCheck_->setEnabled(false);
        svgFlattenCheck_->setToolTip(
            tr("Transforms stay live on the elements."));
        svgTileCheck_ = new QCheckBox(tr("Use tile patterns"), svgRow_);
        svgTileCheck_->setEnabled(false);
        svgTileCheck_->setToolTip(tr("No pattern fills in the model yet."));
        svgViewboxCheck_ = new QCheckBox(tr("Set viewbox"), svgRow_);
        svgViewboxCheck_->setChecked(true);
        svgBreaksCheck_ = new QCheckBox(tr("Add line breaks"), svgRow_);
        svgBreaksCheck_->setChecked(true);
        form->addRow(tr("Rasterize"), svgRasterizeCombo_);
        form->addRow(QString(), svgUseDocResCheck_);
        form->addRow(tr("Raster DPI"), svgRasterDpiSpin_);
        form->addRow(QString(), svgDownsampleCheck_);
        form->addRow(QString(), svgAboveDpiCombo_);
        auto* resampleNote =
            new QLabel(tr("Resample: see Image Size"), svgRow_);
        resampleNote->setStyleSheet(dimStyle(state_));
        form->addRow(QString(), resampleNote);
        form->addRow(QString(), svgAllowJpegCheck_);
        form->addRow(tr("Quality"), svgQualityCombo_);
        form->addRow(tr("Decimal places"), svgDecimalCombo_);
        form->addRow(QString(), svgTextCurvesCheck_);
        form->addRow(QString(), svgLongerSpansCheck_);
        form->addRow(QString(), svgRelativeCheck_);
        form->addRow(QString(), svgHexCheck_);
        form->addRow(QString(), svgFlattenCheck_);
        form->addRow(QString(), svgTileCheck_);
        form->addRow(QString(), svgViewboxCheck_);
        form->addRow(QString(), svgBreaksCheck_);
    }
    advCol->addWidget(svgRow_);
    col->addWidget(advGroup);
    syncMatteButton();

    // -- Metadata -------------------------------------------------------------
    // The composite carries no source metadata through the app yet, so these
    // are shown disabled rather than pretending to change the output.
    auto* metaGroup = new QGroupBox(tr("Metadata"), page);
    auto* metaCol = new QVBoxLayout(metaGroup);
    keepMetadataCheck_ = new QCheckBox(tr("Keep copyright / contact info"), metaGroup);
    stripGpsCheck_ = new QCheckBox(tr("Strip GPS location"), metaGroup);
    stripGpsCheck_->setChecked(settings_.stripGpsLocation);
    keepMetadataCheck_->setEnabled(false);
    stripGpsCheck_->setEnabled(false);
    const QString metaTip = tr("Source metadata is not carried through the "
                               "composite yet; exports never embed GPS.");
    keepMetadataCheck_->setToolTip(metaTip);
    stripGpsCheck_->setToolTip(metaTip);
    metaCol->addWidget(keepMetadataCheck_);
    metaCol->addWidget(stripGpsCheck_);
    auto* metaNote = new QLabel(metaTip, metaGroup);
    metaNote->setWordWrap(true);
    metaNote->setStyleSheet(dimStyle(state_));
    metaCol->addWidget(metaNote);
    col->addWidget(metaGroup);

    // -- Scale & Export Location ---------------------------------------------
    auto* scaleGroup = new QGroupBox(tr("Scale Export"), page);
    auto* scaleCol = new QVBoxLayout(scaleGroup);
    auto* chipRow = new QHBoxLayout;
    chipRow->setContentsMargins(0, 0, 0, 0);
    const QVector<QPair<double, QString>> chips{
        {100.0, QString()}, {150.0, QStringLiteral("@1.5x")},
        {200.0, QStringLiteral("@2x")}, {300.0, QStringLiteral("@3x")},
        {400.0, QStringLiteral("@4x")}};
    for (const auto& [percent, suffix] : chips) {
        auto* chip = new QPushButton(
            percent == 100.0 ? tr("1x") : QStringLiteral("%1x").arg(percent / 100.0),
            scaleGroup);
        chip->setCheckable(true);
        chip->setChecked(percent == 100.0);
        chip->setProperty("percent", percent);
        chip->setProperty("suffix", suffix);
        scaleChips_.append(chip);
        chipRow->addWidget(chip);
    }
    for (QPushButton* chip : scaleChips_) {
        const double percent = chip->property("percent").toDouble();
        const QString suffix = chip->property("suffix").toString();
        connect(chip, &QPushButton::clicked, this, [this, chip, percent, suffix] {
            for (QPushButton* other : scaleChips_) other->setChecked(other == chip);
            applyScaleChip(percent, suffix);
        });
    }
    scaleCol->addLayout(chipRow);
    col->addWidget(scaleGroup);

    auto* locGroup = new QGroupBox(tr("Export Location"), page);
    auto* locForm = new QFormLayout(locGroup);
    exportLocationCombo_ = new QComboBox(locGroup);
    exportLocationCombo_->addItems(
        {tr("Ask each time"), tr("Same folder as document"), tr("Choose folder…")});
    locForm->addRow(tr("Save to"), exportLocationCombo_);
    col->addWidget(locGroup);

    col->addStretch(1);
    scroll->setWidget(page);

    connect(formatCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        rebuildPresetList();
        refreshFormatDependentFields();
    });
    connect(presetCombo_, &QComboBox::currentIndexChanged, this,
            [this](int idx) { applyPreset(idx); });
    connect(qualitySlider_, &QSlider::valueChanged, this, [this](int v) {
        qualityValue_->setText(QString::number(v));
        markCustom();
        refreshEstimatedSize();
    });
    qualityValue_->setText(QString::number(qualitySlider_->value()));

    // Any manual edit drops the preset combo back to Custom (markCustom is a
    // no-op while seeding/applying, and estimate-relevant edits also refresh
    // the footer readout).
    auto touch = [this] {
        markCustom();
        refreshEstimatedSize();
    };
    connect(dpiSpin_, &QSpinBox::valueChanged, this, touch);
    connect(resampleCombo_, &QComboBox::currentIndexChanged, this, touch);
    connect(colorSpaceCombo_, &QComboBox::currentIndexChanged, this,
            [this] { markCustom(); });
    connect(embedIccCheck_, &QCheckBox::toggled, this, touch);
    connect(lockAspectCheck_, &QCheckBox::toggled, this,
            [this] { markCustom(); });
    connect(interlaceCheck_, &QCheckBox::toggled, this, touch);
    connect(hideHiddenCheck_, &QCheckBox::toggled, this, touch);
    connect(pixelFormatCombo_, &QComboBox::currentIndexChanged, this, touch);
    connect(embedMetadataCheck_, &QCheckBox::toggled, this, touch);
    connect(transferCombo_, &QComboBox::currentIndexChanged, this, touch);
    connect(primariesCombo_, &QComboBox::currentIndexChanged, this, touch);
    connect(fullRangeCheck_, &QCheckBox::toggled, this, touch);
    connect(palettizedCheck_, &QCheckBox::toggled, this, touch);
    connect(paletteColorsCombo_, &QComboBox::currentIndexChanged, this, touch);
    connect(paletteDitherCheck_, &QCheckBox::toggled, this, touch);
    connect(limitSizeCheck_, &QCheckBox::toggled, this, [this, touch](bool on) {
        maxSizeSpin_->setEnabled(on);
        touch();
    });
    connect(maxSizeSpin_, &QDoubleSpinBox::valueChanged, this,
            [this, touch](double) { touch(); });
    connect(tiffCompressionCombo_, &QComboBox::currentIndexChanged, this,
            touch);
    connect(svgRasterizeCombo_, &QComboBox::currentIndexChanged, this, touch);
    connect(svgUseDocResCheck_, &QCheckBox::toggled, this,
            [this] { markCustom(); });
    connect(svgRasterDpiSpin_, &QSpinBox::valueChanged, this,
            [this] { markCustom(); });
    connect(svgDownsampleCheck_, &QCheckBox::toggled, this, touch);
    connect(svgAboveDpiCombo_, &QComboBox::currentIndexChanged, this, touch);
    connect(svgAllowJpegCheck_, &QCheckBox::toggled, this, touch);
    connect(svgQualityCombo_, &QComboBox::currentIndexChanged, this, touch);
    connect(svgDecimalCombo_, &QComboBox::currentIndexChanged, this,
            [this] { markCustom(); });
    connect(svgViewboxCheck_, &QCheckBox::toggled, this,
            [this] { markCustom(); });
    connect(svgBreaksCheck_, &QCheckBox::toggled, this,
            [this] { markCustom(); });

    return scroll;
}

}  // namespace pittore::ui
