#include "gamez/zclass/cls_util.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zmodel/gmod_fogcalc.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "platform/prepared_visuals.h"
#include "platform/render3d.h"
#include "prepared_visual_fixture.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
using prepared_fixture::mesh;
using prepared_fixture::textures;
template <class Function> void rejects(const std::string &data, Function read) {
    bool rejected = false;
    try {
        std::istringstream input(data);
        read(input);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "Malformed prepared payload was accepted");
}
void checks() {
    std::istringstream tex_input(textures());
    const auto tex = platform::prepared::read_textures(tex_input);
    require(tex.images.size() == 1 && tex.images[0].pixels == std::vector<uint16_t>{0xf800, 0x07e0},
            "RGB565 material pixels changed");
    const auto read_normals = [&](std::istream &input) {
        return platform::prepared::read_normals(input, tex);
    };
    std::istringstream normal_input(prepared_fixture::normals(0x40ff8080));
    const auto normals = read_normals(normal_input);
    require(normals.images.size() == 1 && normals.images[0].pixels[0] == 0x40ff8080,
            "Normal bytes or authored alpha changed");
    std::istringstream absent_input(prepared_fixture::normals(0, true));
    require(read_normals(absent_input).bindings[0] == platform::prepared::no_normal,
            "Explicit absent normal binding was lost");
    const auto normal_bytes = prepared_fixture::normals();
    rejects(normal_bytes.substr(0, normal_bytes.size() - 1), read_normals);
    rejects(normal_bytes + "extra", read_normals);
    for (size_t offset :
         {size_t(6), size_t(8), size_t(16), size_t(20), size_t(52), size_t(60), size_t(68)}) {
        auto bad = normal_bytes;
        bad[offset] ^= char(0x7f);
        rejects(bad, read_normals);
    }
    for (unsigned version = 1; version <= 6; ++version) {
        const auto data = mesh(version);
        std::istringstream input(data);
        const auto parsed = platform::prepared::read_mesh(input);
        require(parsed.version == version && parsed.batches.size() == 1 &&
                    parsed.batches[0].vertices.size() == 3,
                "Prepared mesh layout changed");
        platform::prepared::validate_bindings(parsed, tex);
        rejects(data.substr(0, data.size() - 1), platform::prepared::read_mesh);
        rejects(data + "extra", platform::prepared::read_mesh);
    }
    auto bad_mesh = mesh(2);
    bad_mesh[8] = 0;
    rejects(bad_mesh, platform::prepared::read_mesh);
    bad_mesh = mesh(2);
    bad_mesh.replace(bad_mesh.size() - 4, 4, std::string("\0\0\x80\x7f", 4));
    rejects(bad_mesh, platform::prepared::read_mesh);
    auto bad_texture = textures();
    bad_texture[16] = 1;
    rejects(bad_texture, platform::prepared::read_textures);
    bad_texture = textures();
    bad_texture[52] = 0;
    rejects(bad_texture, platform::prepared::read_textures);
    rejects(textures() + "extra", platform::prepared::read_textures);
    rejects(textures().substr(0, textures().size() - 1), platform::prepared::read_textures);
    auto *root = zcls::object3d_create();
    require(root != nullptr, "Could not create an authored model tree");
    auto identity = std::make_shared<zcls::SourceAsset>();
    identity->name = "mech_authored.flt";
    root->source_asset = identity;
    root->source_asset_root = true;
    auto *copy = zcls::tree_copy(root, 0, 0);
    require(copy && copy->source_asset == root->source_asset && copy->source_asset_root,
            "Model-tree copy lost source identity");
    zcls::tree_destroy(copy);
    zcls::tree_destroy(root);
    const auto saved_fog = zmod::fog();
    const auto saved_calc = zmod::fog_calc();
    zmod::fog() = {};
    auto &fog = zmod::fog();
    fog.on = 1;
    fog.far_range = 10;
    fog.far_sq = 100;
    fog.inv_range = .1f;
    fog.alt_high = 10;
    fog.inv_alt = .1f;
    zmod::fog_calc().camera_to_world[1][1] = 1;
    const float vertices[][3] = {{0, 0, 0}, {0, -1, 100}, {0, 11, 100}, {0, 5, 100}};
    const float expected[] = {0, 1, 0, .5f};
    zmod::fog_calc().verts = vertices;
    require(zmod::fog_vertices(4) == 1, "Native vertex fog did not report visible fog");
    for (unsigned i = 0; i < 4; ++i)
        require(zmod::fog_at(vertices[i]) == expected[i] &&
                    zmod::fog_calc().factor[i] == expected[i],
                "Prepared/native fog sampling changed near/far or altitude semantics");
    zmod::fog() = saved_fog;
    zmod::fog_calc() = saved_calc;
}

