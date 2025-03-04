//-------------------- Vertex Shader --------------------

#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec2 inUV;


layout(location = 0) out VertexOutput
{
	/// Vertex world position
	vec3 worldPosition;

	/// Camera view position.
	vec3 viewPosition;


	/// TBN (tangent, bitangent, normal) transformation matrix.
	mat3 TBN;


	/// Vertex UV
	vec2 uv;
} vsOut;

//---------- Bindings ----------
layout(binding = 0) uniform CameraBuffer
{
	/// Camera transformation matrix.
	mat4 view;

	/**
	*	Camera inverse view projection matrix.
	*	projection * inverseView.
	*/
	mat4 invViewProj;
} camera;

layout(binding = 1) uniform ObjectBuffer
{
	/// Object transformation matrix.
	mat4 transform;
} object;


void main()
{
	//---------- Position ----------
	const vec4 worldPosition4 = object.transform * vec4(inPosition, 1.0);
	vsOut.worldPosition = worldPosition4.xyz / worldPosition4.w;
	gl_Position = camera.invViewProj * worldPosition4;
	vsOut.viewPosition = vec3(camera.view[0][3], camera.view[1][3], camera.view[2][3]);


	//---------- Normal ----------
	const vec3 normal = normalize(mat3(object.transform) * inNormal);
	const vec3 tangent = normalize(mat3(object.transform) * inTangent);
	const vec3 bitangent = cross(normal, tangent);

	vsOut.TBN = mat3(tangent, bitangent, normal);


	//---------- UV ----------
	vsOut.uv = inUV;
}
