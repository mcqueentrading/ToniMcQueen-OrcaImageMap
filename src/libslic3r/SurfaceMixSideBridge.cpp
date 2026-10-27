#include "SurfaceMixSideBridge.hpp"
#include <algorithm>
#include <cmath>

namespace Slic3r {
std::optional<ColorSolverCandidateSet> surface_mix_side_candidates(const TextureMappingZone&        zone,
                                                                   const std::vector<unsigned int>& ids,
                                                                   std::string*                     error)
{
    auto decoded = SurfaceMixSideOffsets::from_calibration_json(zone.side_surface_color_calibration_json, ids, error);
    if (!decoded)
        return std::nullopt;
    if (!zone.is_image_texture() || zone.texture_mapping_mode != int(TextureMappingZone::TextureMappingFilamentBlending) ||
        zone.transmission_distance_calibration_mode != int(TextureMappingZone::TDCalibrationCalibratedNearestMeasuredSample) ||
        zone.dithering_enabled || zone.compact_offset_mode || !std::isfinite(zone.tone_gamma) ||
        std::abs(zone.tone_gamma - 1.f) > 1e-6f) {
        if (error)
            *error = "SurfaceMix side offsets require target colour, measured side mode, unit gamma and no compact offsets/dithering";
        return std::nullopt;
    }
    ColorSolverCandidateSet result;
    result.component_count = ids.size();
    for (const auto& recipe : decoded->recipes()) {
        const std::array<float, 3> rgb{{float(recipe.measured_rgb[0]), float(recipe.measured_rgb[1]), float(recipe.measured_rgb[2])}};
        const auto                 perceptual = color_solver_oklab_from_srgb(rgb);
        result.rgbs.insert(result.rgbs.end(), rgb.begin(), rgb.end());
        result.perceptual_coords.insert(result.perceptual_coords.end(), perceptual.begin(), perceptual.end());
        for (unsigned int id : ids) {
            const auto slot = std::find(decoded->slots().begin(), decoded->slots().end(), id);
            result.weights.push_back(float(recipe.amplitudes[size_t(slot - decoded->slots().begin())]));
        }
    }
    build_color_solver_candidate_kd_trees(result);
    return result;
}
} // namespace Slic3r
