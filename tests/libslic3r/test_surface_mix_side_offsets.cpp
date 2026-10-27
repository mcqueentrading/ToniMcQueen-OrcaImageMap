#include <catch2/catch_all.hpp>
#include <libslic3r/SurfaceMixSideOffsets.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <limits>
#include <numeric>
using namespace Slic3r;
using nlohmann::json;
namespace {
json catalog()
{
    json record{{"source_role", "side"},
                {"source_pointer", "/swatches/0"},
                {"sample_id", "patch"},
                {"statuses", {{"decoding", {{"state", "pass"}}}}},
                {"measured_rgb", {{"channels", {0.2, 0.3, 0.4}}, {"encoding", "srgb"}, {"channel_range", {0., 1.}}}},
                {"recipe",
                 {{"kind", "side_offsets"},
                  {"offset_order", "slot"},
                  {"max_offset", 255},
                  {"raw_offsets_by_component", {220, 216, 255, 170}},
                  {"amplitudes_by_component", {0.8627, 0.8471, 1., 0.6667}}}}};
    json source{{"role", "side"},
                {"source_sha256", std::string(64, 'a')},
                {"source_component_order", {"cyan", "magenta", "yellow", "white"}},
                {"records", json::array({record})}};
    return {{"schema_version", "surfacemix.calibration-catalog.v1"},
            {"canonical_component_order", {"cyan", "magenta", "yellow", "white"}},
            {"executable_assignment_enabled", false},
            {"sources", json::array({source})}};
}
json transport()
{
    return {{SurfaceMixSideOffsets::transport_key,
             {{"schema", "surfacemix.imagemap-side-offsets.v1"},
              {"experimental", true},
              {"enabled", true},
              {"execution_model", "imagemap_independent_offsets"},
              {"canonical_to_filament_slot", {3, 1, 4, 2}},
              {"catalogue", catalog()}}}};
}
auto decode(const json& value, std::string* error = nullptr)
{ return SurfaceMixSideOffsets::from_calibration_json(value.dump(), {1, 2, 3, 4}, error); }
} // namespace

TEST_CASE("Side amplitudes remain independent and provenance survives", "[SurfaceMixSide]")
{
    const auto input   = catalog();
    const auto before  = input.dump();
    const auto decoded = SurfaceMixSideOffsets::from_catalogue(input);
    REQUIRE(decoded);
    REQUIRE(input.dump() == before);
    const auto& r = decoded->recipes().front();
    REQUIRE(r.amplitudes[0] == 0.8627);
    REQUIRE(r.raw_offsets[0] == 220.);
    REQUIRE(std::accumulate(r.amplitudes.begin(), r.amplitudes.end(), 0.) > 3.);
    REQUIRE(r.source_pointer == "/swatches/0");
    REQUIRE(r.source_sha256 == std::string(64, 'a'));
    auto duplicates = input;
    duplicates["sources"][0]["records"].push_back(input["sources"][0]["records"][0]);
    REQUIRE(SurfaceMixSideOffsets::from_catalogue(duplicates)->recipes().size() == 2);
}
TEST_CASE("Side transport is opt-in with explicit slot identity", "[SurfaceMixSide]")
{
    std::string error = "stale";
    REQUIRE_FALSE(decode(json::object(), &error));
    REQUIRE(error.empty());
    auto t = transport();
    REQUIRE(decode(t));
    REQUIRE(decode(t)->slots()[0] == 3);
    t[SurfaceMixSideOffsets::transport_key]["enabled"] = false;
    REQUIRE_FALSE(decode(t, &error));
    REQUIRE(error.empty());
    for (auto slots : {json{0, 1, 2, 3}, json{1, 1, 3, 4}, json{1, 2, 3, 9}, json{1, 2, 3}, json{1, 2, 3, 4.5}}) {
        t                                                                     = transport();
        t[SurfaceMixSideOffsets::transport_key]["canonical_to_filament_slot"] = slots;
        REQUIRE_FALSE(decode(t, &error));
        REQUIRE_FALSE(error.empty());
    }
}
TEST_CASE("Side transport rejects unreviewed RGB interpretation", "[SurfaceMixSide]")
{
    auto  t         = transport();
    auto& rgb       = t[SurfaceMixSideOffsets::transport_key]["catalogue"]["sources"][0]["records"][0]["measured_rgb"];
    rgb["encoding"] = nullptr;
    REQUIRE_FALSE(decode(t));
    t[SurfaceMixSideOffsets::transport_key]["assume_normalized_srgb"] = true;
    REQUIRE(decode(t));
    rgb["encoding"] = "linear";
    REQUIRE_FALSE(decode(t));
}
TEST_CASE("Side records reject invalid fields instead of clamping or skipping", "[SurfaceMixSide]")
{
    for (auto key : {"raw_offsets_by_component", "amplitudes_by_component"}) {
        for (double value : {-1., 256., std::numeric_limits<double>::infinity()}) {
            auto c                                          = catalog();
            c["sources"][0]["records"][0]["recipe"][key][0] = value;
            REQUIRE_FALSE(SurfaceMixSideOffsets::from_catalogue(c));
        }
    }
    auto c                                                                = catalog();
    c["sources"][0]["records"][0]["recipe"]["amplitudes_by_component"][0] = 0.25;
    REQUIRE_FALSE(SurfaceMixSideOffsets::from_catalogue(c));
    c                                         = catalog();
    c["sources"][0]["source_component_order"] = {"white", "yellow", "magenta", "cyan"};
    REQUIRE_FALSE(SurfaceMixSideOffsets::from_catalogue(c));
    c                                               = catalog();
    c["sources"][0]["records"][0]["recipe"]["kind"] = "top_stack";
    REQUIRE_FALSE(SurfaceMixSideOffsets::from_catalogue(c));
}
TEST_CASE("Bundled Rust side catalogue retains every record", "[SurfaceMixSide]")
{
    std::ifstream input(SURFACEMIX_SIDE_CATALOGUE);
    json          c;
    input >> c;
    std::string error;
    const auto  decoded = SurfaceMixSideOffsets::from_catalogue(c, &error);
    INFO(error);
    REQUIRE(decoded);
    REQUIRE(decoded->recipes().size() == 1680);
    REQUIRE(c["sources"][0]["records"].size() == decoded->recipes().size());
    REQUIRE(c["sources"][0]["source_sha256"] == "37b4c09b5208ceb50e094a609ded9251ae618ef29dc1b8495578944ff3676f48");
    REQUIRE(decoded->assumes_srgb());
}
