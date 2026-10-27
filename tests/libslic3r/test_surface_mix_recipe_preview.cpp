#include <catch2/catch_all.hpp>
#include <libslic3r/SurfaceMixRecipePreview.hpp>
#include <nlohmann/json.hpp>

#include <limits>
#include <cmath>
#include <random>

using namespace Slic3r;
using nlohmann::json;

namespace {
json preview_catalog()
{
    json records = json::array();
    for (size_t i = 0; i < 2; ++i) {
        records.push_back(
            {{"source_role", "top"},
             {"source_pointer", "/samples/" + std::to_string(i)},
             {"sample_id", "sample_" + std::to_string(i)},
             {"statuses", {{"decoding", {{"state", "pass"}}}}},
             {"measured_rgb",
              {{"channels", i == 0 ? json{0.0, 0.9, 0.9} : json{0.9, 0.0, 0.9}}, {"encoding", "srgb"}, {"channel_range", {0.0, 1.0}}}},
             {"recipe",
              {{"kind", "top_stack"},
               {"surface_to_deep", {"cyan", "magenta", "yellow", "white"}},
               {"layer_heights_mm", {0.08, 0.08, 0.16, 0.08}},
               {"total_depth_mm", 0.40}}}});
    }
    return {{"schema_version", "surfacemix.calibration-catalog.v1"},
            {"canonical_component_order", {"cyan", "magenta", "yellow", "white"}},
            {"top_depth_units", "mm"},
            {"executable_assignment_enabled", false},
            {"sources", {{{"role", "top"}, {"source_sha256", std::string(64, 'a')}, {"records", records}}}}};
}

json& first_record(json& catalog) { return catalog["sources"][0]["records"][0]; }
} // namespace

TEST_CASE("SurfaceMix preview consumes Rust catalogue without changing it", "[SurfaceMixPreview]")
{
    const auto  input   = preview_catalog();
    const auto  before  = input.dump();
    std::string error   = "stale";
    const auto  preview = SurfaceMixRecipePreview::from_json(input, &error);
    REQUIRE(preview);
    REQUIRE(error.empty());
    REQUIRE(input.dump() == before);
    REQUIRE(preview->recipes().size() == 2);
    REQUIRE(preview->recipes()[0].source_sha256 == std::string(64, 'a'));
    REQUIRE(preview->recipes()[0].source_pointer == "/samples/0");
    REQUIRE(preview->recipes()[0].sample_id == "sample_0");
    REQUIRE(preview->maximum_depth_mm() == Catch::Approx(0.4));
    REQUIRE_FALSE(preview->assumes_srgb());
}

TEST_CASE("SurfaceMix preview accepts only passing known audit wrappers", "[SurfaceMixPreview]")
{
    json wrapper = {{"report_schema_version", "surfacemix.calibration-audit.v1"}, {"audit_passed", true}, {"catalog", preview_catalog()}};
    REQUIRE(SurfaceMixRecipePreview::from_json(wrapper));
    wrapper["audit_passed"] = false;
    REQUIRE_FALSE(SurfaceMixRecipePreview::from_json(wrapper));
    wrapper["audit_passed"]          = true;
    wrapper["report_schema_version"] = "future";
    REQUIRE_FALSE(SurfaceMixRecipePreview::from_json(wrapper));
}

TEST_CASE("SurfaceMix preview retains all components and exact interval boundaries", "[SurfaceMixPreview]")
{
    const auto preview = SurfaceMixRecipePreview::from_json(preview_catalog());
    REQUIRE(preview);
    REQUIRE(preview->component_at_depth(0, 0.0) == 0);
    REQUIRE(preview->component_at_depth(0, 0.08) == 1);
    REQUIRE(preview->component_at_depth(0, 0.16) == 2);
    REQUIRE(preview->component_at_depth(0, 0.31) == 2);
    REQUIRE(preview->component_at_depth(0, 0.32) == 3);
    REQUIRE_FALSE(preview->component_at_depth(0, 0.4));
    REQUIRE_FALSE(preview->component_at_depth(0, -0.1));
    REQUIRE_FALSE(preview->component_at_depth(2, 0));
    REQUIRE_FALSE(preview->component_at_depth(0, std::numeric_limits<double>::quiet_NaN()));
    REQUIRE(preview->display_colour(0, SurfaceMixPreviewMode::DepthComponent, 0.4) == std::array<double, 3>{0.35, 0.35, 0.35});
}

