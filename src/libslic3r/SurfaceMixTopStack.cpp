#include "SurfaceMixTopStack.hpp"

#include <boost/uuid/detail/sha1.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace Slic3r {
namespace {

bool equal_height(double a, double b)
{
    return std::isfinite(a) && std::isfinite(b) && a > 0.0 && b > 0.0 &&
           std::abs(a - b) <= std::max(1e-6, 1e-6 * std::max(std::abs(a), std::abs(b)));
}

std::string content_key(const nlohmann::json& transport)
{
    const std::string          bytes = transport.dump();
    boost::uuids::detail::sha1 hash;
    hash.process_bytes(bytes.data(), bytes.size());
    boost::uuids::detail::sha1::digest_type digest;
    hash.get_digest(digest);
    std::ostringstream out;
    out << "surfacemix-top-v1:" << std::hex << std::setfill('0');
    for (auto word : digest)
        out << std::setw(sizeof(digest[0]) * 2) << unsigned(word);
    return out.str();
}

} // namespace

std::optional<SurfaceMixTopStack> SurfaceMixTopStack::from_calibration_json(const std::string&               calibration_json,
                                                                            const std::vector<unsigned int>& available_slots,
                                                                            std::string*                     error)
{
    if (error != nullptr)
        error->clear();
    const auto root = nlohmann::json::parse(calibration_json, nullptr, false);
    if (!root.is_object() || !root.contains(transport_key))
        return std::nullopt; // Legacy configurations never opt in implicitly.
    try {
        const auto& transport = root.at(transport_key);
        if (!transport.is_object() || !transport.at("enabled").is_boolean())
            throw std::runtime_error("invalid top-stack enable flag");
        if (!transport.at("enabled").get<bool>())
            return std::nullopt;
        if (transport.at("schema") != "surfacemix.imagemap-top-stacks.v1" || transport.at("experimental") != true ||
            transport.at("coordinate_mode") != "build_z_top")
            throw std::runtime_error("unsupported experimental top-stack transport");

        std::string error_text;
        auto        catalogue = SurfaceMixRecipePreview::from_json(transport.at("catalogue"), &error_text);
        if (!catalogue)
            throw std::runtime_error(error_text);
        if (catalogue->assumes_srgb() && transport.value("assume_normalized_srgb", false) != true)
            throw std::runtime_error("undeclared RGB encoding requires an explicit experimental sRGB assumption");
        const auto& catalogue_root = transport.at("catalogue").contains("catalog") ? transport.at("catalogue").at("catalog") :
                                                                                     transport.at("catalogue");
        for (const auto& source : catalogue_root.at("sources")) {
            if (source.at("role") != "top")
                continue;
            for (const auto& record : source.at("records"))
                if (record.at("recipe").at("source_stack_direction") != "surface_to_deep")
                    throw std::runtime_error("top recipe must explicitly declare surface-to-depth order");
        }
        const auto& slots = transport.at("canonical_to_filament_slot");
        if (!slots.is_array() || slots.size() != 4)
            throw std::runtime_error("explicit C/M/Y/W filament-slot mapping is required");
        SurfaceMixTopStack     result;
        std::set<unsigned int> distinct;
        for (size_t i = 0; i < 4; ++i) {
            if (!slots[i].is_number_unsigned() && !slots[i].is_number_integer())
                throw std::runtime_error("filament slots must be positive integers");
            const auto value = slots[i].get<int64_t>();
            if (value <= 0 || value > 65535 ||
                std::find(available_slots.begin(), available_slots.end(), unsigned(value)) == available_slots.end() ||
                !distinct.insert(unsigned(value)).second)
                throw std::runtime_error("C/M/Y/W slots must be distinct and present in this ImageMap zone");
            result.m_slots[i] = unsigned(value);
        }
        const auto& heights = catalogue->recipes().front().layer_heights_mm;
        const size_t execution_layers = transport.value("execution_layers", default_execution_layer_count);
        if (execution_layers == 0 || execution_layers > heights.size())
            throw std::runtime_error("top execution layer count must be positive and no greater than the measured calibration depth");
        for (const auto& recipe : catalogue->recipes()) {
            if (recipe.layer_heights_mm.size() != heights.size())
                throw std::runtime_error("this adapter requires a common complete layer layout; no recipe resizing");
            for (size_t i = 0; i < heights.size(); ++i)
                if (!equal_height(heights[i], recipe.layer_heights_mm[i]))
                    throw std::runtime_error("catalogue contains differing physical layer layouts");
        }
        result.m_catalogue = std::make_shared<const SurfaceMixRecipePreview>(std::move(*catalogue));
        result.m_execution_layer_count = execution_layers;
        result.m_cache_key = content_key(transport); // Cache identity, not provenance authentication.
        return result;
    } catch (const std::exception& ex) {
        if (error != nullptr)
            *error = "SurfaceMix top stacks: " + std::string(ex.what());
        return std::nullopt;
    }
}

