#include "content/world.h"

#include "miniz/miniz_tinfl.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <set>

namespace cc::content {
namespace {
constexpr size_t kMaxTriangles = 1000000;
constexpr size_t kChunkTriangles = 256;
constexpr size_t kMaxMarkers = 65536;
using Point = std::array<double, 3>;
using Matrix = std::array<double, 16>;

double number(const Json &value) {
    if (!value.is_number() || !std::isfinite(value.get<double>()))
        fail("world-number", "World coordinates must be finite numbers");
    return value.get<double>();
}
Point point(const Json &value) {
    if (!value.is_array() || value.size() != 3)
        fail("world-vector", "Expected three coordinates");
    return {number(value[0]), number(value[1]), number(value[2])};
}
Matrix identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
Matrix multiply(const Matrix &a, const Matrix &b) {
    Matrix out{};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k)
                out[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
    return out;
}
Point transform(const Matrix &m, const Point &p) {
    Point out{};
    for (int r = 0; r < 3; ++r)
        out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
    for (double n : out)
        if (!std::isfinite(n) || std::abs(n) > 10000000)
            fail("world-transform", "Transformed vertex exceeds the supported finite world range");
    return out;
}
Matrix matrix(const Json &node) {
    if (node.contains("matrix")) {
        if (!node["matrix"].is_array() || node["matrix"].size() != 16)
            fail("world-transform", "A matrix must have sixteen entries");
        Matrix m{};
        for (size_t i = 0; i < 16; ++i)
            m[i] = number(node["matrix"][i]);
        if (m[3] != 0 || m[7] != 0 || m[11] != 0 || m[15] != 1)
            fail("world-transform", "Only affine transforms are supported");
        return m;
    }
    const Point t = point(node.value("translation", Json{0, 0, 0}));
    const Point s = point(node.value("scale", Json{1, 1, 1}));
    for (double n : s)
        if (n == 0)
            fail("world-transform", "Zero instance scale is not supported");
    const Json q = node.value("rotation", Json{0, 0, 0, 1});
    if (!q.is_array() || q.size() != 4)
        fail("world-transform", "Rotation must be a quaternion");
    double x = number(q[0]), y = number(q[1]), z = number(q[2]), w = number(q[3]);
    const double length = std::sqrt(x * x + y * y + z * z + w * w);
    if (length < 0.000001)
        fail("world-transform", "Rotation quaternion cannot be zero");
    x /= length;
    y /= length;
    z /= length;
    w /= length;
    return {(1 - 2 * y * y - 2 * z * z) * s[0],
            (2 * x * y + 2 * w * z) * s[0],
            (2 * x * z - 2 * w * y) * s[0],
            0,
            (2 * x * y - 2 * w * z) * s[1],
            (1 - 2 * x * x - 2 * z * z) * s[1],
            (2 * y * z + 2 * w * x) * s[1],
            0,
            (2 * x * z + 2 * w * y) * s[2],
            (2 * y * z - 2 * w * x) * s[2],
            (1 - 2 * x * x - 2 * y * y) * s[2],
            0,
            t[0],
            t[1],
            t[2],
            1};
}
std::string bytes(const fs::path &path) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec || size > 256 * 1024 * 1024)
        fail("world-resource", "Missing or oversized geometry resource: " + path.string());
    std::ifstream in(path, std::ios::binary);
    std::string out(static_cast<size_t>(size), '\0');
    if (size && !in.read(out.data(), static_cast<std::streamsize>(size)))
        fail("world-resource", "Cannot read geometry resource: " + path.string());
    return out;
}
uint32_t u32(const std::string &data, size_t at) {
    if (at > data.size() || data.size() - at < 4)
        fail("world-buffer", "Truncated geometry buffer");
    uint32_t v = 0;
    for (size_t i = 0; i < 4; ++i)
        v |= static_cast<uint32_t>(static_cast<unsigned char>(data[at + i])) << (8 * i);
    return v;
}

