#define USE_MESHLET_ID_AS_VERTEX_COLOR
#define USE_AMPLIFICATIONSHADER
#define USE_INSTANCING
#define DISPLAY_VERTEX_COLOR_ONLY

//-------------------- Amplification Shader --------------------

#define AS_GROUP_SIZE 32

struct Payload
{
#ifdef USE_INSTANCING
	uint instanceIndices[AS_GROUP_SIZE];
#endif

	uint meshletIndices[AS_GROUP_SIZE];
};

groupshared Payload sPayload;

//---------- Bindings ----------
struct Camera
{
	/// Camera transformation matrix.
	float4x4 view;

	/**
	*	Camera inverse view projection matrix.
	*	projection * inverseView.
	*/
	float4x4 invViewProj;
};
cbuffer SceneBuffer : register(b0)
{
	Camera camera;
	uint meshletCount;

#ifdef USE_INSTANCING
	uint instanceCount;
#endif
};

[numthreads(AS_GROUP_SIZE, 1, 1)]
void mainAS(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID, uint gid : SV_GroupID)
{
	const uint meshletIndex = dtid % meshletCount;

	const bool meshletValid = meshletIndex < meshletCount;
	if (meshletValid)
		sPayload.meshletIndices[gtid] = meshletIndex;

#ifdef USE_INSTANCING
	const uint instanceIndex = dtid / meshletCount;
	
	const bool instanceValid = instanceIndex < instanceCount;
	if (instanceValid)
		sPayload.instanceIndices[gtid] = instanceIndex;

	const bool valid = meshletValid && instanceValid;
#else
	const bool valid = meshletValid;
#endif

	bool visible = valid;

	const uint visibleCount = WaveActiveCountBits(visible);
	DispatchMesh(visibleCount, 1, 1, sPayload);
}

//-------------------- Vertex Shader --------------------

struct VertexFactory
{
	float3 position : POSITION;

	float3 normal : NORMAL;

	float3 tangent : TANGENT;

	float2 uv : TEXCOORD;
};


struct VertexOutput
{
	/// Vertex world position
	float3 worldPosition : POSITION;

	/// Shader view position
	float4 svPosition : SV_POSITION;

	/// Camera view position.
	float3 viewPosition : VIEW_POSITION;


	/// TBN (tangent, bitangent, normal) transformation matrix.
	float3x3 TBN : TBN;


	/// Vertex UV
	float2 uv : TEXCOORD;

	float3 color : COLOR;
};

struct Object
{
	/// Object transformation matrix.
	float4x4 transform;
};
cbuffer ObjectBuffer : register(b1)
{
#if defined(USE_AMPLIFICATIONSHADER) && defined(USE_INSTANCING)
	Object objects[20 * 10];
#else
	Object object;
#endif
};

//-------------------- Mesh Shader --------------------

#define MAX_NUM_VERTS 252
#define MAX_NUM_PRIMS (MAX_NUM_VERTS / 3)

struct Meshlet
{
	uint vertexOffset;
	uint triangleOffset;
	uint vertexCount;
	uint triangleCount;
};

StructuredBuffer<Meshlet>        meshlets : register(t5); // meshletBuffer
StructuredBuffer<uint>      vertexIndices : register(t6); // meshletVerticesBuffer
StructuredBuffer<uint>    triangleIndices : register(t7); // meshletTrianglesBuffer
StructuredBuffer<VertexFactory>  vertices : register(t8); // positionBuffer

[numthreads(128, 1, 1)]
[outputtopology("triangle")]
#ifndef USE_AMPLIFICATIONSHADER
void mainMS(uint gtid : SV_GroupThreadID, uint gid : SV_GroupID, out vertices VertexOutput outVertices[MAX_NUM_VERTS], out indices uint3 outTriangles[MAX_NUM_PRIMS])
#else 
void mainMS(uint gtid : SV_GroupThreadID, uint gid : SV_GroupID, in payload Payload payload, out vertices VertexOutput outVertices[MAX_NUM_VERTS], out indices uint3 outTriangles[MAX_NUM_PRIMS])
#endif
{
#ifndef USE_AMPLIFICATIONSHADER
	uint meshletIndex = gid;
#else // USE_AMPLIFICATIONSHADER
	uint meshletIndex = payload.meshletIndices[gid];
#endif // USE_AMPLIFICATIONSHADER

#if defined(USE_AMPLIFICATIONSHADER) && defined(USE_INSTANCING)
	uint instanceIndex = payload.instanceIndices[gid];
	Object currentObject = objects[instanceIndex];
#else
	Object currentObject = object;
#endif

	Meshlet meshlet = meshlets[meshletIndex];

	SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);

	if (gtid < meshlet.triangleCount)
	{
		uint packedIdx = triangleIndices[meshlet.triangleOffset + gtid];
		uint vertIdx0 = (packedIdx >> 0) & 0xFF;
		uint vertIdx1 = (packedIdx >> 8) & 0xFF;
		uint vertIdx2 = (packedIdx >> 16) & 0xFF;
		outTriangles[gtid] = uint3(vertIdx0, vertIdx1, vertIdx2);
	}

	if (gtid < meshlet.vertexCount)
	{
		const uint vertexIndex = vertexIndices[meshlet.vertexOffset + gtid];

		const VertexFactory vertex = vertices[vertexIndex];

		const float4 worldPosition4 = mul(currentObject.transform, float4(vertex.position, 1.0));
		outVertices[gtid].worldPosition = worldPosition4.xyz / worldPosition4.w;
		outVertices[gtid].svPosition = mul(camera.invViewProj, worldPosition4);
		outVertices[gtid].viewPosition = float3(camera.view._14, camera.view._24, camera.view._34);

#ifdef USE_MESHLET_ID_AS_VERTEX_COLOR
		float3 meshletColor = float3(float(gid & 1), float(gid & 3) / 4, float(gid & 7) / 8);
		outVertices[gtid].color = meshletColor;
#else
		outVertices[gtid].color = float3(1.0, 1.0, 1.0);
#endif

		//---------- Normal ----------
		const float3 normal = normalize(vertex.normal);
		const float3 tangent = normalize(vertex.tangent);
		const float3 bitangent = cross(normal, tangent);

		/// HLSL uses row-major constructor: transpose to get TBN matrix.
		outVertices[gtid].TBN = transpose(float3x3(tangent, bitangent, normal));


		//---------- UV ----------
		outVertices[gtid].uv = float2(vertex.uv);
	}
}

