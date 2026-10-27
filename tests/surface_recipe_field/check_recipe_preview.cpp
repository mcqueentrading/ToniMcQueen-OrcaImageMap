#include <libslic3r/SurfaceMixRecipePreview.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
    using namespace Slic3r;
    try {
        if (argc != 2)
            throw std::runtime_error("usage: surface_mix_recipe_preview_check calibration-audit.json");
        std::ifstream input(argv[1]);
        if (!input)
            throw std::runtime_error("cannot read calibration audit");
        const auto  json = nlohmann::json::parse(input);
        std::string error;
        const auto  preview = SurfaceMixRecipePreview::from_json(json, &error);
        if (!preview)
            throw std::runtime_error(error);
        size_t intervals = 0;
        for (size_t i = 0; i < preview->recipes().size(); ++i) {
            const auto&  recipe  = preview->recipes()[i];
            const size_t nearest = preview->nearest_recipe(recipe.measured_rgb);
            if (preview->recipes()[nearest].measured_rgb != recipe.measured_rgb)
                throw std::runtime_error("exact measured RGB did not resolve to an exact measured recipe");
            double start = 0.0;
            for (size_t layer = 0; layer < recipe.surface_to_deep.size(); ++layer) {
                if (preview->component_at_depth(i, start + recipe.layer_heights_mm[layer] * 0.5) != recipe.surface_to_deep[layer])
                    throw std::runtime_error("ordered interval lookup mismatch");
                start += recipe.layer_heights_mm[layer];
                ++intervals;
            }
        }
        std::cout << nlohmann::json({{"source", argv[1]},
                                     {"top_records_retained_including_mirrors", preview->recipes().size()},
                                     {"ordered_intervals_checked", intervals},
                                     {"maximum_depth_mm", preview->maximum_depth_mm()},
                                     {"assumed_normalized_srgb", preview->assumes_srgb()},
                                     {"geometry_modified", false},
                                     {"executable_assignment_enabled", false},
                                     {"physical_colour_validated", false},
                                     {"passed", true}})
                         .dump(2)
                  << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
