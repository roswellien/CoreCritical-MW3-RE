#include "prepared_models.hpp"

#include "gamez/zclass/cls_data.h"
#include "gamez/zmodel/gmod_const.h"
#include "renderer/donor_retarget.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

namespace cc_mw3_re {
namespace {
namespace math = mw34::donor;
using platform::prepared::Batch;
using platform::prepared::Matrix;
using zcls::Node;

std::ifstream open(const std::string &path) {
    std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open prepared visual payload: " + path);
    return input;
}

Matrix from_native(const float *m) {
    if (!m)
        throw std::runtime_error("Prepared model has no native transform");
    for (int i = 0; i < 12; ++i)
        if (!std::isfinite(m[i]))
            throw std::runtime_error("Prepared model has a nonfinite native transform");
    return {m[0], m[3], m[6], m[9], m[1], m[4], m[7], m[10], m[2], m[5], m[8], m[11], 0, 0, 0, 1};
}

using Poses = std::map<std::string, Matrix>;
struct Hierarchy {
    Poses poses;
    std::map<const Node *, Matrix> nodes;
};

Hierarchy hierarchy(const Node *root) {
    Hierarchy output;
    std::vector<const Node *> pending{root};
    output.nodes.emplace(root, math::identity());
    for (size_t i = 0; i < pending.size(); ++i) {
        if (i >= 4096)
            throw std::runtime_error("Prepared source hierarchy exceeds its node limit");
        const auto *node = pending[i];
        const auto current = output.nodes.at(node);
        const auto canonical = root->source_asset->nodes.find(node->name());
        if (canonical != root->source_asset->nodes.end() && canonical->second == 1)
            output.poses.emplace(node->name(), current);
        for (int child = 0; child < node->child_count(); ++child) {
            const auto *next = node->children[child];
            if (next->source_asset != root->source_asset || next->source_asset_root)
                continue;
            Matrix local = math::identity();
            if (next->cls() == 5) {
                const auto *data = static_cast<const zcls::Object3d *>(next->data);
                if (!data)
                    throw std::runtime_error("Prepared source control has no class data");
                if (!(data->get<uint32_t>(0) & 8)) {
                    float values[12];
                    std::memcpy(values, data->raw + 0x30, sizeof values);
                    local = from_native(values);
                }
            } else if (next->cls() != 6) {
                throw std::runtime_error("Prepared source hierarchy contains an "
                                         "unsupported transform class");
            }
            if (!output.nodes.emplace(next, math::product(current, local)).second)
                throw std::runtime_error("Prepared source hierarchy is shared or cyclic");
            pending.push_back(next);
        }
    }
    return output;
}

void require_source(const Node &root, const std::string &name) {
    if (name.empty())
        return;
    const auto found = root.source_asset->nodes.find(name);
    if (found == root.source_asset->nodes.end() || found->second != 1)
        throw std::runtime_error("Prepared binding names a missing or ambiguous source control: " +
                                 name);
}

Matrix placed(const Matrix &root, const Poses &poses, const Batch &batch) {
    Matrix result = math::product(root, poses.at(batch.owner));
    auto offset = [&](const std::string &frame, const std::array<float, 3> &amount) {
        if (frame.empty())
            return;
        const auto reference = math::product(root, poses.at(frame));
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned col = 0; col < 3; ++col)
                result[row * 4 + 3] += reference[row * 4 + col] * amount[col];
    };
    offset(batch.placement_frame, batch.placement_offset);
    offset(batch.joint_frame, batch.joint_offset);
    if (!batch.anchor_control.empty()) {
        const auto parent = math::product(root, poses.at(batch.placement_frame));
        const auto anchor = math::product(root, poses.at(batch.anchor_control));
        const auto desired = math::point(
            parent, {batch.anchor_position[0], batch.anchor_position[1], batch.anchor_position[2]});
        for (unsigned row = 0; row < 3; ++row)
            result[row * 4 + 3] += desired[row] - anchor[row * 4 + 3];
    }
    return result;
}

math::Rig rig_for(const platform::prepared::Rig &source) {
    math::Rig rig;
    rig.ground_reference = source.ground_reference;
    rig.hip_drop = source.hip_drop;
    rig.walk_x = source.walk_x;
    rig.walk_z = source.walk_z;
    for (const auto &bone : source.bones)
        rig.bones.push_back({bone.name, bone.control, bone.parent, bone.rest});
    for (const auto &reference : source.references)
        rig.reference.emplace(reference.name, reference.rest);
    return rig;
}

math::Pose donor_pose(const math::Rig &rig, const Poses &poses) {
    const auto axis = math::axis(), inverse_axis = math::inverse(axis);
    Poses display;
    for (const auto &[name, matrix] : poses)
        if (rig.reference.count(name))
            display.emplace(name, math::product(math::product(axis, matrix), inverse_axis));
    // Detached controls are reconstructed only for the visual solver. Actual
    // visibility still comes from native draws; no reconstructed control is
    // emitted.
    for (const auto &[name, reference] : rig.reference)
        if (!display.count(name)) {
            const std::string side = name.front() == 'l' ? "l" : name.front() == 'r' ? "r" : "";
            std::string ancestor = "torso";
            if (!side.empty()) {
                if (name.rfind(side + "toe", 0) == 0)
                    ancestor = side + "foot";
                else if (name == side + "hand")
                    ancestor = side + "arm";
                else if (name == side + "foot")
                    ancestor = side + "calf";
                else if (name == side + "calf")
                    ancestor = side + "thigh";
                else if (name == side + "thigh")
                    ancestor = "hip";
            }
            if (!display.count(ancestor))
                ancestor = "torso";
            if (!display.count(ancestor) || !rig.reference.count(ancestor))
                throw std::runtime_error("Prepared donor rig has no surviving torso frame");
            display.emplace(name,
                            math::product(math::product(display.at(ancestor),
                                                        math::inverse(rig.reference.at(ancestor))),
                                          reference));
        }
    return math::retarget(rig, display, math::locomotion(rig, display));
}

} // namespace

