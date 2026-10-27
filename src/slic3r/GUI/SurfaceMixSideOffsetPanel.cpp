#include "SurfaceMixRecipePreviewPanel.hpp"
#include "libslic3r/SurfaceMixSideOffsets.hpp"
#include "libslic3r/TextureMapping.hpp"
#include "libslic3r/Utils.hpp"
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>
#include <wx/checkbox.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <stdexcept>

namespace Slic3r::GUI {
bool apply_surface_mix_side_offset_draft(TextureMappingZone&              zone,
                                         const SurfaceMixSideOffsetDraft& draft,
                                         const std::vector<unsigned int>& available,
                                         std::string*                     error)
{
    if (error)
        error->clear();
    if (!draft.changed)
        return true;
    try {
        auto root = zone.side_surface_color_calibration_json.empty() ? nlohmann::json::object() :
                                                                       nlohmann::json::parse(zone.side_surface_color_calibration_json);
        if (!root.is_object())
            throw std::runtime_error("side calibration must be an object");
        auto           next = zone;
        const auto     key  = SurfaceMixSideOffsets::transport_key;
        nlohmann::json previous{{"mode", zone.transmission_distance_calibration_mode},
                                {"gamma", zone.tone_gamma},
                                {"dither", zone.dithering_enabled},
                                {"compact", zone.compact_offset_mode},
                                {"mapping", zone.texture_mapping_mode},
                                {"calibration_json", zone.side_surface_color_calibration_json},
                                {"calibration_name", zone.side_surface_color_calibration_name}};
        if (root.contains(key)) {
            if (root.at(key).contains("previous_side_settings"))
                previous = root.at(key).at("previous_side_settings");
            else
                previous["mode"] = int(TextureMappingZone::TDCalibrationNone);
        }
        if (draft.enabled) {
            const auto  catalogue = nlohmann::json::parse(draft.catalogue_json);
            std::string reason;
            auto        decoded = SurfaceMixSideOffsets::from_catalogue(catalogue, &reason);
            if (!decoded)
                throw std::runtime_error(reason);
            {
                const auto& catalog = catalogue.contains("catalog") ? catalogue.at("catalog") : catalogue;
                for (const auto& source : catalog.at("sources"))
                    if (source.at("role") == "side") {
                        // Retain existing fields; legacy UI readers still need the profile envelope.
                        for (const auto& field : source.at("raw_document").items())
                            if (!root.contains(field.key()))
                                root[field.key()] = field.value();
                        break;
                    }
            }
            root[key] = {{"schema", "surfacemix.imagemap-side-offsets.v1"},
                         {"enabled", true},
                         {"experimental", true},
                         {"execution_model", "imagemap_independent_offsets"},
                         {"canonical_to_filament_slot", draft.filament_slots},
                         {"assume_normalized_srgb", draft.assume_normalized_srgb},
                         {"catalogue", catalogue},
                         {"previous_side_settings", previous}};
            if (!SurfaceMixSideOffsets::from_calibration_json(root.dump(), available, &reason))
                throw std::runtime_error(reason);
            next.transmission_distance_calibration_mode = TextureMappingZone::TDCalibrationCalibratedNearestMeasuredSample;
            next.texture_mapping_mode                   = TextureMappingZone::TextureMappingFilamentBlending;
            next.tone_gamma                             = 1.f;
            next.dithering_enabled                      = false;
            next.compact_offset_mode                    = false;
            next.side_surface_color_calibration_name    = "SurfaceMix measured side offsets (experimental)";
        } else {
            if (!root.contains(key))
                return true;
            next.transmission_distance_calibration_mode = previous.at("mode").get<int>();
            next.texture_mapping_mode                   = previous.at("mapping").get<int>();
            next.tone_gamma                             = previous.at("gamma").get<float>();
            next.dithering_enabled                      = previous.at("dither").get<bool>();
            next.compact_offset_mode                    = previous.at("compact").get<bool>();
            if (previous.contains("calibration_json")) {
                next.side_surface_color_calibration_json = previous.at("calibration_json").get<std::string>();
                next.side_surface_color_calibration_name = previous.at("calibration_name").get<std::string>();
                zone                                     = std::move(next);
                return true;
            }
            root.erase(key);
        }
        next.side_surface_color_calibration_json = root.dump();
        zone                                     = std::move(next);
        return true;
    } catch (const std::exception& ex) {
        if (error)
            *error = ex.what();
        return false;
    }
}

void add_surface_mix_side_offset_controls(wxWindow*                                  parent,
                                          wxSizer*                                   box,
                                          std::shared_ptr<SurfaceMixSideOffsetDraft> draft,
                                          const TextureMappingZone*                  zone)
{
    if (zone) {
        const auto root = nlohmann::json::parse(zone->side_surface_color_calibration_json, nullptr, false);
        try {
            if (root.is_object() && root.contains(SurfaceMixSideOffsets::transport_key)) {
                const auto& transport         = root.at(SurfaceMixSideOffsets::transport_key);
                draft->enabled                = transport.at("enabled").get<bool>();
                draft->filament_slots         = transport.at("canonical_to_filament_slot").get<std::array<unsigned int, 4>>();
                draft->catalogue_json         = transport.at("catalogue").dump();
                draft->assume_normalized_srgb = transport.value("assume_normalized_srgb", false);
            }
        } catch (const std::exception&) {
            draft->enabled = false;
        }
    }
    auto* enabled = new wxCheckBox(parent, wxID_ANY, "Use measured side offsets (experimental)");
    enabled->SetValue(draft->enabled);
    box->Add(enabled, 0, wxTOP | wxBOTTOM, parent->FromDIP(10));
    auto*                         row = new wxBoxSizer(wxHORIZONTAL);
    const std::array<wxString, 4> names{{"C", "M", "Y", "W"}};
    for (size_t i = 0; i < 4; ++i) {
        row->Add(new wxStaticText(parent, wxID_ANY, names[i] + " = F"), 0, wxALIGN_CENTER_VERTICAL);
        auto* slot = new wxSpinCtrl(parent, wxID_ANY);
        slot->SetRange(1, 65535);
        slot->SetValue(int(draft->filament_slots[i]));
        slot->SetMinSize(wxSize(parent->FromDIP(64), -1));
        row->Add(slot, 1, wxRIGHT, parent->FromDIP(4));
        slot->Bind(wxEVT_TEXT, [draft, slot, i](wxCommandEvent&) {
            draft->filament_slots[i] = unsigned(slot->GetValue());
            draft->changed           = true;
        });
        slot->Bind(wxEVT_SPINCTRL, [draft, i](wxSpinEvent& event) {
            draft->filament_slots[i] = unsigned(event.GetValue());
            draft->changed           = true;
        });
    }
    box->Add(row, 0, wxEXPAND | wxBOTTOM, parent->FromDIP(6));
    auto* note = new wxStaticText(
        parent, wxID_ANY,
        "Side offsets are independent amplitudes, not depth layers or fractions. Assign this ImageMap zone to walls to use them. "
        "Slicing uses ImageMap's wall-offset execution. Historical backing, geometry and printed colour are unverified; "
        "this does not enforce a 1.26 mm wall depth. Requires global wall-gradient strength 100%. Changes apply on OK.");
    note->Wrap(parent->FromDIP(340));
    box->Add(note, 0, wxEXPAND);
    enabled->Bind(wxEVT_CHECKBOX, [parent, draft, enabled](wxCommandEvent&) {
        try {
            if (enabled->GetValue()) {
                if (wxMessageBox("Enable experimental side-offset assignment on OK?\n\n"
                                 "Independent measured amplitudes will drive ImageMap wall paths with explicit C/M/Y/W slots. "
                                 "Dithering is disabled and gamma set to 1. Historical backing and execution settings are unknown. "
                                 "Undeclared RGB is treated as normalized sRGB for this experiment only. This is not print approval.",
                                 "SurfaceMix side offsets", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, parent) != wxYES) {
                    enabled->SetValue(false);
                    return;
                }
                if (draft->catalogue_json.empty()) {
                    boost::nowide::ifstream input(resources_dir() + "/surfacemix/creator5-cmyw-side-offsets.v1.json", std::ios::ate);
                    if (!input || input.tellg() <= 0 || input.tellg() > 32 * 1024 * 1024)
                        throw std::runtime_error("bundled side catalogue is unavailable or oversized");
                    input.seekg(0);
                    draft->catalogue_json = nlohmann::json::parse(input).dump();
                }
                std::string reason;
                if (!SurfaceMixSideOffsets::from_catalogue(nlohmann::json::parse(draft->catalogue_json), &reason))
                    throw std::runtime_error(reason);
                draft->assume_normalized_srgb = true;
            }
            draft->enabled = enabled->GetValue();
            draft->changed = true;
        } catch (const std::exception& ex) {
            enabled->SetValue(false);
            wxMessageBox(wxString::FromUTF8(ex.what()), "Side offsets not enabled", wxOK | wxICON_ERROR, parent);
        }
    });
}
} // namespace Slic3r::GUI
