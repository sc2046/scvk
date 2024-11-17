#pragma once

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>

#include "tiny_obj_loader.h"
#include "vk_types.h"
#include "stb_image.h"

#include "mesh.h"

//TODO: Note: GLTF 2.0 only supports static 2D Textures. This is good to know.
inline std::vector<scvk::Texture> loadTexturesFromGLTFAsset(VulkanApp* app, const fastgltf::Asset& asset, const std::filesystem::path& rootPath) {

    std::vector<scvk::Texture> textures;

    for (const auto& tex : asset.textures) {
        //TODO: Assuming no extensions, we can safely assume the texture will always have an image index.
        auto& gltfImageData = asset.images[tex.imageIndex.value()].data;
		int width, height;
		// The image data is stored in an external file, referenced by a URI
        if (const auto* path = std::get_if<fastgltf::sources::URI>(&gltfImageData)) {
            assert(path->uri.isLocalPath());
            assert(path->fileByteOffset == 0);

            std::filesystem::path imagePath = rootPath.parent_path() / path->uri.c_str();
            textures.emplace_back(app->uploadTexture(imagePath.string().c_str()));
        }
        // The image data is stored as a raw array of bytes
        else if (const auto* array = std::get_if<fastgltf::sources::Array>(&gltfImageData)) {
			const auto rawImageData = array->bytes;
            const auto dataSize = array->bytes.size();
			unsigned char* data = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(rawImageData.data()), static_cast<int>(dataSize), &width, &height, nullptr, 4);
			textures.emplace_back(app->uploadTexture(data, width, height));
        }
        // The image data is stored in a buffer view within the GLTF
        else if (const auto* bufferView = std::get_if<fastgltf::sources::BufferView>(&gltfImageData)) {
            auto& bufView = asset.bufferViews[bufferView->bufferViewIndex];
            auto& buffer = asset.buffers[bufView.bufferIndex];

            if (const auto* vector = std::get_if<fastgltf::sources::Vector>(&buffer.data)) {

				unsigned char* data = stbi_load_from_memory(
					reinterpret_cast<const unsigned char*>(vector->bytes.data() + bufView.byteOffset),
					static_cast<int>(bufView.byteLength),
					&width, &height, nullptr, 4);
				textures.emplace_back(app->uploadTexture(data, width, height));
            }
            else if (const auto* array = std::get_if<fastgltf::sources::Array>(&buffer.data)) {
				unsigned char* data = stbi_load_from_memory(
					reinterpret_cast<const unsigned char*>(array->bytes.data() + bufView.byteOffset),
					static_cast<int>(bufView.byteLength),
					&width, &height, nullptr, 4);
					textures.emplace_back(app->uploadTexture(data, width, height));
            }
        }
        else if (const auto* byteView = std::get_if<fastgltf::sources::ByteView>(&gltfImageData)) { throw std::runtime_error("Texture is sourced in byte view\n"); }
        else if (std::holds_alternative<fastgltf::sources::Vector>(gltfImageData)) { throw std::runtime_error("Texture is sourced in vector\n"); }
    }

    return textures;
}

