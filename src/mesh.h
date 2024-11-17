#pragma once

#include "buffer.h"
#include "texture.h"
#include "vk_types.h"


struct Vertex {

	glm::vec3 position;
	float uv_x;
	glm::vec3 normal;
	float uv_y;
	glm::vec4 color;
};

// holds the resources needed for a mesh
struct GPUMeshBuffers {
	scvk::Buffer	mIndexBuffer;
	scvk::Buffer	mVertexBuffer;
	VkDeviceAddress mVertexBufferAddress;
};

// push constants for our mesh object draws
struct GPUDrawPushConstants {
	glm::mat4		mWorldMatrix = glm::mat4(1.f);
	VkDeviceAddress mVertexBufferAddress;
};


struct Primitive
{
	uint32_t indexCount;
	uint32_t firstIndex;

	uint32_t textureID;
};

struct LoadedMesh
{
	std::vector<Primitive> mPrimitives;


	// CPU data.
	std::vector<Vertex>			mVertices;
	std::vector<uint32_t>		mIndices;


	//std::vector<std::string>	mTexturePaths;
	// GPU data.
	GPUMeshBuffers mBuffers;
	std::vector<scvk::Texture>	mTextures;
};
