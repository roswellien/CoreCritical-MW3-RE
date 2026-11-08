#include "check.h"
#include "crt/msvc_rand.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zrender/zrndr_blit.h"
#include "gamez/zrender/zrndr_prim2d.h"
#include "gamez/zrender/zrndr_tint.h"
#include "gamez/ztwod/twod.h"
#include "mech3/content_hud.h"
#include "mech3/content_registry.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace {
struct RestoreContent {
    std::shared_ptr<const cc::content::ResolvedContentSet> saved = mech3::active_content();
    ~RestoreContent() { mech3::activate_content(saved); }
};
cc::content::ResolvedContentSet layout() {
    cc::content::ResolvedContentSet content;
    cc::content::Definition hud;
    hud.kind = "hud";
    hud.id = "demo:wide";
    hud.owner = "demo";
    hud.behavior = "native-widgets-v1";
    hud.data = cc::content::Json::parse(R"({"reference_width":640,"reference_height":480,
    "widgets":{"TIMER":{"position":{"x":580,"y":15,"anchor_x":"right","anchor_y":"top"}},
    "MFD":{"position":{"x":530,"y":370,"anchor_x":"right","anchor_y":"bottom"},
           "size":{"width":75,"height":100}},"WEAPONS":{"width":100}}})");
    content.definitions.push_back(hud);
    content.lock["selections"]["hud"] = hud.id;
    return content;
}
} // namespace

TEST(content_hud_selected_layout_tracks_native_viewport_and_preserves_unselected) {
    RestoreContent restore;
    mech3::activate_content(nullptr);
    int32_t point[2] = {10, 20};
    EXPECT(!mech3::content_hud_position("TIMER", "position", 1920, 1080, point));
    EXPECT(point[0] == 10 && point[1] == 20);
    auto content = std::make_shared<cc::content::ResolvedContentSet>(layout());
    mech3::validate_content_hud(*content);
    mech3::activate_content(content);
    EXPECT(mech3::content_hud_position("TIMER", "position", 1920, 1080, point));
    EXPECT(point[0] == 1860 && point[1] == 15);
    EXPECT(mech3::content_hud_position("MFD", "position", 3440, 1440, point));
    EXPECT(point[0] == 3330 && point[1] == 1330);
    EXPECT(mech3::content_hud_size("MFD", point));
    EXPECT(point[0] == 75 && point[1] == 100);
    int32_t width = 0;
    EXPECT(mech3::content_hud_width("WEAPONS", &width) && width == 100);
    EXPECT(!mech3::content_hud_position("TARGET", "position", 3440, 1440, point));
    content = std::make_shared<cc::content::ResolvedContentSet>(layout());
    content->definitions[0].data["scale"] = 2;
    mech3::activate_content(content);
    EXPECT(mech3::content_hud_position("MFD", "position", 1920, 1080, point));
    EXPECT(point[0] == 1700 && point[1] == 860);
    EXPECT(mech3::content_hud_size("MFD", point));
    EXPECT(point[0] == 150 && point[1] == 200);
}

TEST(content_hud_rejects_unknown_widgets_unsafe_sizes_and_missing_selection) {
    using cc::content::Json;
    const std::vector<std::pair<std::string, Json>> invalid = {
        {"/widgets/UNSUPPORTED", Json::object()},
        {"/widgets/TIMER/position/anchor_x", "sideways"},
        {"/widgets/TIMER/position/x", 1e100},
        {"/widgets/MFD/size/width", 0},
        {"/widgets/TIMER/visible", "false"},
        {"/widgets/RETICLE/position", Json::object()},
        {"/widgets/RADAR/radius", 0},
        {"/widgets/RADAR/zoom_radius", 4097},
        {"/scale", 0},
        {"/script", "run something"}};
    for (const auto &row : invalid) {
        auto content = layout();
        content.definitions[0].data[Json::json_pointer(row.first)] = row.second;
        bool refused = false;
        try {
            mech3::validate_content_hud(content);
        } catch (const cc::content::Error &) {
            refused = true;
        }
        EXPECT(refused);
    }
    auto content = layout();
    content.lock["selections"]["hud"] = "absent:layout";
    bool refused = false;
    try {
        mech3::validate_content_hud(content);
    } catch (const cc::content::Error &) {
        refused = true;
    }
    EXPECT(refused);
}

