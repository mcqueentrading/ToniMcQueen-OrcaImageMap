#include "SurfaceRecipeField.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <set>

namespace Slic3r {

namespace {

constexpr const char* SUPPORTED_SCHEMA = "surfacemix.surface-recipe-field.fixture.v1";

void fail(std::string* error, const std::string& message)
{
    if (error != nullptr)
        *error = message;
}

bool parse_integer(const nlohmann::json& value, int64_t& out)
{
    if (value.is_number_unsigned()) {
        const auto number = value.get<uint64_t>();
        if (number > uint64_t(std::numeric_limits<int64_t>::max()))
            return false;
        out = int64_t(number);
        return true;
    }
    if (!value.is_number_integer())
        return false;
    out = value.get<int64_t>();
    return true;
}

bool parse_component(const nlohmann::json& value, uint8_t& out)
{
    int64_t number = 0;
    if (!parse_integer(value, number) || number < 0 || number > 3)
        return false;
    out = uint8_t(number);
    return true;
}

bool valid_bounds(const std::array<std::array<int64_t, 2>, 2>& bounds, bool allow_boundary)
{
    if (allow_boundary)
        return bounds[0][0] <= bounds[1][0] && bounds[0][1] <= bounds[1][1] && bounds[0] != bounds[1];
    return bounds[0][0] < bounds[1][0] && bounds[0][1] < bounds[1][1];
}

bool parse_bounds(const nlohmann::json& value, std::array<std::array<int64_t, 2>, 2>& out, bool allow_boundary = false)
{
    if (!value.is_array() || value.size() != 2 || !value[0].is_array() || !value[1].is_array() || value[0].size() != 2 ||
        value[1].size() != 2)
        return false;
    for (size_t i = 0; i < 2; ++i)
        for (size_t j = 0; j < 2; ++j) {
            if (!parse_integer(value[i][j], out[i][j]))
                return false;
        }
    return valid_bounds(out, allow_boundary);
}

bool parse_stack(const nlohmann::json& value, SurfaceRecipeStack& out, std::string* error)
{
    if (!value.is_object() || !value.value("stack_id", std::string()).size()) {
        fail(error, "stack missing stack_id");
        return false;
    }
    out.stack_id                      = value.at("stack_id").get<std::string>();
    out.calibration_sha256            = value.value("calibration_content_sha256", std::string());
    const nlohmann::json& calibration = value.contains("calibration") ? value.at("calibration") : value;
    if (!calibration.is_object()) {
        fail(error, out.stack_id + ": calibration is not an object");
        return false;
    }
    if (!calibration.contains("component_ids_surface_to_deep") || !calibration.contains("intervals_um")) {
        fail(error, out.stack_id + ": incomplete ordered stack");
        return false;
    }
    const auto& components = calibration.at("component_ids_surface_to_deep");
    const auto& intervals  = calibration.at("intervals_um");
    if (!components.is_array() || !intervals.is_array() || components.size() == 0 || components.size() != intervals.size()) {
        fail(error, out.stack_id + ": component/interval count mismatch");
        return false;
    }
    for (size_t i = 0; i < components.size(); ++i) {
        SurfaceRecipeInterval interval;
        if (!parse_component(components[i], interval.component)) {
            fail(error, out.stack_id + ": component must be an integer in 0..3");
            return false;
        }
        if (!intervals[i].is_array() || intervals[i].size() != 2 || !parse_integer(intervals[i][0], interval.start_um) ||
            !parse_integer(intervals[i][1], interval.end_um)) {
            fail(error, out.stack_id + ": malformed interval");
            return false;
        }
        out.surface_to_deep.push_back(interval);
    }
    if (!calibration.contains("total_depth_um") || !parse_integer(calibration.at("total_depth_um"), out.total_depth_um)) {
        fail(error, out.stack_id + ": total depth must be an int64 micrometre value");
        return false;
    }
    if (out.calibration_sha256.empty())
        out.calibration_sha256 = calibration.value("profile_sha256", std::string());
    return out.valid(error);
}

bool parse_backing(const nlohmann::json& value, SurfaceRecipeBackingCore& out, std::string* error)
{
    if (!value.is_object()) {
        fail(error, "backing/core must be an object");
        return false;
    }
    if ((value.contains("backing_depth_um") && !parse_integer(value.at("backing_depth_um"), out.backing_depth_um)) ||
        (value.contains("core_guard_depth_um") && !parse_integer(value.at("core_guard_depth_um"), out.core_guard_depth_um)) ||
        (value.contains("backing_component") && !parse_component(value.at("backing_component"), out.backing_component)) ||
        (value.contains("structural_core_component") &&
         !parse_component(value.at("structural_core_component"), out.structural_core_component))) {
        fail(error, "backing/core has an invalid component or int64 depth");
        return false;
    }
    return true;
}

bool parse_transition(const nlohmann::json& value, SurfaceRecipeTransitionBand& out, std::string* error)
{
    if (!value.is_object()) {
        fail(error, "transition band must be an object");
        return false;
    }
    out.left_cell_id  = value.value("left_cell_id", std::string());
    out.right_cell_id = value.value("right_cell_id", std::string());
    const auto rule   = value.value("rule", std::string());
    if (rule == "hard_boundary")
        out.rule = SurfaceRecipeBoundaryRule::HardBoundary;
    else if (rule == "unresolved_band")
        out.rule = SurfaceRecipeBoundaryRule::UnresolvedBand;
    else {
        fail(error, "transition band has an unsupported boundary rule");
        return false;
    }
    if (!value.contains("chart_uv_bounds_um") ||
        !parse_bounds(value.at("chart_uv_bounds_um"), out.chart_bounds_um, out.rule == SurfaceRecipeBoundaryRule::HardBoundary)) {
        fail(error, "transition band has invalid chart bounds");
        return false;
    }
    return true;
}

} // namespace

bool SurfaceRecipeStack::valid(std::string* error) const
{
    if (stack_id.empty() || surface_to_deep.empty() || total_depth_um <= 0) {
        fail(error, "stack has incomplete identity or depth");
        return false;
    }
    int64_t previous = 0;
    for (const auto& interval : surface_to_deep) {
        if (interval.component > 3 || interval.start_um != previous || interval.end_um <= interval.start_um) {
            fail(error, stack_id + ": intervals are not complete surface-to-deep CMYW coverage");
            return false;
        }
        previous = interval.end_um;
    }
    if (previous != total_depth_um) {
        fail(error, stack_id + ": interval depth does not equal calibrated total depth");
        return false;
    }
    return true;
}

std::optional<SurfaceRecipeField> SurfaceRecipeField::from_json(const nlohmann::json& root, std::string* error)
try {
    if (error != nullptr)
        error->clear();
    if (!root.is_object()) {
        fail(error, "recipe field root is not an object");
        return std::nullopt;
    }
    const nlohmann::json* field = &root;
    if (root.contains("SurfaceRecipeField"))
        field = &root.at("SurfaceRecipeField");
    if (!field->is_object() || !field->contains("stacks") || !field->contains("cells")) {
        fail(error, "recipe field requires stacks and cells");
        return std::nullopt;
    }
    SurfaceRecipeField out;
    out.schema = root.value("schema", field->value("schema", std::string()));
    if (out.schema != SUPPORTED_SCHEMA || field->value("schema", out.schema) != out.schema) {
        fail(error, "unsupported or conflicting recipe-field schema");
        return std::nullopt;
    }
    if (!field->at("stacks").is_array() || !field->at("cells").is_array()) {
        fail(error, "recipe field stacks and cells must be arrays");
        return std::nullopt;
    }
    const auto contract     = field->value("coordinate_contract", nlohmann::json::object());
    out.planar_fixture_only = contract.value("fixture_chart_is_planar", false) &&
                              !contract.value("curved_surface_mapping_implemented", false);
    for (const auto& item : field->at("stacks")) {
        SurfaceRecipeStack stack;
        if (!parse_stack(item, stack, error))
            return std::nullopt;
        if (std::any_of(out.stacks.begin(), out.stacks.end(), [&](const auto& s) { return s.stack_id == stack.stack_id; })) {
            fail(error, "duplicate stack identity: " + stack.stack_id);
            return std::nullopt;
        }
        out.stacks.push_back(std::move(stack));
    }
    for (const auto& item : field->at("cells")) {
        SurfaceRecipeCell cell;
        cell.cell_id         = item.value("cell_id", std::string());
        cell.material_region = item.value("material_region", std::string());
        if (!item.contains("chart_uv_bounds_um") || !parse_bounds(item.at("chart_uv_bounds_um"), cell.chart_bounds_um)) {
            fail(error, cell.cell_id + ": invalid chart bounds");
            return std::nullopt;
        }
        const auto selection = item.value("stack_selection", nlohmann::json::object());
        if (selection.value("selection", std::string()) != "whole_measured_stack" || !selection.contains("stack_id")) {
            fail(error, cell.cell_id + ": uncalibrated hybrids are not importable");
            return std::nullopt;
        }
        cell.stack_id = selection.at("stack_id").get<std::string>();
        out.cells.push_back(std::move(cell));
    }
    // Preserve the legacy envelope location, but never silently choose between conflicting records.
    if (field != &root && field->contains("backing_core") && root.contains("backing_core") &&
        field->at("backing_core") != root.at("backing_core")) {
        fail(error, "conflicting backing/core records in envelope and field");
        return std::nullopt;
    }
    const nlohmann::json* backing = field->contains("backing_core") ? &field->at("backing_core") :
                                    root.contains("backing_core")   ? &root.at("backing_core") :
                                                                      nullptr;
    if (backing != nullptr && !parse_backing(*backing, out.backing_core, error))
        return std::nullopt;
    if (field->contains("transition_bands")) {
        const auto& bands = field->at("transition_bands");
        if (!bands.is_array()) {
            fail(error, "transition bands must be an array");
            return std::nullopt;
        }
        for (const auto& item : bands) {
            SurfaceRecipeTransitionBand band;
            if (!parse_transition(item, band, error))
                return std::nullopt;
            out.transition_bands.push_back(std::move(band));
        }
    }
    if (!out.valid(error))
        return std::nullopt;
    return out;
} catch (const nlohmann::json::exception& exception) {
    fail(error, std::string("malformed recipe field: ") + exception.what());
    return std::nullopt;
}

bool SurfaceRecipeField::valid(std::string* error) const
{
    if (schema != SUPPORTED_SCHEMA || stacks.empty() || cells.empty()) {
        fail(error, "recipe field has incomplete identity");
        return false;
    }
    if (backing_core.backing_component > 3 || backing_core.structural_core_component > 3 || backing_core.backing_depth_um < 0 ||
        backing_core.core_guard_depth_um < 0) {
        fail(error, "invalid backing/core contract");
        return false;
    }
    std::set<std::string> cell_ids;
    for (const auto& cell : cells) {
        if (cell.cell_id.empty() || !cell_ids.insert(cell.cell_id).second || !stack_for_cell(cell) ||
            !valid_bounds(cell.chart_bounds_um, false)) {
            fail(error, "cell has duplicate identity or unknown complete stack: " + cell.cell_id);
            return false;
        }
    }
    for (const auto& stack : stacks)
        if (!stack.valid(error))
            return false;
    for (const auto& band : transition_bands) {
        if (band.left_cell_id == band.right_cell_id || cell_ids.count(band.left_cell_id) == 0 || cell_ids.count(band.right_cell_id) == 0 ||
            (band.rule != SurfaceRecipeBoundaryRule::HardBoundary && band.rule != SurfaceRecipeBoundaryRule::UnresolvedBand) ||
            !valid_bounds(band.chart_bounds_um, band.rule == SurfaceRecipeBoundaryRule::HardBoundary)) {
            fail(error, "transition band has unknown/same cell references or invalid bounds");
            return false;
        }
    }
    return true;
}

const SurfaceRecipeStack* SurfaceRecipeField::stack_for_cell(const SurfaceRecipeCell& cell) const
{
    const auto it = std::find_if(stacks.begin(), stacks.end(), [&](const auto& stack) { return stack.stack_id == cell.stack_id; });
    return it == stacks.end() ? nullptr : &*it;
}

std::optional<uint8_t> SurfaceRecipeField::component_at_depth(const SurfaceRecipeCell& cell, int64_t depth_um) const
{
    const auto* stack = stack_for_cell(cell);
    if (stack == nullptr || depth_um < 0 || depth_um >= stack->total_depth_um)
        return std::nullopt;
    const auto it = std::find_if(stack->surface_to_deep.begin(), stack->surface_to_deep.end(),
                                 [&](const auto& interval) { return depth_um >= interval.start_um && depth_um < interval.end_um; });
    return it == stack->surface_to_deep.end() ? std::nullopt : std::optional<uint8_t>(it->component);
}

} // namespace Slic3r