//-------------------- Pixel Shader --------------------

struct PixelInput : VertexOutput
{
};

struct PixelOutput
{
	float4 color  : SV_TARGET;
};


// Constants.
static const float PI = 3.14159265359;


//---------- Bindings ----------
struct PointLight
{
	float3 position;

	float intensity;

	float3 color;

	float radius;
};

StructuredBuffer<PointLight> pointLights : register(t0);


Texture2D<float4> albedo : register(t1);
Texture2D<float3> normalMap : register(t2);
Texture2D<float> metallicMap : register(t3);
Texture2D<float> roughnessMap : register(t4);

SamplerState pbrSampler : register(s0); // Use same sampler for all textures.


//---------- Helper Functions ----------
float ComputeAttenuation(float3 _vLight, float _lightRange)
{
	const float distance = length(_vLight);

	return max(1 - (distance / _lightRange), 0.0);
}

float3 FresnelSchlick(float3 _f0, float _cosTheta)
{
	return _f0 + (1.0 - _f0) * pow(1.0 - _cosTheta, 5.0);
}

float DistributionGGX(float _cosAlpha, float _roughness)
{
	// Normal distribution function: GGX model.
	const float roughSqr = _roughness * _roughness;

	const float denom = _cosAlpha * _cosAlpha * (roughSqr - 1.0) + 1.0;

	return roughSqr / (PI * denom * denom);
}

float GeometrySchlickGGX(float _cosRho, float _roughness)
{
	// Geometry distribution function: GGX model.

	const float r = _roughness + 1.0;

	const float k = (r * r) / 8.0;

	return _cosRho / (_cosRho * (1.0 - k) + k);
}
  
float GeometrySmith(float _cosTheta, float _cosRho, float _roughness)
{
	float ggx1 = GeometrySchlickGGX(_cosRho, _roughness);
	float ggx2 = GeometrySchlickGGX(_cosTheta, _roughness);
	
	return ggx1 * ggx2;
}


//---------- Main ----------
PixelOutput mainPS(PixelInput _input)
{
	PixelOutput output;

#ifdef DISPLAY_VERTEX_COLOR_ONLY
	output.color = float4(_input.color, 1.0);

	return output;
#endif

	//---------- Base Color ----------
	const float4 baseColor = albedo.Sample(pbrSampler, _input.uv);

	if (baseColor.a < 0.001)
		discard;


	//---------- Normal ----------
	const float3 vnNormal = normalize(mul(_input.TBN, normalMap.Sample(pbrSampler, _input.uv) * 2.0f - 1.0f));

	//---------- Lighting ----------
	const float metallic = metallicMap.Sample(pbrSampler, _input.uv);
	const float roughness = roughnessMap.Sample(pbrSampler, _input.uv);
	const float3 vnCamera = normalize(_input.viewPosition - _input.worldPosition);
	const float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor.xyz, metallic);

	float3 finalColor = float3(0.0f, 0.0f, 0.0f);


	//----- Point Lights -----
	float3 sum = float3(0.0f, 0.0f, 0.0f);

	uint num;
	uint stride;
	pointLights.GetDimensions(num, stride);

	for(uint i = 0; i < num; ++i)
	{
		PointLight pLight = pointLights[i];

		const float3 vLight = pLight.position - _input.worldPosition;
		const float3 vnLight = normalize(vLight);

		//----- BRDF -----
		const float cosTheta = dot(vnNormal, vnLight);

		const float attenuation = ComputeAttenuation(vLight, pLight.radius);

		if (cosTheta > 0.0 && attenuation > 0.0)
		{
		//{ Specular Component

			// Halfway vector.
			const float3 vnHalf = normalize(vnLight + vnCamera);

			// Blinn-Phong variant. Phong formula is: dot(vnNormal, vnCamera)
			const float cosAlpha = dot(vnNormal, vnHalf);
			const float cosRho = dot(vnNormal, vnCamera);

			const float3 F = FresnelSchlick(f0, cosTheta);

			float3 specularBRDF = float3(0.0f, 0.0f, 0.0f);

			if(cosAlpha > 0.0 && cosRho > 0.0)
			{
				const float NDF = DistributionGGX(cosAlpha, roughness);
				const float G = GeometrySmith(cosTheta, cosRho, roughness);

				// Cook-Torrance specular BRDF.
				specularBRDF = (NDF * G * F) / (4.0 * cosTheta * cosRho);
			}

		//}


		//{ Diffuse Component

			const float3 kD = (float3(1.0f, 1.0f, 1.0f) - F) * (1.0 - metallic);

			// Lambert Diffuse.
			const float3 diffuseBRDF = kD * baseColor.xyz / PI;

		//}

			finalColor += (diffuseBRDF + specularBRDF) * cosTheta * attenuation * pLight.color * pLight.intensity;
		}
	}

	output.color = float4(finalColor, 1.0f);

	return output;
}
