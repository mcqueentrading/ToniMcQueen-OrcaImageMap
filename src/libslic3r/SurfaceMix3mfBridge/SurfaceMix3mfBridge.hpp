#ifndef slic3r_SurfaceMix3mfBridge_hpp_
#define slic3r_SurfaceMix3mfBridge_hpp_

#include <string>

namespace Slic3r {

class DynamicPrintConfig;

namespace SurfaceMix3mfBridge {

enum class Result {
    NotPresent,
    Applied,
    Rejected,
};

// Imports quantizer metadata into the existing ImageMap zone representation.
// This is deliberately a metadata/configuration bridge; it does not modify
// mesh geometry and does not claim physical or side-wall execution.
Result apply(const std::string& path, DynamicPrintConfig& config, std::string* error = nullptr);

} // namespace SurfaceMix3mfBridge
} // namespace Slic3r

#endif
