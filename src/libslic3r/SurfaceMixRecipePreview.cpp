#include "SurfaceMixRecipePreview.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace Slic3r {
namespace {

std::array<double, 3> lab_from_srgb(const std::array<double, 3>& rgb)
{
    auto         linear = [](double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
    const double r = linear(rgb[0]), g = linear(rgb[1]), b = linear(rgb[2]);
    auto         f = [](double t) { return t > 216.0 / 24389.0 ? std::cbrt(t) : (24389.0 / 27.0 * t + 16.0) / 116.0; };
    const double x = f((0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047);
    const double y = f(0.2126729 * r + 0.7151522 * g + 0.0721750 * b);
    const double z = f((0.0193339 * r + 0.1191920 * g + 0.9503041 * b) / 1.08883);
    return {116.0 * y - 16.0, 500.0 * (x - y), 200.0 * (y - z)};
}

double finite_number(const nlohmann::json& value)
{
    if (!value.is_number() || !std::isfinite(value.get<double>()))
        throw std::runtime_error("expected a finite number");
    return value.get<double>();
}

uint8_t component(const nlohmann::json& value)
{
    const std::string name = value.get<std::string>();
    if (name == "cyan")
        return 0;
    if (name == "magenta")
        return 1;
    if (name == "yellow")
        return 2;
    if (name == "white")
        return 3;
    throw std::runtime_error("unknown canonical CMYW component: " + name);
}

bool sha256_text(const std::string& value)
{
    return value.size() == 64 &&
           std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

} // namespace

std::optional<SurfaceMixRecipePreview> SurfaceMixRecipePreview::from_json(const nlohmann::json& root, std::string* error)
{
    if (error != nullptr)
        error->clear();
    std::string location = "catalogue";
    try {
        const nlohmann::json* catalog = &root;
        if (root.contains("catalog")) {
            if (root.at("report_schema_version") != "surfacemix.calibration-audit.v1" || root.at("audit_passed") != true)
                throw std::runtime_error("requires a passing Rust calibration-audit.v1 report");
            catalog = &root.at("catalog");
        }
        if (catalog->at("schema_version") != "surfacemix.calibration-catalog.v1" ||
            catalog->at("canonical_component_order") != nlohmann::json({"cyan", "magenta", "yellow", "white"}) ||
            catalog->at("top_depth_units") != "mm" || catalog->at("executable_assignment_enabled") != false)
            throw std::runtime_error("unsupported catalogue schema, units, component order or execution flag");
        const auto& sources = catalog->at("sources");
        if (!sources.is_array())
            throw std::runtime_error("sources must be an array");
        SurfaceMixRecipePreview result;
        result.m_catalogue_json = catalog->dump();
        for (const auto& source : sources) {
            if (source.at("role") != "top")
                continue; // Side offsets are not inward depth stacks.
            const std::string hash = source.at("source_sha256").get<std::string>();
            if (!sha256_text(hash))
                throw std::runtime_error("invalid source hash representation");
            const auto& records = source.at("records");
            if (!records.is_array())
                throw std::runtime_error("records must be an array");
            for (const auto& record : records) {
                location = record.at("source_pointer").get<std::string>();
                if (record.at("source_role") != "top")
                    throw std::runtime_error("record/source role mismatch");
                if (record.at("statuses").at("decoding").at("state") != "pass")
                    throw std::runtime_error("top record has not passed catalogue decoding");
                const auto& recipe   = record.at("recipe");
                const auto& measured = record.at("measured_rgb");
                if (recipe.at("kind") != "top_stack")
                    throw std::runtime_error("expected a complete top stack");
                const auto& encoding = measured.at("encoding");
                const auto& range    = measured.at("channel_range");
                if (!encoding.is_null() && encoding != "srgb" && encoding != "sRGB")
                    throw std::runtime_error("preview only supports sRGB, not the declared colour encoding");
                if (!range.is_null() && range != nlohmann::json({0.0, 1.0}))
                    throw std::runtime_error("preview requires normalized RGB channels in [0,1]");
                result.m_assumes_srgb |= encoding.is_null() || range.is_null();
                SurfaceMixPreviewRecipe decoded;
                decoded.sample_id      = record.at("sample_id").is_null() ? location : record.at("sample_id").get<std::string>();
                decoded.source_sha256  = hash;
                decoded.source_pointer = location;
                const auto& rgb        = measured.at("channels");
                if (!rgb.is_array() || rgb.size() != 3)
                    throw std::runtime_error("expected three measured RGB channels");
                for (size_t i = 0; i < 3; ++i) {
                    decoded.measured_rgb[i] = finite_number(rgb[i]);
                    if (decoded.measured_rgb[i] < 0.0 || decoded.measured_rgb[i] > 1.0)
                        throw std::runtime_error("measured RGB is outside [0,1]");
                }
                const auto& stack   = recipe.at("surface_to_deep");
                const auto& heights = recipe.at("layer_heights_mm");
                if (!stack.is_array() || stack.empty() || stack.size() > 4096 || !heights.is_array() || heights.size() != stack.size())
                    throw std::runtime_error("stack and layer heights must be complete and have equal lengths");
                double total = 0.0;
                for (size_t i = 0; i < stack.size(); ++i) {
                    decoded.surface_to_deep.push_back(component(stack[i]));
                    const double height = finite_number(heights[i]);
                    if (height <= 0.0)
                        throw std::runtime_error("layer heights must be positive");
                    decoded.layer_heights_mm.push_back(height);
                    total += height;
                }
                decoded.total_depth_mm = finite_number(recipe.at("total_depth_mm"));
                if (!std::isfinite(total) || total <= 0.0 || decoded.total_depth_mm <= 0.0 ||
                    std::abs(total - decoded.total_depth_mm) > std::max(1e-6, total * 1e-6))
                    throw std::runtime_error("total depth does not match the complete stack");
                result.m_maximum_depth_mm = std::max(result.m_maximum_depth_mm, total);
                result.m_lab.push_back(lab_from_srgb(decoded.measured_rgb));
                result.m_recipes.push_back(std::move(decoded));
                if (result.m_recipes.size() > 10000)
                    throw std::runtime_error("preview catalogue exceeds 10000 top records");
            }
        }
        if (result.m_recipes.empty())
            throw std::runtime_error("catalogue contains no measured top stacks");
        // Retain catalogue mirrors and equal-colour/different-order samples. Ties use source order.
        std::vector<size_t> indices(result.m_recipes.size());
        std::iota(indices.begin(), indices.end(), size_t(0));
        result.m_root = result.build_index(indices, 0, indices.size(), 0);
        return result;
    } catch (const std::exception& ex) {
        if (error != nullptr)
            *error = location + ": " + ex.what();
        return std::nullopt;
    }
}

int SurfaceMixRecipePreview::build_index(std::vector<size_t>& indices, size_t begin, size_t end, unsigned axis)
{
    if (begin == end)
        return -1;
    const size_t mid = begin + (end - begin) / 2;
    std::nth_element(indices.begin() + begin, indices.begin() + mid, indices.begin() + end, [this, axis](size_t a, size_t b) {
        return m_lab[a][axis] == m_lab[b][axis] ? a < b : m_lab[a][axis] < m_lab[b][axis];
    });
    const int node = int(m_nodes.size());
    m_nodes.push_back({indices[mid], -1, -1, axis});
    const int left              = build_index(indices, begin, mid, (axis + 1) % 3);
    const int right             = build_index(indices, mid + 1, end, (axis + 1) % 3);
    m_nodes[size_t(node)].left  = left;
    m_nodes[size_t(node)].right = right;
    return node;
}

void SurfaceMixRecipePreview::search(int node_id, const std::array<double, 3>& lab, size_t& best, double& distance) const
{
    if (node_id < 0)
        return;
    const Node& node    = m_nodes[size_t(node_id)];
    double      squared = 0.0;
    for (size_t i = 0; i < 3; ++i)
        squared += std::pow(lab[i] - m_lab[node.recipe][i], 2);
    if (squared < distance || (squared == distance && node.recipe < best)) {
        distance = squared;
        best     = node.recipe;
    }
    const double delta = lab[node.axis] - m_lab[node.recipe][node.axis];
    search(delta < 0.0 ? node.left : node.right, lab, best, distance);
    if (delta * delta <= distance)
        search(delta < 0.0 ? node.right : node.left, lab, best, distance);
}

size_t SurfaceMixRecipePreview::nearest_recipe(const std::array<double, 3>& target_srgb) const
{
    for (double c : target_srgb)
        if (!std::isfinite(c) || c < 0.0 || c > 1.0)
            throw std::invalid_argument("target RGB must be finite and normalized");
    size_t best     = 0;
    double distance = std::numeric_limits<double>::infinity();
    search(m_root, lab_from_srgb(target_srgb), best, distance);
    return best;
}

std::optional<uint8_t> SurfaceMixRecipePreview::component_at_depth(size_t recipe, double depth_mm) const
{
    if (recipe >= m_recipes.size() || !std::isfinite(depth_mm) || depth_mm < 0.0)
        return std::nullopt;
    const auto& stack = m_recipes[recipe];
    double      end   = 0.0;
    for (size_t i = 0; i < stack.surface_to_deep.size(); ++i) {
        end += stack.layer_heights_mm[i];
        // Summed 19 x 0.08 and the displayed 1.520 boundary can differ by a few ULPs.
        const double rounding = 8.0 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::abs(end));
        if (depth_mm < end && end - depth_mm > rounding)
            return stack.surface_to_deep[i];
    }
    return std::nullopt;
}

std::array<double, 3> SurfaceMixRecipePreview::display_colour(size_t recipe, SurfaceMixPreviewMode mode, double depth_mm) const
{
    if (mode == SurfaceMixPreviewMode::MeasuredColour)
        return m_recipes.at(recipe).measured_rgb;
    if (mode == SurfaceMixPreviewMode::DepthComponent) {
        static constexpr std::array<std::array<double, 3>, 4> labels{{{0.0, 1.0, 1.0}, {1.0, 0.0, 1.0}, {1.0, 1.0, 0.0}, {1.0, 1.0, 1.0}}};
        if (const auto c = component_at_depth(recipe, depth_mm))
            return labels[*c];
    }
    return {0.35, 0.35, 0.35}; // No interval here; not structural-core or default-solid credit.
}

std::vector<unsigned char> SurfaceMixRecipePreview::render_rgba(const std::vector<unsigned char>& source,
                                                                unsigned                          width,
                                                                unsigned                          height,
                                                                unsigned                          output_width,
                                                                unsigned                          output_height,
                                                                SurfaceMixPreviewMode             mode,
                                                                double                            depth_mm,
                                                                const std::function<bool()>&      cancelled) const
{
    if (width == 0 || height == 0 || output_width == 0 || output_height == 0 || uint64_t(width) * height > source.size() / 4 ||
        uint64_t(output_width) * output_height > 16777216 || mode == SurfaceMixPreviewMode::Off || !std::isfinite(depth_mm) ||
        depth_mm < 0.0)
        return {};
    std::vector<unsigned char>           output(size_t(output_width) * output_height * 4);
    std::unordered_map<uint32_t, size_t> cache;
    for (unsigned y = 0; y < output_height; ++y) {
        if (cancelled && cancelled())
            return {};
        const size_t sy = std::min(size_t((double(y) + 0.5) * height / output_height), size_t(height - 1));
        for (unsigned x = 0; x < output_width; ++x) {
            const size_t   sx     = std::min(size_t((double(x) + 0.5) * width / output_width), size_t(width - 1));
            const size_t   in     = (sy * width + sx) * 4;
            const size_t   out    = (size_t(y) * output_width + x) * 4;
            const uint32_t key    = uint32_t(source[in]) << 16 | uint32_t(source[in + 1]) << 8 | source[in + 2];
            const auto     found  = cache.find(key);
            const size_t   recipe = found != cache.end() ?
                                        found->second :
                                        nearest_recipe({source[in] / 255.0, source[in + 1] / 255.0, source[in + 2] / 255.0});
            if (found == cache.end() && cache.size() < 65536)
                cache.emplace(key, recipe);
            const auto rgb = display_colour(recipe, mode, depth_mm);
            for (size_t i = 0; i < 3; ++i)
                output[out + i] = static_cast<unsigned char>(std::lround(rgb[i] * 255.0));
            output[out + 3] = source[in + 3];
        }
    }
    return output;
}

} // namespace Slic3r
