#include "libslic3r/ConfigVariantSelection.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PerimeterGenerator.hpp"
#include "libslic3r/SurfaceMixRecipePreview.hpp"
#include "libslic3r/SurfaceMixTopStack.hpp"
#include "libslic3r/SurfaceMixSideBridge.hpp"
#include "libslic3r/TextureMappingContoning.hpp"
#include "slic3r/GUI/SurfaceMixRecipePreviewPanel.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace Slic3r;
using nlohmann::json;

namespace {
size_t checks = 0;
void   check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
    ++checks;
}

void nullable_checks()
{
    ConfigOptionBoolsNullable flags;
    flags.deserialize("nil,1,0");
    check(dynamic_cast<ConfigOptionBools*>(static_cast<ConfigOption*>(&flags)) == nullptr, "nullable bool is a distinct dynamic type");
    select_nullable_config_variants(flags, {2, 0, 1});
    check(flags.nullable() && flags.serialize() == "0,nil,1", "boolean nil permutation");
    ConfigOptionFloatsNullable floats;
    floats.deserialize("nil,0.125,0.9");
    select_nullable_config_variants(floats, {2, 0, 1});
    check(floats.values[0] == 0.9 && floats.is_nil(1) && floats.values[2] == 0.125, "float nil permutation");
    ConfigOptionIntsNullable ints;
    ints.deserialize("nil,12,7");
    select_nullable_config_variants(ints, {2, 0, 1});
    check(ints.serialize() == "7,nil,12", "integer nil permutation");
    ConfigOptionPercentsNullable percents;
    percents.deserialize("nil,42.5");
    select_nullable_config_variants(percents, {1, 0});
    check(percents.values[0] == 42.5 && percents.is_nil(1), "percent nil permutation");
    ConfigOptionFloatsOrPercentsNullable mixed;
    mixed.deserialize("nil,42.5%,0.08");
    select_nullable_config_variants(mixed, {1, 0, 2});
    check(mixed.values[0].percent && mixed.values[0].value == 42.5 && mixed.is_nil(1) && !mixed.values[2].percent &&
              mixed.values[2].value == 0.08,
          "mixed units and nil permutation");
    ConfigOptionBoolsNullable singleton;
    singleton.deserialize("nil");
    select_nullable_config_variants(singleton, {0, 8});
    check(singleton.serialize() == "nil,nil", "existing front-value fallback preserves nil");
    const auto before   = flags.serialize();
    bool       rejected = false;
    try {
        select_nullable_config_variants(flags, {-1});
    } catch (const ConfigurationError&) {
        rejected = true;
    }
    check(rejected && flags.serialize() == before, "invalid index leaves source unchanged");
    ConfigOptionBoolsNullable empty;
    rejected = false;
    try {
        select_nullable_config_variants(empty, {0});
    } catch (const ConfigurationError&) {
        rejected = true;
    }
    check(rejected, "empty nullable input rejected");
}

void one_wall_infill_checks()
{
    PrintConfig       print;
    PrintObjectConfig object;
    PrintRegionConfig region;
    region.wall_loops.value            = 2;
    region.wall_filament.value         = 1;
    region.sparse_infill_density.value = 100.;
    region.only_one_wall_top.value     = false;
    const ExPolygon   rectangle(Polygon(
        {Point(scale_(0.), scale_(0.)), Point(scale_(12.), scale_(0.)), Point(scale_(12.), scale_(8.)), Point(scale_(0.), scale_(8.))}));
    SurfaceCollection slices;
    slices.append(ExPolygons{rectangle}, stInternal);
    const ExPolygons          shell{rectangle};
    LayerRegionPtrs           compatible;
    ExtrusionEntityCollection walls, gaps;
    SurfaceCollection         fills;
    ExPolygons                no_overlap;
    const Flow                flow = Flow::new_from_config_width(frPerimeter, ConfigOptionFloatOrPercent(0.42, false), 0.4, 0.08);
    PerimeterGenerator        generator(&slices, &compatible, 0.08, 0.04, flow, &region, &object, &print, false, &walls, &gaps, &fills,
                                        &no_overlap);
    generator.layer_id                        = 22;
    generator.lower_slices                    = &shell;
    generator.upper_slices                    = &shell;
    generator.upper_slices_same_region        = &slices;
    generator.contoning_one_wall_shell_infill = &shell;
    generator.process_classic();
    double area = 0.;
    for (const Surface& surface : fills.surfaces)
        area += unscale_(unscale_(surface.expolygon.area()));
    check(!walls.entities.empty(), "one-wall shell retains perimeter output");
    check(area > 60. && area < 96., "aliased one-wall split retains interior fill area");
}
} // namespace

