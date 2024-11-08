#version 460
#extension GL_EXT_ray_query : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : require

#include "host_device_common.h"
#include "device_common.glsl"
#include "sampling.glsl"

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0, set = 0, rgba32f) uniform image2D storageImage;
layout(binding = 1, set = 0) uniform accelerationStructureEXT tlas;
layout(binding = 2, set = 0, scalar) buffer Vertices { Vertex vertices[]; } meshVertices[MAX_MESH_COUNT];	// Contains vertex buffers for meshes in the scene
layout(binding = 3, set = 0, scalar) buffer Indices { uint indices[]; }		meshIndices[MAX_MESH_COUNT];	// Contains index buffers of meshes in the scene.
layout(binding = 4, set = 0, scalar) buffer Materials { Material materials[]; };							// Contains all materials for the scene
layout(binding = 5, set = 0) uniform sampler2D testTexture;


layout(push_constant, scalar) uniform PushConstants
{
	Camera camera;
	uint numSamples; // Samples per batch
	uint numBounces;
	uint batchID;
};

vec3 normalLi(vec3 worldO, vec3 worldD, inout uint rngState)
{
		// Initialise a ray query object.
		rayQueryEXT rayQuery;
		const float tMin = 0.f;
		const float tMax = 10000.f;
		rayQueryInitializeEXT(rayQuery, tlas, gl_RayFlagsOpaqueEXT, 0xFF, worldO, tMin, worldD, tMax);

		// Traverse scene, keeping track of the information at the closest intersection.
		HitInfo hitInfo;
		hitInfo.t = tMax;
		while (rayQueryProceedEXT(rayQuery))
		{
			// For procedural geometry (i.e. geometry defined by AABBs), we must handle intersection routines ourselves.
			if (rayQueryGetIntersectionTypeEXT(rayQuery, false) == gl_RayQueryCandidateIntersectionAABBEXT)
			{
				// For procedural geometry, we use the custom index to determine the type of the geometry.
				const uint geometryType	= rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, false);
				const uint materialID	= rayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetEXT(rayQuery, false);

				// TODO: perform intersection tests in object space to simplify intersecion routines.
				// (For spheres it isn't significantly easier but it might be for other types of procedural geometry.)
				const mat4x3 objectToWorld = rayQueryGetIntersectionObjectToWorldEXT(rayQuery, false);
				const mat4x3 worldToObject = rayQueryGetIntersectionWorldToObjectEXT(rayQuery, false);
				
				const vec3 localO = rayQueryGetIntersectionObjectRayOriginEXT(rayQuery, false);
				const vec3 localD = rayQueryGetIntersectionObjectRayDirectionEXT(rayQuery, false);

				if ( geometryType == SPHERE_CUSTOM_INDEX && hitSphere(localO, localD,  worldToObject, objectToWorld, hitInfo)) {
					
					// Fill in material properties
					Material material		= materials[materialID];
					hitInfo.materialType	= material.type;
					hitInfo.albedo			= material.albedo;
					hitInfo.emitted			= material.emitted;

					rayQueryGenerateIntersectionEXT(rayQuery, hitInfo.t);
				}

				// Other procedural geometry...
			}
		}

		// Determine hit info at closest hit
		if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
			return vec3(0.f);
		}
		else if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
			
			//HitInfo info = closestHitTriangle(rayQuery, ...)

			// Get the ID of the triangle
			const uint meshID		= rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, true);
			const uint triangleID	= rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);
			const uint materialID	= rayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetEXT(rayQuery, true);

			// Get the indices of the vertices of the triangle
			const uint i0 = meshIndices[meshID].indices[3 * triangleID + 0];
			const uint i1 = meshIndices[meshID].indices[3 * triangleID + 1];
			const uint i2 = meshIndices[meshID].indices[3 * triangleID + 2];

			// Get the vertices of the triangle
			const Vertex v0 = meshVertices[meshID].vertices[i0];
			const Vertex v1 = meshVertices[meshID].vertices[i1];
			const Vertex v2 = meshVertices[meshID].vertices[i2];

			// Get the barycentric coordinates of the intersection
			vec3 barycentrics	= vec3(0.f, rayQueryGetIntersectionBarycentricsEXT(rayQuery, true));
			barycentrics.x		= 1.f - barycentrics.y - barycentrics.z;

			// Compute the coordinates of the intersection
			const vec3 objectPos	= v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;
			const vec3 objectSN		= v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;
			const vec3 objectGN		= normalize(cross(v1.position - v0.position, v2.position - v0.position));
			const vec2 objectUV		= v0.tex * barycentrics.x + v1.tex * barycentrics.y + v2.tex * barycentrics.z;

			hitInfo.t	= rayQueryGetIntersectionTEXT(rayQuery, true);
			hitInfo.p	= rayQueryGetIntersectionObjectToWorldEXT(rayQuery, true) * vec4(objectPos, 1.0f);
			hitInfo.gn	= normalize((objectGN * rayQueryGetIntersectionWorldToObjectEXT(rayQuery, true)).xyz);
			hitInfo.sn = (v0.normal == vec3(0.f) && v1.normal == vec3(0.f) && v2.normal == vec3(0.f)) ?
				hitInfo.gn :
				normalize((objectSN *rayQueryGetIntersectionWorldToObjectEXT(rayQuery, true)).xyz);
			hitInfo.uv	= objectUV;

			// Fill in material properties
			Material material		= materials[materialID];
			hitInfo.materialType	= material.type;
			hitInfo.albedo			= material.albedo;
			hitInfo.emitted			= material.emitted;

		}
		else {
			// We already computed hit info for procedural geometry in the traversal loop.
		}
		// Return the normal of the hit point as a color value in [0,1].
		return 0.5f * (vec3(1.f) + hitInfo.sn);
}