TEST_CASE("SurfaceMix preview keeps equal-colour differently ordered recipes distinct", "[SurfaceMixPreview]")
{
    auto  input                         = preview_catalog();
    auto& second                        = input["sources"][0]["records"][1];
    second["measured_rgb"]              = first_record(input)["measured_rgb"];
    second["recipe"]["surface_to_deep"] = {"magenta", "cyan", "yellow", "white"};
    const auto preview                  = SurfaceMixRecipePreview::from_json(input);
    REQUIRE(preview);
    REQUIRE(preview->recipes().size() == 2);
    REQUIRE(preview->component_at_depth(0, 0) == 0);
    REQUIRE(preview->component_at_depth(1, 0) == 1);
    REQUIRE(preview->nearest_recipe({0.0, 0.9, 0.9}) == 0);
}

TEST_CASE("SurfaceMix depth display respects nominal 0.08 mm boundaries after summation", "[SurfaceMixPreview]")
{
    auto  input                = preview_catalog();
    auto& recipe               = first_record(input)["recipe"];
    recipe["surface_to_deep"]  = json::array();
    recipe["layer_heights_mm"] = json::array();
    for (int i = 0; i < 19; ++i) {
        recipe["surface_to_deep"].push_back(i % 2 == 0 ? "cyan" : "magenta");
        recipe["layer_heights_mm"].push_back(0.08);
    }
    recipe["total_depth_mm"] = 1.52;
    const auto preview       = SurfaceMixRecipePreview::from_json(input);
    REQUIRE(preview);
    for (int i = 0; i < 19; ++i) {
        REQUIRE(preview->component_at_depth(0, i * 0.08) == i % 2);
        REQUIRE(preview->component_at_depth(0, (i + 0.5) * 0.08) == i % 2);
    }
    REQUIRE_FALSE(preview->component_at_depth(0, 1.52));
}

TEST_CASE("SurfaceMix preview chooses complete measured recipes not averaged mixtures", "[SurfaceMixPreview]")
{
    const auto preview = SurfaceMixRecipePreview::from_json(preview_catalog());
    REQUIRE(preview);
    REQUIRE(preview->nearest_recipe({0.0, 0.9, 0.9}) == 0);
    REQUIRE(preview->nearest_recipe({0.9, 0.0, 0.9}) == 1);
    const auto chosen = preview->nearest_recipe({0.45, 0.45, 0.9});
    REQUIRE(chosen < 2);
    REQUIRE(preview->display_colour(chosen, SurfaceMixPreviewMode::MeasuredColour, 0) == preview->recipes()[chosen].measured_rgb);
    REQUIRE_THROWS(preview->nearest_recipe({-0.1, 0, 0}));
    REQUIRE_THROWS(preview->nearest_recipe({std::numeric_limits<double>::infinity(), 0, 0}));
}

