#include "SurfaceMixRecipeReport.hpp"

#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>

namespace Slic3r {
namespace {

using nlohmann::json;

void fail(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
}

bool string_value(const json& object, const char* key, std::string& destination, std::string* error)
{
    if (!object.contains(key) || !object.at(key).is_string()) {
        fail(error, std::string("missing or non-string field: ") + key);
        return false;
    }
    destination = object.at(key).get<std::string>();
    return true;
}

bool finite_number(const json& value)
{
    return value.is_number() && std::isfinite(value.get<double>());
}

bool size_value(const json& object, const char* key, std::size_t& destination, std::string* error)
{
    if (!object.contains(key) || !object.at(key).is_number_unsigned()) {
        fail(error, std::string("missing or non-unsigned field: ") + key);
        return false;
    }
    destination = object.at(key).get<std::size_t>();
    return true;
}

bool bool_value(const json& object, const char* key, bool& destination, std::string* error)
{
    if (!object.contains(key) || !object.at(key).is_boolean()) {
        fail(error, std::string("missing or non-boolean field: ") + key);
        return false;
    }
    destination = object.at(key).get<bool>();
    return true;
}

bool micrometres(double millimetres, int64_t& result, std::string* error)
{
    constexpr double scale = 1000.0;
    const double scaled = millimetres * scale;
    if (!std::isfinite(scaled) || scaled < 0.0 || scaled > static_cast<double>(std::numeric_limits<int64_t>::max())) {
        fail(error, "depth value is outside the supported micrometre range");
        return false;
    }
    result = static_cast<int64_t>(std::llround(scaled));
    return true;
}

bool parse_interval(const json& value, SurfaceMixRecipeInterval& result, std::string* error)
{
    if (!value.is_object() || !value.contains("component_id") || !value.at("component_id").is_number_integer() ||
        value.at("component_id").get<int64_t>() < 0 || value.at("component_id").get<int64_t>() > 3) {
        fail(error, "interval has an invalid component_id");
        return false;
    }
    if (!value.contains("start_mm") || !value.contains("end_mm") || !finite_number(value.at("start_mm")) ||
        !finite_number(value.at("end_mm"))) {
        fail(error, "interval has invalid start_mm or end_mm");
        return false;
    }
    result.component = static_cast<uint8_t>(value.at("component_id").get<int64_t>());
    if (!micrometres(value.at("start_mm").get<double>(), result.start_um, error) ||
        !micrometres(value.at("end_mm").get<double>(), result.end_um, error))
        return false;
    if (result.end_um <= result.start_um) {
        fail(error, "interval end must be greater than start");
        return false;
    }
    return true;
}

bool parse_plan(const json& value, SurfaceMixRecipePlan& result, std::string* error)
{
    if (!value.is_object() || !string_value(value, "sample_id", result.sample_id, error) ||
        !string_value(value, "source_pointer", result.source_pointer, error))
        return false;
    if (!value.contains("valid") || !value.at("valid").is_boolean() || !value.at("intervals").is_array() ||
        !value.contains("total_depth_mm") || !finite_number(value.at("total_depth_mm"))) {
        fail(error, "recipe stack is missing valid, intervals, or total_depth_mm");
        return false;
    }
    result.valid = value.at("valid").get<bool>();
    if (!micrometres(value.at("total_depth_mm").get<double>(), result.total_depth_um, error))
        return false;
    int64_t expected_start = 0;
    for (const json& interval_json : value.at("intervals")) {
        SurfaceMixRecipeInterval interval;
        if (!parse_interval(interval_json, interval, error))
            return false;
        if (interval.start_um != expected_start) {
            fail(error, "recipe intervals are not contiguous from the surface");
            return false;
        }
        expected_start = interval.end_um;
        result.intervals.push_back(interval);
    }
    if (result.intervals.empty() || expected_start != result.total_depth_um) {
        fail(error, "recipe intervals do not cover total_depth_mm");
        return false;
    }
    return true;
}

} // namespace

bool SurfaceMixRecipeReport::diagnostic_only() const
{
    return !execution_enabled && !depth_stack_encoded && !dithering_enabled;
}

bool SurfaceMixRecipeReport::valid(std::string* error) const
{
    if (schema.empty() || recipe_report_schema.empty() || plans.empty()) {
        fail(error, "report is missing schema or recipe plans");
        return false;
    }
    for (const SurfaceMixRecipePlan& plan : plans) {
        if (!plan.valid || plan.intervals.empty()) {
            fail(error, "report contains an invalid recipe plan");
            return false;
        }
    }
    return true;
}

std::optional<SurfaceMixRecipeReport> SurfaceMixRecipeReport::from_json(const nlohmann::json& envelope,
                                                                         const nlohmann::json& depth_plan,
                                                                         std::string* error)
{
    if (!envelope.is_object() || !depth_plan.is_object()) {
        fail(error, "SurfaceMix report and depth plan must be JSON objects");
        return std::nullopt;
    }
    SurfaceMixRecipeReport result;
    if (!string_value(envelope, "schema", result.schema, error) || !envelope.contains("source_recipe_report") ||
        !envelope.at("source_recipe_report").is_object() || !string_value(envelope.at("source_recipe_report"), "schema", result.recipe_report_schema, error) ||
        !size_value(envelope.at("source_recipe_report"), "triangle_count", result.triangle_count, error) ||
        !size_value(envelope.at("source_recipe_report"), "matched_triangle_count", result.matched_triangle_count, error) ||
        !size_value(envelope.at("source_recipe_report"), "unique_target_count", result.summary_count, error) ||
        !bool_value(envelope, "execution_enabled", result.execution_enabled, error) ||
        !bool_value(envelope, "depth_stack_encoded", result.depth_stack_encoded, error) ||
        !bool_value(envelope, "dithering_enabled", result.dithering_enabled, error))
        return std::nullopt;

    const json& source_report = envelope.at("source_recipe_report");
    if (!source_report.contains("source_sha256") || !source_report.at("source_sha256").is_object()) {
        fail(error, "source_recipe_report is missing source_sha256");
        return std::nullopt;
    }
    for (const auto& [name, hash] : source_report.at("source_sha256").items()) {
        if (!hash.is_string()) {
            fail(error, "source_sha256 entries must be strings");
            return std::nullopt;
        }
        result.source_sha256.emplace(name, hash.get<std::string>());
    }
    if (result.source_sha256.empty()) {
        fail(error, "source_sha256 must contain at least one hash");
        return std::nullopt;
    }
    if (!depth_plan.contains("stacks") || !depth_plan.at("stacks").is_array()) {
        fail(error, "depth plan is missing stacks");
        return std::nullopt;
    }
    for (const json& stack_json : depth_plan.at("stacks")) {
        SurfaceMixRecipePlan plan;
        if (!parse_plan(stack_json, plan, error))
            return std::nullopt;
        result.plans.push_back(std::move(plan));
    }
    if (!result.valid(error))
        return std::nullopt;
    return result;
}

} // namespace Slic3r
