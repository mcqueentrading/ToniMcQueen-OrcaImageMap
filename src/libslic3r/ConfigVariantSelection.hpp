#ifndef slic3r_ConfigVariantSelection_hpp_
#define slic3r_ConfigVariantSelection_hpp_

#include "Config.hpp"

namespace Slic3r {

// Nullable vectors are different dynamic types from their non-nullable counterparts.
inline void select_nullable_config_variants(ConfigOptionVectorBase& option, const std::vector<int>& indices)
{
    if (!option.nullable() || option.empty())
        throw ConfigurationError("Nullable variant selection requires a nonempty nullable vector");
    for (int index : indices)
        if (index < 0)
            throw ConfigurationError("Nullable variant selection requires resolved nonnegative indices");
    const std::unique_ptr<ConfigOption> original(option.clone());
    const std::unique_ptr<ConfigOption> selected(option.clone());
    auto*                               values = dynamic_cast<ConfigOptionVectorBase*>(selected.get());
    values->resize(indices.size());
    for (size_t i = 0; i < indices.size(); ++i)
        values->set_at(original.get(), i, size_t(indices[i]));
    option.set(selected.get());
}

} // namespace Slic3r

#endif