TEST_CASE("SurfaceMix exact nearest index agrees with a brute-force Lab search", "[SurfaceMixPreview]")
{
    auto  input   = preview_catalog();
    auto  record  = first_record(input);
    auto& records = input["sources"][0]["records"];
    records       = json::array();
    std::mt19937                           random(71);
    std::uniform_real_distribution<double> channel(0.0, 1.0);
    auto                                   lab = [](const std::array<double, 3>& rgb) {
        std::array<double, 3> linear{};
        for (size_t i = 0; i < 3; ++i)
            linear[i] = rgb[i] <= 0.04045 ? rgb[i] / 12.92 : std::pow((rgb[i] + 0.055) / 1.055, 2.4);
        const std::array<std::array<double, 3>, 3> matrix{
            {{0.4124564, 0.3575761, 0.1804375}, {0.2126729, 0.7151522, 0.0721750}, {0.0193339, 0.1191920, 0.9503041}}};
        const std::array<double, 3> white{0.95047, 1.0, 1.08883};
        std::array<double, 3>       f{};
        for (size_t i = 0; i < 3; ++i) {
            double t = 0.0;
            for (size_t j = 0; j < 3; ++j)
                t += matrix[i][j] * linear[j];
            t /= white[i];
            f[i] = t > 216.0 / 24389.0 ? std::cbrt(t) : (24389.0 / 27.0 * t + 16.0) / 116.0;
        }
        return std::array<double, 3>{116 * f[1] - 16, 500 * (f[0] - f[1]), 200 * (f[1] - f[2])};
    };
    std::vector<std::array<double, 3>> colours;
    for (size_t i = 0; i < 128; ++i) {
        const std::array<double, 3> rgb{channel(random), channel(random), channel(random)};
        record["measured_rgb"]["channels"] = rgb;
        record["sample_id"]                = std::to_string(i);
        record["source_pointer"]           = "/samples/" + std::to_string(i);
        records.push_back(record);
        colours.push_back(lab(rgb));
    }
    const auto preview = SurfaceMixRecipePreview::from_json(input);
    REQUIRE(preview);
    for (size_t query = 0; query < 256; ++query) {
        const std::array<double, 3> target{channel(random), channel(random), channel(random)};
        const auto                  target_lab = lab(target);
        double                      best       = std::numeric_limits<double>::infinity();
        size_t                      expected   = 0;
        for (size_t i = 0; i < colours.size(); ++i) {
            double distance = 0.0;
            for (size_t j = 0; j < 3; ++j)
                distance += std::pow(colours[i][j] - target_lab[j], 2);
            if (distance < best) {
                best     = distance;
                expected = i;
            }
        }
        REQUIRE(preview->nearest_recipe(target) == expected);
    }
}

TEST_CASE("SurfaceMix preview declares missing colour encoding rather than inventing calibration", "[SurfaceMixPreview]")
{
    auto input                                           = preview_catalog();
    first_record(input)["measured_rgb"]["encoding"]      = nullptr;
    first_record(input)["measured_rgb"]["channel_range"] = nullptr;
    const auto preview                                   = SurfaceMixRecipePreview::from_json(input);
    REQUIRE(preview);
    REQUIRE(preview->assumes_srgb());
    first_record(input)["measured_rgb"]["encoding"] = "linear_rgb";
    REQUIRE_FALSE(SurfaceMixRecipePreview::from_json(input));
}

TEST_CASE("SurfaceMix preview rejects malformed recipes atomically", "[SurfaceMixPreview]")
{
    for (int invalid = 0; invalid < 11; ++invalid) {
        CAPTURE(invalid);
        auto  input = preview_catalog();
        auto& r     = first_record(input);
        switch (invalid) {
        case 0: r["recipe"]["surface_to_deep"][0] = "black"; break;
        case 1: r["recipe"]["layer_heights_mm"] = {0.08}; break;
        case 2: r["recipe"]["layer_heights_mm"][0] = -0.08; break;
        case 3: r["recipe"]["total_depth_mm"] = 0.24; break;
        case 4: r["measured_rgb"]["channels"][0] = 256; break;
        case 5: r["measured_rgb"]["channels"][0] = true; break;
        case 6: r["statuses"]["decoding"]["state"] = "fail"; break;
        case 7: input["executable_assignment_enabled"] = true; break;
        case 8: input["top_depth_units"] = "um"; break;
        case 9: input["sources"][0]["source_sha256"] = "bad"; break;
        case 10: r["measured_rgb"]["channel_range"] = {0, 255}; break;
        }
        std::string error;
        REQUIRE_FALSE(SurfaceMixRecipePreview::from_json(input, &error));
        REQUIRE_FALSE(error.empty());
    }
}

