#include "WipeTowerIntegration.hpp"

#include "../GCode.hpp"
#include "../libslic3r.h"

#include "boost/algorithm/string/replace.hpp"

namespace Slic3r::GCode {

static inline Point wipe_tower_point_to_object_point(GCodeGenerator &gcodegen, const Vec2f& wipe_tower_pt)
{
    return Point(scale_(wipe_tower_pt.x() - gcodegen.origin()(0)), scale_(wipe_tower_pt.y() - gcodegen.origin()(1)));
}

std::string WipeTowerIntegration::append_tcr(GCodeGenerator &gcodegen, const WipeTower::ToolChangeResult& tcr, int new_extruder_id, double z) const
{
    if (new_extruder_id != -1 && new_extruder_id != tcr.new_tool)
        throw Slic3r::InvalidArgument("Error: WipeTowerIntegration::append_tcr was asked to do a toolchange it didn't expect.");

    std::string gcode;

    // Toolchangeresult.gcode assumes the wipe tower corner is at the origin
    // We want to rotate and shift all extrusions (gcode postprocessing) and starting and ending position
    float alpha = m_wipe_tower_rotation / 180.f * float(M_PI);

    auto transform_wt_pt = [&alpha, this](const Vec2f& pt) -> Vec2f {
        Vec2f out = Eigen::Rotation2Df(alpha) * pt;
        out += m_wipe_tower_pos;
        return out;
    };

    Vec2f start_pos = tcr.start_pos;
    Vec2f end_pos = tcr.end_pos;
    start_pos = transform_wt_pt(start_pos);
    end_pos = transform_wt_pt(end_pos);

    Vec2f wipe_tower_offset = m_wipe_tower_pos;
    float wipe_tower_rotation = alpha;

    std::string tcr_rotated_gcode = post_process_wipe_tower_moves(tcr, wipe_tower_offset, wipe_tower_rotation);

    double current_z = gcodegen.writer().get_position().z();
    gcode += gcodegen.writer().travel_to_z(current_z);

    if (z == -1.) // in case no specific z was provided, print at current_z pos
        z = current_z;

    const bool needs_toolchange = gcodegen.writer().need_toolchange(new_extruder_id);
    const bool will_go_down = ! is_approx(z, current_z);
    const bool is_ramming = (gcodegen.config().single_extruder_multi_material)
                         || (! gcodegen.config().single_extruder_multi_material && gcodegen.config().filament_multitool_ramming.get_at(tcr.initial_tool));
    const bool should_travel_to_tower = (tcr.force_travel        // wipe tower says so
                                         || ! needs_toolchange   // this is just finishing the tower with no toolchange
                                         || is_ramming
                                         || will_go_down);       // don't dig into the print
    if (should_travel_to_tower) {
        const Point xy_point = wipe_tower_point_to_object_point(gcodegen, start_pos);
        gcode += gcodegen.m_label_objects.maybe_stop_instance();
        gcode += gcodegen.retract_and_wipe();
        gcodegen.m_avoid_crossing_perimeters.use_external_mp_once = true;
        const std::string comment{"Travel to a Wipe Tower"};
        if (gcodegen.m_current_layer_first_position) {
            if (gcodegen.last_position) {
                gcode += gcodegen.travel_to(
                    *gcodegen.last_position, xy_point, ExtrusionRole::Mixed, comment, [](){return "";}
                );
            } else {
                gcode += gcodegen.writer().travel_to_xy(gcodegen.point_to_gcode(xy_point), comment);
                gcode += gcodegen.writer().get_travel_to_z_gcode(z, comment);
            }
        } else {
            const Vec3crd point = to_3d(xy_point, scaled(z));
            gcode += gcodegen.travel_to_first_position(point, current_z, ExtrusionRole::Mixed, [](){return "";});
        }
        gcode += gcodegen.unretract();
    } else {
        // When this is multiextruder printer without any ramming, we can just change
        // the tool without travelling to the tower.
    }

    if (will_go_down) {
        gcode += gcodegen.writer().retract();
        gcode += gcodegen.writer().travel_to_z(z, "Travel down to the last wipe tower layer.");
        gcode += gcodegen.writer().unretract();
    }

    std::string contour_toolchange_gcode_str;
    std::string inner_toolchange_gcode_str;
    std::string deretraction_str;

    contour_toolchange_gcode_str = gcodegen.set_extruder(tcr.contour_tool, tcr.print_z); // TODO: toolchange_z vs print_z
    inner_toolchange_gcode_str = gcodegen.set_extruder(tcr.new_tool, tcr.print_z); // TODO: toolchange_z vs print_z
    if (gcodegen.config().wipe_tower) {
        deretraction_str += gcodegen.writer().get_travel_to_z_gcode(z, "restore layer      Z");
        Vec3d position{gcodegen.writer().get_position()};
        position.z() = z;
        gcodegen.writer().update_position(position);
        deretraction_str += gcodegen.unretract();
    }

    if (contour_toolchange_gcode_str.empty()
    || inner_toolchange_gcode_str.empty()
    || deretraction_str.empty())
        throw;

    // Insert the toolchange and deretraction gcode into the generated gcode.
    boost::replace_first(tcr_rotated_gcode, "[toolchange_gcode_from_wipe_tower_generator]", contour_toolchange_gcode_str);
    boost::replace_first(tcr_rotated_gcode, "[toolchange_gcode_from_wipe_tower_generator]", inner_toolchange_gcode_str);
    boost::replace_all(tcr_rotated_gcode, "[deretraction_from_wipe_tower_generator]", deretraction_str);
    std::string tcr_gcode;
    unescape_string_cstyle(tcr_rotated_gcode, tcr_gcode);

    if (gcodegen.config().default_acceleration > 0)
        gcode += gcodegen.writer().set_print_acceleration(fast_round_up<unsigned int>(gcodegen.config().wipe_tower_acceleration.value));
    gcode += tcr_gcode;
    gcode += gcodegen.writer().set_print_acceleration(fast_round_up<unsigned int>(gcodegen.config().default_acceleration.value));

    // A phony move to the end position at the wipe tower.
    gcodegen.writer().travel_to_xy(end_pos.cast<double>());
    gcodegen.last_position = wipe_tower_point_to_object_point(gcodegen, end_pos);
    if (!is_approx(z, current_z)) {
        gcode += gcodegen.writer().retract();
        gcode += gcodegen.writer().travel_to_z(current_z, "Travel back up to the topmost object layer.");
        gcode += gcodegen.writer().unretract();
    }

    // Let the planner know we are traveling between objects.
    gcodegen.m_avoid_crossing_perimeters.use_external_mp_once = true;
    return gcode;
}

// This function postprocesses gcode_original, rotates and moves all G1 extrusions and returns resulting gcode
// Starting position has to be supplied explicitely (otherwise it would fail in case first G1 command only contained one coordinate)
std::string WipeTowerIntegration::post_process_wipe_tower_moves(const WipeTower::ToolChangeResult& tcr, const Vec2f& translation, float angle) const
{
    Vec2f extruder_offset = m_extruder_offsets[tcr.initial_tool].cast<float>();

    std::istringstream gcode_str(tcr.gcode);
    std::string gcode_out;
    std::string line;
    Vec2f pos = tcr.start_pos;
    Vec2f transformed_pos = Eigen::Rotation2Df(angle) * pos + translation;
    Vec2f old_pos(-1000.1f, -1000.1f);

    while (gcode_str) {
        std::getline(gcode_str, line);  // we read the gcode line by line

        // All G1 commands should be translated and rotated. X and Y coords are
        // only pushed to the output when they differ from last time.
        // WT generator can override this by appending the never_skip_tag
        if (boost::starts_with(line, "G1 ")) {
            bool never_skip = false;
            auto it = line.find(WipeTower::never_skip_tag());
            if (it != std::string::npos) {
                // remove the tag and remember we saw it
                never_skip = true;
                line.erase(it, it + WipeTower::never_skip_tag().size());
            }
            std::ostringstream line_out;
            std::istringstream line_str(line);
            line_str >> std::noskipws;  // don't skip whitespace
            char ch = 0;
            line_str >> ch >> ch; // read the "G1"
            while (line_str >> ch) {
                if (ch == 'X' || ch == 'Y')
                    line_str >> (ch == 'X' ? pos.x() : pos.y());
                else
                    line_out << ch;
            }

            line = line_out.str();
            boost::trim(line); // Remove leading and trailing spaces.

            transformed_pos = Eigen::Rotation2Df(angle) * pos + translation;

            if (transformed_pos != old_pos || never_skip || ! line.empty()) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(3) << "G1";
                if (transformed_pos.x() != old_pos.x() || never_skip)
                    oss << " X" << transformed_pos.x() - extruder_offset.x();
                if (transformed_pos.y() != old_pos.y() || never_skip)
                    oss << " Y" << transformed_pos.y() - extruder_offset.y();
                if (! line.empty())
                    oss << " ";
                line = oss.str() + line;
                old_pos = transformed_pos;
            }
        }

        gcode_out += line + "\n";

        // If this was a toolchange command, we should change current extruder offset
        if (line == "[toolchange_gcode_from_wipe_tower_generator]") {
            extruder_offset = m_extruder_offsets[tcr.new_tool].cast<float>();

            // If the extruder offset changed, add an extra move so everything is continuous
            if (extruder_offset != m_extruder_offsets[tcr.initial_tool].cast<float>()) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(3)
                    << "G1 X" << transformed_pos.x() - extruder_offset.x()
                    << " Y" << transformed_pos.y() - extruder_offset.y()
                    << "\n";
                gcode_out += oss.str();
            }
        }
    }
    return gcode_out;
}


