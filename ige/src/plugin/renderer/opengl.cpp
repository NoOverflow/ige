#include "backend.hpp"
#include "blobs/shaders/gl3/main-fs.glsl.h"
#include "blobs/shaders/gl3/main-vs.glsl.h"
#include "glad/gl.h"
#include "ige/asset/Material.hpp"
#include "ige/asset/Mesh.hpp"
#include "ige/asset/Texture.hpp"
#include "ige/ecs/World.hpp"
#include "ige/plugin/RenderPlugin.hpp"
#include "ige/plugin/TransformPlugin.hpp"
#include "ige/plugin/WindowPlugin.hpp"
#include "opengl/Buffer.hpp"
#include "opengl/Error.hpp"
#include "opengl/Program.hpp"
#include "opengl/Shader.hpp"
#include "opengl/VertexArray.hpp"
#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
; // TODO: https://bit.ly/3hhMJ58

namespace gl = ige::gl;

using glm::mat4;
using glm::vec4;
using ige::asset::Material;
using ige::asset::Mesh;
using ige::asset::Texture;
using ige::ecs::World;
using ige::plugin::render::MeshRenderer;
using ige::plugin::render::PerspectiveCamera;
using ige::plugin::transform::Transform;
using ige::plugin::window::WindowInfo;

template <typename T>
struct WeakRefHash {
    std::size_t operator()(const std::weak_ptr<T>& ptr) const
    {
        std::size_t res = 17;
        res = res * 31 + std::hash<bool>()(ptr.expired());
        res = res * 31 + std::hash<T*>()(ptr.lock().get());
        return res;
    }
};

template <typename T>
struct WeakRefEq {
    bool
    operator()(const std::weak_ptr<T>& lhs, const std::weak_ptr<T>& rhs) const
    {
        if (lhs.expired() || rhs.expired()) {
            return lhs.expired() && rhs.expired();
        }

        return lhs.lock().get() == rhs.lock().get();
    }
};

template <typename K, typename V>
using WeakRefCache
    = std::unordered_map<std::weak_ptr<K>, V, WeakRefHash<K>, WeakRefEq<K>>;

class MeshCache {
private:
    static std::size_t GID;

    std::size_t m_id;

    static gl::VertexArray::Type convert(Mesh::DataType type)
    {
        switch (type) {
        case Mesh::DataType::FLOAT:
            return gl::VertexArray::Type::FLOAT;
        default:
            throw std::runtime_error("Unsupported data type");
        }
    }

public:
    gl::VertexArray vertex_array;
    std::optional<gl::Buffer> index_buffer;
    std::vector<gl::Buffer> vertex_buffers;

    MeshCache(const Mesh& mesh)
        : m_id(++GID)
    {
        auto mesh_data = mesh.buffers();
        vertex_buffers.resize(mesh_data.size());
        for (std::size_t i = 0; i < vertex_buffers.size(); i++) {
            vertex_buffers[i].load<std::byte>(mesh_data[i]);
        }

        gl::Error::audit("mesh cache - buffers");

        const auto pos = mesh.attr_position();
        const auto norm = mesh.attr_normal();
        const auto uv = mesh.attr_tex_coords();

        vertex_array.attrib(
            0, 3, convert(pos.type), vertex_buffers[pos.buffer],
            static_cast<GLsizei>(pos.stride), static_cast<GLsizei>(pos.offset));
        vertex_array.attrib(
            1, 3, convert(norm.type), vertex_buffers[norm.buffer],
            static_cast<GLsizei>(norm.stride),
            static_cast<GLsizei>(norm.offset));

        if (!uv.empty()) {
            const auto& uvs = uv[0];
            vertex_array.attrib(
                2, 2, convert(uvs.type), vertex_buffers[uvs.buffer],
                static_cast<GLsizei>(uvs.stride),
                static_cast<GLsizei>(uvs.offset));
        }

        index_buffer.emplace(GL_ELEMENT_ARRAY_BUFFER);
        index_buffer->load(mesh.index_buffer());
        gl::Error::audit("mesh cache - index buffer");

        gl::VertexArray::unbind();

        gl::Error::audit("mesh cache - vertex array");

        std::cerr << "Made mesh cache " << m_id << " - "
                  << mesh.index_buffer().size() << " vertices" << std::endl;
    }

