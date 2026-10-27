#include <catch2/catch_all.hpp>
#include <libslic3r/SurfaceMixTopStack.hpp>
#include <nlohmann/json.hpp>

#include <limits>

using namespace Slic3r;
using nlohmann::json;

namespace {
json transport()
{
    json records = json::array();
    for (size_t i = 0; i < 2; ++i)
        records.push_back(
            {{"source_role", "top"},
             {"source_pointer", "/samples/" + std::to_string(i)},
             {"sample_id", "sample_" + std::to_string(i)},
             {"statuses", {{"decoding", {{"state", "pass"}}}}},
             {"measured_rgb",
              {{"channels", i == 0 ? json{0.0, 0.9, 0.9} : json{0.9, 0.0, 0.9}}, {"encoding", "srgb"}, {"channel_range", {0.0, 1.0}}}},
             {"recipe",
              {{"kind", "top_stack"},
               {"surface_to_deep", i == 0 ? json{"cyan", "magenta", "yellow", "white"} : json{"magenta", "cyan", "yellow", "white"}},
               {"source_stack_direction", "surface_to_deep"},
               {"layer_heights_mm", {0.08, 0.08, 0.16, 0.08}},
               {"total_depth_mm", 0.40}}}});
    return {{SurfaceMixTopStack::transport_key,
             {{"schema", "surfacemix.imagemap-top-stacks.v1"},
              {"enabled", true},
              {"experimental", true},
              {"coordinate_mode", "build_z_top"},
              {"canonical_to_filament_slot", {3, 1, 4, 2}},
              {"catalogue",
               {{"schema_version", "surfacemix.calibration-catalog.v1"},
                {"canonical_component_order", {"cyan", "magenta", "yellow", "white"}},
                {"top_depth_units", "mm"},
                {"executable_assignment_enabled", false},
                {"sources", {{{"role", "top"}, {"source_sha256", std::string(64, 'a')}, {"records", records}}}}}}}}};
}

auto adapter(const json& input) { return SurfaceMixTopStack::from_calibration_json(input.dump(), {1, 2, 3, 4}); }
} // namespace

TEST_CASE("SurfaceMix top transport is opt-in and preserves its source", "[SurfaceMixTopStack]")
{
    std::string error = "stale";
    REQUIRE_FALSE(SurfaceMixTopStack::from_calibration_json("{}", {1, 2, 3, 4}, &error));
    REQUIRE(error.empty());
    auto       input  = transport();
    const auto before = input.dump();
    REQUIRE(adapter(input));
    REQUIRE(input.dump() == before);
    input[SurfaceMixTopStack::transport_key]["enabled"] = false;
    REQUIRE_FALSE(adapter(input));
}

TEST_CASE("SurfaceMix top transport keeps order and explicitly maps component zero", "[SurfaceMixTopStack]")
{
    auto top = adapter(transport());
    REQUIRE(top);
    REQUIRE(top->layer_count() == 4);
    REQUIRE(top->bottom_to_top({0.f, 0.9f, 0.9f}) == std::vector<unsigned int>{2, 4, 1, 3});
    REQUIRE(top->bottom_to_top({0.9f, 0.f, 0.9f}) == std::vector<unsigned int>{2, 4, 3, 1});
    REQUIRE(top->measured_stack_rgb({2, 4, 1, 3}));
    REQUIRE_FALSE(top->measured_stack_rgb({2, 4, 3, 3}));
    REQUIRE_THROWS(top->bottom_to_top({-0.1f, 0.f, 0.f}));
}