uint32_t big32(const std::string &data, size_t at) {
    if (at > data.size() || data.size() - at < 4)
        fail("png-truncated", "Truncated PNG chunk");
    uint32_t value = 0;
    for (size_t k = 0; k < 4; ++k)
        value = (value << 8) | static_cast<unsigned char>(data[at + k]);
    return value;
}
uint32_t crc32(const std::string &data, size_t start, size_t length) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= static_cast<unsigned char>(data[start + i]);
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
    }
    return crc ^ 0xffffffffu;
}
struct Png {
    size_t width = 0, height = 0;
    int depth = 0, channels = 0;
    std::vector<uint8_t> pixels;
};
Png png(const std::string &data) {
    if (data.size() < 33 || data.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)) != 0)
        fail("png-signature", "Expected a PNG image");
    Png image;
    std::string compressed;
    bool header = false, end = false;
    for (size_t cursor = 8; cursor < data.size();) {
        const size_t length = big32(data, cursor);
        const auto type = data.substr(cursor + 4, 4);
        cursor += 8;
        if (cursor > data.size() || length > data.size() - cursor ||
            data.size() - cursor - length < 4)
            fail("png-truncated", "PNG chunk exceeds the image");
        if (crc32(data, cursor - 4, length + 4) != big32(data, cursor + length))
            fail("png-checksum", "PNG chunk checksum does not match");
        if (type == "IHDR") {
            if (header || length != 13)
                fail("png-header", "Invalid PNG image header");
            image.width = big32(data, cursor);
            image.height = big32(data, cursor + 4);
            image.depth = static_cast<unsigned char>(data[cursor + 8]);
            const int color = static_cast<unsigned char>(data[cursor + 9]);
            image.channels = color == 0 ? 1 : color == 2 ? 3 : color == 4 ? 2 : color == 6 ? 4 : 0;
            if (!image.width || !image.height || image.width > 4096 || image.height > 4096 ||
                !image.channels || (image.depth != 8 && !(image.depth == 16 && color == 0)) ||
                data[cursor + 10] != 0 || data[cursor + 11] != 0 || data[cursor + 12] != 0)
                fail("png-feature", "PNG source must be noninterlaced RGB/RGBA/gray "
                                    "8-bit, or grayscale "
                                    "16-bit, at most 4096 pixels per axis");
            header = true;
        } else if (type == "IDAT") {
            if (!header || end || compressed.size() + length > 128 * 1024 * 1024)
                fail("png-order", "Invalid or oversized PNG data");
            compressed.append(data, cursor, length);
        } else if (type == "IEND") {
            if (length != 0)
                fail("png-end", "Invalid PNG end chunk");
            end = true;
        } else if (!type.empty() && (static_cast<unsigned char>(type[0]) & 32) == 0)
            fail("png-feature", "Unsupported required PNG chunk: " + type);
        cursor += length + 4;
        if (end) {
            if (cursor != data.size())
                fail("png-trailing", "PNG has trailing bytes");
            break;
        }
    }
    if (!header || !end || compressed.empty())
        fail("png-truncated", "PNG image is incomplete");
    const size_t bpp = image.channels * (image.depth / 8), row = image.width * bpp;
    std::vector<uint8_t> filtered(image.height * (row + 1));
    const auto size =
        tinfl_decompress_mem_to_mem(filtered.data(), filtered.size(), compressed.data(),
                                    compressed.size(), TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (size != filtered.size())
        fail("png-deflate", "PNG pixel stream is truncated or exceeds its declared dimensions");
    image.pixels.resize(image.height * row);
    for (size_t y = 0; y < image.height; ++y) {
        const uint8_t filter = filtered[y * (row + 1)];
        if (filter > 4)
            fail("png-filter", "Unsupported PNG row filter");
        for (size_t x = 0; x < row; ++x) {
            const int left = x >= bpp ? image.pixels[y * row + x - bpp] : 0,
                      above = y ? image.pixels[(y - 1) * row + x] : 0,
                      corner = y && x >= bpp ? image.pixels[(y - 1) * row + x - bpp] : 0;
            int prediction = 0;
            if (filter == 1)
                prediction = left;
            else if (filter == 2)
                prediction = above;
            else if (filter == 3)
                prediction = (left + above) / 2;
            else if (filter == 4) {
                const int p = left + above - corner, pa = std::abs(p - left),
                          pb = std::abs(p - above), pc = std::abs(p - corner);
                prediction = pa <= pb && pa <= pc ? left : pb <= pc ? above : corner;
            }
            image.pixels[y * row + x] =
                static_cast<uint8_t>(filtered[y * (row + 1) + 1 + x] + prediction);
        }
    }
    return image;
}
Json texture_image(const Png &image) {
    if ((image.width & (image.width - 1)) || (image.height & (image.height - 1)))
        fail("world-image-size", "Native material textures require power-of-two dimensions");
    Json pixels = Json::array(), alpha = Json::array();
    bool transparent = false;
    for (size_t i = 0; i < image.width * image.height; ++i) {
        const auto *p = image.pixels.data() + i * image.channels * (image.depth / 8);
        const int r = p[0], g = image.channels >= 3 ? p[1] : r, b = image.channels >= 3 ? p[2] : r;
        const int a = image.channels == 4 ? p[3] : image.channels == 2 ? p[1] : 255;
        pixels.push_back(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        alpha.push_back(a);
        transparent |= a != 255;
    }
    return {{"width", image.width},
            {"height", image.height},
            {"pixels", pixels},
            {"alpha", transparent ? alpha : Json::array()}};
}

struct Compiler {
    const ResolvedContentSet &content;
    const Definition &definition;
    Json out;
    size_t triangles = 0;

    void mesh(const Json &mesh, const Matrix &placement, const std::string &instance) {
        if (!mesh.contains("vertices") || !mesh["vertices"].is_array() ||
            !mesh.contains("triangles") || !mesh["triangles"].is_array())
            fail("world-mesh", "Mesh requires vertices and triangle indices", definition.owner,
                 definition.id, instance);
        const Json &vertices = mesh["vertices"];
        Json chunk;
        auto fresh = [&] {
            chunk = {{"name", instance + ":" + std::to_string(out["chunks"].size())},
                     {"instance", instance},
                     {"vertices", Json::array()},
                     {"triangles", Json::array()},
                     {"uvs", Json::array()},
                     {"color", mesh.value("color", Json{0.65, 0.65, 0.65})},
                     {"alpha", mesh.value("alpha", 255)},
                     {"soil", mesh.value("soil", 0)},
                     {"texture", mesh.value("texture", std::string{})},
                     {"image", mesh.value("image", std::string{})},
                     {"collision", mesh.value("collision", true)},
                     {"ground", mesh.value("ground", true)},
                     {"visible", mesh.value("visible", true)}};
        };
        fresh();
        const double determinant =
            placement[0] * (placement[5] * placement[10] - placement[9] * placement[6]) -
            placement[4] * (placement[1] * placement[10] - placement[9] * placement[2]) +
            placement[8] * (placement[1] * placement[6] - placement[5] * placement[2]);
        for (Json triangle : mesh["triangles"]) {
            if (!triangle.is_array() || triangle.size() != 3)
                fail("world-triangle", "Only triangle primitives are supported");
            if (determinant < 0)
                std::swap(triangle[1], triangle[2]);
            if (++triangles > kMaxTriangles)
                fail("world-limit", "World exceeds one million triangles");
            std::array<Point, 3> p{};
            for (size_t k = 0; k < 3; ++k) {
                if (!triangle[k].is_number_unsigned() && !triangle[k].is_number_integer())
                    fail("world-index", "Triangle index must be an integer");
                const auto index = triangle[k].get<int64_t>();
                if (index < 0 || static_cast<uint64_t>(index) >= vertices.size())
                    fail("world-index", "Triangle index is outside its vertex array");
                p[k] = transform(placement, point(vertices[static_cast<size_t>(index)]));
            }
            const Point a{p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
            const Point b{p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
            const Point n{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                          a[0] * b[1] - a[1] * b[0]};
            if (n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < 1e-16)
                fail("world-degenerate", "Mesh contains a degenerate triangle", definition.owner,
                     definition.id, instance);
            const size_t base = chunk["vertices"].size();
            for (size_t k = 0; k < 3; ++k) {
                chunk["vertices"].push_back(p[k]);
                const auto vi = triangle[k].get<size_t>();
                if (mesh.contains("uvs")) {
                    if (!mesh["uvs"].is_array() || mesh["uvs"].size() != vertices.size() ||
                        mesh["uvs"][vi].size() != 2)
                        fail("world-uv", "Texture coordinates must match the vertex array");
                    chunk["uvs"].push_back(
                        Json{number(mesh["uvs"][vi][0]), number(mesh["uvs"][vi][1])});
                } else
                    chunk["uvs"].push_back(Json{0, 0});
            }
            chunk["triangles"].push_back(Json{base, base + 1, base + 2});
            if (chunk["triangles"].size() == kChunkTriangles) {
                out["chunks"].push_back(chunk);
                fresh();
            }
        }
        if (!chunk["triangles"].empty())
            out["chunks"].push_back(chunk);
    }

    void gltf(std::string owner, std::string path, const Matrix &placement,
              const std::string &instance, const Json &options) {
        const auto reference = resource_reference(owner, path);
        owner = reference.owner;
        path = reference.path;
        std::string extension = fs::path(path).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".gltf" && extension != ".glb")
            fail("gltf-extension", "Static geometry sources require a .gltf or .glb extension");
        std::string raw = bytes(content.resource(owner, path)), bin;
        Json doc;
        if (extension == ".glb") {
            if (raw.size() < 20 || u32(raw, 0) != 0x46546c67 || u32(raw, 4) != 2 ||
                u32(raw, 8) != raw.size())
                fail("gltf-header", "Invalid GLB 2 header");
            size_t cursor = 12;
            while (cursor < raw.size()) {
                const uint32_t length = u32(raw, cursor), type = u32(raw, cursor + 4);
                cursor += 8;
                if (cursor > raw.size() || length > raw.size() - cursor)
                    fail("gltf-buffer", "Truncated GLB chunk");
                if (type == 0x4e4f534a) {
                    if (!doc.is_null())
                        fail("gltf-header", "Duplicate JSON chunk");
                    doc = parse_json(raw.substr(cursor, length), path);
                } else if (type == 0x004e4942)
                    bin = raw.substr(cursor, length);
                else
                    fail("gltf-chunk", "Unsupported GLB chunk");
                cursor += length;
            }
        } else
            doc = parse_json(raw, path);
        if (!doc.contains("asset") || doc["asset"].value("version", "") != "2.0")
            fail("gltf-version", "Expected glTF 2.0");
        if (!doc.value("extensionsRequired", Json::array()).empty() || doc.contains("skins") ||
            doc.contains("animations"))
            fail("gltf-feature", "Authored static worlds do not support required "
                                 "extensions, skinning or animation");
        std::vector<std::string> buffers;
        for (const auto &buffer : doc.value("buffers", Json::array())) {
            if (buffer.contains("uri"))
                buffers.push_back(bytes(content.resource(
                    owner, (fs::path(path).parent_path() / buffer["uri"].get<std::string>())
                               .generic_string())));
            else
                buffers.push_back(bin);
            if (buffer.value("byteLength", size_t{}) > buffers.back().size())
                fail("gltf-buffer", "Buffer is shorter than its declared size");
        }
        auto accessor = [&](int index, int components) -> Json {
            if (index < 0 ||
                static_cast<size_t>(index) >= doc.value("accessors", Json::array()).size())
                fail("gltf-accessor", "Missing accessor");
            const auto &a = doc["accessors"][index];
            if (a.contains("sparse") || a.value("normalized", false))
                fail("gltf-accessor", "Sparse and normalized accessors are unsupported");
            const int ct = a.value("componentType", 0), vi = a.value("bufferView", -1);
            const std::string expected = components == 3   ? "VEC3"
                                         : components == 2 ? "VEC2"
                                                           : "SCALAR";
            if (a.value("type", "") != expected || (components > 1 && ct != 5126) ||
                (components == 1 && ct != 5121 && ct != 5123 && ct != 5125))
                fail("gltf-accessor", "Unsupported accessor shape or component type");
            if (vi < 0 || static_cast<size_t>(vi) >= doc.value("bufferViews", Json::array()).size())
                fail("gltf-view", "Missing buffer view");
            const auto &view = doc["bufferViews"][vi];
            const size_t bi = view.value("buffer", size_t(-1));
            if (bi >= buffers.size())
                fail("gltf-buffer", "Invalid buffer index");
            const size_t width = ct == 5121 ? 1 : ct == 5123 ? 2 : 4;
            const size_t count = a.value("count", size_t{}),
                         stride = view.value("byteStride", width * components);
            const size_t offset = a.value("byteOffset", size_t{}),
                         view_offset = view.value("byteOffset", size_t{}),
                         length = view.value("byteLength", size_t{});
            if (count > kMaxTriangles * 3 || stride < width * components ||
                view_offset > buffers[bi].size() || length > buffers[bi].size() - view_offset ||
                offset > length || (count && (count - 1) > (length - offset) / stride) ||
                (count && width * components > length - offset - (count - 1) * stride))
                fail("gltf-bounds", "Accessor exceeds its buffer view");
            Json result = Json::array();
            for (size_t k = 0; k < count; ++k) {
                Json v = Json::array();
                for (int c = 0; c < components; ++c) {
                    const size_t at = view_offset + offset + k * stride + c * width;
                    if (ct == 5126) {
                        const uint32_t word = u32(buffers[bi], at);
                        float f;
                        std::memcpy(&f, &word, 4);
                        v.push_back(number(f));
                    } else {
                        uint32_t n = 0;
                        for (size_t b = 0; b < width; ++b)
                            n |= static_cast<uint32_t>(
                                     static_cast<unsigned char>(buffers[bi][at + b]))
                                 << (8 * b);
                        v.push_back(n);
                    }
                }
                result.push_back(components == 1 ? v[0] : v);
            }
            return result;
        };
        const auto nodes = doc.value("nodes", Json::array());
        std::set<int> stack;
        std::function<void(int, const Matrix &, int)> walk = [&](int ni, const Matrix &parent,
                                                                 int depth) {
            if (depth > 64 || ni < 0 || static_cast<size_t>(ni) >= nodes.size() ||
                !stack.insert(ni).second)
                fail("gltf-nodes", "Invalid or cyclic glTF node graph");
            const auto &node = nodes[ni];
            const Matrix placed = multiply(parent, matrix(node));
            if (node.contains("skin") || node.contains("weights"))
                fail("gltf-feature", "Only static mesh nodes are supported");
            if (node.contains("mesh")) {
                const size_t mi = node["mesh"].get<size_t>();
                if (mi >= doc.value("meshes", Json::array()).size())
                    fail("gltf-mesh", "Invalid mesh reference");
                for (const auto &primitive : doc["meshes"][mi]["primitives"]) {
                    if (primitive.value("mode", 4) != 4 || primitive.contains("targets"))
                        fail("gltf-primitive", "Only static triangle primitives are supported");
                    Json m;
                    m["vertices"] = accessor(primitive["attributes"].value("POSITION", -1), 3);
                    m["triangles"] = Json::array();
                    Json indices = Json::array();
                    if (primitive.contains("indices"))
                        indices = accessor(primitive["indices"].get<int>(), 1);
                    else
                        for (size_t k = 0; k < m["vertices"].size(); ++k)
                            indices.push_back(k);
                    if (indices.size() % 3)
                        fail("gltf-primitive", "Triangle index count is not divisible by three");
                    for (size_t k = 0; k < indices.size(); k += 3)
                        m["triangles"].push_back(Json{indices[k], indices[k + 1], indices[k + 2]});
                    if (primitive["attributes"].contains("TEXCOORD_0"))
                        m["uvs"] = accessor(primitive["attributes"]["TEXCOORD_0"].get<int>(), 2);
                    if (primitive.contains("material")) {
                        const size_t material = primitive["material"].get<size_t>();
                        if (material >= doc.value("materials", Json::array()).size())
                            fail("gltf-material", "Invalid material reference");
                        const auto mat = doc["materials"][material];
                        const auto pbr = mat.value("pbrMetallicRoughness", Json::object());
                        const std::string alpha_mode =
                            mat.value("alphaMode", std::string("OPAQUE"));
                        if (alpha_mode != "OPAQUE" && alpha_mode != "MASK" && alpha_mode != "BLEND")
                            fail("gltf-material", "Unsupported material alpha mode");
                        if (pbr.contains("baseColorTexture")) {
                            const auto texture_index =
                                pbr["baseColorTexture"].at("index").get<size_t>();
                            if (texture_index >= doc.value("textures", Json::array()).size())
                                fail("gltf-texture", "Invalid texture index");
                            const auto image_index =
                                doc["textures"][texture_index].at("source").get<size_t>();
                            if (image_index >= doc.value("images", Json::array()).size())
                                fail("gltf-image", "Invalid image index");
                            const auto &image = doc["images"][image_index];
                            std::string image_bytes;
                            if (image.contains("uri"))
                                image_bytes =
                                    bytes(content.resource(owner, (fs::path(path).parent_path() /
                                                                   image["uri"].get<std::string>())
                                                                      .generic_string()));
                            else {
                                const auto vi = image.at("bufferView").get<size_t>();
                                if (vi >= doc.value("bufferViews", Json::array()).size())
                                    fail("gltf-image", "Invalid image buffer view");
                                const auto &view = doc["bufferViews"][vi];
                                const auto bi = view.at("buffer").get<size_t>(),
                                           offset = view.value("byteOffset", size_t{}),
                                           length = view.at("byteLength").get<size_t>();
                                if (bi >= buffers.size() || offset > buffers[bi].size() ||
                                    length > buffers[bi].size() - offset)
                                    fail("gltf-image", "Image buffer exceeds its source");
                                image_bytes = buffers[bi].substr(offset, length);
                            }
                            const double cutoff = number(mat.value("alphaCutoff", Json(0.5)));
                            if (cutoff < 0 || cutoff > 1)
                                fail("gltf-material", "Alpha cutoff must be from zero to one");
                            const auto key =
                                sha256(image_bytes + alpha_mode + std::to_string(cutoff));
                            if (!out["textures"].contains(key)) {
                                auto image_data = texture_image(png(image_bytes));
                                if (alpha_mode == "OPAQUE")
                                    image_data["alpha"] = Json::array();
                                else if (alpha_mode == "MASK")
                                    for (auto &alpha : image_data["alpha"])
                                        alpha = alpha.get<int>() >= cutoff * 255 ? 255 : 0;
                                out["textures"][key] = image_data;
                            }
                            m["image"] = key;
                        }
                        const Json color = pbr.value("baseColorFactor", Json{1, 1, 1, 1});
                        if (color.size() != 4 || number(color[3]) < 0 || number(color[3]) > 1)
                            fail("gltf-material", "Invalid base color factor");
                        m["alpha"] =
                            alpha_mode == "OPAQUE" ? 255 : static_cast<int>(number(color[3]) * 255);
                        m["color"] = Json{color[0], color[1], color[2]};
                    }
                    for (const char *key :
                         {"collision", "ground", "visible", "soil", "texture", "color", "alpha"})
                        if (options.contains(key))
                            m[key] = options[key];
                    mesh(m, placed, instance);
                }
            }
            for (const auto &child : node.value("children", Json::array()))
                walk(child.get<int>(), placed, depth + 1);
            stack.erase(ni);
        };
        const int scene = doc.value("scene", 0);
        if (scene < 0 || static_cast<size_t>(scene) >= doc.value("scenes", Json::array()).size())
            fail("gltf-scene", "A static world source must select a scene");
        for (const auto &node : doc["scenes"][scene].value("nodes", Json::array()))
            walk(node.get<int>(), placement, 0);
    }

    void asset(const Definition &d, const Json &placement, const std::string &id) {
        validate_world_fields(d);
        if (d.kind == "world_asset" && d.behavior != "static_mesh")
            fail("world-asset-behavior", "Unknown required reusable world asset behavior", d.owner,
                 d.id);
        if (d.kind == "world_asset" && !d.data.contains("source") &&
            (!d.data.contains("meshes") || !d.data["meshes"].is_array() ||
             d.data["meshes"].empty()))
            fail("world-asset", "Reusable world asset requires a source scene or inline meshes",
                 d.owner, d.id);
        const Matrix placed = matrix(placement);
        const auto markers = d.data.value("markers", Json::array());
        if (!markers.is_array() || markers.size() > 256 ||
            out["markers"].size() > kMaxMarkers - markers.size())
            fail("world-markers", "An asset accepts at most 256 markers and a world at most 65536");
        std::set<std::string> names;
        for (const auto &marker : markers) {
            if (!marker.is_object() || marker.size() != 2 || !marker.contains("name") ||
                !marker["name"].is_string() || !marker.contains("position"))
                fail("world-marker", "Asset markers require exactly name and position");
            const auto name = marker["name"].get<std::string>();
            if (name.empty() || name.size() > 35 ||
                name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRS"
                                       "TUVWXYZ0123456789_") != std::string::npos ||
                !names.insert(name).second)
                fail("world-marker",
                     "Asset markers require unique native names of at most 35 bytes");
            out["markers"].push_back({{"instance", id},
                                      {"name", name},
                                      {"position", transform(placed, point(marker["position"]))}});
        }
        if (d.data.contains("source"))
            gltf(d.owner, d.data["source"].get<std::string>(), placed, id, placement);
        for (auto mesh_data : d.data.value("meshes", Json::array())) {
            for (const char *key :
                 {"collision", "ground", "visible", "soil", "texture", "color", "alpha"})
                if (placement.contains(key))
                    mesh_data[key] = placement[key];
            if (mesh_data.contains("albedo")) {
                const auto reference =
                    resource_reference(d.owner, mesh_data["albedo"].get<std::string>());
                const auto source = bytes(content.resource(reference.owner, reference.path));
                const auto key = sha256(source);
                if (!out["textures"].contains(key))
                    out["textures"][key] = texture_image(png(source));
                mesh_data["image"] = key;
            }
            mesh(mesh_data, placed, id);
        }
    }
};
} // namespace