    ~MeshCache()
    {
        std::cerr << "Releasing mesh cache " << m_id << std::endl;
    }
};

std::size_t MeshCache::GID = 0;

class TextureCache {
private:
    static std::size_t GID;

    std::size_t m_id;

    gl::Texture::Format convert(Texture::Format fmt)
    {
        switch (fmt) {
        case Texture::Format::R:
            return gl::Texture::Format::RED;
        case Texture::Format::RG:
            return gl::Texture::Format::RG;
        case Texture::Format::RGB:
            return gl::Texture::Format::RGB;
        case Texture::Format::RGBA:
            return gl::Texture::Format::RGBA;
        default:
            throw std::runtime_error("Unsupported texture format");
        }
    }

    gl::Texture::MagFilter convert(Texture::MagFilter filter)
    {
        switch (filter) {
        case Texture::MagFilter::LINEAR:
            return gl::Texture::MagFilter::LINEAR;
        case Texture::MagFilter::NEAREST:
            return gl::Texture::MagFilter::NEAREST;
        default:
            throw std::runtime_error("Unsupported texture mag filter");
        }
    }

    gl::Texture::MinFilter convert(Texture::MinFilter filter)
    {
        switch (filter) {
        case Texture::MinFilter::LINEAR:
            return gl::Texture::MinFilter::LINEAR;
        case Texture::MinFilter::LINEAR_MIPMAP_LINEAR:
            return gl::Texture::MinFilter::LINEAR_MIPMAP_LINEAR;
        case Texture::MinFilter::LINEAR_MIPMAP_NEAREST:
            return gl::Texture::MinFilter::LINEAR_MIPMAP_NEAREST;
        case Texture::MinFilter::NEAREST:
            return gl::Texture::MinFilter::NEAREST;
        case Texture::MinFilter::NEAREST_MIPMAP_LINEAR:
            return gl::Texture::MinFilter::NEAREST_MIPMAP_LINEAR;
        case Texture::MinFilter::NEAREST_MIPMAP_NEAREST:
            return gl::Texture::MinFilter::NEAREST_MIPMAP_NEAREST;
        default:
            throw std::runtime_error("Unsupported texture min filter");
        }
    }

    gl::Texture::Wrap convert(Texture::WrappingMode mode)
    {
        switch (mode) {
        case Texture::WrappingMode::CLAMP_TO_EDGE:
            return gl::Texture::Wrap::CLAMP_TO_EDGE;
        case Texture::WrappingMode::MIRRORED_REPEAT:
            return gl::Texture::Wrap::MIRRORED_REPEAT;
        case Texture::WrappingMode::REPEAT:
            return gl::Texture::Wrap::REPEAT;
        default:
            throw std::runtime_error("Unsupported texture wrapping mode");
        }
    }

public:
    gl::Texture gl_texture;

    TextureCache(const Texture& texture)
        : m_id(++GID)
    {
        gl_texture.load_pixels(
            static_cast<GLsizei>(texture.width()),
            static_cast<GLsizei>(texture.height()), convert(texture.format()),
            gl::Texture::Type::UNSIGNED_BYTE, texture.data().data());
        gl_texture.filter(
            convert(texture.mag_filter()), convert(texture.min_filter()));
        gl_texture.wrap(convert(texture.wrap_s()), convert(texture.wrap_t()));

        gl::Error::audit("texture cache - texture loading");

        switch (texture.min_filter()) {
        case Texture::MinFilter::NEAREST_MIPMAP_NEAREST:
        case Texture::MinFilter::NEAREST_MIPMAP_LINEAR:
        case Texture::MinFilter::LINEAR_MIPMAP_NEAREST:
        case Texture::MinFilter::LINEAR_MIPMAP_LINEAR:
            gl_texture.gen_mipmaps();
            break;
        default:
            break;
        }

        gl::Error::audit("texture cache - mipmaps gen");
    }

    ~TextureCache()
    {
        std::cerr << "Releasing texture cache " << m_id << std::endl;
    }
};

std::size_t TextureCache::GID = 0;

class RenderCache {
private:
    WeakRefCache<Mesh, MeshCache> m_meshes;
    WeakRefCache<Texture, TextureCache> m_textures;

public:
    std::optional<gl::Program> main_program;

