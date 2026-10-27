#include "SurfaceMixSideOffsets.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace Slic3r {
namespace {
double finite_number(const nlohmann::json& value)
{
    if (!value.is_number() || !std::isfinite(value.get<double>()))
        throw std::runtime_error("expected a finite numeric value");
    return value.get<double>();
}
} // namespace

std::optional<SurfaceMixSideOffsets> SurfaceMixSideOffsets::from_catalogue(const nlohmann::json& root, std::string* error)
{
    if (error)
        error->clear();
    std::string location = "catalogue";
    try {
        const nlohmann::json* catalog = &root;
        if (root.contains("catalog")) {
            if (root.at("report_schema_version") != "surfacemix.calibration-audit.v1" || root.at("audit_passed") != true)
                throw std::runtime_error("requires a passing calibration audit");
            catalog = &root.at("catalog");
        }
        if (catalog->at("schema_version") != "surfacemix.calibration-catalog.v1" ||
            catalog->at("canonical_component_order") != nlohmann::json({"cyan", "magenta", "yellow", "white"}) ||
            catalog->at("executable_assignment_enabled") != false)
            throw std::runtime_error("unsupported catalogue schema, component order or assignment flag");
        SurfaceMixSideOffsets out;
        out.m_catalogue = catalog->dump();
        if (!catalog->at("sources").is_array())
            throw std::runtime_error("sources must be an array");
        for (const auto& source : catalog->at("sources")) {
            if (source.at("role") != "side")
                continue;
            const auto hash = source.at("source_sha256").get<std::string>();
            if (hash.size() != 64 ||
                !std::all_of(hash.begin(), hash.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
                throw std::runtime_error("invalid source hash representation");
            if (source.at("source_component_order") != catalog->at("canonical_component_order"))
                throw std::runtime_error("side component order must explicitly match canonical C/M/Y/W");
            const auto& records = source.at("records");
            if (!records.is_array())
                throw std::runtime_error("records must be an array");
            for (const auto& record : records) {
                location = record.at("source_pointer").get<std::string>();
                if (record.at("source_role") != "side" || record.at("statuses").at("decoding").at("state") != "pass")
                    throw std::runtime_error("side record has not passed decoding");
                const auto& recipe = record.at("recipe");
                const auto& rgb    = record.at("measured_rgb");
                if (recipe.at("kind") != "side_offsets" || recipe.at("offset_order") != "slot")
                    throw std::runtime_error("expected independent slot-ordered side offsets");
                if ((!rgb.at("encoding").is_null() && rgb.at("encoding") != "srgb" && rgb.at("encoding") != "sRGB") ||
                    (!rgb.at("channel_range").is_null() && rgb.at("channel_range") != nlohmann::json({0.0, 1.0})))
                    throw std::runtime_error("only normalized sRGB is supported");
                out.m_assumes_srgb |= rgb.at("encoding").is_null() || rgb.at("channel_range").is_null();
                SurfaceMixSideRecipe item;
                item.sample_id      = record.at("sample_id").is_null() ? location : record.at("sample_id").get<std::string>();
                item.source_pointer = location;
                item.source_sha256  = hash;
                item.max_offset     = finite_number(recipe.at("max_offset"));
                if (item.max_offset <= 0.)
                    throw std::runtime_error("max_offset must be positive");
                const auto& raw  = recipe.at("raw_offsets_by_component");
                const auto& amps = recipe.at("amplitudes_by_component");
                if (!raw.is_array() || raw.size() != 4 || !amps.is_array() || amps.size() != 4 || !rgb.at("channels").is_array() ||
                    rgb.at("channels").size() != 3)
                    throw std::runtime_error("expected four offsets/amplitudes and three colour channels");
                for (size_t i = 0; i < 4; ++i) {
                    item.raw_offsets[i] = finite_number(raw[i]);
                    item.amplitudes[i]  = finite_number(amps[i]);
                    if (item.raw_offsets[i] < 0. || item.raw_offsets[i] > item.max_offset || item.amplitudes[i] < 0. ||
                        item.amplitudes[i] > 1. || std::abs(item.raw_offsets[i] / item.max_offset - item.amplitudes[i]) > 0.000050001)
                        throw std::runtime_error("side offset/amplitude mismatch (four-decimal source precision)");
                }
                for (size_t i = 0; i < 3; ++i) {
                    item.measured_rgb[i] = finite_number(rgb.at("channels")[i]);
                    if (item.measured_rgb[i] < 0. || item.measured_rgb[i] > 1.)
                        throw std::runtime_error("RGB channel outside [0,1]");
                }
                out.m_recipes.push_back(std::move(item));
                if (out.m_recipes.size() > 100000)
                    throw std::runtime_error("too many side records");
            }
        }
        if (out.m_recipes.empty())
            throw std::runtime_error("no side recipes in catalogue");
        return out;
    } catch (const std::exception& ex) {
        if (error)
            *error = "SurfaceMix side offsets at " + location + ": " + ex.what();
        return std::nullopt;
    }
}

std::optional<SurfaceMixSideOffsets> SurfaceMixSideOffsets::from_calibration_json(const std::string&               text,
                                                                                  const std::vector<unsigned int>& available,
                                                                                  std::string*                     error)
{
    if (error)
        error->clear();
    const auto root = nlohmann::json::parse(text, nullptr, false);
    if (!root.is_object() || !root.contains(transport_key))
        return std::nullopt;
    try {
        const auto& transport = root.at(transport_key);
        if (!transport.at("enabled").is_boolean())
            throw std::runtime_error("invalid enable flag");
        if (!transport.at("enabled").get<bool>())
            return std::nullopt;
        if (transport.at("schema") != "surfacemix.imagemap-side-offsets.v1" || transport.at("experimental") != true ||
            transport.at("execution_model") != "imagemap_independent_offsets")
            throw std::runtime_error("unsupported side-offset execution model");
        std::string reason;
        auto        result = from_catalogue(transport.at("catalogue"), &reason);
        if (!result)
            throw std::runtime_error(reason);
        if (result->assumes_srgb() && transport.value("assume_normalized_srgb", false) != true)
            throw std::runtime_error("explicit experimental normalized-sRGB assumption required");
        const auto& slots = transport.at("canonical_to_filament_slot");
        if (!slots.is_array() || slots.size() != 4)
            throw std::runtime_error("four explicit filament slots required");
        std::set<unsigned int> distinct;
        for (size_t i = 0; i < 4; ++i) {
            if (!slots[i].is_number_integer())
                throw std::runtime_error("filament slots must be integers");
            const auto slot = slots[i].get<int64_t>();
            if (slot < 1 || slot > 65535 || !distinct.insert(unsigned(slot)).second ||
                std::find(available.begin(), available.end(), unsigned(slot)) == available.end())
                throw std::runtime_error("C/M/Y/W slots must be distinct and available");
            result->m_slots[i] = unsigned(slot);
        }
        if (available.size() != 4)
            throw std::runtime_error("this side adapter requires exactly four active components");
        return result;
    } catch (const std::exception& ex) {
        if (error)
            *error = "SurfaceMix side offsets: " + std::string(ex.what());
        return std::nullopt;
    }
}
} // namespace Slic3r