void validate_compiled_world(const Json &world) {
    validate_compiled_world_fields(world);
    if (world.value("format", "") != "CoreCriticalWorld" || !world.contains("schema") ||
        !world["schema"].is_number_integer() || world["schema"] != 1)
        fail("world-schema", "Unsupported compiled world schema");
    const auto b = world.value("bounds", Json::array());
    if (b.size() != 4 || number(b[2]) <= 0 || number(b[3]) <= 0 || number(b[2]) > 100000 ||
        number(b[3]) > 100000)
        fail("world-bounds", "World needs positive bounded width and depth");
    if (std::abs(number(b[0])) > 10000000 || std::abs(number(b[1])) > 10000000)
        fail("world-bounds", "World origin exceeds native coordinates");
    transform(identity(), point(world.value("spawn", Json{0, 0, 0})));
    if (!world.contains("chunks") || !world["chunks"].is_array() || world["chunks"].empty())
        fail("world-empty", "An authored world must contain geometry");
    const auto textures = world.value("textures", Json::object());
    if (!textures.is_object() || textures.size() > 4096)
        fail("world-images", "Too many material images");
    size_t image_bytes = 0;
    std::set<std::string> native_image_names;
    for (auto it = textures.begin(); it != textures.end(); ++it) {
        if (it.key().size() != 64 ||
            it.key().find_first_not_of("0123456789abcdef") != std::string::npos ||
            !native_image_names.insert(it.key().substr(0, 16)).second)
            fail("world-image-identity",
                 "Compiled material images require unique hashed native names");
        const auto &image = it.value();
        const int width = image.at("width").get<int>(), height = image.at("height").get<int>();
        if (width < 1 || height < 1 || width > 4096 || height > 4096 || (width & (width - 1)) ||
            (height & (height - 1)))
            fail("world-image", "Invalid compiled image dimensions");
        const size_t count = static_cast<size_t>(width) * height;
        if (image.at("pixels").size() != count ||
            (!image.at("alpha").empty() && image.at("alpha").size() != count) ||
            (image_bytes += count * 3) > 128 * 1024 * 1024)
            fail("world-image",
                 "Compiled image payload exceeds its supported dimensions or budget");
        for (const auto &p : image.at("pixels"))
            if (!p.is_number_integer() || p.get<int64_t>() < 0 || p.get<int64_t>() > 65535)
                fail("world-pixel", "Invalid RGB565 texel");
        for (const auto &p : image.at("alpha"))
            if (!p.is_number_integer() || p.get<int64_t>() < 0 || p.get<int64_t>() > 255)
                fail("world-pixel", "Invalid alpha texel");
    }
    size_t total = 0;
    std::set<std::string> instances;
    for (const auto &c : world["chunks"]) {
        instances.insert(c.value("instance", std::string("terrain")));
        if (c["vertices"].size() > kChunkTriangles * 3 || c["triangles"].size() > kChunkTriangles)
            fail("world-chunk", "World mesh chunk exceeds native limits");
        if ((total += c["triangles"].size()) > kMaxTriangles)
            fail("world-limit", "World geometry exceeds its budget");
        for (const auto &p : c["vertices"])
            transform(identity(), point(p));
        const auto color = point(c.value("color", Json{1, 1, 1}));
        for (double v : color)
            if (v < 0 || v > 1)
                fail("world-color", "Material color must be within zero and one");
        if (c.value("alpha", 255) < 0 || c.value("alpha", 255) > 255)
            fail("world-alpha", "Material alpha must be from zero to 255");
        if (c.value("soil", 0) < 0 || c.value("soil", 0) >= 100)
            fail("world-soil", "Invalid soil identifier");
        if (c["uvs"].size() != c["vertices"].size())
            fail("world-uv", "Compiled UV count does not match vertices");
        const auto image = c.value("image", std::string{});
        if (!image.empty() && !textures.contains(image))
            fail("world-image", "Compiled material refers to a missing image");
        for (const auto &uv : c["uvs"]) {
            if (uv.size() != 2)
                fail("world-uv", "Expected two texture coordinates");
            if (std::abs(number(uv[0])) > 1000000 || std::abs(number(uv[1])) > 1000000)
                fail("world-uv", "Texture coordinates exceed native precision limits");
        }
        for (const auto &t : c["triangles"]) {
            if (!t.is_array() || t.size() != 3)
                fail("world-triangle", "Invalid compiled triangle");
            for (const auto &i : t)
                if (!i.is_number_unsigned() || i.get<size_t>() >= c["vertices"].size())
                    fail("world-index", "Invalid compiled vertex index");
            const Point a = point(c["vertices"][t[0].get<size_t>()]),
                        b = point(c["vertices"][t[1].get<size_t>()]),
                        d = point(c["vertices"][t[2].get<size_t>()]);
            const Point u{b[0] - a[0], b[1] - a[1], b[2] - a[2]},
                v{d[0] - a[0], d[1] - a[1], d[2] - a[2]};
            const Point n{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                          u[0] * v[1] - u[1] * v[0]};
            if (n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < 1e-16)
                fail("world-degenerate", "Compiled world contains a degenerate triangle");
        }
    }
    const auto markers = world.value("markers", Json::array());
    if (!markers.is_array() || markers.size() > kMaxMarkers)
        fail("world-markers", "Compiled marker count exceeds its native budget");
    std::set<std::pair<std::string, std::string>> marker_names;
    for (const auto &marker : markers) {
        if (!marker.is_object() || marker.size() != 3 || !marker.contains("instance") ||
            !marker["instance"].is_string() || !marker.contains("name") ||
            !marker["name"].is_string() || !marker.contains("position"))
            fail("world-marker", "Compiled markers require exactly instance, name and position");
        const auto instance = marker["instance"].get<std::string>();
        const auto name = marker["name"].get<std::string>();
        if (!instances.count(instance) || name.empty() || name.size() > 35 ||
            name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTU"
                                   "VWXYZ0123456789_") != std::string::npos ||
            !marker_names.insert({instance, name}).second)
            fail("world-marker", "Compiled markers need a geometry instance and "
                                 "unique bounded native names");
        transform(identity(), point(marker["position"]));
    }
}