// TODO: Add a maxDistance parameter (see pbrt4)
vec3 ambientOcclusionLi(vec3 worldO, vec3 worldD, inout uint rngState)
{
	const float tMin = 0.f;
	const float tMax = 10000.f;

	// Initialise a ray query object.
	rayQueryEXT cameraRayQuery;
	rayQueryInitializeEXT(cameraRayQuery, tlas, gl_RayFlagsOpaqueEXT, 0xFF, worldO, tMin, worldD, tMax);

	// Traverse scene, keeping track of the information at the closest intersection.
	HitInfo hitInfo;
	hitInfo.t = tMax;
	while (rayQueryProceedEXT(cameraRayQuery)) {
		// For procedural geometry (i.e. geometry defined by AABBs), we must handle intersection routines ourselves.
		if (rayQueryGetIntersectionTypeEXT(cameraRayQuery, false) == gl_RayQueryCandidateIntersectionAABBEXT)
		{
			// For procedural geometry, we use the custom index to determine the type of the geometry.
			const uint geometryType = rayQueryGetIntersectionInstanceCustomIndexEXT(cameraRayQuery, false);
			const uint materialID = rayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetEXT(cameraRayQuery, false);

			// TODO: perform intersection tests in object space to simplify intersecion routines.
			// (For spheres it isn't significantly easier but it might be for other types of procedural geometry.)
			const mat4x3 objectToWorld = rayQueryGetIntersectionObjectToWorldEXT(cameraRayQuery, false);
			const mat4x3 worldToObject = rayQueryGetIntersectionWorldToObjectEXT(cameraRayQuery, false);

			const vec3 localO = rayQueryGetIntersectionObjectRayOriginEXT(cameraRayQuery, false);
			const vec3 localD = rayQueryGetIntersectionObjectRayDirectionEXT(cameraRayQuery, false);

			if (geometryType == SPHERE_CUSTOM_INDEX && hitSphere(localO, localD, worldToObject, objectToWorld, hitInfo)) {

				// Fill in material properties (technically we only care about the material type here...)
				Material material = materials[materialID];
				hitInfo.materialType = material.type;
				hitInfo.albedo = material.albedo;
				hitInfo.emitted = material.emitted;

				rayQueryGenerateIntersectionEXT(cameraRayQuery, hitInfo.t);
			}

			// Other procedural geometry...
		}
	}

	if (rayQueryGetIntersectionTypeEXT(cameraRayQuery, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
		// Return black if the original ray doesn't hit anything.
		return vec3(1.f);
	}
	else if (rayQueryGetIntersectionTypeEXT(cameraRayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {

		// Get the ID of the triangle
		const uint meshID = rayQueryGetIntersectionInstanceCustomIndexEXT(cameraRayQuery, true);
		const uint triangleID = rayQueryGetIntersectionPrimitiveIndexEXT(cameraRayQuery, true);
		const uint materialID = rayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetEXT(cameraRayQuery, true);

		// Get the indices of the vertices of the triangle
		const uint i0 = meshIndices[meshID].indices[3 * triangleID + 0];
		const uint i1 = meshIndices[meshID].indices[3 * triangleID + 1];
		const uint i2 = meshIndices[meshID].indices[3 * triangleID + 2];

		// Get the vertices of the triangle
		const Vertex v0 = meshVertices[meshID].vertices[i0];
		const Vertex v1 = meshVertices[meshID].vertices[i1];
		const Vertex v2 = meshVertices[meshID].vertices[i2];

		// Get the barycentric coordinates of the intersection
		vec3 barycentrics = vec3(0.f, rayQueryGetIntersectionBarycentricsEXT(cameraRayQuery, true));
		barycentrics.x = 1.f - barycentrics.y - barycentrics.z;

		// Compute the coordinates of the intersection
		const vec3 objectPos = v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;
		const vec3 objectSN = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;
		const vec3 objectGN = normalize(cross(v1.position - v0.position, v2.position - v0.position));
		const vec2 objectUV = v0.tex * barycentrics.x + v1.tex * barycentrics.y + v2.tex * barycentrics.z;

		hitInfo.t = rayQueryGetIntersectionTEXT(cameraRayQuery, true);
		hitInfo.p = rayQueryGetIntersectionObjectToWorldEXT(cameraRayQuery, true) * vec4(objectPos, 1.0f);
		hitInfo.gn = normalize((objectGN * rayQueryGetIntersectionWorldToObjectEXT(cameraRayQuery, true)).xyz);
		hitInfo.sn = (v0.normal == vec3(0.f) && v1.normal == vec3(0.f) && v2.normal == vec3(0.f)) ?
			hitInfo.gn :
			normalize((objectSN * rayQueryGetIntersectionWorldToObjectEXT(cameraRayQuery, true)).xyz);
		hitInfo.uv = objectUV;

		// Fill in material properties
		Material material = materials[materialID];
		hitInfo.materialType = material.type;
		hitInfo.albedo = material.albedo;
		hitInfo.emitted = material.emitted;

	}
	else {
		// We already computed hit info for procedural geometry in the traversal loop.
	}
	
	// Directly compute scattered ray for a lambertian material.
	const ONB onb			= generateONB(hitInfo.sn);
	const vec2	rv			= vec2(stepAndOutputRNGFloat(rngState), stepAndOutputRNGFloat(rngState));
	const vec3 shadowDir	= normalize(toWorld(onb, sampleHemisphereCosine(rv)));
	const vec3 shadowOrigin = offsetPositionAlongNormal(hitInfo.p, hitInfo.sn);
	const float pdf			= max(0.f, dot(normalize(shadowDir), hitInfo.sn)) * INV_PI;

	// Initialise a new ray query for the shadow ray.
	rayQueryEXT shadowRayQuery;
	const float maxDistance = tMax; //TODO: Add this as a variable parameter.
	rayQueryInitializeEXT(shadowRayQuery, tlas, gl_RayFlagsOpaqueEXT, 0xFF, shadowOrigin, tMin, shadowDir, maxDistance);

	HitInfo shadowHitInfo;
	shadowHitInfo.t = tMax;
	while (rayQueryProceedEXT(shadowRayQuery))
	{
		if (rayQueryGetIntersectionTypeEXT(shadowRayQuery, false) == gl_RayQueryCandidateIntersectionAABBEXT)
		{
			const uint geometryType = rayQueryGetIntersectionInstanceCustomIndexEXT(shadowRayQuery, false);
			
			const mat4x3 objectToWorld = rayQueryGetIntersectionObjectToWorldEXT(shadowRayQuery, false);
			const mat4x3 worldToObject = rayQueryGetIntersectionWorldToObjectEXT(shadowRayQuery, false);
			const vec3 localO	= rayQueryGetIntersectionObjectRayOriginEXT(shadowRayQuery, false);
			const vec3 localD	= rayQueryGetIntersectionObjectRayDirectionEXT(shadowRayQuery, false);

			if (geometryType == SPHERE_CUSTOM_INDEX && hitSphere(localO, localD, worldToObject, objectToWorld, shadowHitInfo)) {
				// We can return early here.
				return vec3(0.f);
			}
		}
	}
	// If the shadow ray intersects no geometry (i.e. is unoccluded) then it hits the white sky.
	if (rayQueryGetIntersectionTypeEXT(shadowRayQuery, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
		return vec3(1.f) * dot(normalize(shadowDir), hitInfo.sn) * INV_PI / pdf;
	}
	// If the shadow ray hit something then the original hit point was occluded and no color is accumulated.
	else return vec3(0.f);
}


vec3 pathBasicLi(vec3 origin, vec3 direction, inout uint rngState);

void main()
{
	// The resolution of the image:
	const ivec2 resolution = imageSize(storageImage);
	// The screen-space coordinates of the pixel being evaluated.
	const uvec2 pixel = gl_GlobalInvocationID.xy;
	
	if ((pixel.x >= resolution.x) || (pixel.y >= resolution.y)) { return; }

	// Use the linear index of the pixel as the initial seed for the RNG.
	uint rngState =  resolution.x * (batchID * resolution.y + pixel.y)  + pixel.x;  // Initial seed

	vec3 pixelColor = vec3(0.f);
	for (int sampleID = 0; sampleID < numSamples; ++sampleID)
	{
		vec3 origin;
		vec3 direction;
		generateRay(camera, vec2(pixel) + vec2(stepAndOutputRNGFloat(rngState), stepAndOutputRNGFloat(rngState)), resolution, origin, direction);
		
		pixelColor += pathBasicLi(origin, direction, rngState);
		//pixelColor += normalLi(origin, direction, rngState);
		//pixelColor += ambientOcclusionLi(origin, direction, rngState);

	}

	pixelColor /= numSamples;
	// Update the color value for the texel.
	if (batchID!= 0)
	{
		const vec3 previousAverageColor = imageLoad(storageImage, ivec2(pixel)).rgb;
		pixelColor = (batchID * previousAverageColor + pixelColor) / (batchID + 1);
	}
	imageStore(storageImage, ivec2(pixel), vec4(pixelColor, 0.f));
}

vec3 pathBasicLi(vec3 origin, vec3 direction, inout uint rngState)
{
	vec3 curAttenuation = vec3(1.0);
	vec3 result			= vec3(0.f);
	for (int depth = 0; depth <= numBounces; ++depth)
	{

		// Initialise a ray query object.
		rayQueryEXT rayQuery;
		const float tMin = 0.f;
		const float tMax = 10000.f;
		rayQueryInitializeEXT(rayQuery, tlas, gl_RayFlagsOpaqueEXT, 0xFF, origin, tMin, direction, tMax);

		// Traverse scene, keeping track of the information at the closest intersection.
		HitInfo hitInfo;
		hitInfo.t = tMax;
		while (rayQueryProceedEXT(rayQuery))
		{
			// For procedural geometry (i.e. geometry defined by AABBs), we must handle intersection routines ourselves.
			if (rayQueryGetIntersectionTypeEXT(rayQuery, false) == gl_RayQueryCandidateIntersectionAABBEXT)
			{
				// For procedural geometry, we use the custom index to determine the type of the geometry.
				const uint geometryType	= rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, false);
				const uint materialID	= rayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetEXT(rayQuery, false);

				// TODO: perform intersection tests in object space to simplify intersecion routines.
				// (For spheres it isn't significantly easier but it might be for other types of procedural geometry.)
				const mat4x3 objectToWorld = rayQueryGetIntersectionObjectToWorldEXT(rayQuery, false);
				const mat4x3 worldToObject = rayQueryGetIntersectionWorldToObjectEXT(rayQuery, false);
				
				const vec3 localO = rayQueryGetIntersectionObjectRayOriginEXT(rayQuery, false);
				const vec3 localD = rayQueryGetIntersectionObjectRayDirectionEXT(rayQuery, false);

				if ( geometryType == SPHERE_CUSTOM_INDEX && hitSphere(localO, localD,  worldToObject, objectToWorld, hitInfo)) {
					
					// Fill in material properties
					Material material		= materials[materialID];
					hitInfo.materialType	= material.type;
					hitInfo.albedo			= material.albedo;
					hitInfo.emitted			= material.emitted;

					rayQueryGenerateIntersectionEXT(rayQuery, hitInfo.t);
				}

				// Other procedural geometry...
			}
		}

		// Determine hit info at closest hit
		if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
			return curAttenuation * camera.backgroundColor;
		}
		else if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
			
			// Get the ID of the triangle
			const uint meshID		= rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, true);
			const uint triangleID	= rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);
			const uint materialID	= rayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetEXT(rayQuery, true);

			// Get the indices of the vertices of the triangle
			const uint i0 = meshIndices[meshID].indices[3 * triangleID + 0];
			const uint i1 = meshIndices[meshID].indices[3 * triangleID + 1];
			const uint i2 = meshIndices[meshID].indices[3 * triangleID + 2];

			// Get the vertices of the triangle
			const Vertex v0 = meshVertices[meshID].vertices[i0];
			const Vertex v1 = meshVertices[meshID].vertices[i1];
			const Vertex v2 = meshVertices[meshID].vertices[i2];

			// Get the barycentric coordinates of the intersection
			vec3 barycentrics	= vec3(0.f, rayQueryGetIntersectionBarycentricsEXT(rayQuery, true));
			barycentrics.x		= 1.f - barycentrics.y - barycentrics.z;

			// Compute the coordinates of the intersection
			const vec3 objectPos	= v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;
			const vec3 objectSN		= v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;
			const vec3 objectGN		= normalize(cross(v1.position - v0.position, v2.position - v0.position));
			const vec2 objectUV		= v0.tex * barycentrics.x + v1.tex * barycentrics.y + v2.tex * barycentrics.z;


			hitInfo.t	= rayQueryGetIntersectionTEXT(rayQuery, true);
			hitInfo.p	= rayQueryGetIntersectionObjectToWorldEXT(rayQuery, true) * vec4(objectPos, 1.0f);
			hitInfo.gn	= normalize((objectGN * rayQueryGetIntersectionWorldToObjectEXT(rayQuery, true)).xyz);
			hitInfo.sn = (v0.normal == vec3(0.f) && v1.normal == vec3(0.f) && v2.normal == vec3(0.f)) ?
				hitInfo.gn :
				normalize((objectSN * rayQueryGetIntersectionWorldToObjectEXT(rayQuery, true)).xyz);
			hitInfo.uv	= objectUV;

			// Fill in material properties
			Material material		= materials[materialID];
			hitInfo.materialType	= material.type;
			hitInfo.albedo			= material.albedo;
			hitInfo.emitted			= material.emitted;
			hitInfo.phongExponent	= material.phongExponent;


		}
		else {
			// We already computed hit info for procedural geometry in the traversal loop.
		}


		// Now use material to determine scatter properties.
		vec3 attenuation;
		vec3 scatteredOrigin;
		vec3 scatteredDir;

		vec3 emitted = vec3(0.f);
		if (dot(direction, hitInfo.sn) < 0.f) {
			// Assume emissive surfaces only emit from the outward normal-facing side.
			emitted = hitInfo.emitted;
		}

		bool scatter;
		vec3 bsdf_term;
		const vec2	rv	= vec2(stepAndOutputRNGFloat(rngState), stepAndOutputRNGFloat(rngState));
		const float rv1 = stepAndOutputRNGFloat(rngState);
		switch (hitInfo.materialType) {
		case DIFFUSE:
			scatter		= sampleLambertian(origin, direction, hitInfo, attenuation, scatteredOrigin, scatteredDir, rv, rv1);
			bsdf_term	= evalLambertian(origin, direction, scatteredDir, hitInfo) / pdfLambertian(origin, direction, scatteredDir, hitInfo);
			break;
		case MIRROR:
			scatter		= scatterMirror(origin, direction, hitInfo, attenuation, scatteredOrigin, scatteredDir, rngState);
			bsdf_term	= attenuation;
			break;
		case DIELECTRIC:
			scatter		= sampleDielectric(origin, direction, hitInfo, attenuation, scatteredOrigin, scatteredDir, rv, rv1);
			bsdf_term	= attenuation;
			break;
		case PHONG:
			scatter		= samplePhong(origin, direction, hitInfo, attenuation, scatteredOrigin, scatteredDir, rv, rv1);
			bsdf_term	= evalPhong(origin, direction, scatteredDir, hitInfo) / pdfPhong(origin, direction, scatteredDir, hitInfo);
			break;
		case LIGHT:
			scatter = false;
			break;
		default:
			scatter = false;
			break;
		}
		if (scatter) {
			// Set properties for the next bounce.
			result			+= (curAttenuation * emitted);
			curAttenuation *= bsdf_term;
			origin 			= scatteredOrigin;
			direction		= scatteredDir;
		}
		else {
			// Ray was absorbed by the surface.
			result += (curAttenuation * emitted);
			return result;
		}
	}
	// Exceeded recursion - assume the sample provides no contribution to the light.
	return vec3(0.f);
}