// An invalid binding fails inside the synchronous producer callback. Both its
// visual scope and the native matrix push must unwind before another frame.
void check_producer_unwind() {
    struct RejectingRenderer : platform::Render3D {
        bool ended = false;
        bool wants_model_context() const override { return true; }
        void begin_model_draw(const platform::ModelDrawContext &) override {
            throw std::runtime_error("authored binding rejection");
        }
        void end_model_draw() noexcept override { ended = true; }
        int32_t create_texture(int, int, const uint8_t *, uint8_t) override { return 0; }
        void release_texture(int32_t) override {}
        void update_texture(int32_t, int, int, const uint8_t *) override {}
        void set_viewport(int, int, int, int) override {}
        void clear(bool, uint8_t, uint8_t, uint8_t) override {}
        void begin_scene() override {}
        void end_scene() override {}
        void draw(platform::Primitive, const platform::TLVertex *, int,
                  const platform::DrawState &) override {}
        void set_fog_color(uint8_t, uint8_t, uint8_t) override {}
        void present() override {}
    } renderer;
    const auto saved_stack = zmth::matrix_stack();
    const auto saved_view = zmth::view();
    const auto saved_draw = zmod::draw_state();
    auto *saved_renderer = zvid::render3d();
    zmth::reset_stack();
    zmth::Mat34 root{};
    zmth::push(&root);
    zmth::load_identity();
    zmth::view().view = root;
    const auto depth = zmth::matrix_stack().top;
    zcls::Node node{};
    zmod::Model model{};
    node.model = &model;
    zvid::set_render3d(&renderer);
    bool rejected = false;
    try {
        zmod::draw_model(&node, 0);
    } catch (const std::runtime_error &error) {
        rejected = std::string(error.what()) == "authored binding rejection";
    }
    const bool balanced =
        zmth::matrix_stack().top == depth && zmth::matrix_stack().slot[depth] == &root;
    zvid::set_render3d(saved_renderer);
    zmth::matrix_stack() = saved_stack;
    zmth::view() = saved_view;
    zmod::draw_state() = saved_draw;
    require(rejected && renderer.ended && balanced,
            "Model rejection did not unwind the native matrix/visual scopes");
}
} // namespace

int main(int argc, char **argv) {
    try {
        if ((argc == 4 || argc == 5) && std::string(argv[1]) == "--check") {
            std::ifstream mesh_input(std::filesystem::u8path(argv[2]), std::ios::binary),
                texture_input(std::filesystem::u8path(argv[3]), std::ios::binary);
            if (!mesh_input || !texture_input)
                throw std::runtime_error("Cannot open the selected prepared components");
            const auto geometry = platform::prepared::read_mesh(mesh_input);
            const auto materials = platform::prepared::read_textures(texture_input);
            platform::prepared::validate_bindings(geometry, materials);
            if (argc == 5) {
                std::ifstream normal_input(std::filesystem::u8path(argv[4]), std::ios::binary);
                if (!normal_input)
                    throw std::runtime_error("Cannot open selected normal component");
                const auto normals = platform::prepared::read_normals(normal_input, materials);
                std::printf("BWNRM2: %zu images; ", normals.images.size());
            }
            std::printf("BWMESH%u: %zu batches; BWTEX1: %zu images\n", geometry.version,
                        geometry.batches.size(), materials.images.size());
        } else if (argc == 1) {
            checks();
            check_producer_unwind();
        } else
            throw std::runtime_error("usage: mw3prepared_checks [--check MESH TEXTURES [NORMALS]]");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
