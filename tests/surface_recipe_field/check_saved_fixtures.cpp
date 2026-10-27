#include "libslic3r/SurfaceRecipeField.hpp"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using nlohmann::json;

namespace {

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

json check(const char* path)
{
    std::ifstream input(path);
    require(bool(input), "could not open saved fixture");
    json report;
    input >> report;
    require(report.value("schema", std::string()) == "surfacemix.surface-recipe-field-diagnostic.v1",
            "expected a planar diagnostic report, not an executable plan");
    const auto& source = report.at("field");
    std::string error;
    const auto  field = Slic3r::SurfaceRecipeField::from_json(source, &error);
    const auto  name  = report.at("fixture").get<std::string>();
    if (name == "hybrid_rejected") {
        require(!field && !error.empty(), "uncalibrated hybrid must remain rejected");
    } else {
        require(field.has_value(), error.empty() ? "saved fixture rejected" : error.c_str());
        require(field->planar_fixture_only, "fixture scope must not become product scope");
        require(field->cells.size() == source.at("cells").size(), "cell count changed");
        require(field->stacks.size() == source.at("stacks").size(), "stack count changed");
        for (size_t i = 0; i < field->cells.size(); ++i) {
            const auto& cell     = field->cells[i];
            const auto& original = source.at("cells").at(i);
            require(cell.cell_id == original.at("cell_id").get<std::string>(), "cell identity changed");
            require(cell.stack_id == original.at("stack_selection").at("stack_id").get<std::string>(), "whole-stack choice changed");
        }
        for (size_t i = 0; i < field->stacks.size(); ++i) {
            const auto& stack    = field->stacks[i];
            const auto& original = source.at("stacks").at(i);
            const auto& recipe   = original.at("calibration");
            require(stack.stack_id == original.at("stack_id").get<std::string>(), "stack identity changed");
            require(stack.calibration_sha256 == original.at("calibration_content_sha256").get<std::string>(), "stack hash changed");
            require(stack.total_depth_um == recipe.at("total_depth_um"), "total depth changed");
            require(stack.surface_to_deep.size() == recipe.at("intervals_um").size(), "recipe truncated");
            for (size_t j = 0; j < stack.surface_to_deep.size(); ++j) {
                const auto& span = stack.surface_to_deep[j];
                require(span.component == recipe.at("component_ids_surface_to_deep").at(j), "component order changed");
                require(span.start_um == recipe.at("intervals_um").at(j).at(0), "interval start changed");
                require(span.end_um == recipe.at("intervals_um").at(j).at(1), "interval end changed");
            }
        }
        require(field->transition_bands.size() == source.at("transition_bands").size(), "transition count changed");
        for (size_t i = 0; i < field->transition_bands.size(); ++i) {
            const auto& band     = field->transition_bands[i];
            const auto& original = source.at("transition_bands").at(i);
            require(band.left_cell_id == original.at("left_cell_id").get<std::string>(), "left transition owner changed");
            require(band.right_cell_id == original.at("right_cell_id").get<std::string>(), "right transition owner changed");
            const char* rule = band.rule == Slic3r::SurfaceRecipeBoundaryRule::HardBoundary ? "hard_boundary" : "unresolved_band";
            require(original.at("rule") == rule, "transition rule changed");
            for (size_t point = 0; point < 2; ++point)
                for (size_t axis = 0; axis < 2; ++axis)
                    require(band.chart_bounds_um[point][axis] == original.at("chart_uv_bounds_um").at(point).at(axis),
                            "transition bounds changed");
        }
        const auto& backing = source.at("backing_core");
        require(field->backing_core.backing_depth_um == backing.at("backing_depth_um"), "backing depth changed");
        require(field->backing_core.core_guard_depth_um == backing.at("core_guard_depth_um"), "guard depth changed");
        require(field->backing_core.backing_component == backing.at("backing_component"), "backing component changed");
        require(field->backing_core.structural_core_component == backing.at("structural_core_component"), "core component changed");
    }
    require(report.at("colour_assignment_enabled") == false && report.at("slicing_enabled") == false &&
                report.at("printing_enabled") == false,
            "saved evidence must remain diagnostic-only");
    return {{"fixture", name},
            {"source", path},
            {"expected_rejection", name == "hybrid_rejected"},
            {"inspection_check_passed", true},
            {"geometry_feasible", report.at("geometry_feasible")},
            {"actual_orca_path_coverage", report.at("actual_orca_path_coverage")},
            {"colour_assignment_enabled", false},
            {"slicing_enabled", false},
            {"printing_enabled", false}};
}

} // namespace

int main(int argc, char** argv)
{
    json reports = json::array();
    bool failed  = argc < 2;
    for (int i = 1; i < argc; ++i) {
        try {
            reports.push_back(check(argv[i]));
        } catch (const std::exception& error) {
            failed = true;
            reports.push_back({{"source", argv[i]}, {"inspection_check_passed", false}, {"error", error.what()}});
        }
    }
    std::cout << json{{"scope", "saved planar fixture import; no model painting"}, {"reports", reports}, {"passed", !failed}}.dump(2)
              << '\n';
    return failed ? 1 : 0;
}