PreparedModels::PreparedModels(const sdlfe::CoreCriticalAssets &options,
                               const std::vector<PreparedMaterialBinding> &materials) {
    if (!options.textures.empty()) {
        auto input = open(options.textures);
        named_ = platform::prepared::read_textures(input);
    }
    std::set<std::string> selected;
    std::map<std::pair<std::string, std::string>, std::string> normals;
    std::map<std::pair<std::string, std::string>, std::string> surface_materials;
    for (const auto &binding : materials)
        if (!surface_materials.emplace(std::make_pair(binding.chassis, binding.lod), binding.path)
                 .second)
            throw std::runtime_error("Duplicate prepared surface-material selection");
    for (const auto &binding : options.normals)
        if (!normals.emplace(std::make_pair(binding.chassis, binding.lod), binding.path).second)
            throw std::runtime_error("Duplicate prepared normal-map selection");
    for (const auto &binding : options.models) {
        if (binding.chassis.empty() || binding.chassis.size() > 32 ||
            binding.chassis.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") !=
                std::string::npos ||
            (binding.lod != "0" && binding.lod != "1"))
            throw std::runtime_error("--cc-model requires a lowercase chassis ID and LOD 0 or 1");
        Model model;
        model.source_asset = "mech_" + binding.chassis + (binding.lod == "1" ? "_1.flt" : ".flt");
        if (!selected.insert(model.source_asset).second)
            throw std::runtime_error("Duplicate prepared model selection");
        auto geometry = open(binding.mesh), textures = open(binding.textures);
        model.mesh = platform::prepared::read_mesh(geometry);
        model.textures = platform::prepared::read_textures(textures);
        platform::prepared::validate_bindings(model.mesh, model.textures);
        const auto normal = normals.find({binding.chassis, binding.lod});
        if (normal != normals.end()) {
            auto input = open(normal->second);
            model.normals = platform::prepared::read_normals(input, model.textures);
            normals.erase(normal);
            std::fprintf(stderr, "Prepared normals loaded: %s, %zu images\n",
                         model.source_asset.c_str(), model.normals.images.size());
        }
        const auto mat = surface_materials.find({binding.chassis, binding.lod});
        if (mat != surface_materials.end()) {
            auto input = open(mat->second);
            std::vector<std::string> batch_materials;
            batch_materials.reserve(model.mesh.batches.size());
            for (std::size_t b = 0; b < model.mesh.batches.size(); ++b) {
                if (b < model.textures.bindings.size()) {
                    const auto img_idx = model.textures.bindings[b];
                    if (img_idx < model.textures.images.size())
                        batch_materials.push_back(model.textures.images[img_idx].name);
                    else
                        batch_materials.push_back({});
                } else {
                    batch_materials.push_back({});
                }
            }
            const uint64_t uv_hash = mw34::materials::compute_mesh_uv_hash(
                model.mesh.batches, 0, model.mesh.batches.size());
            const std::string expected_asset_id =
                binding.chassis + (binding.lod == "1" ? ":far" : ":close");
            model.surface_materials = std::make_shared<mw34::materials::SurfaceMaterialPack>(
                platform::prepared::validate_surface_materials(input, expected_asset_id, uv_hash,
                                                               batch_materials));
            surface_materials.erase(mat);
            std::fprintf(stderr, "Prepared surface materials loaded: %s, %zu records\n",
                         model.source_asset.c_str(), model.surface_materials->records.size());
        }
        // Mesh colors are diagnostics from preparation. As in Core Critical's
        // existing adapter, a bound albedo supplies the final material color.
        for (auto &batch : model.mesh.batches)
            batch.color = 0xffffffff;
        if (model.mesh.version == 4) {
            const auto rig = rig_for(model.mesh.rig);
            std::set<std::string> names;
            for (const auto &bone : rig.bones)
                names.insert(bone.name);
            for (const char *name : {"Pelvis", "Torso_Twist", "Upperarm_Left", "Upperarm_Right",
                                     "Forearm_Left", "Forearm_Right", "Thigh_Left", "Thigh_Right",
                                     "Calf_Left", "Calf_Right", "Foot0_Left", "Foot0_Right"})
                if (!names.count(name))
                    throw std::runtime_error("Prepared donor rig is missing a required joint");
            for (const char *name : {"hip", "torso", "larm", "rarm", "lfoot", "rfoot", "lthigh",
                                     "rthigh", "lcalf", "rcalf"})
                if (!rig.reference.count(name))
                    throw std::runtime_error(
                        "Prepared donor rig is missing a required source reference");
        }
        std::fprintf(stderr, "Prepared model loaded: %s, BWMESH%u, %zu batches, %zu materials\n",
                     model.source_asset.c_str(), model.mesh.version, model.mesh.batches.size(),
                     model.textures.images.size());
        models_.push_back(std::move(model));
    }
    if (!surface_materials.empty())
        throw std::runtime_error("A prepared surface-material selection has no matching model/LOD");
    if (!normals.empty())
        throw std::runtime_error("A prepared normal-map selection has no matching model/LOD");
    if (options.lighting != "off" && models_.empty())
        throw std::runtime_error("Prepared lighting requires at least one --cc-model selection");
    if (options.lighting == "normal" && options.normals.empty())
        throw std::runtime_error("Normal lighting requires --cc-model-normals");
}

