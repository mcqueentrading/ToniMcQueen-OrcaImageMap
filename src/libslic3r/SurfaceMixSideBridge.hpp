#ifndef slic3r_SurfaceMixSideBridge_hpp_
#define slic3r_SurfaceMixSideBridge_hpp_
#include "TextureMapping.hpp"
#include "SurfaceMixSideOffsets.hpp"
namespace Slic3r {
std::optional<ColorSolverCandidateSet> surface_mix_side_candidates(const TextureMappingZone&,
                                                                   const std::vector<unsigned int>& component_ids,
                                                                   std::string*                     error = nullptr);
} // namespace Slic3r
#endif
