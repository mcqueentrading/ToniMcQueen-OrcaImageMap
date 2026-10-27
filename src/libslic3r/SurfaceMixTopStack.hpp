#ifndef slic3r_SurfaceMixTopStack_hpp_
#define slic3r_SurfaceMixTopStack_hpp_

#include "SurfaceMixRecipePreview.hpp"

#include <memory>

namespace Slic3r {

// Opt-in execution transport for complete Rust catalogue recipes, not a new recipe generator.
class SurfaceMixTopStack
{
public:
    static constexpr const char*             transport_key = "surfacemix_top_stacks";
    static constexpr size_t                  default_execution_layer_count = 3;
    static std::optional<SurfaceMixTopStack> from_calibration_json(const std::string&               calibration_json,
                                                                   const std::vector<unsigned int>& available_slots,
                                                                   std::string*                     error = nullptr);

    // The catalogue retains the complete measured specimen; execution uses the
    // explicitly requested top-surface prefix.
    size_t                              layer_count() const { return m_execution_layer_count; }
    size_t                              calibration_layer_count() const { return m_catalogue->recipes().front().layer_heights_mm.size(); }
    const std::string&                  cache_key() const { return m_cache_key; }
    bool                                compatible(int                       stack_layers,
                                                   int                       visible_layers,
                                                   const std::vector<float>& actual_heights_mm,
                                                   const std::vector<int>&   actual_layer_ids,
                                                   bool                      lower_surface,
                                                   std::string*              error = nullptr) const;
    std::vector<unsigned int>           bottom_to_top(const std::array<float, 3>& target_rgb) const;
    std::vector<unsigned int>           bottom_to_top(const std::array<float, 3>& target_rgb,
                                                     size_t                         execution_layers) const;
    std::array<float, 3>                measured_rgb(const std::array<float, 3>& target_rgb) const;
    std::optional<std::array<float, 3>> measured_stack_rgb(const std::vector<unsigned int>& bottom_to_top) const;

private:
    std::shared_ptr<const SurfaceMixRecipePreview> m_catalogue;
    std::array<unsigned int, 4>                    m_slots{};
    size_t                                          m_execution_layer_count = default_execution_layer_count;
    std::string                                    m_cache_key;
};

} // namespace Slic3r

#endif
