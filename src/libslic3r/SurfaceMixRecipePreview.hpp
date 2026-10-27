#ifndef slic3r_SurfaceMixRecipePreview_hpp_
#define slic3r_SurfaceMixRecipePreview_hpp_

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r {

enum class SurfaceMixPreviewMode { Off, MeasuredColour, DepthComponent };

struct SurfaceMixPreviewRecipe
{
    std::string           sample_id;
    std::string           source_sha256;
    std::string           source_pointer;
    std::array<double, 3> measured_rgb{};
    std::vector<uint8_t>  surface_to_deep;
    std::vector<double>   layer_heights_mm;
    double                total_depth_mm{0.0};
};

// Immutable display data from the Rust catalogue, never a filament-assignment plan.
class SurfaceMixRecipePreview
{
public:
    static std::optional<SurfaceMixRecipePreview> from_json(const nlohmann::json& root, std::string* error = nullptr);

    const std::vector<SurfaceMixPreviewRecipe>& recipes() const { return m_recipes; }
    bool                                        assumes_srgb() const { return m_assumes_srgb; }
    double                                      maximum_depth_mm() const { return m_maximum_depth_mm; }
    size_t                                      nearest_recipe(const std::array<double, 3>& target_srgb) const;
    const std::string&                          catalogue_json() const { return m_catalogue_json; }
    std::optional<uint8_t>                      component_at_depth(size_t recipe, double depth_mm) const;
    std::array<double, 3>                       display_colour(size_t recipe, SurfaceMixPreviewMode mode, double depth_mm) const;
    std::vector<unsigned char>                  render_rgba(const std::vector<unsigned char>& source,
                                                            unsigned                          width,
                                                            unsigned                          height,
                                                            unsigned                          output_width,
                                                            unsigned                          output_height,
                                                            SurfaceMixPreviewMode             mode,
                                                            double                            depth_mm,
                                                            const std::function<bool()>&      cancelled = {}) const;

private:
    SurfaceMixRecipePreview() = default;

    struct Node
    {
        size_t   recipe{0};
        int      left{-1};
        int      right{-1};
        unsigned axis{0};
    };
    int  build_index(std::vector<size_t>& indices, size_t begin, size_t end, unsigned axis);
    void search(int node, const std::array<double, 3>& lab, size_t& best, double& distance) const;

    std::vector<SurfaceMixPreviewRecipe> m_recipes;
    std::vector<std::array<double, 3>>   m_lab;
    std::vector<Node>                    m_nodes;
    int                                  m_root{-1};
    bool                                 m_assumes_srgb{false};
    double                               m_maximum_depth_mm{0.0};
    std::string                          m_catalogue_json;
};

} // namespace Slic3r

#endif