void side_checks(const std::string& top_path)
{
    json catalogue;
    std::ifstream(std::filesystem::path(top_path).parent_path() / "creator5-cmyw-side-offsets.v1.json") >> catalogue;
    std::string error;
    auto        decoded = SurfaceMixSideOffsets::from_catalogue(catalogue, &error);
    check(bool(decoded) && decoded->recipes().size() == 1680, "all side records decode including mirrors");
    TextureMappingZone zone;
    zone.side_surface_color_calibration_json = "{\"preserved\":17}";
    const auto                     original  = zone;
    GUI::SurfaceMixSideOffsetDraft draft;
    draft.changed = draft.enabled = draft.assume_normalized_srgb = true;
    draft.filament_slots                                         = {{3, 1, 4, 2}};
    draft.catalogue_json                                         = catalogue.dump();
    check(GUI::apply_surface_mix_side_offset_draft(zone, draft, {1, 2, 3, 4}, &error), "side UI draft applies");
    check(zone.top_surface_color_calibration_json == original.top_surface_color_calibration_json, "side import leaves top data unchanged");
    check(json::parse(zone.side_surface_color_calibration_json).at("preserved") == 17, "side import preserves original metadata");
    check(bool(texture_mapping_parse_side_surface_color_calibration(zone.side_surface_color_calibration_json)),
          "side profile remains recognized by ImageMap UI");
    auto candidates = surface_mix_side_candidates(zone, {1, 2, 3, 4}, &error);
    check(bool(candidates) && candidates->weights.size() == 1680 * 4, "actual wall candidate bridge is populated");
    const size_t canonical_by_slot[] = {1, 3, 0, 2};
    for (size_t row = 0; row < decoded->recipes().size(); ++row)
        for (size_t slot = 0; slot < 4; ++slot)
            check(std::abs(candidates->weights[row * 4 + slot] - decoded->recipes()[row].amplitudes[canonical_by_slot[slot]]) < 1e-7,
                  "wall amplitudes follow explicit slots without normalization");
    const auto before  = zone.side_surface_color_calibration_json;
    auto       bad     = draft;
    bad.filament_slots = {{1, 1, 3, 4}};
    check(!GUI::apply_surface_mix_side_offset_draft(zone, bad, {1, 2, 3, 4}, &error) && zone.side_surface_color_calibration_json == before,
          "invalid side draft is atomic");
    auto dithered              = zone;
    dithered.dithering_enabled = true;
    check(!surface_mix_side_candidates(dithered, {1, 2, 3, 4}, &error) && !error.empty(), "unmeasured side hybrids have no fallback");
    auto invalid_gamma       = zone;
    invalid_gamma.tone_gamma = std::numeric_limits<float>::quiet_NaN();
    check(!surface_mix_side_candidates(invalid_gamma, {1, 2, 3, 4}, &error) && !error.empty(), "nonfinite side gamma is rejected");
    draft.enabled = false;
    check(GUI::apply_surface_mix_side_offset_draft(zone, draft, {1, 2, 3, 4}, &error), "side disable succeeds");
    check(zone.side_surface_color_calibration_json == original.side_surface_color_calibration_json &&
              zone.transmission_distance_calibration_mode == original.transmission_distance_calibration_mode,
          "side disable restores original calibration and mode");
}