vec3 pathNEELi(vec3 origin, vec3 direction, inout uint rngState)
{
	vec3 curAttenuation = vec3(1.0);
	vec3 result = vec3(0.f);

	// Initialise a ray query object.
	rayQueryEXT rayQuery;
	const float tMin = 0.f;
	const float tMax = 10000.f;
	rayQueryInitializeEXT(rayQuery, tlas, gl_RayFlagsOpaqueEXT, 0xFF, origin, tMin, direction, tMax);

	// Traverse scene, keeping track of the information at the closest intersection.
	HitInfo hitInfo;
	hitInfo.t = tMax;
	while (rayQueryProceedEXT(rayQuery))
	{
		// For procedural geometry (i.e. geometry defined by AABBs), we must handle intersection routines ourselves.
		if (rayQueryGetIntersectionTypeEXT(rayQuery, false) == gl_RayQueryCandidateIntersectionAABBEXT)
		{
			// For procedural geometry, we use the custom index to determine the type of the geometry.
			const uint geometryType = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, false);
			const uint materialID = rayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetEXT(rayQuery, false);

			// TODO: perform intersection tests in object space to simplify intersecion routines.
			// (For spheres it isn't significantly easier but it might be for other types of procedural geometry.)
			const mat4x3 objectToWorld = rayQueryGetIntersectionObjectToWorldEXT(rayQuery, false);
			const mat4x3 worldToObject = rayQueryGetIntersectionWorldToObjectEXT(rayQuery, false);

			const vec3 localO = rayQueryGetIntersectionObjectRayOriginEXT(rayQuery, false);
			const vec3 localD = rayQueryGetIntersectionObjectRayDirectionEXT(rayQuery, false);

			if (geometryType == SPHERE_CUSTOM_INDEX && hitSphere(localO, localD, worldToObject, objectToWorld, hitInfo)) {

				// Fill in material properties
				Material material = materials[materialID];
				hitInfo.materialType = material.type;
				hitInfo.albedo = material.albedo;
				hitInfo.emitted = material.emitted;

				rayQueryGenerateIntersectionEXT(rayQuery, hitInfo.t);
			}

			// Other procedural geometry...
		}
	}

	// Determine hit info at closest hit
	if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
		return curAttenuation * camera.backgroundColor;
	}
	else if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {

		// Get the ID of the triangle
		const uint meshID = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, true);
		const uint triangleID = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);
		const uint materialID = rayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetEXT(rayQuery, true);

		// Get the indices of the vertices of the triangle
		const uint i0 = meshIndices[meshID].indices[3 * triangleID + 0];
		const uint i1 = meshIndices[meshID].indices[3 * triangleID + 1];
		const uint i2 = meshIndices[meshID].indices[3 * triangleID + 2];

		// Get the vertices of the triangle
		const Vertex v0 = meshVertices[meshID].vertices[i0];
		const Vertex v1 = meshVertices[meshID].vertices[i1];
		const Vertex v2 = meshVertices[meshID].vertices[i2];

		// Get the barycentric coordinates of the intersection
		vec3 barycentrics = vec3(0.f, rayQueryGetIntersectionBarycentricsEXT(rayQuery, true));
		barycentrics.x = 1.f - barycentrics.y - barycentrics.z;

		// Compute the coordinates of the intersection
		const vec3 objectPos = v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;
		const vec3 objectSN = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;
		const vec3 objectGN = normalize(cross(v1.position - v0.position, v2.position - v0.position));
		const vec2 objectUV = v0.tex * barycentrics.x + v1.tex * barycentrics.y + v2.tex * barycentrics.z;


		hitInfo.t = rayQueryGetIntersectionTEXT(rayQuery, true);
		hitInfo.p = rayQueryGetIntersectionObjectToWorldEXT(rayQuery, true) * vec4(objectPos, 1.0f);
		hitInfo.gn = normalize((objectGN * rayQueryGetIntersectionWorldToObjectEXT(rayQuery, true)).xyz);
		hitInfo.sn = (v0.normal == vec3(0.f) && v1.normal == vec3(0.f) && v2.normal == vec3(0.f)) ?
			hitInfo.gn :
			normalize((objectSN * rayQueryGetIntersectionWorldToObjectEXT(rayQuery, true)).xyz);
		hitInfo.uv = objectUV;

		// Fill in material properties
		Material material = materials[materialID];
		hitInfo.materialType = material.type;
		hitInfo.albedo = material.albedo;
		hitInfo.emitted = material.emitted;
		hitInfo.phongExponent = material.phongExponent;


	}
	else {
		// We already computed hit info for procedural geometry in the traversal loop.
	}


	// Sample from a light source
	vec3 attenuation;
	vec3 scatteredOrigin;
	vec3 scatteredDir;

	vec3 emitted = vec3(0.f);
	if (dot(direction, hitInfo.sn) < 0.f) {
		// Assume emissive surfaces only emit from the outward normal-facing side.
		emitted = hitInfo.emitted;
	}

	bool scatter;
	vec3 bsdf_term;
	const vec2	rv = vec2(stepAndOutputRNGFloat(rngState), stepAndOutputRNGFloat(rngState));
	const float rv1 = stepAndOutputRNGFloat(rngState);
	switch (hitInfo.materialType) {
	case DIFFUSE:
		scatter = sampleLambertian(origin, direction, hitInfo, attenuation, scatteredOrigin, scatteredDir, rv, rv1);
		bsdf_term = evalLambertian(origin, direction, scatteredDir, hitInfo) / pdfLambertian(origin, direction, scatteredDir, hitInfo);
		break;
	case MIRROR:
		scatter = scatterMirror(origin, direction, hitInfo, attenuation, scatteredOrigin, scatteredDir, rngState);
		bsdf_term = attenuation;
		break;
	case DIELECTRIC:
		scatter = sampleDielectric(origin, direction, hitInfo, attenuation, scatteredOrigin, scatteredDir, rv, rv1);
		bsdf_term = attenuation;
		break;
	case PHONG:
		scatter = samplePhong(origin, direction, hitInfo, attenuation, scatteredOrigin, scatteredDir, rv, rv1);
		bsdf_term = evalPhong(origin, direction, scatteredDir, hitInfo) / pdfPhong(origin, direction, scatteredDir, hitInfo);
		break;
	case LIGHT:
		scatter = false;
		break;
	default:
		scatter = false;
		break;
	}
	if (scatter) {
		// Set properties for the next bounce.
		result += (curAttenuation * emitted);
		curAttenuation *= bsdf_term;
		origin = scatteredOrigin;
		direction = scatteredDir;
	}
	else {
		// Ray was absorbed by the surface.
		result += (curAttenuation * emitted);
		return result;
	}
	
	// Exceeded recursion - assume the sample provides no contribution to the light.
	return vec3(0.f);
}