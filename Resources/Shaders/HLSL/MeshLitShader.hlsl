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
};

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
cbuffer CameraBuffer : register(b0)
{
	Camera camera;
};

struct Object
{
	/// Object transformation matrix.
	float4x4 transform;
};
cbuffer ObjectBuffer : register(b1)
{
	Object object;
};

//-------------------- Mesh Shader --------------------

#define MAX_NUM_VERTS 252
#define MAX_NUM_PRIMS (MAX_NUM_VERTS / 3)

struct Meshlet
{
	float3 position;
};

StructuredBuffer<Meshlet> meshlets : register(t0);

[numthreads(1, 1, 1)]
[outputtopology("triangle")]
void mainMS(uint gtid : SV_GroupThreadID, uint gid : SV_GroupID, out vertices VertexOutput outVertices[MAX_NUM_VERTS], out indices uint3 outTriangles[MAX_NUM_PRIMS])
{
	SetMeshOutputCounts(MAX_NUM_VERTS, MAX_NUM_PRIMS);

	for (int i = 0; i < MAX_NUM_VERTS; i++)
	{
		VertexOutput vertOutput;
		outVertices[i] = vertOutput;
	}

	for (int i = 0; i < MAX_NUM_PRIMS; i++)
	{
		outTriangles[i] = uint3(0, 0, 0);
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
