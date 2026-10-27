#ifndef slic3r_SurfaceMixRecipePreviewPanel_hpp_
#define slic3r_SurfaceMixRecipePreviewPanel_hpp_

#include <functional>
#include <array>
#include <memory>
#include <string>
#include <vector>

class wxWindow;
class wxSizer;

namespace Slic3r {
class Model;
struct TextureMappingZone;
namespace GUI {

struct SurfaceMixSideOffsetDraft
{
    bool changed{false}, enabled{false}, assume_normalized_srgb{false};
    std::array<unsigned int, 4> filament_slots{{1, 2, 3, 4}};
    std::string catalogue_json;
};

struct SurfaceMixTopStackDraft
{
    bool                        changed{false};
    bool                        enabled{false};
    bool                        assume_normalized_srgb{false};
    std::array<unsigned int, 4> filament_slots{{1, 2, 3, 4}};
    std::string                 catalogue_json;
    SurfaceMixSideOffsetDraft   side;
};

bool apply_surface_mix_side_offset_draft(TextureMappingZone&, const SurfaceMixSideOffsetDraft&,
    const std::vector<unsigned int>& available_slots, std::string* error);
void add_surface_mix_side_offset_controls(wxWindow*, wxSizer*, std::shared_ptr<SurfaceMixSideOffsetDraft>,
    const TextureMappingZone*);

bool apply_surface_mix_top_stack_draft(TextureMappingZone&              zone,
                                       const SurfaceMixTopStackDraft&   draft,
                                       const std::vector<unsigned int>& available_slots,
                                       std::string*                     error);

wxSizer* create_surface_mix_recipe_preview_controls(wxWindow*                                  parent,
                                                    std::function<const Model*()>              model,
                                                    std::function<const TextureMappingZone*()> zone,
                                                    std::function<void()>                      refresh,
                                                    std::shared_ptr<SurfaceMixTopStackDraft>   top_stack_draft = {});

} // namespace GUI
} // namespace Slic3r

#endif
