#include "SurfaceMix3mfBridge.hpp"

#include "../TextureMapping.hpp"
#include "../PrintConfig.hpp"
#include "../miniz_extension.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace Slic3r::SurfaceMix3mfBridge {
namespace {

constexpr const char* report_entry = "Metadata/tonimcqueen_surfacemix_recipe_report.json";
constexpr const char* depth_entry = "Metadata/tonimcqueen_surfacemix_depth_stack_plan.json";
constexpr const char* side_entry = "Metadata/tonimcqueen_surfacemix_side_recipe_plan.json";
constexpr size_t execution_layers = 3;
constexpr double execution_depth_mm = 0.24;

bool has_entry(mz_zip_archive& archive, const char* name)
{
    return mz_zip_reader_locate_file(&archive, name, nullptr, 0) >= 0;
}

bool read_entry(mz_zip_archive& archive, const char* name, std::string& result, std::string& error)
{
    mz_zip_archive_file_stat stat{};
    const int index = mz_zip_reader_locate_file(&archive, name, nullptr, 0);
    if (index < 0 || !mz_zip_reader_file_stat(&archive, static_cast<mz_uint>(index), &stat)) {
        error = std::string("missing 3MF entry: ") + name;
        return false;
    }
    if (stat.m_uncomp_size > 64u * 1024u * 1024u) {
        error = std::string("3MF metadata entry is too large: ") + name;
        return false;
    }
    result.resize(static_cast<size_t>(stat.m_uncomp_size));
    if (!result.empty() && !mz_zip_reader_extract_file_to_mem(&archive, stat.m_filename, result.data(), result.size(), 0)) {
        error = std::string("cannot extract 3MF entry: ") + name;
        return false;
    }
    return true;
}

std::string component_name(const std::string& name)
{
    if (name == "Cyan") return "cyan";
    if (name == "Magenta") return "magenta";
    if (name == "Yellow") return "yellow";
    if (name == "White") return "white";
    return {};
}

bool valid_hash(const std::string& value)
{
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

nlohmann::json build_catalogue(const nlohmann::json& source_report)
{
    const auto& hashes = source_report.at("source_sha256");
    const std::string top_hash = hashes.at("top").get<std::string>();
    if (!valid_hash(top_hash))
        throw std::runtime_error("source_recipe_report.source_sha256.top is not a SHA-256 hash");

    nlohmann::json records = nlohmann::json::array();
    std::unordered_set<std::string> measured_record_keys;
    for (const auto& summary : source_report.at("summaries")) {
        if (!summary.contains("recipe") || summary.at("recipe").is_null() ||
            !summary.contains("measured_rgb_255") || summary.at("measured_rgb_255").is_null())
            continue;
        const auto& recipe = summary.at("recipe");
        const auto& components = recipe.at("surface_to_deep");
        const auto& heights = recipe.at("layer_heights_mm");
        const auto& rgb255 = summary.at("measured_rgb_255");
        if (!components.is_array() || components.empty() || components.size() != heights.size() || rgb255.size() != 3)
            throw std::runtime_error("quantizer summary has an incomplete recipe");

        nlohmann::json stack = nlohmann::json::array();
        for (const auto& component : components) {
            const std::string canonical = component_name(component.get<std::string>());
            if (canonical.empty())
                throw std::runtime_error("quantizer summary contains an unknown CMYW component");
            stack.push_back(canonical);
        }
        nlohmann::json channels = nlohmann::json::array();
        for (const auto& channel : rgb255) {
            const double value = channel.get<double>() / 255.0;
            if (!std::isfinite(value) || value < 0.0 || value > 1.0)
                throw std::runtime_error("quantizer measured RGB is outside 0..255");
            channels.push_back(value);
        }
        const std::string source_pointer = summary.contains("source_pointer") && summary.at("source_pointer").is_string() ?
                                               summary.at("source_pointer").get<std::string>() :
                                               "quantizer-summary";
        const nlohmann::json sample_id = summary.contains("sample_id") ? summary.at("sample_id") : nlohmann::json(nullptr);
        nlohmann::json record = {
            {"source_pointer", source_pointer},
            {"source_role", "top"},
            {"sample_id", sample_id},
            {"statuses", {{"decoding", {{"state", "pass"}}}}},
            {"measured_rgb", {{"encoding", "srgb"}, {"channel_range", {0.0, 1.0}}, {"channels", channels}}},
            {"recipe", {
                {"kind", "top_stack"},
                {"surface_to_deep", stack},
                {"layer_heights_mm", heights},
                {"total_depth_mm", recipe.at("total_depth_mm")},
                {"source_stack_direction", "surface_to_deep"}
            }}
        };
        // The quantizer report has one summary per quantized target, while
        // the slicer catalogue needs one entry per distinct measured sample.
        // Collapse exact repeats without merging different ordered recipes.
        const std::string record_key = record.at("source_pointer").get<std::string>() + "\n" + record.dump();
        if (measured_record_keys.insert(record_key).second)
            records.push_back(std::move(record));
    }
    if (records.empty())
        throw std::runtime_error("quantizer report contains no measured top recipes");

    return {
        {"schema_version", "surfacemix.calibration-catalog.v1"},
        {"canonical_component_order", {"cyan", "magenta", "yellow", "white"}},
        {"top_depth_units", "mm"},
        {"executable_assignment_enabled", false},
        {"sources", {{{"role", "top"}, {"source_sha256", top_hash}, {"records", records}}}}
    };
}

nlohmann::json build_side_catalogue(const nlohmann::json& side_plan)
{
    if (side_plan.at("schema") != "tonimcqueen.surfacemix-side-recipe-plan.v1")
        throw std::runtime_error("unsupported side recipe plan schema");
    const std::string source_hash = side_plan.at("source_sha256").get<std::string>();
    if (!valid_hash(source_hash))
        throw std::runtime_error("side recipe plan source hash is not a SHA-256 hash");

    nlohmann::json records = nlohmann::json::array();
    for (const auto& summary : side_plan.at("summaries")) {
        if (summary.value("state", "unknown") != "diagnostic_match" ||
            !summary.contains("measured_rgb_255") || summary.at("measured_rgb_255").is_null() ||
            !summary.contains("raw_offsets_by_component") || summary.at("raw_offsets_by_component").is_null())
            continue;
        const auto& rgb255 = summary.at("measured_rgb_255");
        const auto& raw = summary.at("raw_offsets_by_component");
        const auto& amplitudes = summary.at("amplitudes_by_component");
        if (rgb255.size() != 3 || raw.size() != 4 || amplitudes.size() != 4)
            throw std::runtime_error("side recipe plan contains an incomplete matched summary");
        nlohmann::json channels = nlohmann::json::array();
        for (const auto& channel : rgb255) {
            const double value = channel.get<double>() / 255.0;
            if (!std::isfinite(value) || value < 0.0 || value > 1.0)
                throw std::runtime_error("side measured RGB is outside 0..255");
            channels.push_back(value);
        }
        records.push_back({
            {"source_pointer", summary.value("source_pointer", "side-plan-summary")},
            {"source_role", "side"},
            {"sample_id", summary.value("sample_id", "side-plan-summary")},
            {"statuses", {{"decoding", {{"state", "pass"}}}}},
            {"measured_rgb", {{"encoding", "srgb"}, {"channel_range", {0.0, 1.0}}, {"channels", channels}}},
            {"recipe", {
                {"kind", "side_offsets"},
                {"offset_order", summary.value("offset_order", "slot")},
                {"raw_offsets_by_component", raw},
                {"amplitudes_by_component", amplitudes},
                {"max_offset", summary.at("max_offset")}
            }}
        });
    }
    if (records.empty())
        throw std::runtime_error("side recipe plan contains no matched measured summaries");
    return {
        {"schema_version", "surfacemix.calibration-catalog.v1"},
        {"canonical_component_order", {"cyan", "magenta", "yellow", "white"}},
        {"executable_assignment_enabled", false},
        {"sources", {{{"role", "side"}, {"source_sha256", source_hash}, {"records", records}}}}
    };
}

std::vector<std::string> filament_colours(const DynamicPrintConfig& config)
{
    if (const auto* option = config.option<ConfigOptionStrings>("filament_colour"); option != nullptr && !option->values.empty())
        return option->values;
    return {"#00FFFF", "#FF00FF", "#FFFF00", "#FFFFFF"};
}

} // namespace

Result apply(const std::string& path, DynamicPrintConfig& config, std::string* error)
{
    if (error != nullptr)
        error->clear();

    mz_zip_archive archive{};
    mz_zip_zero_struct(&archive);
    if (!open_zip_reader(&archive, path))
        return Result::NotPresent;
    const bool present = has_entry(archive, report_entry) || has_entry(archive, depth_entry);
    if (!present) {
        close_zip_reader(&archive);
        return Result::NotPresent;
    }

    std::string report_text;
    std::string depth_text;
    std::string side_text;
    std::string local_error;
    const bool read_report = read_entry(archive, report_entry, report_text, local_error);
    const bool read_depth = read_entry(archive, depth_entry, depth_text, local_error);
    const bool read_side = has_entry(archive, side_entry) && read_entry(archive, side_entry, side_text, local_error);
    close_zip_reader(&archive);
    if (!read_report || !read_depth) {
        if (error != nullptr) *error = local_error;
        return Result::Rejected;
    }

    try {
        const auto report = nlohmann::json::parse(report_text);
        const auto depth = nlohmann::json::parse(depth_text);
        if (report.at("schema") != "tonimcqueen.surfacemix-executable-surface-3mf.v1" ||
            depth.at("schema") != "tonimcqueen.surfacemix-ordered-depth-plan.v1")
            throw std::runtime_error("unsupported quantizer metadata schema");
        if (report.at("top_surface_execution").at("execution_layers") != execution_layers ||
            std::abs(report.at("top_surface_execution").at("execution_depth_mm").get<double>() - execution_depth_mm) > 1e-6 ||
            depth.at("execution_layer_count") != execution_layers)
            throw std::runtime_error("quantizer metadata does not declare the three-layer 0.24 mm execution contract");

        const auto& source_report = report.at("source_recipe_report");
        const nlohmann::json transport = {
            {"surfacemix_top_stacks", {
                {"schema", "surfacemix.imagemap-top-stacks.v1"},
                {"enabled", true},
                {"experimental", true},
                {"coordinate_mode", "build_z_top"},
                {"assume_normalized_srgb", true},
                {"execution_layers", execution_layers},
                {"canonical_to_filament_slot", {1, 2, 3, 4}},
                {"catalogue", build_catalogue(source_report)}
            }}
        };

        const std::vector<std::string> colours = filament_colours(config);
        TextureMappingManager manager;
        if (const auto* defs = config.option<ConfigOptionString>("texture_mapping_definitions"); defs != nullptr)
            manager.load_entries(defs->value, colours);
        const unsigned int zone_id = manager.ensure_image_texture_zone(colours.size(), colours);
        auto* zone = manager.zone_from_id(zone_id);
        if (zone == nullptr)
            throw std::runtime_error("could not create an ImageMap texture zone for SurfaceMix");
        zone->filament_color_mode = int(TextureMappingZone::FilamentColorCMYW);
        zone->surface_mix_solver_enabled = true;
        zone->force_sequential_filaments = true;
        // Dispatch the imported recipe through ImageMap's real top-surface
        // contoning path instead of leaving the generic texture painter active.
        zone->top_surface_image_printing_enabled = true;
        zone->top_surface_image_printing_method = int(TextureMappingZone::TopSurfaceImageContoning);
        zone->top_surface_image_colored_top_layers = int(execution_layers);
        zone->top_surface_contoning_stack_layers = int(execution_layers);
        zone->top_surface_contoning_pattern_filaments = int(execution_layers);
        zone->top_surface_contoning_colored_surfaces = int(TextureMappingZone::ContoningColoredUpperSurfaces);
        zone->top_surface_contoning_only_color_surface_infill = true;
        zone->top_surface_contoning_surface_anchored_stacks_enabled = true;
        zone->top_surface_contoning_layer_phase_enabled = false;
        zone->top_surface_contoning_blue_noise_error_diffusion_enabled = false;
        zone->top_surface_contoning_replace_top_perimeters_with_infill = false;
        zone->top_surface_contoning_recolor_surrounding_perimeters = false;
        zone->dithering_enabled = false;
        zone->top_surface_color_calibration_name = "ToniMcQueen SurfaceMix 3MF metadata";
        zone->top_surface_color_calibration_json = transport.dump();
        if (read_side) {
            const auto side_plan = nlohmann::json::parse(side_text);
            const nlohmann::json side_transport = {
                {"surfacemix_side_offsets", {
                    {"schema", "surfacemix.imagemap-side-offsets.v1"},
                    // The quantizer currently marks side plans diagnostic-only.
                    // Preserve the transport for inspection without activating
                    // unverified wall amplitudes as executable colour.
                    {"enabled", false},
                    {"experimental", true},
                    {"execution_model", "imagemap_independent_offsets"},
                    {"assume_normalized_srgb", true},
                    {"canonical_to_filament_slot", {1, 2, 3, 4}},
                    {"catalogue", build_side_catalogue(side_plan)}
                }}
            };
            zone->side_surface_color_calibration_name = "ToniMcQueen SurfaceMix measured side offsets";
            zone->side_surface_color_calibration_json = side_transport.dump();
            zone->texture_mapping_mode = int(TextureMappingZone::TextureMappingFilamentBlending);
            zone->transmission_distance_calibration_mode = int(TextureMappingZone::TDCalibrationCalibratedNearestMeasuredSample);
            zone->dithering_enabled = false;
            zone->compact_offset_mode = false;
            zone->tone_gamma = 1.f;
        }
        config.set_key_value("texture_mapping_definitions", new ConfigOptionString(manager.serialize_entries()));
        return Result::Applied;
    } catch (const std::exception& exception) {
        if (error != nullptr) *error = exception.what();
        return Result::Rejected;
    }
}

} // namespace Slic3r::SurfaceMix3mfBridge