TEST_CASE("SurfaceMix top execution requires complete actual intervals", "[SurfaceMixTopStack]")
{
    const auto top = adapter(transport());
    REQUIRE(top);
    const std::vector<float> heights{0.08f, 0.08f, 0.16f, 0.08f};
    const std::vector<int>   ids{9, 8, 7, 6};
    REQUIRE(top->compatible(4, 4, heights, ids, false));
    REQUIRE_FALSE(top->compatible(3, 4, heights, ids, false));
    REQUIRE_FALSE(top->compatible(4, 3, heights, ids, false));
    REQUIRE_FALSE(top->compatible(4, 4, heights, ids, true));
    REQUIRE_FALSE(top->compatible(4, 4, {}, ids, false));
    REQUIRE_FALSE(top->compatible(4, 4, heights, {}, false));
    REQUIRE_FALSE(top->compatible(4, 4, {0.08f, 0.16f, 0.08f, 0.08f}, ids, false));
    REQUIRE_FALSE(top->compatible(4, 4, heights, {9, 8, 6, 5}, false));
    REQUIRE_FALSE(top->compatible(4, 4, heights, {6, 7, 8, 9}, false));
    REQUIRE_FALSE(top->compatible(4, 4, {0.08f, 0.08f, std::numeric_limits<float>::infinity(), 0.08f}, ids, false));
}

TEST_CASE("SurfaceMix top transport rejects implicit and invalid mappings", "[SurfaceMixTopStack]")
{
    for (const json slots : {json{0, 1, 2, 3}, json{1, 1, 2, 3}, json{1, 2, 3, 5}, json{1, 2, 3}, json{1, 2, 3, 4.5}, json{1, 2, 3, -1},
                             json{1, 2, 3, 65537}}) {
        auto input                                                             = transport();
        input[SurfaceMixTopStack::transport_key]["canonical_to_filament_slot"] = slots;
        REQUIRE_FALSE(adapter(input));
    }
}

TEST_CASE("SurfaceMix top transport cannot acquire display assumptions silently", "[SurfaceMixTopStack]")
{
    auto  input                                                                   = transport();
    auto& settings                                                                = input[SurfaceMixTopStack::transport_key];
    settings["catalogue"]["sources"][0]["records"][0]["measured_rgb"]["encoding"] = nullptr;
    REQUIRE_FALSE(adapter(input));
    settings["assume_normalized_srgb"] = true;
    REQUIRE(adapter(input));
    settings["coordinate_mode"] = "local_normal_wall";
    REQUIRE_FALSE(adapter(input));
}

TEST_CASE("SurfaceMix cache identity includes recipe order and slot mapping", "[SurfaceMixTopStack]")
{
    auto       input = transport();
    const auto first = adapter(input);
    REQUIRE(first);
    REQUIRE(first->cache_key() == adapter(input)->cache_key());
    input[SurfaceMixTopStack::transport_key]["canonical_to_filament_slot"] = {1, 2, 3, 4};
    REQUIRE(first->cache_key() != adapter(input)->cache_key());
    input[SurfaceMixTopStack::transport_key]["catalogue"]["sources"][0]["records"][0]["recipe"]["surface_to_deep"] = {"magenta", "cyan",
                                                                                                                      "yellow", "white"};
    REQUIRE(first->cache_key() != adapter(input)->cache_key());
}

TEST_CASE("SurfaceMix top transport rejects differently dimensioned recipes", "[SurfaceMixTopStack]")
{
    auto  input                = transport();
    auto& recipe               = input[SurfaceMixTopStack::transport_key]["catalogue"]["sources"][0]["records"][1]["recipe"];
    recipe["layer_heights_mm"] = {0.08, 0.16, 0.08, 0.08};
    REQUIRE_FALSE(adapter(input));
}

TEST_CASE("SurfaceMix top transport requires explicit depth direction", "[SurfaceMixTopStack]")
{
    auto  input                      = transport();
    auto& recipe                     = input[SurfaceMixTopStack::transport_key]["catalogue"]["sources"][0]["records"][0]["recipe"];
    recipe["source_stack_direction"] = "bottom_to_top";
    REQUIRE_FALSE(adapter(input));
    recipe.erase("source_stack_direction");
    REQUIRE_FALSE(adapter(input));
}