TEST(content_hud_visibility_supports_all_existing_widgets_and_radar_metrics) {
    RestoreContent restore;
    auto content = std::make_shared<cc::content::ResolvedContentSet>(layout());
    for (const char *piece : {"MFD", "TARGET", "TIMER", "WEAPONS", "EQUIPMENT", "COMPASS", "GAUGES",
                              "RADAR", "RETICLE", "OBJECTIVES"})
        content->definitions[0].data["widgets"][piece]["visible"] = false;
    content->definitions[0].data["widgets"]["RADAR"]["radius"] = 72;
    content->definitions[0].data["widgets"]["RADAR"]["zoom_radius"] = 240;
    content->definitions[0].data["widgets"]["RADAR"]["zoom_position"] = {
        {"x", 320}, {"y", 240}, {"anchor_x", "center"}, {"anchor_y", "center"}};
    content->definitions[0].data["widgets"]["OBJECTIVES"]["position"] = {
        {"x", 20}, {"y", 90}, {"anchor_x", "left"}, {"anchor_y", "top"}};
    mech3::activate_content(content);
    for (const char *piece : {"MFD", "TARGET", "TIMER", "WEAPONS", "EQUIPMENT", "COMPASS", "GAUGES",
                              "RADAR", "RETICLE", "OBJECTIVES"})
        EXPECT(!mech3::content_hud_visible(piece));
    int32_t radius = 0;
    EXPECT(mech3::content_hud_radius(false, &radius) && radius == 72);
    EXPECT(mech3::content_hud_radius(true, &radius) && radius == 240);
    int32_t position[2] = {};
    EXPECT(mech3::content_hud_position("RADAR", "zoom_position", 1920, 1080, position));
    EXPECT(position[0] == 960 && position[1] == 540);
    EXPECT(mech3::content_hud_position("OBJECTIVES", "position", 1920, 1080, position));
    EXPECT(position[0] == 20 && position[1] == 90);
    mech3::activate_content(nullptr);
    EXPECT(mech3::content_hud_visible("RADAR"));
}

TEST(content_hud_presentation_mask_preserves_queued_draws_timers_and_nested_updates) {
    struct Host : ztwod::Host {
        int blits = 0;
        void blit(zimg::Texture *, int32_t, int32_t, int32_t, const ztwod::Rect *) override {
            ++blits;
        }
    } host;
    auto *previous = &ztwod::host();
    ztwod::set_host(&host);
    {
        zimg::Texture texture{};
        ztwod::Image image(&texture, 0, 0, 0);
        image.flags = ztwod::kTimed;
        image.timer = 1;
        image.queued = 1;
        image.queue[0].count = 2;
        image.set_presentation_visible(false);
        image.update(0.25f);
        EXPECT(host.blits == 0 && image.timer == 0.75f && image.queue[0].count == 1);
        image.update(0.25f);
        EXPECT(host.blits == 0 && image.timer == 0.5f && image.queued == 0);
        image.set_presentation_visible(true);
        image.update(0.25f);
        EXPECT(host.blits == 1 && image.timer == 0.25f);

        struct Composite : ztwod::Object2D {
            ztwod::Image &child;
            int updates = 0;
            ztwod::Rect rect{0, 0, 10, 10};
            explicit Composite(ztwod::Image &value) : Object2D(0, 0), child(value) {}
            void update(float dt) override {
                ++updates;
                child.update(dt);
            }
            ztwod::Rect *get_rect() override { return &rect; }
        } composite(image);
        ztwod::Layer layer;
        layer.set_active(1);
        layer.add(&composite);
        composite.set_presentation_visible(false);
        EXPECT(composite.hit(5, 5) == 0);
        layer.update(0.125f);
        EXPECT(composite.updates == 1 && image.timer == 0.125f && host.blits == 1);
        composite.set_presentation_visible(true);
        EXPECT(composite.hit(5, 5) == 1);
        layer.update(0.125f);
        EXPECT(composite.updates == 2 && image.timer == 0 && (image.flags & ztwod::kHidden));
        EXPECT(host.blits == 2);
    }
    ztwod::set_host(previous);
}

