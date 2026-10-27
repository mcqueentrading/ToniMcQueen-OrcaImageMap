#include <catch2/catch_all.hpp>

#include <libslic3r/SurfaceRecipeField.hpp>
#include <nlohmann/json.hpp>

#include <limits>

using namespace Slic3r;
using nlohmann::json;

namespace {
json fixture(bool hybrid = false)
{
    return {{"schema", "surfacemix.surface-recipe-field.fixture.v1"},
            {"SurfaceRecipeField",
             {{"schema", "surfacemix.surface-recipe-field.fixture.v1"},
              {"coordinate_contract", {{"fixture_chart_is_planar", true}, {"curved_surface_mapping_implemented", false}}},
              {"stacks",
               {{{"stack_id", "stack_a"},
                 {"calibration_content_sha256", "abc"},
                 {"calibration",
                  {{"component_ids_surface_to_deep", {1, 2}}, {"intervals_um", {{0, 80}, {80, 160}}}, {"total_depth_um", 160}}}}}},
              {"cells",
               {{{"cell_id", "cell_a"},
                 {"chart_uv_bounds_um", {{0, 0}, {1000, 1000}}},
                 {"stack_selection", hybrid ? json{{"selection", "uncalibrated_hybrid"}} :
                                              json{{"selection", "whole_measured_stack"}, {"stack_id", "stack_a"}}}}}}}},
            {"backing_core", {{"backing_depth_um", 320}, {"core_guard_depth_um", 640}}}};
}

json field_with_transition(const char* rule = "hard_boundary")
{
    auto  input                     = fixture();
    auto& field                     = input["SurfaceRecipeField"];
    auto  neighbour                 = field["cells"][0];
    neighbour["cell_id"]            = "cell_b";
    neighbour["chart_uv_bounds_um"] = {{1000, 0}, {2000, 1000}};
    field["cells"].push_back(neighbour);
    field["transition_bands"] = {
        {{"left_cell_id", "cell_a"}, {"right_cell_id", "cell_b"}, {"chart_uv_bounds_um", {{1000, 0}, {1000, 1000}}}, {"rule", rule}}};
    if (std::string(rule) == "unresolved_band")
        field["transition_bands"][0]["chart_uv_bounds_um"] = {{950, 0}, {1050, 1000}};
    return input;
}
} // namespace

TEST_CASE("SurfaceRecipeField preserves complete ordered stack", "[SurfaceRecipeField]")
{
    std::string error;
    auto        field = SurfaceRecipeField::from_json(fixture(), &error);
    REQUIRE(field.has_value());
    REQUIRE(error.empty());
    REQUIRE(field->planar_fixture_only);
    REQUIRE(field->component_at_depth(field->cells.front(), 0) == 1);
    REQUIRE(field->component_at_depth(field->cells.front(), 80) == 2);
    REQUIRE_FALSE(field->component_at_depth(field->cells.front(), 160).has_value());
}

TEST_CASE("SurfaceRecipeField rejects uncalibrated hybrids", "[SurfaceRecipeField]")
{
    std::string error;
    REQUIRE_FALSE(SurfaceRecipeField::from_json(fixture(true), &error).has_value());
    REQUIRE(error.find("uncalibrated hybrids") != std::string::npos);
}

TEST_CASE("SurfaceRecipeField validates components before narrowing", "[SurfaceRecipeField]")
{
    for (const json& invalid : {json(-256), json(-1), json(4), json(256), json(512), json(std::numeric_limits<uint64_t>::max()), json(0.5),
                                json(true), json("0")}) {
        CAPTURE(invalid);
        auto input                                                                                  = fixture();
        input["SurfaceRecipeField"]["stacks"][0]["calibration"]["component_ids_surface_to_deep"][0] = invalid;
        std::string                       error;
        std::optional<SurfaceRecipeField> field;
        REQUIRE_NOTHROW(field = SurfaceRecipeField::from_json(input, &error));
        REQUIRE_FALSE(field);
        REQUIRE_FALSE(error.empty());
    }
}

TEST_CASE("SurfaceRecipeField retains all four canonical components", "[SurfaceRecipeField]")
{
    for (int component = 0; component < 4; ++component) {
        auto input                                                                                  = fixture();
        input["SurfaceRecipeField"]["stacks"][0]["calibration"]["component_ids_surface_to_deep"][0] = component;
        std::string error                                                                           = "stale error";
        const auto  field = SurfaceRecipeField::from_json(input, &error);
        REQUIRE(field);
        REQUIRE(error.empty());
        REQUIRE(field->component_at_depth(field->cells.front(), 0) == component);
    }
}

TEST_CASE("SurfaceRecipeField preserves hard and unresolved transitions", "[SurfaceRecipeField]")
{
    for (const char* rule : {"hard_boundary", "unresolved_band"}) {
        CAPTURE(rule);
        const auto input = field_with_transition(rule);
        const auto field = SurfaceRecipeField::from_json(input);
        REQUIRE(field);
        REQUIRE(field->transition_bands.size() == 1);
        const auto& band = field->transition_bands.front();
        REQUIRE(band.left_cell_id == "cell_a");
        REQUIRE(band.right_cell_id == "cell_b");
        REQUIRE(band.rule == (std::string(rule) == "hard_boundary" ? SurfaceRecipeBoundaryRule::HardBoundary :
                                                                     SurfaceRecipeBoundaryRule::UnresolvedBand));
        const auto& expected = input["SurfaceRecipeField"]["transition_bands"][0]["chart_uv_bounds_um"];
        REQUIRE(band.chart_bounds_um[0][0] == expected[0][0].get<int64_t>());
        REQUIRE(band.chart_bounds_um[1][0] == expected[1][0].get<int64_t>());
        REQUIRE(band.chart_bounds_um[1][1] == 1000);
        REQUIRE(field->component_at_depth(field->cells.front(), 0) == 1);
        REQUIRE(field->component_at_depth(field->cells.back(), 80) == 2);
    }
}

