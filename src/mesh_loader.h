#pragma once

#include "tiny_obj_loader.h"
#include "vk_types.h"

#include "mesh.h"

bool loadMeshFromfile(const char* path, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
{
	tinyobj::ObjReader       reader;

	reader.ParseFromFile(path);
	assert(reader.Valid());

	auto& attrib = reader.GetAttrib();
	auto& shapes = reader.GetShapes();

	// Parse all vertices, normals, and texture coordinates
	for (const auto& shape : shapes) {
		for (const auto& index : shape.mesh.indices) {
			Vertex vertex;

			// Position
			vertex.position.x = attrib.vertices[3 * index.vertex_index + 0];
			vertex.position.y = attrib.vertices[3 * index.vertex_index + 1];
			vertex.position.z = attrib.vertices[3 * index.vertex_index + 2];

			// Normal
			if (index.normal_index >= 0) {
				vertex.normal.x = attrib.normals[3 * index.normal_index + 0];
				vertex.normal.y = attrib.normals[3 * index.normal_index + 1];
				vertex.normal.z = attrib.normals[3 * index.normal_index + 2];
			}
			// Texture Coordinate
			if (index.texcoord_index >= 0) { // Check if texture coordinate exists
				//vertex.tex.x = vertex.tex.y = 0.0f;

				vertex.uv_x = attrib.texcoords[2 * index.texcoord_index + 0];
				vertex.uv_y = attrib.texcoords[2 * index.texcoord_index + 1];
			}
			else { vertex.uv_x = vertex.uv_y = 0.0f; }

			vertex.color = glm::vec4(vertex.normal, 1.f);

			vertices.push_back(std::move(vertex));
			indices.push_back(static_cast<uint32_t>(vertices.size() - 1));
		}
	}

	return true;

}