TEST(content_hud_mask_suppresses_noisy_pixels_and_damage_static_without_changing_rng) {
    const auto previous_target = zrndr::target2d();
    const auto previous_finish = zrndr::finish_target();
    const auto previous_cover = zrndr::layer_cover();
    const auto previous_noise = zrndr::noise_table();
    std::array<uint16_t, 256> pixels{};
    zrndr::target2d() = {reinterpret_cast<uint8_t *>(pixels.data()), 16, 16, 32, 6};
    zrndr::set_finish_target(pixels.data(), 16, 16, 32);
    zrndr::layer_cover() = {};
    zrndr::noise_table().assign(0x6401, 1);
    auto draw = [&] {
        zrndr::circle16_noisy(8, 8, 3, 0xffff, 0.2f, nullptr);
        zrndr::line16_noisy(2, 2, 10, 10, 0xffff, 0.2f, nullptr);
        const int32_t rect[4] = {1, 1, 7, 7};
        zrndr::static_fill(rect, 0.8, 1, 2, 3);
    };
    crt::msvc_srand(123);
    draw();
    const int next = crt::msvc_rand();
    EXPECT(std::any_of(pixels.begin(), pixels.end(), [](uint16_t value) { return value != 0; }));
    pixels.fill(0);
    crt::msvc_srand(123);
    {
        ztwod::PresentationScope hidden(false);
        draw();
        ztwod::PresentationScope nested(true);
        EXPECT(!ztwod::presentation_output_enabled());
    }
    EXPECT(ztwod::presentation_output_enabled());
    EXPECT(crt::msvc_rand() == next);
    EXPECT(std::all_of(pixels.begin(), pixels.end(), [](uint16_t value) { return value == 0; }));
    zrndr::target2d() = previous_target;
    zrndr::finish_target() = previous_finish;
    zrndr::layer_cover() = previous_cover;
    zrndr::noise_table() = previous_noise;
}

TEST(content_hud_artwork_metadata_and_owned_lazy_resource_contract) {
    RestoreContent restore;
    namespace fs = std::filesystem;
    const fs::path directory = fs::path(MW3_TEST_TMP) / "content-hud-artwork";
    fs::create_directories(directory);
    std::string bytes(36, '\0');
    std::memcpy(bytes.data(), "MW3H", 4);
    bytes[4] = 2;
    bytes[8] = 1;
    const float uv[] = {0, 0, 1, 1};
    std::memcpy(bytes.data() + 12, uv, sizeof uv);
    const uint8_t pixels[] = {200, 100, 50, 128, 255, 200, 100, 0};
    std::memcpy(bytes.data() + 28, pixels, sizeof pixels);
    const auto path = directory / "authored.rgba";
    std::ofstream(path, std::ios::binary) << bytes;
    const auto metadata = platform::inspect_hud_sprite(path);
    EXPECT(metadata.width == 2 && metadata.height == 1 && metadata.pixels.empty());
    EXPECT(platform::load_hud_sprite(path)->pixels == std::vector<uint32_t>({0x80193264, 0}));
    auto content = std::make_shared<cc::content::ResolvedContentSet>(layout());
    cc::content::Package package;
    package.id = "demo";
    package.root = directory;
    package.manifest["files"]["authored.rgba"] = {{"bytes", bytes.size()},
                                                  {"sha256", cc::content::sha256(bytes)}};
    content->packages.push_back(package);
    content->definitions[0].data["artwork"] = {
        {"format", "mw3h-v1"}, {"sprites", {{"daa81911a07d967e", "authored.rgba"}}}};
#if MW3_CONTENT_HUD_ARTWORK
    mech3::activate_content(content);
    auto sources = mech3::content_hud_artwork();
    EXPECT(sources.size() == 1 && sources.at(0xdaa81911a07d967eULL)() == path);
    // Same-size mutation after metadata validation must fail at lazy access.
    bytes.back() = 1;
    std::ofstream(path, std::ios::binary) << bytes;
    bool rejected = false;
    try {
        (void)sources.at(0xdaa81911a07d967eULL)();
    } catch (const cc::content::Error &) {
        rejected = true;
    }
    EXPECT(rejected);
    content->packages[0].manifest["files"]["authored.rgba"]["sha256"] = cc::content::sha256(bytes);
    bytes[4] = 0; // Admitted but malformed payload must fail metadata validation.
    std::ofstream(path, std::ios::binary) << bytes;
    content->packages[0].manifest["files"]["authored.rgba"]["sha256"] = cc::content::sha256(bytes);
    rejected = false;
    try {
        mech3::validate_content_hud(*content);
    } catch (const cc::content::Error &) {
        rejected = true;
    }
    EXPECT(rejected);
#else
    bool rejected = false;
    try {
        mech3::validate_content_hud(*content);
    } catch (const cc::content::Error &error) {
        rejected = error.diagnostic.code == "hud-artwork-backend";
    }
    EXPECT(rejected);
#endif
}