    RenderCache() noexcept
    {
        try {
            std::cerr << "Compiling shaders..." << std::endl;
            gl::Shader vs(gl::Shader::VERTEX, BLOBS_SHADERS_GL3_MAIN_VS_GLSL);
            gl::Shader fs(gl::Shader::FRAGMENT, BLOBS_SHADERS_GL3_MAIN_FS_GLSL);

            std::cerr << "Linking program..." << std::endl;
            main_program.emplace(std::move(vs), std::move(fs));

            gl::Error::audit("load_main_program");
        } catch (const std::exception& e) {
            std::cerr << e.what() << std::endl;
        }
    }

    bool is_valid() const
    {
        return main_program.has_value();
    }

    const MeshCache& get(std::shared_ptr<Mesh> mesh)
    {
        auto iter = m_meshes.find(mesh);

        if (iter == m_meshes.end()) {
            iter = m_meshes.emplace(mesh, *mesh).first;
        }

        return iter->second;
    }

    const TextureCache& get(std::shared_ptr<Texture> texture)
    {
        auto iter = m_textures.find(texture);

        if (iter == m_textures.end()) {
            iter = m_textures.emplace(texture, *texture).first;
        }

        return iter->second;
    }
};

static void draw_mesh(
    RenderCache& cache, const MeshRenderer& renderer, const mat4& projection,
    const mat4& view, const mat4& model)
{
    mat4 vm = view * model;
    mat4 pvm = projection * vm;

    mat4 normal_matrix = glm::transpose(glm::inverse(vm));
    bool double_sided = false;

    const MeshCache& mesh = cache.get(renderer.mesh);

    cache.main_program->use();
    cache.main_program->uniform("u_ProjViewModel", pvm);
    cache.main_program->uniform("u_NormalMatrix", normal_matrix);

    if (renderer.material) {
        double_sided = renderer.material->double_sided();

        cache.main_program->uniform(
            "u_BaseColorFactor",
            renderer.material->get_or("base_color_factor", vec4 { 1.0f }));

        auto base_texture = renderer.material->get("base_color_texture");
        if (base_texture
            && base_texture->type == Material::ParameterType::TEXTURE) {
            cache.main_program->uniform(
                "u_BaseColorTexture",
                cache.get(base_texture->texture).gl_texture);
            cache.main_program->uniform("u_HasBaseColorTexture", true);
        } else {
            cache.main_program->uniform("u_HasBaseColorTexture", false);
        }
    }

    mesh.vertex_array.bind();
    GLenum topology = GL_TRIANGLES;

    switch (renderer.mesh->topology()) {
    case Mesh::Topology::TRIANGLES:
        topology = GL_TRIANGLES;
        break;
    case Mesh::Topology::TRIANGLE_STRIP:
        topology = GL_TRIANGLE_STRIP;
        break;
    default:
        std::cerr << "Unknown mesh topology." << std::endl;
        break;
    }

    gl::Error::audit("before draw elements");

    glEnable(GL_DEPTH_TEST);
    if (double_sided) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }
    glDrawElements(
        topology, static_cast<GLsizei>(renderer.mesh->index_buffer().size()),
        GL_UNSIGNED_INT, 0);

    gl::Error::audit("draw elements");
}

namespace ige::plugin::render::backend {

void render_meshes(World& world)
{
    auto& cache = world.get_or_emplace<RenderCache>();

    if (!cache.is_valid()) {
        return;
    }

    // TODO: gracefully handle the case where no WindowInfo is present
    auto wininfo = world.get<WindowInfo>();
    auto cameras = world.query<PerspectiveCamera, Transform>();

    if (cameras.empty()) {
        return;
    }

    auto& camera = std::get<1>(cameras[0]);
    auto& camera_xform = std::get<2>(cameras[0]);

    mat4 projection = glm::perspective(
        glm::radians(camera.fov),
        float(wininfo->width) / float(wininfo->height), camera.near,
        camera.far);
    mat4 view = camera_xform.world_to_local();

    auto meshes = world.query<MeshRenderer, Transform>();
    for (auto& [entity, renderer, xform] : meshes) {
        mat4 model = xform.local_to_world();

        draw_mesh(cache, renderer, projection, view, model);
    }
}

void clear_buffers(World&)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void clear_cache(World& world)
{
    world.remove<RenderCache>();
}

}