const platform::prepared::Image *PreparedModels::named_texture(const char *name) const {
    if (name)
        for (const auto &image : named_.images)
            if (image.name == name)
                return &image;
    return nullptr;
}

PreparedModelDraw PreparedModels::draw(const platform::ModelDrawContext &context,
                                       bool rigid_only) const {
    sdlfe::CpuInterval interval(cpu_profile_ ? &cpu_statistics_.selection_ticks : nullptr);
    if (cpu_profile_)
        ++cpu_statistics_.calls;
    PreparedModelDraw output;
    const auto *node = static_cast<const Node *>(context.node);
    if (!node || !node->source_asset || !context.ordinary || context.morphing || context.overlay ||
        context.opacity != 1)
        return output;
    if (!node->model || node->model->count(0x14) != node->source_model_vertices)
        return output;
    const Model *selected = nullptr;
    for (const auto &model : models_)
        if (model.source_asset == node->source_asset->name)
            selected = &model;
    if (!selected)
        return output;
    // Admission must not run mixed-bone vertex deformation merely to discover
    // that this early GPU route cannot consume it.
    if (rigid_only && selected->mesh.version == 4)
        for (const auto &batch : selected->mesh.batches)
            if (!batch.donor_indices.empty())
                return output;
    const Node *root = node;
    for (unsigned depth = 0; !root->source_asset_root; ++depth) {
        if (depth >= 64 || root->parent_count() != 1 ||
            root->parents[0]->source_asset != node->source_asset)
            return output; // a detached part retains the ordinary native geometry
        root = root->parents[0];
    }
    for (const auto &batch : selected->mesh.batches) {
        for (const auto *name : {&batch.owner, &batch.visibility, &batch.placement_frame,
                                 &batch.joint_frame, &batch.anchor_control})
            require_source(*root, *name);
    }
    for (const auto &reference : selected->mesh.rig.references)
        require_source(*root, reference.name);
    const auto live = hierarchy(root);
    const auto inverse_node = math::inverse(live.nodes.at(node));
    const auto root_camera = math::product(from_native(context.model_to_camera), inverse_node);
    const auto root_world = math::product(from_native(context.model_to_world), inverse_node);
    math::Pose donor;
    if (selected->mesh.version == 4) {
        sdlfe::CpuInterval retarget(cpu_profile_ ? &cpu_statistics_.retarget_ticks : nullptr);
        donor = donor_pose(rig_for(selected->mesh.rig), live.poses);
    }
    output.replace = true;
    for (size_t index = 0; index < selected->mesh.batches.size(); ++index) {
        const auto &batch = selected->mesh.batches[index];
        const auto &visibility = batch.visibility.empty() ? batch.owner : batch.visibility;
        if (visibility != node->name() || !live.poses.count(batch.owner))
            continue;
        PreparedBatchDraw draw;
        draw.batch = &batch;
        draw.texture = &selected->textures.images.at(selected->textures.bindings[index]);
        if (!selected->normals.bindings.empty()) {
            const auto normal = selected->normals.bindings[index];
            if (normal != platform::prepared::no_normal)
                draw.normal = &selected->normals.images.at(normal);
        }
        if (selected->surface_materials && !selected->surface_materials->bindings.empty()) {
            const auto sm_index = selected->surface_materials->bindings[index];
            if (sm_index != ::mw34::materials::no_surface_material &&
                sm_index < selected->surface_materials->records.size())
                draw.surface_material = selected->surface_materials->records[sm_index].get();
        }
        if (selected->mesh.version != 4) {
            draw.camera = placed(root_camera, live.poses, batch);
            draw.world = placed(root_world, live.poses, batch);
        } else {
            const auto camera = math::product(root_camera, math::inverse(math::axis()));
            const auto world = math::product(root_world, math::inverse(math::axis()));
            if (batch.donor_indices.empty()) {
                draw.camera = math::product(camera, donor.world.at(batch.donor_bone));
                draw.world = math::product(world, donor.world.at(batch.donor_bone));
            } else {
                sdlfe::CpuInterval deform(cpu_profile_ ? &cpu_statistics_.deform_ticks : nullptr);
                draw.camera = camera;
                draw.world = world;
                draw.deformed = batch.vertices;
                for (size_t vertex = 0; vertex < draw.deformed.size(); ++vertex) {
                    auto &value = draw.deformed[vertex];
                    const auto &bone = donor.world.at(batch.donor_indices[vertex]);
                    const auto position = math::point(bone, {value[0], value[1], value[2]});
                    const auto inverse = math::inverse(bone);
                    const auto normal = math::unit(
                        {inverse[0] * value[3] + inverse[4] * value[4] + inverse[8] * value[5],
                         inverse[1] * value[3] + inverse[5] * value[4] + inverse[9] * value[5],
                         inverse[2] * value[3] + inverse[6] * value[4] + inverse[10] * value[5]});
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        value[axis] = float(position[axis]);
                        value[axis + 3] = float(normal[axis]);
                    }
                }
            }
        }
        output.batches.push_back(std::move(draw));
    }
    return output;
}

bool PreparedModels::has_binding(const platform::ModelDrawContext &context) const {
    const auto *node = static_cast<const Node *>(context.node);
    if (!node || !node->source_asset)
        return false;
    for (const auto &model : models_)
        if (model.source_asset == node->source_asset->name)
            return true;
    return false;
}

} // namespace cc_mw3_re
