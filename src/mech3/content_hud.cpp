#include "mech3/content_hud.h"

#include <cmath>
#include <map>
#include <set>

#include "mech3/content_registry.h"

namespace mech3 {
namespace {
using cc::content::Json;
const std::map<std::string, std::set<std::string>> positions = {
    {"MFD", {"position"}},
    {"TARGET", {"position"}},
    {"TIMER", {"position"}},
    {"WEAPONS", {"position"}},
    {"EQUIPMENT", {"position"}},
    {"RADAR", {"position", "zoom_position"}},
    {"RETICLE", {}},
    {"OBJECTIVES", {"position"}},
    {"COMPASS", {"tape", "frame"}},
    {"GAUGES",
     {"speed_frame", "fuel_frame", "heat_bar", "heat_meter", "heat_text", "heat_arrow",
      "coolant_frame"}}};

void reject(const cc::content::Definition &definition, const std::string &field) {
    cc::content::fail("hud-layout", "Unsupported or invalid native HUD layout field",
                      definition.owner, definition.id, field);
}
bool number(const Json &value, double minimum, double maximum) {
    return value.is_number() && std::isfinite(value.get<double>()) &&
           value.get<double>() >= minimum && value.get<double>() <= maximum;
}
struct Selection {
    std::shared_ptr<const cc::content::ResolvedContentSet> content;
    const cc::content::Definition *definition = nullptr;
};
Selection selected() {
    Selection result{active_content(), nullptr};
    if (!result.content || !result.content->lock.contains("selections"))
        return result;
    const auto &selections = result.content->lock.at("selections");
    if (selections.contains("hud") && selections.at("hud").is_string())
        result.definition = result.content->find("hud", selections.at("hud").get<std::string>());
    return result;
}
const Json *widget(const Selection &selection, const char *piece) {
    if (!selection.definition)
        return nullptr;
    const auto &widgets = selection.definition->data.at("widgets");
    const auto found = widgets.find(piece);
    return found == widgets.end() ? nullptr : &*found;
}
double scale(const Selection &selection) { return selection.definition->data.value("scale", 1.0); }
void validate_artwork(const cc::content::ResolvedContentSet &content,
                      const cc::content::Definition &definition, const Json &artwork) {
#if !MW3_CONTENT_HUD_ARTWORK
    cc::content::fail("hud-artwork-backend", "This runtime has no native GPU HUD artwork consumer",
                      definition.owner, definition.id, "artwork");
#endif
    if (!artwork.is_object() || artwork.size() != 2 || !artwork.contains("format") ||
        artwork.at("format") != "mw3h-v1" || !artwork.contains("sprites") ||
        !artwork.at("sprites").is_object() || artwork.at("sprites").empty() ||
        artwork.at("sprites").size() > 256)
        reject(definition, "artwork");
    uint64_t total = 0;
    for (const auto &sprite : artwork.at("sprites").items()) {
        if (sprite.key().size() != 16 ||
            sprite.key().find_first_not_of("0123456789abcdef") != std::string::npos ||
            !sprite.value().is_string() || sprite.value().get_ref<const std::string &>().empty())
            reject(definition, "artwork.sprites." + sprite.key());
        const auto path = content.resource(definition.owner, sprite.value().get<std::string>());
        try {
            const auto metadata = platform::inspect_hud_sprite(path);
            total += uint64_t(metadata.width) * metadata.height * 4;
            if (total > 512 * 1024 * 1024)
                reject(definition, "artwork.sprites");
        } catch (const cc::content::Error &) {
            throw;
        } catch (const std::exception &error) {
            cc::content::fail("hud-artwork", error.what(), definition.owner, definition.id,
                              "artwork.sprites." + sprite.key());
        }
    }
}
} // namespace

void register_hud_capabilities(cc::content::Capabilities &capabilities) {
    capabilities["hud-layouts"] = 1;
#if MW3_CONTENT_HUD_ARTWORK
    capabilities["hud-artwork-mw3h-v1"] = 1;
#endif
}

void validate_content_hud(const cc::content::ResolvedContentSet &content) {
    for (const auto *definition : content.of_kind("hud")) {
        if (definition->behavior != "native-widgets-v1")
            reject(*definition, "behavior");
        const auto &data = definition->data;
        if (!data.is_object() || !data.contains("widgets") || !data.at("widgets").is_object())
            reject(*definition, "widgets");
        for (const auto &entry : data.items()) {
            if (entry.key() == "widgets")
                continue;
            if (entry.key() == "artwork") {
                validate_artwork(content, *definition, entry.value());
                continue;
            }
            if (entry.key() == "scale" && number(entry.value(), 0.25, 4))
                continue;
            if ((entry.key() == "reference_width" || entry.key() == "reference_height") &&
                number(entry.value(), 1, 16384))
                continue;
            reject(*definition, entry.key());
        }
        for (const auto &entry : data.at("widgets").items()) {
            const auto allowed = positions.find(entry.key());
            if (allowed == positions.end() || !entry.value().is_object())
                reject(*definition, entry.key());
            for (const auto &rule : entry.value().items()) {
                const std::string field = "widgets." + entry.key() + "." + rule.key();
                if (rule.key() == "visible") {
                    if (!rule.value().is_boolean())
                        reject(*definition, field);
                } else if (entry.key() == "RADAR" &&
                           (rule.key() == "radius" || rule.key() == "zoom_radius")) {
                    if (!number(rule.value(), 1, 4096) ||
                        !number(rule.value().get<double>() * data.value("scale", 1.0), 1, 4096))
                        reject(*definition, field);
                } else if (rule.key() == "width" &&
                           (entry.key() == "WEAPONS" || entry.key() == "EQUIPMENT")) {
                    if (!number(rule.value(), 1, 8192) ||
                        !number(rule.value().get<double>() * data.value("scale", 1.0), 1, 8192))
                        reject(*definition, field);
                } else if (rule.key() == "size" &&
                           (entry.key() == "MFD" || entry.key() == "TARGET")) {
                    if (!rule.value().is_object() || rule.value().size() != 2 ||
                        !rule.value().contains("width") || !rule.value().contains("height") ||
                        !number(rule.value().at("width"), 1, 8192) ||
                        !number(rule.value().at("height"), 1, 8192) ||
                        !number(rule.value().at("width").get<double>() * data.value("scale", 1.0),
                                1, 8192) ||
                        !number(rule.value().at("height").get<double>() * data.value("scale", 1.0),
                                1, 8192))
                        reject(*definition, field);
                } else {
                    if (!allowed->second.count(rule.key()) || !rule.value().is_object())
                        reject(*definition, field);
                    for (const auto &coordinate : rule.value().items()) {
                        if ((coordinate.key() == "x" || coordinate.key() == "y") &&
                            number(coordinate.value(), -32768, 32768))
                            continue;
                        if (coordinate.key() == "anchor_x" && coordinate.value().is_string() &&
                            std::set<std::string>{"left", "center", "right"}.count(
                                coordinate.value().get<std::string>()))
                            continue;
                        if (coordinate.key() == "anchor_y" && coordinate.value().is_string() &&
                            std::set<std::string>{"top", "center", "bottom"}.count(
                                coordinate.value().get<std::string>()))
                            continue;
                        reject(*definition, field + "." + coordinate.key());
                    }
                    if (!rule.value().contains("x") || !rule.value().contains("y"))
                        reject(*definition, field);
                }
            }
        }
    }
    if (content.lock.contains("selections") && content.lock.at("selections").contains("hud")) {
        const auto &id = content.lock.at("selections").at("hud");
        if (!id.is_string() || !content.find("hud", id.get<std::string>()))
            cc::content::fail("hud-selection", "Selected HUD is unavailable", {}, {},
                              "selections.hud");
    }
}

bool content_hud_selected() { return selected().definition != nullptr; }

bool content_hud_position(const char *piece, const char *key, int32_t width, int32_t height,
                          int32_t out[2]) {
    const auto selection = selected();
    const auto *item = widget(selection, piece);
    if (!item || !item->contains(key))
        return false;
    const auto &rule = item->at(key);
    const auto &data = selection.definition->data;
    const double factor = scale(selection);
    const std::string x = rule.value("anchor_x", "left"), y = rule.value("anchor_y", "top");
    double px = rule.at("x").get<double>() * factor;
    double py = rule.at("y").get<double>() * factor;
    const double dx = width - data.value("reference_width", 640.0) * factor;
    const double dy = height - data.value("reference_height", 480.0) * factor;
    if (x == "right")
        px += dx;
    else if (x == "center")
        px += dx / 2;
    if (y == "bottom")
        py += dy;
    else if (y == "center")
        py += dy / 2;
    out[0] = static_cast<int32_t>(std::lround(px));
    out[1] = static_cast<int32_t>(std::lround(py));
    return true;
}

bool content_hud_size(const char *piece, int32_t out[2]) {
    const auto selection = selected();
    const auto *item = widget(selection, piece);
    if (!item || !item->contains("size"))
        return false;
    out[0] = static_cast<int32_t>(
        std::lround(item->at("size").at("width").get<double>() * scale(selection)));
    out[1] = static_cast<int32_t>(
        std::lround(item->at("size").at("height").get<double>() * scale(selection)));
    return true;
}
bool content_hud_width(const char *piece, int32_t *width) {
    const auto selection = selected();
    const auto *item = widget(selection, piece);
    if (!item || !item->contains("width"))
        return false;
    *width = static_cast<int32_t>(std::lround(item->at("width").get<double>() * scale(selection)));
    return true;
}
bool content_hud_visible(const char *piece) {
    const auto selection = selected();
    const auto *item = widget(selection, piece);
    return !item || item->value("visible", true);
}
bool content_hud_radius(bool zoomed, int32_t *radius) {
    const auto selection = selected();
    const auto *item = widget(selection, "RADAR");
    const char *key = zoomed ? "zoom_radius" : "radius";
    if (!item || !item->contains(key))
        return false;
    *radius = static_cast<int32_t>(std::lround(item->at(key).get<double>() * scale(selection)));
    return true;
}
platform::HudArtworkSources content_hud_artwork() {
    platform::HudArtworkSources sources;
    const auto selection = selected();
    if (!selection.definition || !selection.definition->data.contains("artwork"))
        return sources;
    for (const auto &sprite : selection.definition->data.at("artwork").at("sprites").items()) {
        const auto content = selection.content;
        const auto owner = selection.definition->owner;
        const auto reference = sprite.value().get<std::string>();
        sources.emplace(std::stoull(sprite.key(), nullptr, 16), [content, owner, reference] {
            return content->resource(owner, reference);
        });
    }
    return sources;
}
} // namespace mech3
