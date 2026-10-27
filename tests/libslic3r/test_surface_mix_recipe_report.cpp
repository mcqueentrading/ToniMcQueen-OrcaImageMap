#include <catch2/catch_all.hpp>

#include <libslic3r/SurfaceMixRecipeReport.hpp>
#include <nlohmann/json.hpp>

using namespace Slic3r;
using nlohmann::json;

namespace {
json envelope(bool executable = false)
{
    return {{"schema", "tonimcqueen.surfacemix-executable-surface-3mf.v1"},
            {"execution_enabled", executable},
            {"depth_stack_encoded", false},
            {"dithering_enabled", false},
            {"source_recipe_report",
             {{"schema", "tonimcqueen.surfacemix-recipe-report.v1"},
              {"source_sha256", {{"top", "abc"}, {"side", "def"}}},
              {"triangle_count", 2},
              {"matched_triangle_count", 2},
              {"unique_target_count", 1}}}};
}

json depth_plan()
{
    return {{"stacks",
             {{{"sample_id", "patch_1"},
               {"source_pointer", "/surface/samples/0"},
               {"valid", true},
               {"total_depth_mm", 0.16},
               {"intervals", {{{"component_id", 1}, {"start_mm", 0.0}, {"end_mm", 0.08}},
                              {{"component_id", 3}, {"start_mm", 0.08}, {"end_mm", 0.16}}}}}}}};
}
} // namespace

TEST_CASE("SurfaceMix recipe report imports complete diagnostic stack", "[SurfaceMixRecipeReport]")
{
    std::string error;
    const auto report = SurfaceMixRecipeReport::from_json(envelope(), depth_plan(), &error);
    REQUIRE(report);
    REQUIRE(error.empty());
    REQUIRE(report->diagnostic_only());
    REQUIRE(report->plans.size() == 1);
    REQUIRE(report->plans.front().intervals[1].component == 3);
    REQUIRE(report->plans.front().total_depth_um == 160);
}

TEST_CASE("SurfaceMix recipe report rejects non-contiguous intervals", "[SurfaceMixRecipeReport]")
{
    auto input = depth_plan();
    input["stacks"][0]["intervals"][1]["start_mm"] = 0.09;
    std::string error;
    REQUIRE_FALSE(SurfaceMixRecipeReport::from_json(envelope(), input, &error));
    REQUIRE(error.find("contiguous") != std::string::npos);
}

TEST_CASE("SurfaceMix recipe report rejects unsupported components", "[SurfaceMixRecipeReport]")
{
    auto input = depth_plan();
    input["stacks"][0]["intervals"][0]["component_id"] = 4;
    std::string error;
    REQUIRE_FALSE(SurfaceMixRecipeReport::from_json(envelope(), input, &error));
    REQUIRE(error.find("component_id") != std::string::npos);
}

TEST_CASE("SurfaceMix recipe report exposes execution flags without enabling execution", "[SurfaceMixRecipeReport]")
{
    std::string error;
    const auto report = SurfaceMixRecipeReport::from_json(envelope(true), depth_plan(), &error);
    REQUIRE(report);
    REQUIRE_FALSE(report->diagnostic_only());
    REQUIRE(report->execution_enabled);
}