Json compile_world(const Definition &definition, const ResolvedContentSet &content) {
    validate_world_fields(definition);
    if (definition.kind == "world" && definition.behavior == "compiled") {
        auto world = read_json(
            content.resource(definition.owner, definition.data.at("resource").get<std::string>()));
        validate_compiled_world(world);
        if (world.value("id", std::string{}) != definition.id)
            fail("world-identity", "Compiled world identity differs from its definition");
        const auto digest = world.at("digest").get<std::string>();
        auto checked = world;
        checked.erase("digest");
        if (sha256(checked.dump()) != digest)
            fail("world-digest", "Compiled world content does not match its digest");
        return world;
    }
    if (definition.kind != "world" || definition.behavior != "authored")
        fail("world-behavior", "World compiler requires an authored or compiled world",
             definition.owner, definition.id);
    Json data = definition.data;
    if (data.contains("base")) {
        const auto *base = content.find("world", data["base"].get<std::string>());
        if (!base || base->behavior != "authored" || base->id == definition.id ||
            base->data.contains("base"))
            fail("world-base", "World variant requires an existing non-variant authored base");
        validate_world_fields(*base);
        Json combined = base->data;
        auto qualify = [&](Json &value) {
            if (value.is_string() && value.get<std::string>().find("::") == std::string::npos)
                value = base->owner + "::" + value.get<std::string>();
        };
        if (combined.contains("source"))
            qualify(combined["source"]);
        if (combined.contains("heightfield") && combined["heightfield"].contains("source"))
            qualify(combined["heightfield"]["source"]);
        if (combined.contains("meshes"))
            for (auto &mesh : combined["meshes"])
                if (mesh.contains("albedo"))
                    qualify(mesh["albedo"]);
        for (auto it = data.begin(); it != data.end(); ++it)
            if (it.key() != "base" && it.key() != "instances")
                combined[it.key()] = it.value();
        Json instances = combined.value("instances", Json::array());
        for (auto edit : data.value("instances", Json::array())) {
            const std::string id = edit.at("id").get<std::string>();
            auto found = std::find_if(instances.begin(), instances.end(),
                                      [&](const Json &v) { return v.value("id", "") == id; });
            if (edit.value("remove", false)) {
                if (found == instances.end())
                    fail("world-variant", "Cannot remove an unknown instance");
                instances.erase(found);
            } else {
                edit.erase("remove");
                if (found == instances.end())
                    instances.push_back(edit);
                else {
                    for (auto key = edit.begin(); key != edit.end(); ++key)
                        (*found)[key.key()] = key.value();
                }
            }
        }
        combined["instances"] = instances;
        data = combined;
    }
    Compiler compiler{content,
                      definition,
                      {{"format", "CoreCriticalWorld"},
                       {"schema", 1},
                       {"id", definition.id},
                       {"bounds", data.value("bounds", Json{-512, 512, 1024, 1024})},
                       {"chunks", Json::array()},
                       {"markers", Json::array()},
                       {"textures", Json::object()},
                       {"readers", data.value("readers", Json::object())},
                       {"spawn", data.value("spawn", Json{0, 0, 0})}}};
    Definition source = definition;
    source.data = data;
    compiler.asset(source, Json::object(), "terrain");
    if (data.contains("heightfield")) {
        const auto &hf = data["heightfield"];
        Json heights = hf.value("heights", Json::array());
        if (hf.contains("source")) {
            const auto reference =
                resource_reference(definition.owner, hf["source"].get<std::string>());
            const auto image = png(bytes(content.resource(reference.owner, reference.path)));
            if (image.channels != 1 || image.depth != 16)
                fail("heightfield-image", "Heightfield PNG must contain 16-bit grayscale samples");
            heights = Json::array();
            for (size_t y = 0; y < image.height; ++y) {
                Json row = Json::array();
                for (size_t x = 0; x < image.width; ++x) {
                    const auto at = (y * image.width + x) * 2;
                    row.push_back((static_cast<unsigned>(image.pixels[at]) << 8) |
                                  image.pixels[at + 1]);
                }
                heights.push_back(row);
            }
        }
        if (!heights.is_array() || heights.size() < 2 || heights.size() > 1024 ||
            !heights[0].is_array() || heights[0].size() < 2 || heights[0].size() > 1024)
            fail("heightfield-size", "Heightfield requires a two-dimensional grid "
                                     "from 2 to 1024 samples per axis");
        const size_t rows = heights.size(), cols = heights[0].size();
        const double spacing = number(hf.value("spacing", Json(4))),
                     scale = number(hf.value("height_scale", Json(1)));
        if (spacing <= 0)
            fail("heightfield-spacing", "Heightfield spacing must be positive");
        const Point origin = point(hf.value("origin", Json{0, 0, 0}));
        Json mesh = {{"vertices", Json::array()},
                     {"triangles", Json::array()},
                     {"color", hf.value("color", Json{0.3, 0.5, 0.2})},
                     {"soil", hf.value("soil", 0)}};
        for (size_t z = 0; z < rows; ++z) {
            if (heights[z].size() != cols)
                fail("heightfield-shape", "Heightfield rows must have equal lengths");
            for (size_t x = 0; x < cols; ++x)
                mesh["vertices"].push_back(Json{origin[0] + x * spacing,
                                                origin[1] + number(heights[z][x]) * scale,
                                                origin[2] - z * spacing});
        }
        for (size_t z = 0; z + 1 < rows; ++z)
            for (size_t x = 0; x + 1 < cols; ++x) {
                const size_t a = z * cols + x;
                mesh["triangles"].push_back(Json{a, a + 1, a + cols});
                mesh["triangles"].push_back(Json{a + 1, a + cols + 1, a + cols});
            }
        compiler.mesh(mesh, identity(), "heightfield");
    }
    std::set<std::string> instance_ids;
    for (const auto &instance : data.value("instances", Json::array())) {
        const auto id = instance.at("id").get<std::string>();
        if (id.empty() || id.size() > 35 || !instance_ids.insert(id).second)
            fail("world-instance", "Duplicate, empty or overlong native instance identity");
        const auto *asset = content.find("world_asset", instance.at("asset").get<std::string>());
        if (!asset)
            fail("world-asset", "Missing reusable world asset", definition.owner, definition.id,
                 id);
        compiler.asset(*asset, instance, id);
    }
    validate_compiled_world(compiler.out);
    compiler.out["digest"] = sha256(compiler.out.dump());
    return compiler.out;
}
} // namespace cc::content