bool SurfaceMixTopStack::compatible(int                       stack_layers,
                                    int                       visible_layers,
                                    const std::vector<float>& actual_heights_mm,
                                    const std::vector<int>&   actual_layer_ids,
                                    bool                      lower_surface,
                                    std::string*              error) const
{
    auto reject = [error](const std::string& reason) {
        if (error != nullptr)
            *error = "SurfaceMix top stacks: " + reason + ". No generic colour fallback was used.";
        return false;
    };
    if (error != nullptr)
        error->clear();
    if (lower_surface)
        return reject("only upper build-Z top stacks are supported by this experimental adapter");
    const size_t count = size_t(stack_layers);
    if (count == 0 || count > layer_count() || visible_layers < int(count))
        return reject("requires every interval of the requested " + std::to_string(count) +
                      "-layer calibrated prefix; the longer measured catalogue remains provenance only");
    if (actual_heights_mm.size() < count || actual_layer_ids.size() < count)
        return reject("actual slice layer heights and IDs are required");
    const auto& heights = m_catalogue->recipes().front().layer_heights_mm;
    for (size_t i = 0; i < count; ++i) {
        if (!equal_height(actual_heights_mm[i], heights[i]))
            return reject("slice layer height differs from calibration at inward interval " + std::to_string(i));
        if (actual_layer_ids[i] < 0 || (i > 0 && int64_t(actual_layer_ids[i]) + 1 != actual_layer_ids[i - 1]))
            return reject("slice layers are not consecutive in surface-to-depth order");
    }
    return true;
}

std::vector<unsigned int> SurfaceMixTopStack::bottom_to_top(const std::array<float, 3>& target_rgb) const
{
    return bottom_to_top(target_rgb, m_execution_layer_count);
}

std::vector<unsigned int> SurfaceMixTopStack::bottom_to_top(const std::array<float, 3>& target_rgb,
                                                            size_t                         execution_layers) const
{
    for (float c : target_rgb)
        if (!std::isfinite(c) || c < 0.f || c > 1.f)
            throw std::runtime_error("SurfaceMix top stacks: target RGB must be finite and normalized");
    const auto                index  = m_catalogue->nearest_recipe({target_rgb[0], target_rgb[1], target_rgb[2]});
    const auto&               recipe = m_catalogue->recipes()[index];
    std::vector<unsigned int> out;
    const size_t count = std::min({execution_layers, m_execution_layer_count, recipe.surface_to_deep.size()});
    out.reserve(count);
    for (size_t i = count; i > 0; --i)
        out.push_back(m_slots[recipe.surface_to_deep[i - 1]]);
    return out;
}

std::array<float, 3> SurfaceMixTopStack::measured_rgb(const std::array<float, 3>& target_rgb) const
{
    const auto  index = m_catalogue->nearest_recipe({target_rgb[0], target_rgb[1], target_rgb[2]});
    const auto& rgb   = m_catalogue->recipes()[index].measured_rgb;
    return {float(rgb[0]), float(rgb[1]), float(rgb[2])};
}

std::optional<std::array<float, 3>> SurfaceMixTopStack::measured_stack_rgb(const std::vector<unsigned int>& bottom_to_top) const
{
    for (const auto& recipe : m_catalogue->recipes()) {
        if (bottom_to_top.size() != m_execution_layer_count || recipe.surface_to_deep.size() < m_execution_layer_count)
            continue;
        bool same = true;
        for (size_t i = 0; i < bottom_to_top.size(); ++i)
            same &= bottom_to_top[i] == m_slots[recipe.surface_to_deep[m_execution_layer_count - 1 - i]];
        if (same)
            return std::array<float, 3>{float(recipe.measured_rgb[0]), float(recipe.measured_rgb[1]), float(recipe.measured_rgb[2])};
    }
    return std::nullopt;
}

} // namespace Slic3r
