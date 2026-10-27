#include "SurfaceMixRecipePreviewPanel.hpp"
#include "MMUPaintedTexturePreview.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/SurfaceMixTopStack.hpp"

#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/checkbox.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <memory>
#include <stdexcept>

namespace Slic3r::GUI {
namespace {

std::shared_ptr<const SurfaceMixRecipePreview> read_recipe_catalogue(const std::string& path)
{
    boost::nowide::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto              size = input.tellg();
    if (!input || size <= 0 || size > 32 * 1024 * 1024)
        throw std::runtime_error("Cannot read the catalogue, or its size exceeds the 32 MiB preview limit.");
    input.seekg(0);
    std::string error;
    auto        decoded = SurfaceMixRecipePreview::from_json(nlohmann::json::parse(input), &error);
    if (!decoded)
        throw std::runtime_error(error);
    return std::make_shared<const SurfaceMixRecipePreview>(std::move(*decoded));
}

bool confirm_display_assumption(wxWindow* parent, const SurfaceMixRecipePreview& catalogue)
{
    return !catalogue.assumes_srgb() ||
           wxMessageBox("This catalogue does not declare its RGB encoding/range. For this display only, treat the supplied normalized "
                        "channels as sRGB?\n\nThis is not colour calibration approval, material assignment or print validation.",
                        "SurfaceMix display assumption", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, parent) == wxYES;
}

wxString recipe_component_text(const SurfaceMixPreviewRecipe& recipe)
{
    static constexpr const char* names[] = {"C", "M", "Y", "W"};
    wxString                    result;
    for (size_t i = 0; i < recipe.surface_to_deep.size(); ++i) {
        if (i != 0)
            result += " > ";
        const auto component = recipe.surface_to_deep[i];
        result += component < 4 ? names[component] : "?";
        if (i < recipe.layer_heights_mm.size())
            result += wxString::Format("(%.3f)", recipe.layer_heights_mm[i]);
    }
    return result;
}

bool recipe_matches_filter(const SurfaceMixPreviewRecipe& recipe, const wxString& filter, const wxString& query)
{
    if (filter == "Short stacks" && recipe.total_depth_mm >= 1.0)
        return false;
    if (filter == "Full/deep stacks" && recipe.total_depth_mm < 1.0)
        return false;
    if (query.empty())
        return true;
    const wxString needle = query.Lower();
    const wxString sample = wxString::FromUTF8(recipe.sample_id.c_str()).Lower();
    const wxString source = wxString::FromUTF8(recipe.source_pointer.c_str()).Lower();
    return sample.Contains(needle) || source.Contains(needle);
}

} // namespace

bool apply_surface_mix_top_stack_draft(TextureMappingZone&              zone,
                                       const SurfaceMixTopStackDraft&   draft,
                                       const std::vector<unsigned int>& available_slots,
                                       std::string*                     error)
{
    if (error != nullptr)
        error->clear();
    if (!draft.changed)
        return true;
    try {
        auto root = nlohmann::json::parse(zone.top_surface_color_calibration_json, nullptr, false);
        if (!root.is_object())
            root = nlohmann::json::object();
        auto           next     = zone;
        const auto     key      = SurfaceMixTopStack::transport_key;
        nlohmann::json previous = {{"enabled", zone.top_surface_image_printing_enabled},
                                   {"method", zone.top_surface_image_printing_method},
                                   {"layers", zone.top_surface_contoning_stack_layers},
                                   {"pattern", zone.top_surface_contoning_pattern_filaments},
                                   {"surfaces", zone.top_surface_contoning_colored_surfaces},
                                   {"phase", zone.top_surface_contoning_layer_phase_enabled},
                                   {"blue_noise", zone.top_surface_contoning_blue_noise_error_diffusion_enabled}};
        if (root.contains(key)) {
            if (root.at(key).contains("previous_top_settings"))
                previous = root.at(key).at("previous_top_settings");
            else
                previous["enabled"] = false; // Imported transports without a backup must not silently enable the generic painter on disable.
        }
        if (draft.enabled) {
            const auto catalogue = nlohmann::json::parse(draft.catalogue_json);
            root[key]            = {{"schema", "surfacemix.imagemap-top-stacks.v1"},
                                    {"enabled", true},
                                    {"experimental", true},
                                    {"coordinate_mode", "build_z_top"},
                                    {"execution_layers", SurfaceMixTopStack::default_execution_layer_count},
                                    {"canonical_to_filament_slot", draft.filament_slots},
                                    {"assume_normalized_srgb", draft.assume_normalized_srgb},
                                    {"catalogue", catalogue},
                                    {"previous_top_settings", previous}};
            std::string reason;
            const auto  top = SurfaceMixTopStack::from_calibration_json(root.dump(), available_slots, &reason);
            if (!top)
                throw std::runtime_error(reason);
            if (top->layer_count() > size_t(TextureMappingZone::MaxTopSurfaceContoningStackLayers))
                throw std::runtime_error("measured stack exceeds ImageMap's supported layer count");
            next.top_surface_image_printing_enabled                       = true;
            next.top_surface_image_printing_method                        = TextureMappingZone::TopSurfaceImageContoning;
            next.top_surface_contoning_stack_layers                       = int(top->layer_count());
            next.top_surface_contoning_pattern_filaments                  = int(top->layer_count());
            next.top_surface_contoning_colored_surfaces                   = TextureMappingZone::ContoningColoredUpperSurfaces;
            next.top_surface_contoning_layer_phase_enabled                = false;
            next.top_surface_contoning_blue_noise_error_diffusion_enabled = false;
        } else {
            if (!root.contains(key))
                return true;
            next.top_surface_image_printing_enabled                       = previous.at("enabled").get<bool>();
            next.top_surface_image_printing_method                        = previous.at("method").get<int>();
            next.top_surface_contoning_stack_layers                       = previous.at("layers").get<int>();
            next.top_surface_contoning_pattern_filaments                  = previous.at("pattern").get<int>();
            next.top_surface_contoning_colored_surfaces                   = previous.at("surfaces").get<int>();
            next.top_surface_contoning_layer_phase_enabled                = previous.at("phase").get<bool>();
            next.top_surface_contoning_blue_noise_error_diffusion_enabled = previous.at("blue_noise").get<bool>();
            root.erase(key);
        }
        next.top_surface_contoning_color_lower_surfaces = next.top_surface_contoning_colors_lower_surfaces();
        next.top_surface_color_calibration_json         = root.dump();
        zone                                            = std::move(next);
        return true;
    } catch (const std::exception& ex) {
        if (error != nullptr)
            *error = ex.what();
        return false;
    }
}

wxSizer* create_surface_mix_recipe_preview_controls(wxWindow*                                  parent,
                                                    std::function<const Model*()>              model,
                                                    std::function<const TextureMappingZone*()> zone,
                                                    std::function<void()>                      refresh,
                                                    std::shared_ptr<SurfaceMixTopStackDraft>   top_stack_draft)
{
    std::shared_ptr<const SurfaceMixRecipePreview> bundled_catalogue;
    std::string                                    bundled_error;
    const auto*                                    initial_model = model();
    const auto*                                    initial_zone  = zone();
    if (initial_model != nullptr && initial_zone != nullptr &&
        surface_mix_recipe_preview_settings(*initial_model, *initial_zone).catalog == nullptr) {
        try {
            bundled_catalogue = read_recipe_catalogue(resources_dir() + "/surfacemix/creator5-cmyw-top-preview.v1.json");
        } catch (const std::exception& ex) {
            bundled_error = ex.what();
        }
    }
    auto* box = new wxBoxSizer(wxVERTICAL);
    box->Add(new wxStaticText(parent, wxID_ANY, "Colouring method (preview)"), 0, wxBOTTOM, parent->FromDIP(4));
    auto* mode = new wxChoice(parent, wxID_ANY);
    mode->Append("ImageMap (existing colours)");
    mode->Append("SurfaceMix calibrated colours");
    mode->Append("SurfaceMix depth components");
    mode->SetToolTip("Apply SurfaceMix to the mapped texture colours for display only. Does not paint the model or change G-code.");
    mode->SetSelection(0);
    box->Add(mode, 0, wxEXPAND | wxBOTTOM, parent->FromDIP(4));
    auto* load = new wxButton(parent, wxID_ANY, "Replace recipe catalogue...");
    load->SetToolTip("Optional: replace the bundled measured CMYW recipes with a Rust calibration audit. Does not change slicing.");
    box->Add(load, 0, wxEXPAND | wxBOTTOM, parent->FromDIP(4));
    auto* depth_row = new wxBoxSizer(wxHORIZONTAL);
    depth_row->Add(new wxStaticText(parent, wxID_ANY, "Inward depth (mm)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, parent->FromDIP(4));
    auto* depth = new wxSpinCtrlDouble(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                       wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER);
    depth->SetDigits(3);
    depth->SetRange(0.0, 10.0);
    depth->SetIncrement(0.08);
    depth_row->Add(depth, 1, wxEXPAND);
    box->Add(depth_row, 0, wxEXPAND | wxBOTTOM, parent->FromDIP(4));
    auto* status = new wxStaticText(parent, wxID_ANY, wxEmptyString);
    status->SetForegroundColour(wxColour(180, 86, 0));
    box->Add(status, 0, wxEXPAND);

    auto* catalogue_title = new wxStaticText(parent, wxID_ANY, "Measured recipe catalogue (read-only)");
    catalogue_title->SetToolTip("Browse all loaded measured recipes. This view does not add filament slots or assign tools.");
    box->Add(catalogue_title, 0, wxTOP | wxBOTTOM, parent->FromDIP(10));
    auto* catalogue_note = new wxStaticText(
        parent, wxID_ANY,
        "Top-surface records are shown here. C/M/Y/W order is surface to deep; numbers in parentheses are layer heights in mm. "
        "Catalogue entries are calibration evidence, not proof of printable colour.");
    catalogue_note->Wrap(parent->FromDIP(340));
    box->Add(catalogue_note, 0, wxEXPAND | wxBOTTOM, parent->FromDIP(5));
    auto* catalogue_filter_row = new wxBoxSizer(wxHORIZONTAL);
    auto* catalogue_filter = new wxChoice(parent, wxID_ANY);
    catalogue_filter->Append("All stacks");
    catalogue_filter->Append("Short stacks");
    catalogue_filter->Append("Full/deep stacks");
    catalogue_filter->SetSelection(0);
    catalogue_filter_row->Add(catalogue_filter, 1, wxEXPAND | wxRIGHT, parent->FromDIP(4));
    auto* catalogue_search = new wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    catalogue_search->SetHint("Search sample or source");
    catalogue_filter_row->Add(catalogue_search, 2, wxEXPAND);
    box->Add(catalogue_filter_row, 0, wxEXPAND | wxBOTTOM, parent->FromDIP(4));
    auto* catalogue_count = new wxStaticText(parent, wxID_ANY, wxEmptyString);
    box->Add(catalogue_count, 0, wxEXPAND | wxBOTTOM, parent->FromDIP(3));
    auto* catalogue_view = new wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                          wxSize(-1, parent->FromDIP(220)), wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
    catalogue_view->SetToolTip("Read-only measured recipe catalogue. No record here is an additional physical filament.");
    box->Add(catalogue_view, 1, wxEXPAND | wxBOTTOM, parent->FromDIP(4));

    if (top_stack_draft != nullptr) {
        const auto root = initial_zone != nullptr ?
                              nlohmann::json::parse(initial_zone->top_surface_color_calibration_json, nullptr, false) :
                              nlohmann::json();
        if (root.is_object() && root.contains(SurfaceMixTopStack::transport_key)) {
            const auto& settings = root.at(SurfaceMixTopStack::transport_key);
            try {
                top_stack_draft->enabled                = settings.at("enabled").get<bool>();
                top_stack_draft->assume_normalized_srgb = settings.value("assume_normalized_srgb", false);
                top_stack_draft->filament_slots         = settings.at("canonical_to_filament_slot").get<std::array<unsigned int, 4>>();
                top_stack_draft->catalogue_json         = settings.at("catalogue").dump();
            } catch (const std::exception&) {
                // Leave malformed runtime metadata visible to the strict slicer validator; never activate it here.
                top_stack_draft->enabled = false;
            }
        }
        auto* use_stacks = new wxCheckBox(parent, wxID_ANY, "Use measured top stacks when slicing (experimental)");
        use_stacks->SetValue(top_stack_draft->enabled);
        box->Add(use_stacks, 0, wxTOP | wxBOTTOM, parent->FromDIP(10));
        auto*                         slots_row = new wxBoxSizer(wxHORIZONTAL);
        const std::array<wxString, 4> names{{"C", "M", "Y", "W"}};
        std::array<wxSpinCtrl*, 4>    slots{};
        for (size_t i = 0; i < slots.size(); ++i) {
            slots_row->Add(new wxStaticText(parent, wxID_ANY, names[i] + " = F"), 0, wxALIGN_CENTER_VERTICAL);
            slots[i] = new wxSpinCtrl(parent, wxID_ANY);
            slots[i]->SetRange(1, 65535);
            slots[i]->SetValue(int(top_stack_draft->filament_slots[i]));
            slots[i]->SetMinSize(wxSize(parent->FromDIP(64), -1));
            slots_row->Add(slots[i], 1, wxRIGHT, parent->FromDIP(4));
            slots[i]->Bind(wxEVT_SPINCTRL, [top_stack_draft, slots, i](wxSpinEvent& event) {
                top_stack_draft->filament_slots[i] = unsigned(event.GetValue());
                top_stack_draft->changed           = true;
            });
            slots[i]->Bind(wxEVT_TEXT, [top_stack_draft, slots, i](wxCommandEvent&) {
                top_stack_draft->filament_slots[i] = unsigned(slots[i]->GetValue());
                top_stack_draft->changed           = true;
            });
        }
        box->Add(slots_row, 0, wxEXPAND | wxBOTTOM, parent->FromDIP(6));
        auto* execution_note = new wxStaticText(
            parent, wxID_ANY,
            "Changes apply on OK. Upper, build-Z top stacks only; not wall-depth colouring. Requires complete calibrated layers. "
            "Incompatible slices stop instead of changing the recipe. Start with a small flat test model; physical colour is unverified.");
        execution_note->Wrap(parent->FromDIP(340));
        box->Add(execution_note, 0, wxEXPAND);
        use_stacks->Bind(wxEVT_CHECKBOX, [parent, model, zone, top_stack_draft, use_stacks, bundled_catalogue](wxCommandEvent&) {
            if (use_stacks->GetValue()) {
                const auto* m         = model();
                const auto* z         = zone();
                auto        catalogue = m != nullptr && z != nullptr ? surface_mix_recipe_preview_settings(*m, *z).catalog : nullptr;
                if (catalogue == nullptr)
                    catalogue = bundled_catalogue;
                if (catalogue == nullptr) {
                    wxMessageBox("Load a recipe catalogue first.", "SurfaceMix", wxOK | wxICON_ERROR, parent);
                    use_stacks->SetValue(false);
                    return;
                }
                if (wxMessageBox("Enable experimental top-stack filament assignment for this zone on OK?\n\n"
            "This changes sliced top-surface paths, not just the display. It uses the calibrated three-layer top prefix; the "
            "complete 19-layer profile remains preserved as measurement evidence. "
                                 "explicit C/M/Y/W slots are required. Start with a flat fixture, not the dancer. Wall-depth painting "
                                 "and physical colour accuracy are not validated.\n\n"
                                 "If the source lacks RGB declarations, normalized sRGB is an experimental assumption only.",
                                 "Experimental SurfaceMix slicing", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, parent) != wxYES) {
                    use_stacks->SetValue(false);
                    return;
                }
                top_stack_draft->catalogue_json         = catalogue->catalogue_json();
                top_stack_draft->assume_normalized_srgb = catalogue->assumes_srgb();
            }
            top_stack_draft->enabled = use_stacks->GetValue();
            top_stack_draft->changed = true;
        });
    }

    if (top_stack_draft != nullptr)
        add_surface_mix_side_offset_controls(parent, box,
            std::shared_ptr<SurfaceMixSideOffsetDraft>(top_stack_draft, &top_stack_draft->side), initial_zone);

    auto sync = [parent, model, zone, mode, depth, status, bundled_catalogue, bundled_error, catalogue_filter, catalogue_search,
                 catalogue_count, catalogue_view]() {
        const auto* m     = model();
        const auto* z     = zone();
        const auto  state = m != nullptr && z != nullptr ? surface_mix_recipe_preview_settings(*m, *z) : SurfaceMixRecipePreviewSettings{};
        const auto  catalogue = state.catalog != nullptr ? state.catalog : bundled_catalogue;
        mode->SetSelection(int(state.mode));
        depth->Enable(state.mode == SurfaceMixPreviewMode::DepthComponent);
        wxString message = "Select a SurfaceMix mode to apply measured recipe colours to this ImageMap zone. Preview only; the original "
                           "texture is unchanged.";
        if (state.catalog == nullptr && bundled_catalogue != nullptr) {
            message += wxString::Format(
                "\nBundled CMYW measurements are ready: %zu top records, including catalogue mirrors. No recipe file is needed.",
                bundled_catalogue->recipes().size());
            message += "\nThese measurements are profile/filament-specific, not universal CMYW recipes.";
        } else if (state.catalog == nullptr && !bundled_error.empty()) {
            message += "\nBundled recipes unavailable: " + wxString::FromUTF8(bundled_error.c_str());
            message += " Use Replace recipe catalogue to load a Rust calibration audit.";
        }
        if (state.catalog != nullptr) {
            depth->SetRange(0.0, state.catalog->maximum_depth_mm());
            depth->SetValue(state.depth_mm);
            message = wxString::Format("%zu top records (catalogue mirrors retained).\n", state.catalog->recipes().size());
            message += "Display only: not wall eligibility, sliced paths or physically validated colour.\n";
            if (state.catalog->assumes_srgb())
                message += "Display assumption: normalized sRGB; profile encoding/range unspecified.\n";
            if (state.mode == SurfaceMixPreviewMode::DepthComponent)
                message += "C=cyan M=magenta Y=yellow W=white; grey=no interval. Depth is projected onto the surface, not a mesh cutaway.";
            size_t mapped_sources = 0;
            for (const auto* object : m->objects) {
                for (const auto* volume : object->volumes) {
                    const auto& used_states = volume->mmu_segmentation_facets.get_data().used_states;
                    const bool  uses_zone   = volume->extruder_id() == int(z->zone_id) ||
                                              (z->zone_id < used_states.size() && used_states[z->zone_id]);
                    const bool  raw_atlas   = volume->imported_texture_raw_channels != 0 ||
                                              !volume->imported_texture_raw_metadata_json.empty() ||
                                              !volume->imported_texture_raw_filament_offsets.empty() ||
                                              !volume->imported_texture_raw_top_surface_depths.empty() ||
                                              !volume->imported_texture_raw_top_surface_filament_slots.empty();
                    if (uses_zone && !raw_atlas &&
                        (model_volume_has_texture_preview_data(*volume) || model_volume_has_texture_mapping_color_preview_data(*volume) ||
                         model_volume_has_vertex_color_preview_data(*volume)))
                        ++mapped_sources;
                }
            }
            if (mapped_sources == 0)
                message += "\nNo supported model currently uses this ImageMap zone. Apply ImageMap to a textured/coloured model first. Raw "
                           "label atlases are not recoloured.";
        }
        status->SetLabel(message);
        status->Wrap(parent->FromDIP(280));

        if (catalogue == nullptr) {
            catalogue_count->SetLabel("No catalogue loaded.");
            catalogue_view->Clear();
        } else {
            const wxString filter = catalogue_filter->GetStringSelection();
            const wxString query  = catalogue_search->GetValue();
            wxString       rows;
            size_t         shown = 0;
            for (size_t i = 0; i < catalogue->recipes().size(); ++i) {
                const auto& recipe = catalogue->recipes()[i];
                if (!recipe_matches_filter(recipe, filter, query))
                    continue;
                rows += wxString::Format("%zu  RGB %.3f %.3f %.3f  depth %.3f mm  %zu layers\n", i + 1,
                                         recipe.measured_rgb[0], recipe.measured_rgb[1], recipe.measured_rgb[2],
                                         recipe.total_depth_mm, recipe.layer_heights_mm.size());
                rows += "    " + recipe_component_text(recipe) + "\n";
                rows += "    sample: " + wxString::FromUTF8(recipe.sample_id.c_str()) + "\n";
                rows += "    source: " + wxString::FromUTF8(recipe.source_pointer.c_str()) + "\n\n";
                ++shown;
            }
            catalogue_count->SetLabel(wxString::Format("Showing %zu of %zu measured top records", shown, catalogue->recipes().size()));
            catalogue_view->SetValue(rows);
        }
        parent->Layout();
    };

    auto load_recipes = [parent, model, zone, sync](SurfaceMixPreviewMode requested_mode) -> bool {
        if (model() == nullptr || zone() == nullptr)
            return false;
        wxFileDialog dialog(parent, "Load Rust SurfaceMix calibration audit", wxEmptyString, wxEmptyString,
                            "SurfaceMix calibration JSON (*.json)|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() != wxID_OK)
            return false;
        try {
            std::shared_ptr<const SurfaceMixRecipePreview> catalogue;
            {
                wxBusyCursor busy;
                catalogue = read_recipe_catalogue(dialog.GetPath().ToUTF8().data());
            }
            if (!confirm_display_assumption(parent, *catalogue))
                return false;
            if (model() == nullptr || zone() == nullptr)
                return false;
            set_surface_mix_recipe_preview(*model(), *zone(), std::move(catalogue), requested_mode, 0.0);
            sync();
            return true;
        } catch (const std::exception& ex) {
            wxMessageBox(wxString::FromUTF8(ex.what()), "SurfaceMix preview not loaded", wxOK | wxICON_ERROR, parent);
            return false;
        }
    };
    load->Bind(wxEVT_BUTTON, [load_recipes, mode, sync](wxCommandEvent&) {
        load_recipes(mode->GetSelection() == int(SurfaceMixPreviewMode::DepthComponent) ? SurfaceMixPreviewMode::DepthComponent :
                                                                                          SurfaceMixPreviewMode::MeasuredColour);
        sync();
    });
    auto apply = [model, zone, mode, depth, sync]() {
        if (model() == nullptr || zone() == nullptr)
            return;
        const auto state = surface_mix_recipe_preview_settings(*model(), *zone());
        if (state.mode == SurfaceMixPreviewMode(mode->GetSelection()) && state.depth_mm == depth->GetValue())
            return;
        set_surface_mix_recipe_preview(*model(), *zone(), state.catalog, SurfaceMixPreviewMode(mode->GetSelection()), depth->GetValue());
        sync();
    };
    mode->Bind(wxEVT_CHOICE, [parent, model, zone, mode, load_recipes, apply, sync, bundled_catalogue](wxCommandEvent&) {
        if (model() == nullptr || zone() == nullptr) {
            sync();
            return;
        }
        const auto requested_mode = SurfaceMixPreviewMode(mode->GetSelection());
        const auto state          = surface_mix_recipe_preview_settings(*model(), *zone());
        if (requested_mode != SurfaceMixPreviewMode::Off && state.catalog == nullptr) {
            if (bundled_catalogue != nullptr) {
                if (confirm_display_assumption(parent, *bundled_catalogue)) {
                    set_surface_mix_recipe_preview(*model(), *zone(), bundled_catalogue, requested_mode, 0.0);
                }
            } else {
                load_recipes(requested_mode);
            }
            sync();
        } else {
            apply();
        }
    });
    depth->Bind(wxEVT_SPINCTRLDOUBLE, [apply](wxSpinDoubleEvent&) { apply(); });
    depth->Bind(wxEVT_TEXT_ENTER, [apply](wxCommandEvent&) { apply(); });
    depth->Bind(wxEVT_KILL_FOCUS, [apply](wxFocusEvent& event) {
        apply();
        event.Skip();
    });
    catalogue_filter->Bind(wxEVT_CHOICE, [sync](wxCommandEvent&) { sync(); });
    catalogue_search->Bind(wxEVT_TEXT, [sync](wxCommandEvent&) { sync(); });
    catalogue_search->Bind(wxEVT_TEXT_ENTER, [sync](wxCommandEvent&) { sync(); });
    sync();
    return box;
}

} // namespace Slic3r::GUI
