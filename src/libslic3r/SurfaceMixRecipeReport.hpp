#ifndef slic3r_SurfaceMixRecipeReport_hpp_
#define slic3r_SurfaceMixRecipeReport_hpp_

#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r {

struct SurfaceMixRecipeInterval
{
    uint8_t component{0};
    int64_t start_um{0};
    int64_t end_um{0};
};

struct SurfaceMixRecipePlan
{
    std::string                        sample_id;
    std::string                        source_pointer;
    std::vector<SurfaceMixRecipeInterval> intervals;
    int64_t                            total_depth_um{0};
    bool                               valid{false};
};

struct SurfaceMixRecipeReport
{
    std::string schema;
    std::string recipe_report_schema;
    std::map<std::string, std::string> source_sha256;
    std::size_t triangle_count{0};
    std::size_t matched_triangle_count{0};
    std::size_t summary_count{0};
    bool execution_enabled{false};
    bool depth_stack_encoded{false};
    bool dithering_enabled{false};
    std::vector<SurfaceMixRecipePlan> plans;

    bool diagnostic_only() const;
    bool valid(std::string* error = nullptr) const;

    // This consumer validates metadata only. It never returns a tool assignment.
    static std::optional<SurfaceMixRecipeReport> from_json(const nlohmann::json& envelope,
                                                           const nlohmann::json& depth_plan,
                                                           std::string* error = nullptr);
};

} // namespace Slic3r

#endif
