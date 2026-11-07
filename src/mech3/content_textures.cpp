#include "mech3/content_textures.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>

#include "gamez/zimage/zimg_library.h"
#include "mech3/content_registry.h"
#include "platform/prepared_visuals.h"

namespace mech3 {
namespace {
using TextureOwners =
    std::map<zimg::Texture *, std::shared_ptr<const cc::content::ResolvedContentSet>>;
TextureOwners *&live_slot() {
    // This trivial slot remains safe after the owner's destructor has run.
    static TextureOwners *value = nullptr;
    return value;
}
struct TextureOwnerLifetime {
    TextureOwners owners;
    TextureOwnerLifetime() { live_slot() = &owners; }
    ~TextureOwnerLifetime() { live_slot() = nullptr; }
};
TextureOwners &live_owners() {
    static TextureOwnerLifetime lifetime;
    return lifetime.owners;
}
std::string lower(std::string text) {
    for (char &value : text)
        if (value >= 'A' && value <= 'Z')
            value += 'a' - 'A';
    return text;
}
const cc::content::Definition *named(const cc::content::ResolvedContentSet &content,
                                     const char *name) {
    if (!name)
        return nullptr;
    const std::string wanted = lower(name);
    for (const auto *definition : content.of_kind("texture"))
        if (definition->data.at("name") == wanted)
            return definition;
    return nullptr;
}
void reject(const cc::content::Definition &definition, const std::string &field,
            const std::string &message) {
    cc::content::fail("named-texture", message, definition.owner, definition.id, field);
}
} // namespace

void register_texture_capabilities(cc::content::Capabilities &capabilities) {
    capabilities["named-textures"] = 1;
}

void validate_content_textures(const cc::content::ResolvedContentSet &content) {
    std::set<std::string> names;
    std::map<cc::content::fs::path, platform::prepared::Textures> indexes;
    for (const auto *definition : content.of_kind("texture")) {
        if (definition->behavior != "rgb565-v1" || !definition->data.is_object())
            reject(*definition, "behavior", "Unsupported named texture contract");
        for (const auto &field : definition->data.items())
            if (field.key() != "name" && field.key() != "file" && field.key() != "image")
                reject(*definition, field.key(), "Unknown named texture field");
        for (const auto *field : {"name", "file", "image"})
            if (!definition->data.contains(field) || !definition->data.at(field).is_string() ||
                definition->data.at(field).get<std::string>().empty())
                reject(*definition, field, "Required texture field is missing");
        const std::string name = definition->data.at("name").get<std::string>();
        if (name.size() > 19 ||
            name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
            reject(*definition, "name",
                   "Native texture names require 1-19 lowercase letters, digits or underscores");
        if (!names.insert(name).second)
            reject(*definition, "name", "Two definitions claim the same native texture name");
        const auto path =
            content.resource(definition->owner, definition->data.at("file").get<std::string>());
        std::ifstream input(path, std::ios::binary);
        char magic[8]{};
        if (!input.read(magic, sizeof magic) || std::memcmp(magic, "BWTEX1\0\0", 8))
            reject(*definition, "file", "Named textures require a BWTEX1 payload");
        auto index = indexes.find(path);
        if (index == indexes.end()) {
            input.seekg(0);
            index = indexes.emplace(path, platform::prepared::read_texture_index(input)).first;
        }
        const auto image = std::find_if(
            index->second.images.begin(), index->second.images.end(), [&](const auto &entry) {
                return entry.name == definition->data.at("image").get<std::string>();
            });
        if (image == index->second.images.end())
            reject(*definition, "image", "Named image is absent from its payload");
        if ((image->width & (image->width - 1)) || (image->height & (image->height - 1)))
            reject(*definition, "image", "Native named textures require power-of-two dimensions");
    }
}

bool content_texture_named(const char *name) {
    const auto content = active_content();
    return content && named(*content, name);
}

zimg::Texture *load_content_texture(const char *name) {
    const auto content = active_content();
    if (!content)
        return nullptr;
    const auto *definition = named(*content, name);
    if (!definition)
        return nullptr;
    const auto path =
        content->resource(definition->owner, definition->data.at("file").get<std::string>());
    std::ifstream input(path, std::ios::binary);
    const auto payload = platform::prepared::read_textures(input);
    const auto image =
        std::find_if(payload.images.begin(), payload.images.end(), [&](const auto &entry) {
            return entry.name == definition->data.at("image").get<std::string>();
        });
    if (image == payload.images.end())
        reject(*definition, "image", "Named image is absent from its payload");
    if (!image->width || !image->height || image->width > 8192 || image->height > 8192 ||
        (image->width & (image->width - 1)) || (image->height & (image->height - 1)) ||
        image->pixels.size() > 16 * 1024 * 1024)
        reject(*definition, "image",
               "Native named textures require bounded power-of-two dimensions");
    auto *texture = static_cast<zimg::Texture *>(std::calloc(1, sizeof(zimg::Texture)));
    if (!texture)
        reject(*definition, "image", "Cannot allocate the native texture");
    texture->pixels = std::malloc(image->pixels.size() * sizeof(uint16_t));
    if (!texture->pixels) {
        std::free(texture);
        reject(*definition, "image", "Cannot allocate the native texture pixels");
    }
    std::memcpy(texture->pixels, image->pixels.data(), image->pixels.size() * sizeof(uint16_t));
    texture->pixel_count = static_cast<int32_t>(image->pixels.size());
    texture->width = static_cast<int16_t>(image->width);
    texture->height = static_cast<int16_t>(image->height);
    texture->mip_scale = 1;
    texture->flags = zimg::kTwoBytes | zimg::kNoAlpha | zimg::kPixelsOwned;
    if (zimg::state().display_format == 5) {
        auto *pixels = static_cast<uint16_t *>(texture->pixels);
        for (int32_t i = 0; i < texture->pixel_count; ++i)
            pixels[i] = ((pixels[i] >> 1) & 0x7fe0) | (pixels[i] & 0x1f);
    }
    live_owners().emplace(texture, content);
    return texture;
}

void content_texture_released(zimg::Texture *texture) {
    // Native font/UI statics can release unrelated textures after content
    // owners are destroyed. Never enter an already destroyed map at shutdown.
    if (auto *owners = live_slot())
        owners->erase(texture);
}

void require_content_texture_quiescence() {
    if (live_slot() && !live_slot()->empty())
        cc::content::fail("content-in-use",
                          "Release native named textures before changing the content profile");
}
} // namespace mech3