TEST_CASE("SurfaceRecipeField refuses malformed transitions", "[SurfaceRecipeField]")
{
    for (int mode = 0; mode < 8; ++mode) {
        CAPTURE(mode);
        auto  input = field_with_transition();
        auto& band  = input["SurfaceRecipeField"]["transition_bands"][0];
        switch (mode) {
        case 0: band["rule"] = "guess_a_mix"; break;
        case 1: band["left_cell_id"] = "missing"; break;
        case 2: band["right_cell_id"] = "cell_a"; break;
        case 3: band["chart_uv_bounds_um"] = {{1100, 0}, {1000, 1000}}; break;
        case 4: band["rule"] = "unresolved_band"; break; // A positive-area band is required.
        case 5: input["SurfaceRecipeField"]["transition_bands"] = json::object(); break;
        case 6: band = nullptr; break;
        case 7: band.erase("rule"); break;
        }
        std::string                       error;
        std::optional<SurfaceRecipeField> field;
        REQUIRE_NOTHROW(field = SurfaceRecipeField::from_json(input, &error));
        REQUIRE_FALSE(field);
        REQUIRE_FALSE(error.empty());
    }
}

TEST_CASE("SurfaceRecipeField preserves backing without narrowing components", "[SurfaceRecipeField]")
{
    auto input = fixture();
    auto field = SurfaceRecipeField::from_json(input);
    REQUIRE(field);
    REQUIRE(field->backing_core.backing_depth_um == 320);
    REQUIRE(field->backing_core.core_guard_depth_um == 640);

    input["SurfaceRecipeField"]["backing_core"] = input["backing_core"];
    input.erase("backing_core");
    field = SurfaceRecipeField::from_json(input);
    REQUIRE(field);
    REQUIRE(field->backing_core.backing_depth_um == 320);
    for (const char* key : {"backing_component", "structural_core_component"}) {
        auto invalid                                       = input;
        invalid["SurfaceRecipeField"]["backing_core"][key] = 256;
        std::string error;
        REQUIRE_FALSE(SurfaceRecipeField::from_json(invalid, &error));
        REQUIRE_FALSE(error.empty());
    }
}

TEST_CASE("SurfaceRecipeField rejects conflicting backing sources", "[SurfaceRecipeField]")
{
    auto input                                                      = fixture();
    input["SurfaceRecipeField"]["backing_core"]                     = input["backing_core"];
    input["SurfaceRecipeField"]["backing_core"]["backing_depth_um"] = 400;
    std::string error;
    REQUIRE_FALSE(SurfaceRecipeField::from_json(input, &error));
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("SurfaceRecipeField rejects integer overflow in physical fields", "[SurfaceRecipeField]")
{
    for (int mode = 0; mode < 3; ++mode) {
        auto       input    = fixture();
        const auto overflow = std::numeric_limits<uint64_t>::max();
        if (mode == 0)
            input["SurfaceRecipeField"]["stacks"][0]["calibration"]["intervals_um"][0][1] = overflow;
        else if (mode == 1)
            input["SurfaceRecipeField"]["cells"][0]["chart_uv_bounds_um"][0][0] = overflow;
        else
            input["backing_core"]["backing_depth_um"] = overflow;
        std::string error;
        REQUIRE_FALSE(SurfaceRecipeField::from_json(input, &error));
        REQUIRE_FALSE(error.empty());
    }
}

TEST_CASE("SurfaceRecipeField rejects malformed structures without throwing", "[SurfaceRecipeField]")
{
    for (int mode = 0; mode < 6; ++mode) {
        auto  input = fixture();
        auto& field = input["SurfaceRecipeField"];
        switch (mode) {
        case 0: field["stacks"] = nullptr; break;
        case 1: field["cells"] = json::object(); break;
        case 2: field["coordinate_contract"] = "not-an-object"; break;
        case 3: field["cells"][0]["stack_selection"] = false; break;
        case 4: field["stacks"][0]["stack_id"] = 42; break;
        case 5: field["coordinate_contract"]["fixture_chart_is_planar"] = "true"; break;
        }
        std::string                       error;
        std::optional<SurfaceRecipeField> result;
        REQUIRE_NOTHROW(result = SurfaceRecipeField::from_json(input, &error));
        REQUIRE_FALSE(result);
        REQUIRE_FALSE(error.empty());
    }
}

TEST_CASE("SurfaceRecipeField rejects unknown or conflicting schemas", "[SurfaceRecipeField]")
{
    auto input      = fixture();
    input["schema"] = "surfacemix.future.v99";
    REQUIRE_FALSE(SurfaceRecipeField::from_json(input));
    input                                 = fixture();
    input["SurfaceRecipeField"]["schema"] = "surfacemix.future.v99";
    REQUIRE_FALSE(SurfaceRecipeField::from_json(input));
    input = fixture()["SurfaceRecipeField"];
    REQUIRE(SurfaceRecipeField::from_json(input));
}

TEST_CASE("SurfaceRecipeField rejects incomplete ordered depth", "[SurfaceRecipeField]")
{
    auto input                                                                = fixture();
    input["SurfaceRecipeField"]["stacks"][0]["calibration"]["total_depth_um"] = 200;
    REQUIRE_FALSE(SurfaceRecipeField::from_json(input));
}
