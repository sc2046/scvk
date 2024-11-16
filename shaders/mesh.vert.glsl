#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require


layout(location = 0) out vec3 outColor;
layout(location = 1) out vec2 outUV;

struct Vertex {

	vec3 position;
	float uv_x;
	vec3 normal;
	float uv_y;
	vec4 color;
};

layout(set = 0, binding = 0, std430) uniform FrameData {
	mat4 view;
	mat4 proj;
	mat4 viewProj;
} frameData;

// Buffer_reference tells the shader that the data will be accessed direcly using the buffer address.
layout(buffer_reference, std430) readonly buffer VertexBuffer {
	Vertex vertices[];
};

//push constants block
layout(push_constant) uniform constants
{
	mat4 render_matrix;
	VertexBuffer vertexBuffer; // Note that this is a uint64_t handle.
} PushConstants;

void main()
{
	//load vertex data from device adress
	Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];

	//output data
	gl_Position = frameData.proj * frameData.view * PushConstants.render_matrix * vec4(v.position, 1.0f);
	outColor = v.color.xyz;
	outUV.x = v.uv_x;
	outUV.y = v.uv_y;
}