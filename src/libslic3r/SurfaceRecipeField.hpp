// Strict, opt-in bridge for the SurfaceMix recipe-field contract.
#ifndef slic3r_SurfaceRecipeField_hpp_
#define slic3r_SurfaceRecipeField_hpp_

#include <array>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r {

struct SurfaceRecipeInterval
{
    uint8_t component{0};
    int64_t start_um{0};
    int64_t end_um{0};
};

struct SurfaceRecipeStack
{
    std::string                        stack_id;
    std::string                        calibration_sha256;
    std::vector<SurfaceRecipeInterval> surface_to_deep;
    int64_t                            total_depth_um{0};

    bool valid(std::string* error = nullptr) const;
};

struct SurfaceRecipeCell
{
    std::string                           cell_id;
    std::array<std::array<int64_t, 2>, 2> chart_bounds_um{};
    std::string                           stack_id;
    std::string                           material_region;
};

enum class SurfaceRecipeBoundaryRule { HardBoundary, UnresolvedBand };

struct SurfaceRecipeTransitionBand
{
    std::string                           left_cell_id;
    std::string                           right_cell_id;
    std::array<std::array<int64_t, 2>, 2> chart_bounds_um{};
    SurfaceRecipeBoundaryRule             rule{SurfaceRecipeBoundaryRule::HardBoundary};
};

struct SurfaceRecipeBackingCore
{
    int64_t backing_depth_um{0};
    uint8_t backing_component{3};
    int64_t core_guard_depth_um{0};
    uint8_t structural_core_component{3};
};

struct SurfaceRecipeField
{
    std::string                              schema;
    bool                                     planar_fixture_only{false};
    std::vector<SurfaceRecipeCell>           cells;
    std::vector<SurfaceRecipeStack>          stacks;
    std::vector<SurfaceRecipeTransitionBand> transition_bands;
    SurfaceRecipeBackingCore                 backing_core;

    // Imports only complete measured stacks. It never creates a hybrid or falls back.
    static std::optional<SurfaceRecipeField> from_json(const nlohmann::json& root, std::string* error = nullptr);
    bool                                     valid(std::string* error = nullptr) const;
    const SurfaceRecipeStack*                stack_for_cell(const SurfaceRecipeCell& cell) const;
    std::optional<uint8_t>                   component_at_depth(const SurfaceRecipeCell& cell, int64_t depth_um) const;
};

} // namespace Slic3r

#endif