int main(int argc, char** argv)
{
    try {
        if (argc != 2)
            throw std::runtime_error("requires bundled catalogue path");
        nullable_checks();
        one_wall_infill_checks();
        side_checks(argv[1]);
        json catalogue;
        std::ifstream(argv[1]) >> catalogue;
        auto preview = SurfaceMixRecipePreview::from_json(catalogue);
        check(bool(preview), "actual catalogue decodes");
        TextureMappingZone zone;
        zone.top_surface_color_calibration_json = "{\"preserved_test_field\":17}";
        const auto                   original   = zone;
        GUI::SurfaceMixTopStackDraft draft;
        draft.changed                = true;
        draft.enabled                = true;
        draft.assume_normalized_srgb = true;
        draft.catalogue_json         = catalogue.dump();
        std::string error;
        check(GUI::apply_surface_mix_top_stack_draft(zone, draft, {1, 2, 3, 4}, &error), "native UI draft applies transactionally");
        check(zone.top_surface_image_printing_enabled && zone.top_surface_contoning_stack_layers == 19, "complete top mode enabled");
        check(json::parse(zone.top_surface_color_calibration_json).at("preserved_test_field") == 17,
              "prior calibration metadata preserved");
        PrintConfig config;
        config.filament_colour.values = {"#00AEEF", "#EC008C", "#FFF200", "#FFFFFF"};
        TextureMappingContoningSolver solver(zone, config, {1, 2, 3, 4}, 0.08f);
        check(solver.valid() && solver.surface_mix_top_stack_mode(), "real ImageMap solver uses SurfaceMix transport");
        std::vector<float> heights(19, 0.08f);
        std::vector<int>   ids;
        for (int i = 0; i < 19; ++i)
            ids.push_back(40 - i);
        for (const auto& record : preview->recipes()) {
            const std::array<float, 3> target{float(record.measured_rgb[0]), float(record.measured_rgb[1]), float(record.measured_rgb[2])};
            const auto                 selected = preview->nearest_recipe({target[0], target[1], target[2]});
            const auto                 stack    = solver.solve(target, 19, false, 19, heights, ids);
            check(stack.bottom_to_top.size() == 19 && stack.rgb.has_value(), "native solver returns complete measured stack");
            const auto& expected = preview->recipes()[selected];
            for (size_t i = 0; i < 19; ++i)
                check(stack.bottom_to_top[i] == expected.surface_to_deep[18 - i] + 1u, "native solver preserves exact component interval");
        }
        auto reject_solve = [&](int count, const std::vector<float>& actual) {
            try {
                solver.solve({0.5f, 0.5f, 0.5f}, count, false, count, actual, ids);
            } catch (const std::exception&) {
                return true;
            }
            return false;
        };
        check(reject_solve(18, heights), "native short stack stops");
        auto wrong = heights;
        wrong[7]   = 0.16f;
        check(reject_solve(19, wrong), "native mismatched layer height stops");
        check(reject_solve(19, {}), "native missing physical intervals stop");
        check(!solver.nearest_measured_sample_fallback_used(), "no generic fallback credited");
        const auto active    = zone.top_surface_color_calibration_json;
        draft.filament_slots = {1, 1, 3, 4};
        check(!GUI::apply_surface_mix_top_stack_draft(zone, draft, {1, 2, 3, 4}, &error), "duplicate slot draft rejected");
        check(zone.top_surface_color_calibration_json == active, "rejected draft leaves active zone unchanged");
        draft.enabled = false;
        check(GUI::apply_surface_mix_top_stack_draft(zone, draft, {1, 2, 3, 4}, &error), "native disable succeeds");
        check(zone.top_surface_image_printing_enabled == original.top_surface_image_printing_enabled &&
                  zone.top_surface_contoning_stack_layers == original.top_surface_contoning_stack_layers &&
                  zone.top_surface_contoning_pattern_filaments == original.top_surface_contoning_pattern_filaments,
              "disable restores prior top settings");
        check(json::parse(zone.top_surface_color_calibration_json) == json::parse(original.top_surface_color_calibration_json),
              "disable restores previous calibration content");
        auto imported_transport = json::parse(active);
        imported_transport[SurfaceMixTopStack::transport_key].erase("previous_top_settings");
        zone.top_surface_image_printing_enabled = true;
        zone.top_surface_color_calibration_json = imported_transport.dump();
        check(GUI::apply_surface_mix_top_stack_draft(zone, draft, {1, 2, 3, 4}, &error) && !zone.top_surface_image_printing_enabled,
              "disabling an imported transport without backup does not activate generic painting");
        std::cout << json{{"checks_passed", checks},
                          {"catalogue_records", preview->recipes().size()},
                          {"side_catalogue_records", 1680},
                          {"scope", "linked native transport and transaction checks, not full path coverage or physical colour"},
                          {"physical_colour_validated", false},
                          {"printing_enabled", false}}
                         .dump(2)
                  << '\n';
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
