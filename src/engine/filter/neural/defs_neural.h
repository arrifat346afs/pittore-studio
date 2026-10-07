#pragma once
// Neural filter declarations. Split from engine/filter/filters.h.
#include <vector>

#include "engine/filter/core/filter_types.h"

namespace pittore::filter {

inline std::vector<FilterDef> NeuralFilterDefs() {
    return {
        {"neural.colorize", "Neural Colorize", "Neural", {{{"warmth", "Warmth", -100, 100, 15, 0, {}, ""}, {"strength", "Strength", 0, 100, 60, 0, {}, " %"}}}},
        {"neural.color_transfer", "Neural Color Transfer", "Neural", {{{"hue", "Hue Shift", -180, 180, 0, 0, {}, " deg"}, {"strength", "Strength", 0, 100, 60, 0, {}, " %"}, {"contrast", "Contrast", 0, 100, 30, 0, {}, " %"}}}},
        {"neural.depth_blur", "Neural Depth Blur", "Neural", {{{"focus", "Focus Depth", 0, 100, 50, 0, {}, " %"}, {"blur", "Blur", 0, 50, 12, 0, {}, " px"}}}},
        {"neural.face_to_caricature", "Face Caricature", "Neural", {{{"eyes", "Eyes", 0, 100, 40, 0, {}, " %"}, {"mouth", "Mouth", 0, 100, 40, 0, {}, " %"}, {"head", "Head Size", 0, 100, 40, 0, {}, " %"}}}},
        {"neural.harmonization", "Harmonization", "Neural", {{{"strength", "Strength", 0, 100, 60, 0, {}, " %"}, {"tone", "Tone Match", 0, 100, 60, 0, {}, " %"}}}},
        {"neural.jpeg_artifacts", "JPEG Artifact Removal", "Neural", {{{"strength", "Strength", 0, 100, 60, 0, {}, " %"}}}},
        {"neural.landscape_mixer", "Landscape Mixer", "Neural", {{{"strength", "Strength", 0, 100, 50, 0, {}, " %"}, {"bands", "Detail Bands", 1, 8, 4, 0, {}, ""}}}},
        {"neural.makeup_transfer", "Makeup Transfer", "Neural", {{{"lips", "Lips", 0, 100, 50, 0, {}, " %"}, {"eyes", "Eyes", 0, 100, 50, 0, {}, " %"}, {"skin", "Skin Soften", 0, 100, 40, 0, {}, " %"}}}},
        {"neural.photo_restoration", "Photo Restoration", "Neural", {{{"enhance", "Enhance", 0, 100, 60, 0, {}, " %"}, {"scratches", "Scratch Removal", 0, 100, 40, 0, {}, " %"}, {"tone", "Tone Balance", 0, 100, 50, 0, {}, " %"}}}},
        {"neural.photo_to_sketch", "Photo to Sketch", "Neural", {{{"weight", "Line Weight", 1, 20, 5, 0, {}, ""}, {"shading", "Shading", 0, 100, 50, 0, {}, " %"}}}},
        {"neural.sketch_to_portrait", "Sketch to Portrait", "Neural", {{{"detail", "Detail", 0, 100, 50, 0, {}, " %"}}}},
        {"neural.skin_smoothing", "Skin Smoothing", "Neural", {{{"blur", "Blur", 0, 100, 40, 0, {}, " %"}, {"detail", "Detail", 0, 100, 60, 0, {}, " %"}}}},
        {"neural.smart_portrait", "Smart Portrait", "Neural", {{{"happiness", "Happiness", -100, 100, 0, 0, {}, ""}, {"surprise", "Surprise", -100, 100, 0, 0, {}, ""}, {"age", "Age", -100, 100, 0, 0, {}, ""}, {"gaze", "Gaze", -100, 100, 0, 0, {}, ""}, {"light", "Light Direction", -100, 100, 20, 0, {}, ""}}}},
        {"neural.style_transfer", "Style Transfer", "Neural", {{{"style", "Style", 0, 2, 0, 1, {"Mosaic", "Candy", "Udnie"}, ""}, {"strength", "Strength", 0, 100, 70, 0, {}, " %"}}}},
        {"neural.super_zoom", "Super Zoom", "Neural", {{{"factor", "Zoom Factor", 2, 4, 2, 0, {}, " x"}}}},
    };
}

}  // namespace pittore::filter