std::string WipeTowerIntegration::prime(GCodeGenerator &gcodegen)
{
    std::string gcode;
    for (const WipeTower::ToolChangeResult& tcr : m_priming) {
        if (! tcr.extrusions.empty())
            gcode += append_tcr(gcodegen, tcr, tcr.new_tool);
    }
    return gcode;
}

std::string WipeTowerIntegration::tool_change(GCodeGenerator &gcodegen, int extruder_id, bool finish_layer)
{
    std::string gcode;
    assert(m_layer_idx >= 0);
    if (gcodegen.writer().need_toolchange(extruder_id) || finish_layer) {
        if (m_layer_idx < (int)m_tool_changes.size()) {
            if (!(size_t(m_tool_change_idx) < m_tool_changes[m_layer_idx].size()))
                throw Slic3r::RuntimeError("Wipe tower generation failed, possibly due to empty first layer.");

            // Calculate where the wipe tower layer will be printed. -1 means that print z will not change.
            double wipe_tower_z = -1;

            gcode += append_tcr(gcodegen, m_tool_changes[m_layer_idx][m_tool_change_idx++], extruder_id, wipe_tower_z);
            m_last_wipe_tower_print_z = wipe_tower_z;
        }
    }
    return gcode;
}

// Print is finished. Now it remains to unload the filament safely with ramming over the wipe tower.
std::string WipeTowerIntegration::finalize(GCodeGenerator &gcodegen)
{
    std::string gcode;
    return gcode;
}

} // namespace Slic3r::GCode
