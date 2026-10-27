#ifndef slic3r_SurfaceMixSideOffsets_hpp_
#define slic3r_SurfaceMixSideOffsets_hpp_

#include <nlohmann/json_fwd.hpp>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r {

struct SurfaceMixSideRecipe
{
    std::string           sample_id, source_pointer, source_sha256;
    std::array<double, 3> measured_rgb;
    std::array<double, 4> raw_offsets, amplitudes;
    double                max_offset;
};

// Independent side-offset amplitudes, not mixture fractions or inward depths.
class SurfaceMixSideOffsets
{
public:
    static constexpr const char*                transport_key = "surfacemix_side_offsets";
    static std::optional<SurfaceMixSideOffsets> from_catalogue(const nlohmann::json&, std::string* error = nullptr);
    static std::optional<SurfaceMixSideOffsets> from_calibration_json(const std::string&,
                                                                      const std::vector<unsigned int>& available_slots,
                                                                      std::string*                     error = nullptr);
    const std::vector<SurfaceMixSideRecipe>&    recipes() const { return m_recipes; }
    const std::array<unsigned int, 4>&          slots() const { return m_slots; }
    const std::string&                          catalogue_json() const { return m_catalogue; }
    bool                                        assumes_srgb() const { return m_assumes_srgb; }

private:
    std::vector<SurfaceMixSideRecipe> m_recipes;
    std::array<unsigned int, 4>       m_slots{{1, 2, 3, 4}};
    std::string                       m_catalogue;
    bool                              m_assumes_srgb{false};
};
} // namespace Slic3r
#endif
