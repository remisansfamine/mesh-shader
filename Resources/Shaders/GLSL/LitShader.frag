//-------------------- Pixel Shader --------------------

#version 450

layout(location = 0) in PixelInput
{
	/// Vertex world position
	vec3 worldPosition;

	/// Camera view position.
	vec3 viewPosition;


	/// TBN (tangent, bitangent, normal) transformation matrix.
	mat3 TBN;


	/// Vertex UV
	vec2 uv;
} fsIn;

layout(location = 0) out vec4 fsOut_color;


// Constants.
const float PI = 3.14159265359;


//---------- Bindings ----------
layout(binding = 2) uniform sampler2D albedo;
layout(binding = 3) uniform sampler2D normalMap;
layout(binding = 4) uniform sampler2D metallicMap;
layout(binding = 5) uniform sampler2D roughnessMap;

struct PointLight
{
	vec3 position;

	float intensity;

	vec3 color;

	float radius;
};

layout(binding = 6) readonly buffer pointLightBuffer
{
	PointLight lights[];
} pointLights;



//---------- Helper Functions ----------
float ComputeAttenuation(vec3 _vLight, float _lightRange)
{
	const float distance = length(_vLight);

	return max(1 - (distance / _lightRange), 0.0);
}

vec3 FresnelSchlick(vec3 _f0, float _cosTheta)
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
void main()
{
	//---------- Base Color ----------
	const vec4 baseColor = texture(albedo, fsIn.uv);

	if (baseColor.a < 0.001)
		discard;


	//---------- Normal ----------
	const vec3 vnNormal = normalize(fsIn.TBN * (texture(normalMap, fsIn.uv).rgb * 2.0f - 1.0f));

	//---------- Lighting ----------
	const float metallic = texture(metallicMap, fsIn.uv).r;
	const float roughness = texture(roughnessMap, fsIn.uv).r;
	const vec3 vnCamera = normalize(fsIn.viewPosition - fsIn.worldPosition);
	const vec3 f0 = mix(vec3(0.04, 0.04, 0.04), baseColor.xyz, metallic);

	vec3 finalColor = vec3(0.0f, 0.0f, 0.0f);


	//----- Point Lights -----
	vec3 sum = vec3(0.0f, 0.0f, 0.0f);

	const uint num = pointLights.lights.length();

	for(uint i = 0; i < num; ++i)
	{
		PointLight pLight = pointLights.lights[i];

		const vec3 vLight = pLight.position - fsIn.worldPosition;
		const vec3 vnLight = normalize(vLight);

		//----- BRDF -----
		const float cosTheta = dot(vnNormal, vnLight);

		const float attenuation = ComputeAttenuation(vLight, pLight.radius);

		if (cosTheta > 0.0 && attenuation > 0.0)
		{
		//{ Specular Component

			// Halfway vector.
			const vec3 vnHalf = normalize(vnLight + vnCamera);

			// Blinn-Phong variant. Phong formula is: dot(vnNormal, vnCamera)
			const float cosAlpha = dot(vnNormal, vnHalf);
			const float cosRho = dot(vnNormal, vnCamera);

			const vec3 F = FresnelSchlick(f0, cosTheta);

			vec3 specularBRDF = vec3(0.0f, 0.0f, 0.0f);

			if(cosAlpha > 0.0 && cosRho > 0.0)
			{
				const float NDF = DistributionGGX(cosAlpha, roughness);
				const float G = GeometrySmith(cosTheta, cosRho, roughness);

				// Cook-Torrance specular BRDF.
				specularBRDF = (NDF * G * F) / (4.0 * cosTheta * cosRho);
			}

		//}


		//{ Diffuse Component

			const vec3 kD = (vec3(1.0f, 1.0f, 1.0f) - F) * (1.0 - metallic);

			// Lambert Diffuse.
			const vec3 diffuseBRDF = kD * baseColor.xyz / PI;

		//}

			finalColor += (diffuseBRDF + specularBRDF) * cosTheta * attenuation * pLight.color * pLight.intensity;
		}
	}

	fsOut_color = vec4(finalColor, 1.0f);
}