inline LoadedMesh processGltfMesh(const fastgltf::Asset& asset, const fastgltf::Mesh& gltf_mesh)
{
    std::vector<Primitive>      primitives;
	std::vector<Vertex>			vertices;
	std::vector<std::uint32_t>  indices;

    for (const auto& gltfPrimitive : gltf_mesh.primitives)
    {
		Primitive prim;
		prim.firstIndex = indices.size();
		prim.indexCount = asset.accessors[gltfPrimitive.indicesAccessor.value()].count;

		assert(gltfPrimitive.materialIndex.has_value());

		auto matID = gltfPrimitive.materialIndex.value();
		prim.textureID = asset.materials[matID].pbrData.baseColorTexture.value().textureIndex;

		const auto initial_vertex = vertices.size();

		// Process indices for the primitive.
		const auto& indicesAccessor = asset.accessors[gltfPrimitive.indicesAccessor.value()];
		indices.reserve(indices.size() + indicesAccessor.count);

		fastgltf::iterateAccessor<std::uint32_t>(asset, indicesAccessor,
			[&](std::uint32_t index) {
				indices.push_back(index + initial_vertex);
			});
		
		// Process vertex positions.
		const auto& posAccessor = asset.accessors[gltfPrimitive.findAttribute("POSITION")->accessorIndex];
		vertices.resize(vertices.size() + posAccessor.count);
		fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, posAccessor,
			[&](glm::vec3 v, size_t index) {
				Vertex newvtx;
				newvtx.position = v;
				newvtx.normal = { 0, 1, 0 };
				newvtx.color = glm::vec4{ 1.f };
				newvtx.uv_x = 0;
				newvtx.uv_y = 0;
				vertices[initial_vertex + index] = newvtx;
			});

        // Process vertex normals.
        // TODO: Follow gltf 2.0 spec: "When normals are not specified, client implementations MUST calculate flat normals and the provided tangents (if present) MUST be ignored."
        auto& normalAccessor = asset.accessors[gltfPrimitive.findAttribute("NORMAL")->accessorIndex];
		assert(normalAccessor.bufferViewIndex.has_value());
        fastgltf::iterateAccessorWithIndex<glm::vec3>(
            asset,
            normalAccessor,
            [&](glm::vec3 normal, std::size_t idx) {
                vertices[initial_vertex + idx].normal = normal;
            });

		// Process vertex texture coords.
        //TODO: Read spec about 2nd set of tex coords - They are for skinning.
        if (gltfPrimitive.findAttribute("TEXCOORD_0") != gltfPrimitive.attributes.end()){
            auto& texAccessor = asset.accessors[gltfPrimitive.findAttribute("TEXCOORD_0")->accessorIndex];
            assert(texAccessor.bufferViewIndex.has_value());
            fastgltf::iterateAccessorWithIndex<glm::vec2>(
                asset,
                texAccessor,
                [&](glm::vec2 t, std::size_t idx) {
					vertices[initial_vertex + idx].uv_x = t.x;
					vertices[initial_vertex + idx].uv_y = t.y;
                });
		}

		// Process vertex colors.
		if (gltfPrimitive.findAttribute("COLOR_0") != gltfPrimitive.attributes.end()) {
			auto& colAccessor = asset.accessors[gltfPrimitive.findAttribute("COLOR_0")->accessorIndex];
			assert(colAccessor.bufferViewIndex.has_value());
			fastgltf::iterateAccessorWithIndex<glm::vec4>(
				asset,
				colAccessor,
				[&](glm::vec4 c, std::size_t idx) {
					vertices[initial_vertex + idx].color = c;
				});
		}

		for (auto& vtx : vertices) {
			vtx.color = glm::vec4(vtx.normal, 1.f);
		}


		primitives.push_back(std::move(prim));

    }
    

    LoadedMesh mesh = {
        .mPrimitives = std::move(primitives),
		.mVertices = std::move(vertices),
		.mIndices = std::move(indices),
		//.mTextures = loadTexturesFromGLTFAsset()

    };

	return mesh;

}


bool loadGltfFromFile(VulkanApp* app, const fs::path& path, LoadedMesh& loaded)
{
	constexpr auto extensions =
		fastgltf::Extensions::KHR_lights_punctual |
		fastgltf::Extensions::EXT_mesh_gpu_instancing;
	auto parser = fastgltf::Parser(extensions);

	// Load contents of the file into a buffer.
	auto eGltfFile = fastgltf::GltfDataBuffer::FromPath(path);
	if (auto error = eGltfFile.error(); error != fastgltf::Error::None) {
		//return fastgltf::Error(eGltfFile.error());
	}
	auto GltfFile = std::move(eGltfFile.get());

	// Parse the file.
	constexpr auto parseOptions =
		fastgltf::Options::LoadExternalBuffers |
		//fastgltf::Options::LoadExternalImages  |
		fastgltf::Options::DecomposeNodeMatrices |
		fastgltf::Options::GenerateMeshIndices |
		fastgltf::Options::LoadGLBBuffers;
	auto eAsset = parser.loadGltf(GltfFile, path.parent_path(), parseOptions);
	if (auto error = eAsset.error(); error != fastgltf::Error::None) {
		//return fastgltf::Error(eGltfFile.error());
	}
	auto asset = std::move(eAsset.get());

	loaded = processGltfMesh(asset, asset.meshes[0]);
	loaded.mTextures = loadTexturesFromGLTFAsset(app, asset, path);
	return true;
}
