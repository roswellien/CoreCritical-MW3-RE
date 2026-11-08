#include "check.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zimage/zimg_texture.h"
#include "mech3/content_registry.h"
#include "mech3/content_textures.h"
#include "prepared_visual_fixture.h"

#include <filesystem>
#include <fstream>

TEST(content_named_texture_uses_native_cache_and_blocks_live_registry_replacement) {
    namespace fs = std::filesystem;
    const auto previous = mech3::active_content();
    const auto saved_load = zimg::texture_load();
    const auto saved_state = zimg::state();
    const fs::path folder = fs::path(MW3_TEST_TMP) / "content-textures";
    fs::create_directories(folder);
    const auto payload = prepared_fixture::textures();
    std::ofstream(folder / "texture.bin", std::ios::binary) << payload;
    auto content = std::make_shared<cc::content::ResolvedContentSet>();
    cc::content::Package package;
    package.id = "demo";
    package.root = folder;
    package.manifest["files"]["texture.bin"] = {{"bytes", payload.size()},
                                                {"sha256", cc::content::sha256(payload)}};
    content->packages.push_back(package);
    content->definitions.push_back(
        {"texture",
         "demo:color",
         "demo",
         "rgb565-v1",
         {{"name", "demo_color"}, {"file", "texture.bin"}, {"image", "authored"}}});
    mech3::activate_content(content);
    zimg::state().enabled = true;
    zimg::state().display_format = 6;
    auto *first = zimg::cache_fetch("demo_color");
    auto *second = zimg::cache_fetch("demo_color");
    EXPECT(first && first == second);
    if (first) {
        EXPECT(first->width == 2 && first->height == 1 && first->pixel_count == 2);
        EXPECT(static_cast<uint16_t *>(first->pixels)[0] == 0xf800);
    }
    bool refused = false;
    try {
        mech3::activate_content(nullptr);
    } catch (const cc::content::Error &) {
        refused = true;
    }
    EXPECT(refused && mech3::active_content() == content);
    zimg::cache_release(first);
    zimg::cache_release(second);
    mech3::activate_content(nullptr);
    EXPECT(!mech3::content_texture_named("demo_color"));

    auto invalid = *content;
    invalid.definitions.push_back(invalid.definitions.front());
    invalid.definitions.back().id = "demo:duplicate";
    refused = false;
    try {
        mech3::validate_content_textures(invalid);
    } catch (const cc::content::Error &) {
        refused = true;
    }
    EXPECT(refused);
    invalid = *content;
    invalid.definitions[0].data["file"] = "../outside.bin";
    refused = false;
    try {
        mech3::validate_content_textures(invalid);
    } catch (const cc::content::Error &) {
        refused = true;
    }
    EXPECT(refused);
    invalid = *content;
    invalid.definitions[0].data["image"] = "missing";
    refused = false;
    try {
        mech3::validate_content_textures(invalid);
    } catch (const cc::content::Error &) {
        refused = true;
    }
    EXPECT(refused);
    zimg::texture_load() = saved_load;
    zimg::state() = saved_state;
    mech3::activate_content(previous);
    fs::remove_all(folder);
}
