// Diagnostic-only probe. No GUI, mesh import, slicing, or printer operations.
#include "field_fragments.hpp"
#include "SurfaceRecipeField.hpp"

#include <iostream>
#include <nlohmann/json.hpp>

using nlohmann::json;

static ImagemapProbe::TextureMappingOffsetWeightField field(bool raw_labels = false)
{
    ImagemapProbe::TextureMappingOffsetWeightField out;
    out.component_count = 4;
    out.bucket_width = out.bucket_height = 1;
    out.bucket_width_mm = out.bucket_height_mm = 0.22f;
    out.sample_x_mm                            = {0.0f, 0.1f};
    out.sample_y_mm                            = {0.0f, 0.0f};
    out.sample_weight                          = {1.0f, 1.0f};
    out.sample_normal_z                        = {1.0f, -1.0f};
    out.sample_has_surface_normal_z            = {1, 1};
    out.sample_component_weights               = {1, 0, 0, 0, 0, 1, 0, 0};
    out.buckets                                = {{0, 1}};
    out.fallback_weights                       = {0.25f, 0.25f, 0.25f, 0.25f};
    out.raw_top_surface_labels_from_texture    = raw_labels;
    return out;
}

static bool close(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::abs(a[i] - b[i]) > 0.00001f)
            return false;
    return true;
}

static json recipe_field(int component = 0)
{
    return {{"schema", "surfacemix.surface-recipe-field.fixture.v1"},
            {"SurfaceRecipeField",
             {{"coordinate_contract", {{"fixture_chart_is_planar", true}, {"curved_surface_mapping_implemented", false}}},
              {"stacks",
               {{{"stack_id", "a"},
                 {"calibration_content_sha256", "synthetic-probe-not-a-calibration"},
                 {"calibration",
                  {{"component_ids_surface_to_deep", {component, 1}}, {"intervals_um", {{0, 80}, {80, 160}}}, {"total_depth_um", 160}}}}}},
              {"cells",
               {{{"cell_id", "cell_a"},
                 {"material_region", "synthetic"},
                 {"chart_uv_bounds_um", {{0, 0}, {1000, 1000}}},
                 {"stack_selection", {{"selection", "whole_measured_stack"}, {"stack_id", "a"}}}},
                {{"cell_id", "cell_b"},
                 {"material_region", "synthetic"},
                 {"chart_uv_bounds_um", {{1000, 0}, {2000, 1000}}},
                 {"stack_selection", {{"selection", "whole_measured_stack"}, {"stack_id", "a"}}}}}},
              {"backing_core", {{"backing_depth_um", 320}, {"core_guard_depth_um", 640}}},
              {"transition_bands",
               {{{"left_cell_id", "cell_a"},
                 {"right_cell_id", "cell_b"},
                 {"chart_uv_bounds_um", {{400, 0}, {600, 1000}}},
                 {"rule", "unresolved_band"}}}}}}};
}

int main()
{
    json out    = {{"scope", "source-fragment and existing C++ contract probes; synthetic inputs only"},
                   {"product_mesh_loaded", false},
                   {"colour_assignment_enabled", false},
                   {"slicing_enabled", false},
                   {"printing_enabled", false}};
    json checks = json::array();
    auto check  = [&](const char* name, bool pass, json observed) {
        checks.push_back({{"name", name}, {"passed", pass}, {"observed", std::move(observed)}});
    };
    auto       f      = field();
    const auto hybrid = ImagemapProbe::sample_weight_field_components_impl(f, 0.05f, 0.0f, false);
    check("side_field_can_average_two_discrete_recipes", close(hybrid, {0.5f, 0.5f, 0, 0}), hybrid);

    f.sample_normal_z   = {-1.0f, 1.0f};
    const auto reversed = ImagemapProbe::sample_weight_field_components_impl(f, 0.05f, 0.0f, false);
    check("side_component_lookup_does_not_use_sample_normals_in_this_control", close(hybrid, reversed), reversed);

    f.sample_component_weights = {1, 0, 0, 0, 1, 0, 0, 0};
    const auto constant        = ImagemapProbe::sample_weight_field_components_impl(f, 0.05f, 0.0f, false);
    check("constant_recipe_control", close(constant, {1, 0, 0, 0}), constant);

    f                = field(true);
    const auto label = ImagemapProbe::sample_weight_field_components_impl(f, 0.0f, 0.0f, false);
    check("raw_top_labels_use_discrete_sample_in_this_control", close(label, {1, 0, 0, 0}), label);

    f.sample_x_mm.clear();
    const auto fallback = ImagemapProbe::sample_weight_field_components_impl(f, 0.0f, 0.0f, false);
    check("missing_samples_return_fallback_not_an_error", close(fallback, {0.25f, 0.25f, 0.25f, 0.25f}), fallback);

    std::string error;
    const auto  parsed  = Slic3r::SurfaceRecipeField::from_json(recipe_field(), &error);
    const bool  ordered = parsed && parsed->component_at_depth(parsed->cells.front(), 0) == 0 &&
                          parsed->component_at_depth(parsed->cells.front(), 79) == 0 &&
                          parsed->component_at_depth(parsed->cells.front(), 80) == 1 &&
                          !parsed->component_at_depth(parsed->cells.front(), 160).has_value();
    check("existing_contract_preserves_valid_interval_order_and_component_zero", ordered, {{"error", error}});

    auto partial                                                                = recipe_field();
    partial["SurfaceRecipeField"]["stacks"][0]["calibration"]["total_depth_um"] = 200;
    error.clear();
    const auto partial_result = Slic3r::SurfaceRecipeField::from_json(partial, &error);
    check("existing_contract_rejects_incomplete_depth", !partial_result, {{"error", error}});

    json findings = json::array();
    for (int invalid : {256, -256}) {
        error.clear();
        const auto invalid_result = Slic3r::SurfaceRecipeField::from_json(recipe_field(invalid), &error);
        findings.push_back(
            {{"name", "component_id_range_before_narrowing"},
             {"input_component", invalid},
             {"strict_contract_satisfied", !invalid_result.has_value()},
             {"accepted", invalid_result.has_value()},
             {"decoded_component", invalid_result ? int(invalid_result->stacks.front().surface_to_deep.front().component) : -1},
             {"error", error}});
    }
    findings.push_back({{"name", "transition_band_preservation"},
                        {"input_transition_bands", 1},
                        {"decoded_transition_bands", parsed ? parsed->transition_bands.size() : 0},
                        {"strict_contract_satisfied", parsed && parsed->transition_bands.size() == 1}});
    out["mechanism_checks"]               = checks;
    out["contract_findings"]              = findings;
    out["mechanism_checks_passed"]        = std::all_of(checks.begin(), checks.end(), [](const auto& c) { return c.at("passed") == true; });
    out["all_strict_contracts_satisfied"] = std::all_of(findings.begin(), findings.end(),
                                                        [](const auto& c) { return c.at("strict_contract_satisfied") == true; });
    std::cout << out.dump(2) << '\n';
    return out["mechanism_checks_passed"] == true ? 0 : 1;
}