TEST_CASE("SurfaceMix preview ignores side offsets instead of converting them to depth", "[SurfaceMixPreview]")
{
    auto input = preview_catalog();
    input["sources"].push_back({{"role", "side"}, {"records", {{{"recipe", {{"kind", "side_offsets"}}}}}}});
    const auto preview = SurfaceMixRecipePreview::from_json(input);
    REQUIRE(preview);
    REQUIRE(preview->recipes().size() == 2);
    input["sources"].erase(input["sources"].begin());
    REQUIRE_FALSE(SurfaceMixRecipePreview::from_json(input));
}

TEST_CASE("SurfaceMix raster preserves spatial colour mapping and source alpha", "[SurfaceMixPreview]")
{
    const auto preview = SurfaceMixRecipePreview::from_json(preview_catalog());
    REQUIRE(preview);
    const std::vector<unsigned char> source{0, 230, 230, 255, 230, 0, 230, 128, 230, 0, 230, 0, 0, 230, 230, 40};
    const auto                       copy   = source;
    const auto                       output = preview->render_rgba(source, 2, 2, 2, 2, SurfaceMixPreviewMode::MeasuredColour, 0);
    REQUIRE(output == std::vector<unsigned char>{0, 230, 230, 255, 230, 0, 230, 128, 230, 0, 230, 0, 0, 230, 230, 40});
    REQUIRE(source == copy);
    const auto depth = preview->render_rgba(source, 2, 2, 2, 2, SurfaceMixPreviewMode::DepthComponent, 0.08);
    REQUIRE(depth[0] == 255);
    REQUIRE(depth[1] == 0);
    REQUIRE(depth[2] == 255);
    REQUIRE(depth[3] == 255);
    REQUIRE(depth[7] == 128);
    REQUIRE(depth[11] == 0);
}

TEST_CASE("SurfaceMix raster downsamples without transposing UV texture or changing input", "[SurfaceMixPreview]")
{
    const auto preview = SurfaceMixRecipePreview::from_json(preview_catalog());
    REQUIRE(preview);
    const std::vector<unsigned char> source{0, 230, 230, 255, 230, 0, 230, 255, 230, 0, 230, 255, 0, 230, 230, 255};
    const auto                       output = preview->render_rgba(source, 4, 1, 2, 1, SurfaceMixPreviewMode::MeasuredColour, 0);
    REQUIRE(output == std::vector<unsigned char>{230, 0, 230, 255, 0, 230, 230, 255});
}

TEST_CASE("SurfaceMix raster is bounded cancellable deterministic and rejects invalid buffers", "[SurfaceMixPreview]")
{
    const auto preview = SurfaceMixRecipePreview::from_json(preview_catalog());
    REQUIRE(preview);
    const std::vector<unsigned char> source{0, 230, 230, 255};
    REQUIRE(preview->render_rgba(source, 1, 1, 8, 8, SurfaceMixPreviewMode::MeasuredColour, 0) ==
            preview->render_rgba(source, 1, 1, 8, 8, SurfaceMixPreviewMode::MeasuredColour, 0));
    REQUIRE(preview->render_rgba(source, 1, 1, 8, 8, SurfaceMixPreviewMode::MeasuredColour, 0, [] { return true; }).empty());
    REQUIRE(preview->render_rgba({}, 1, 1, 1, 1, SurfaceMixPreviewMode::MeasuredColour, 0).empty());
    REQUIRE(preview->render_rgba(source, 1, 1, 65536, 65536, SurfaceMixPreviewMode::MeasuredColour, 0).empty());
    REQUIRE(preview->render_rgba(source, 1, 1, 1, 1, SurfaceMixPreviewMode::Off, 0).empty());
}
