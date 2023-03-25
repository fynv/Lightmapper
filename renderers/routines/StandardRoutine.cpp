#include <GL/glew.h>
#include "models/ModelComponents.h"
#include "lights/DirectionalLight.h"
#include "StandardRoutine.h"

static std::string g_vertex =
R"(#version 430

#DEFINES#

#if HAS_INDICES
layout (location = LOCATION_ATTRIB_INDICES) in uint aInd;
#endif

#if HAS_LIGHTMAP
layout (location = LOCATION_ATTRIB_ATLAS_INDICES) in uint aAtlasInd;
#endif

layout (location = LOCATION_TEX_POS) uniform samplerBuffer uTexPos;
layout (location = LOCATION_TEX_VERT_NORM) uniform samplerBuffer uTexNorm;

layout (std140, binding = BINDING_CAMERA) uniform Camera
{
	mat4 uProjMat;
	mat4 uViewMat;	
	mat4 uInvProjMat;
	mat4 uInvViewMat;	
	vec3 uEyePos;
};

layout (std140, binding = BINDING_MODEL) uniform Model
{
	mat4 uModelMat;
	mat4 uNormalMat;
};

layout (location = LOCATION_VARYING_VIEWDIR) out vec3 vViewDir;
layout (location = LOCATION_VARYING_NORM) out vec3 vNorm;

#if HAS_COLOR
layout (location = LOCATION_TEX_VERT_COLOR) uniform samplerBuffer uTexVertCol;
layout (location = LOCATION_VARYING_COLOR) out vec4 vColor;
#endif

#if HAS_UV
layout (location = LOCATION_TEX_UV) uniform samplerBuffer uTexUV;
layout (location = LOCATION_VARYING_UV) out vec2 vUV;
#endif

#if HAS_LIGHTMAP
layout (location = LOCATION_TEX_ATLAS_UV) uniform samplerBuffer uTexAtlasUV;
layout (location = LOCATION_VARYING_ATLAS_UV) out vec2 vAtlasUV;
#endif

#if HAS_NORMAL_MAP
layout (location = LOCATION_TEX_TANGENT) uniform samplerBuffer uTexTangent;
layout (location = LOCATION_VARYING_TANGENT) out vec3 vTangent;
layout (location = LOCATION_TEX_BITANGENT) uniform samplerBuffer uTexBitangent;
layout (location = LOCATION_VARYING_BITANGENT) out vec3 vBitangent;
#endif

layout (location = LOCATION_VARYING_WORLD_POS) out vec3 vWorldPos;

void main()
{
#if HAS_INDICES
	int index = int(aInd);
#else
	int index = int(gl_VertexID);
#endif

	vec4 wolrd_pos = uModelMat * texelFetch(uTexPos, index);
	gl_Position = uProjMat*(uViewMat*wolrd_pos);
	vWorldPos = wolrd_pos.xyz;
	vViewDir = uEyePos - wolrd_pos.xyz;
	vec4 world_norm = uNormalMat * texelFetch(uTexNorm, index);
	vNorm = world_norm.xyz;

#if HAS_COLOR
	vColor = texelFetch(uTexVertCol, index);
#endif

#if HAS_UV
	vUV = texelFetch(uTexUV, index).xy;
#endif

#if HAS_NORMAL_MAP
	vec4 world_tangent = uModelMat * texelFetch(uTexTangent, index);
	vTangent =  world_tangent.xyz;

	vec4 world_bitangent = uModelMat * texelFetch(uTexBitangent, index);
	vBitangent =  world_bitangent.xyz;
#endif

#if HAS_LIGHTMAP
	int atlas_index = int(aAtlasInd);
	vAtlasUV = texelFetch(uTexAtlasUV, atlas_index).xy;
#endif
}
)";

static std::string g_frag_part0 =
R"(#version 430
#DEFINES#

layout (location = LOCATION_VARYING_VIEWDIR) in vec3 vViewDir;
layout (location = LOCATION_VARYING_NORM) in vec3 vNorm;

layout (std140, binding = BINDING_MATERIAL) uniform Material
{
	vec4 uColor;
	vec4 uEmissive;
	vec4 uSpecularGlossiness;
	vec2 uNormalScale;
	float uMetallicFactor;
	float uRoughnessFactor;
	float uAlphaCutoff;
	int uDoubleSided;
};


#if HAS_COLOR
layout (location = LOCATION_VARYING_COLOR) in vec4 vColor;
#endif

#if HAS_UV
layout (location = LOCATION_VARYING_UV) in vec2 vUV;
#endif

#if HAS_COLOR_TEX
layout (location = LOCATION_TEX_COLOR) uniform sampler2D uTexColor;
#endif

#if HAS_METALNESS_MAP
layout (location = LOCATION_TEX_METALNESS) uniform sampler2D uTexMetalness;
#endif

#if HAS_ROUGHNESS_MAP
layout (location = LOCATION_TEX_ROUGHNESS) uniform sampler2D uTexRoughness;
#endif

#if HAS_NORMAL_MAP
layout (location = LOCATION_TEX_NORMAL) uniform sampler2D uTexNormal;
layout (location = LOCATION_VARYING_TANGENT) in vec3 vTangent;
layout (location = LOCATION_VARYING_BITANGENT) in vec3 vBitangent;
#endif

layout (location = LOCATION_VARYING_WORLD_POS) in vec3 vWorldPos;

#if HAS_EMISSIVE_MAP
layout (location = LOCATION_TEX_EMISSIVE) uniform sampler2D uTexEmissive;
#endif

#if HAS_SPECULAR_MAP
layout (location = LOCATION_TEX_SPECULAR) uniform sampler2D uTexSpecular;
#endif

#if HAS_GLOSSINESS_MAP
layout (location = LOCATION_TEX_GLOSSINESS) uniform sampler2D uTexGlossiness;
#endif

struct IncidentLight {
	vec3 color;
	vec3 direction;
	bool visible;
};

#define EPSILON 1e-6
#define PI 3.14159265359
#define RECIPROCAL_PI 0.3183098861837907

#ifndef saturate
#define saturate( a ) clamp( a, 0.0, 1.0 )
#endif

float pow2( const in float x ) { return x*x; }

struct PhysicalMaterial 
{
	vec3 diffuseColor;
	float roughness;
	vec3 specularColor;
	float specularF90;
};


vec3 F_Schlick(const in vec3 f0, const in float f90, const in float dotVH) 
{
	float fresnel = exp2( ( - 5.55473 * dotVH - 6.98316 ) * dotVH );
	return f0 * ( 1.0 - fresnel ) + ( f90 * fresnel );
}

float V_GGX_SmithCorrelated( const in float alpha, const in float dotNL, const in float dotNV ) 
{
	float a2 = pow2( alpha );
	float gv = dotNL * sqrt( a2 + ( 1.0 - a2 ) * pow2( dotNV ) );
	float gl = dotNV * sqrt( a2 + ( 1.0 - a2 ) * pow2( dotNL ) );
	return 0.5 / max( gv + gl, EPSILON );
}

float D_GGX( const in float alpha, const in float dotNH ) 
{
	float a2 = pow2( alpha );
	float denom = pow2( dotNH ) * ( a2 - 1.0 ) + 1.0; 
	return RECIPROCAL_PI * a2 / pow2( denom );
}

vec3 BRDF_Lambert(const in vec3 diffuseColor) 
{
	return RECIPROCAL_PI * diffuseColor;
}

vec3 BRDF_GGX( const in vec3 lightDir, const in vec3 viewDir, const in vec3 normal, const in vec3 f0, const in float f90, const in float roughness ) 
{
	float alpha = pow2(roughness);

	vec3 halfDir = normalize(lightDir + viewDir);

	float dotNL = saturate(dot(normal, lightDir));
	float dotNV = saturate(dot(normal, viewDir));
	float dotNH = saturate(dot(normal, halfDir));
	float dotVH = saturate(dot(viewDir, halfDir));

	vec3 F = F_Schlick(f0, f90, dotVH);
	float V = V_GGX_SmithCorrelated(alpha, dotNL, dotNV);
	float D = D_GGX( alpha, dotNH );
	return F*(V*D);
}


struct DirectionalLight
{	
	vec4 color;
	vec4 origin;
	vec4 direction;
	int has_shadow;
};


#if NUM_DIRECTIONAL_LIGHTS>0
layout (std140, binding = BINDING_DIRECTIONAL_LIGHTS) uniform DirectionalLights
{
	DirectionalLight uDirectionalLights[NUM_DIRECTIONAL_LIGHTS];
};
#endif
)";

static std::string g_frag_part1 =
R"(
#if NUM_DIRECTIONAL_SHADOWS>0
struct DirectionalShadow
{
	mat4 VPSBMat;
	mat4 projMat;
	mat4 viewMat;
    vec2 leftRight;
	vec2 bottomTop;
	vec2 nearFar;
	float lightRadius;
	float padding;
};

layout (std140, binding = BINDING_DIRECTIONAL_SHADOWS) uniform DirectionalShadows
{
	DirectionalShadow uDirectionalShadows[NUM_DIRECTIONAL_SHADOWS];
};

layout (location = LOCATION_TEX_DIRECTIONAL_SHADOW) uniform sampler2DShadow uDirectionalShadowTex[NUM_DIRECTIONAL_SHADOWS];
layout (location = LOCATION_TEX_DIRECTIONAL_SHADOW_DEPTH) uniform sampler2D uDirectionalShadowTexDepth[NUM_DIRECTIONAL_SHADOWS];



vec3 computeShadowCoords(in mat4 VPSB)
{
	vec4 shadowCoords = VPSB * vec4(vWorldPos, 1.0);
	return shadowCoords.xyz;
}

float borderDepthTexture(sampler2D shadowTex, vec2 uv)
{
	return ((uv.x <= 1.0) && (uv.y <= 1.0) &&
	 (uv.x >= 0.0) && (uv.y >= 0.0)) ? textureLod(shadowTex, uv, 0.0).x : 1.0;
}

float borderPCFTexture(sampler2DShadow shadowTex, vec3 uvz)
{
	return ((uvz.x <= 1.0) && (uvz.y <= 1.0) &&
	 (uvz.x >= 0.0) && (uvz.y >= 0.0)) ? texture(shadowTex, uvz) : 
	 ((uvz.z <= 1.0) ? 1.0 : 0.0);
}


float computeShadowCoef(in mat4 VPSB, sampler2DShadow shadowTex)
{
	vec3 shadowCoords;
	shadowCoords = computeShadowCoords(VPSB);
	return borderPCFTexture(shadowTex, shadowCoords);
}

const vec2 Poisson32[32] = vec2[](
    vec2(-0.975402, -0.0711386),
    vec2(-0.920347, -0.41142),
    vec2(-0.883908, 0.217872),
    vec2(-0.884518, 0.568041),
    vec2(-0.811945, 0.90521),
    vec2(-0.792474, -0.779962),
    vec2(-0.614856, 0.386578),
    vec2(-0.580859, -0.208777),
    vec2(-0.53795, 0.716666),
    vec2(-0.515427, 0.0899991),
    vec2(-0.454634, -0.707938),
    vec2(-0.420942, 0.991272),
    vec2(-0.261147, 0.588488),
    vec2(-0.211219, 0.114841),
    vec2(-0.146336, -0.259194),
    vec2(-0.139439, -0.888668),
    vec2(0.0116886, 0.326395),
    vec2(0.0380566, 0.625477),
    vec2(0.0625935, -0.50853),
    vec2(0.125584, 0.0469069),
    vec2(0.169469, -0.997253),
    vec2(0.320597, 0.291055),
    vec2(0.359172, -0.633717),
    vec2(0.435713, -0.250832),
    vec2(0.507797, -0.916562),
    vec2(0.545763, 0.730216),
    vec2(0.56859, 0.11655),
    vec2(0.743156, -0.505173),
    vec2(0.736442, -0.189734),
    vec2(0.843562, 0.357036),
    vec2(0.865413, 0.763726),
    vec2(0.872005, -0.927)
);

const vec2 Poisson64[64] = vec2[](
    vec2(-0.934812, 0.366741),
    vec2(-0.918943, -0.0941496),
    vec2(-0.873226, 0.62389),
    vec2(-0.8352, 0.937803),
    vec2(-0.822138, -0.281655),
    vec2(-0.812983, 0.10416),
    vec2(-0.786126, -0.767632),
    vec2(-0.739494, -0.535813),
    vec2(-0.681692, 0.284707),
    vec2(-0.61742, -0.234535),
    vec2(-0.601184, 0.562426),
    vec2(-0.607105, 0.847591),
    vec2(-0.581835, -0.00485244),
    vec2(-0.554247, -0.771111),
    vec2(-0.483383, -0.976928),
    vec2(-0.476669, -0.395672),
    vec2(-0.439802, 0.362407),
    vec2(-0.409772, -0.175695),
    vec2(-0.367534, 0.102451),
    vec2(-0.35313, 0.58153),
    vec2(-0.341594, -0.737541),
    vec2(-0.275979, 0.981567),
    vec2(-0.230811, 0.305094),
    vec2(-0.221656, 0.751152),
    vec2(-0.214393, -0.0592364),
    vec2(-0.204932, -0.483566),
    vec2(-0.183569, -0.266274),
    vec2(-0.123936, -0.754448),
    vec2(-0.0859096, 0.118625),
    vec2(-0.0610675, 0.460555),
    vec2(-0.0234687, -0.962523),
    vec2(-0.00485244, -0.373394),
    vec2(0.0213324, 0.760247),
    vec2(0.0359813, -0.0834071),
    vec2(0.0877407, -0.730766),
    vec2(0.14597, 0.281045),
    vec2(0.18186, -0.529649),
    vec2(0.188208, -0.289529),
    vec2(0.212928, 0.063509),
    vec2(0.23661, 0.566027),
    vec2(0.266579, 0.867061),
    vec2(0.320597, -0.883358),
    vec2(0.353557, 0.322733),
    vec2(0.404157, -0.651479),
    vec2(0.410443, -0.413068),
    vec2(0.413556, 0.123325),
    vec2(0.46556, -0.176183),
    vec2(0.49266, 0.55388),
    vec2(0.506333, 0.876888),
    vec2(0.535875, -0.885556),
    vec2(0.615894, 0.0703452),
    vec2(0.637135, -0.637623),
    vec2(0.677236, -0.174291),
    vec2(0.67626, 0.7116),
    vec2(0.686331, -0.389935),
    vec2(0.691031, 0.330729),
    vec2(0.715629, 0.999939),
    vec2(0.8493, -0.0485549),
    vec2(0.863582, -0.85229),
    vec2(0.890622, 0.850581),
    vec2(0.898068, 0.633778),
    vec2(0.92053, -0.355693),
    vec2(0.933348, -0.62981),
    vec2(0.95294, 0.156896)
);

// Returns average blocker depth in the search region, as well as the number of found blockers.
// Blockers are defined as shadow-map samples between the surface point and the light.
void findBlocker(
	sampler2D shadowTex,
    out float accumBlockerDepth, 
    out float numBlockers,
    out float maxBlockers,
    vec2 uv,
    float z,
    vec2 searchRegionRadiusUV)
{
    accumBlockerDepth = 0.0;
    numBlockers = 0.0;
	maxBlockers = 300.0;

	// case POISSON_32_64:
    {
        maxBlockers = 32.0;
        for (int i = 0; i < 32; ++i)
        {
            vec2 offset = Poisson32[i] * searchRegionRadiusUV;
            float shadowMapDepth = borderDepthTexture(shadowTex, uv + offset);
            if (shadowMapDepth < z)
            {
                accumBlockerDepth += shadowMapDepth;
                numBlockers++;
            }
        }
    }

}

float zClipToEye(in DirectionalShadow shadow, float z)
{
    return (shadow.nearFar.x + (shadow.nearFar.y - shadow.nearFar.x) * z);
}

// Using similar triangles between the area light, the blocking plane and the surface point
vec2 penumbraRadiusUV(vec2 light_radius_uv, float zReceiver, float zBlocker)
{
    return light_radius_uv * (zReceiver - zBlocker);
}


// Performs PCF filtering on the shadow map using multiple taps in the filter region.
float pcfFilter(sampler2DShadow shadowTex, vec2 uv, float z, vec2 filterRadiusUV)
{
    float sum = 0.0;

	//case POISSON_32_64:
    {
        for (int i = 0; i < 64; ++i)
        {
            vec2 offset = Poisson64[i] * filterRadiusUV;        
            sum += borderPCFTexture(shadowTex, vec3(uv + offset, z));
        }
        return sum / 64.0;
    }
}

float pcssShadow( sampler2DShadow shadowTex, sampler2D shadowTexDepth, in DirectionalShadow shadow, vec2 uv, float z, float zEye)
{
    // ------------------------
    // STEP 1: blocker search
    // ------------------------
    float accumBlockerDepth, numBlockers, maxBlockers;
    
    vec2 frustum_size = vec2(shadow.leftRight.y - shadow.leftRight.x, shadow.bottomTop.y - shadow.bottomTop.x);
    vec2 light_radius_uv = vec2(shadow.lightRadius) / frustum_size;
    vec2 searchRegionRadiusUV = light_radius_uv* (zEye - shadow.nearFar.x);
	findBlocker(shadowTexDepth, accumBlockerDepth, numBlockers, maxBlockers, uv, z, searchRegionRadiusUV);

    // Early out if not in the penumbra
    if (numBlockers == 0.0)
        return 1.0;

    // ------------------------
    // STEP 2: penumbra size
    // ------------------------
    float avgBlockerDepth = accumBlockerDepth / numBlockers;
    float avgBlockerDepthWorld = zClipToEye(shadow, avgBlockerDepth);
    
    vec2 penumbraRadius = penumbraRadiusUV(light_radius_uv, zEye, avgBlockerDepthWorld);
    
	return pcfFilter(shadowTex, uv, z, penumbraRadius);
}


float computePCSSShadowCoef(in DirectionalShadow shadow, sampler2DShadow shadowTex, sampler2D shadowTexDepth)
{	
	vec3 uvz = computeShadowCoords(shadow.VPSBMat);	
	float zEye = -(shadow.viewMat * vec4(vWorldPos, 1.0)).z;
	return pcssShadow(shadowTex, shadowTexDepth, shadow, uvz.xy, uvz.z, zEye);
}
#endif

#if HAS_LIGHTMAP
layout (location = LOCATION_VARYING_ATLAS_UV) in vec2 vAtlasUV;
layout (location = LOCATION_TEX_LIGHTMAP) uniform sampler2D uTexLightmap;
#endif
)";

static std::string g_frag_part2 =
R"(
layout (location = 0) out vec4 out0;

#if ALPHA_BLEND
layout (location = 1) out float out1;
#endif

void main()
{
	vec4 base_color = uColor;
#if HAS_COLOR
	base_color *= vColor;
#endif

	float tex_alpha = 1.0;

#if HAS_COLOR_TEX
	vec4 tex_color = texture(uTexColor, vUV);
	tex_alpha = tex_color.w;
	base_color *= tex_color;
#endif
    
#if ALPHA_MASK
	base_color.w = base_color.w > uAlphaCutoff ? 1.0 : 0.0;
#endif

#if ALPHA_MASK || ALPHA_BLEND
	if (base_color.w == 0.0) discard;
#endif

#if SPECULAR_GLOSSINESS

	vec3 specularFactor = uSpecularGlossiness.xyz;
#if HAS_SPECULAR_MAP	
	specularFactor *= texture( uTexSpecular, vUV ).xyz;
#endif
	float glossinessFactor = uSpecularGlossiness.w;
#if HAS_GLOSSINESS_MAP
	glossinessFactor *= texture( uTexGlossiness, vUV ).w;
#endif

#else

	float metallicFactor = uMetallicFactor;
	float roughnessFactor = uRoughnessFactor;

#if HAS_METALNESS_MAP
	metallicFactor *= texture(uTexMetalness, vUV).z;
#endif

#if HAS_ROUGHNESS_MAP
	roughnessFactor *= texture(uTexRoughness, vUV).y;
#endif

#endif

	vec3 viewDir = normalize(vViewDir);
	vec3 norm = normalize(vNorm);	

#if HAS_NORMAL_MAP
	if (length(vTangent)>0.0 && length(vBitangent)>0.0)
	{
		vec3 T = normalize(vTangent);
		vec3 B = normalize(vBitangent);
		vec3 bump =  texture(uTexNormal, vUV).xyz;
		bump = (2.0 * bump - 1.0) * vec3(uNormalScale.x, uNormalScale.y, 1.0);
		norm = normalize(bump.x*T + bump.y*B + bump.z*norm);
	}
#endif	

    if (uDoubleSided!=0 && !gl_FrontFacing)
	{		
		norm = -norm;
	}

    PhysicalMaterial material;

#if SPECULAR_GLOSSINESS
	material.diffuseColor = base_color.xyz * ( 1.0 -
                          max( max( specularFactor.r, specularFactor.g ), specularFactor.b ) );
	material.roughness = max( 1.0 - glossinessFactor, 0.0525 );	
	material.specularColor = specularFactor.rgb;
#else
	material.diffuseColor = base_color.xyz * ( 1.0 - metallicFactor );	
	material.roughness = max( roughnessFactor, 0.0525 );	
	material.specularColor = mix( vec3( 0.04 ), base_color.xyz, metallicFactor );	
#endif

	vec3 dxy = max(abs(dFdx(norm)), abs(dFdy(norm)));
	float geometryRoughness = max(max(dxy.x, dxy.y), dxy.z);	
	material.roughness += geometryRoughness;
	material.roughness = min( material.roughness, 1.0 );
	material.specularF90 = 1.0;

    vec3 emissive = uEmissive.xyz;
#if HAS_EMISSIVE_MAP
	emissive *= texture(uTexEmissive, vUV).xyz;
#endif

    vec3 specular = vec3(0.0);
	vec3 diffuse = vec3(0.0);

#if NUM_DIRECTIONAL_LIGHTS>0
	int shadow_id = 0;
	for (int i=0; i< NUM_DIRECTIONAL_LIGHTS; i++)
	{	
		DirectionalLight light_source = uDirectionalLights[i];
		float l_shadow = 1.0;
#if NUM_DIRECTIONAL_SHADOWS>0
		if (light_source.has_shadow!=0)
		{
			DirectionalShadow shadow = uDirectionalShadows[shadow_id];
			if (shadow.lightRadius>0.0)
			{
				l_shadow = computePCSSShadowCoef(shadow, uDirectionalShadowTex[shadow_id], uDirectionalShadowTexDepth[shadow_id]);
			}
			else
			{
				l_shadow = computeShadowCoef(shadow.VPSBMat, uDirectionalShadowTex[shadow_id]);
			}
			shadow_id++;
		}
#endif
		IncidentLight directLight = IncidentLight(light_source.color.xyz * l_shadow, light_source.direction.xyz, true);	

		float dotNL =  saturate(dot(norm, directLight.direction));
		vec3 irradiance = dotNL * directLight.color;
        diffuse += irradiance * BRDF_Lambert( material.diffuseColor );
        specular += irradiance * BRDF_GGX( directLight.direction, viewDir, norm, material.specularColor, material.specularF90, material.roughness );
    }
#endif

#if HAS_LIGHTMAP
	{
		vec4 lm = texture(uTexLightmap, vAtlasUV);
		vec3 light_color = lm.w>0.0 ? lm.xyz/lm.w : vec3(0.0);
		diffuse += material.diffuseColor * light_color;
		specular += material.specularColor * light_color;
	}
#endif

    vec3 col = emissive + specular;
#if !IS_HIGHTLIGHT
	col += diffuse;
#endif
//	col = clamp(col, 0.0, 1.0);	

#if ALPHA_BLEND
	float alpha = base_color.w;
	float a = min(1.0, alpha) * 8.0 + 0.01;
	float b = -gl_FragCoord.z * 0.95 + 1.0;
	float weight = clamp(a * a * a * 1e8 * b * b * b, 1e-2, 3e2);
	out0 = vec4(col * alpha, alpha) * weight;
	out1 = alpha;
#elif IS_HIGHTLIGHT
	out0 = vec4(col*tex_alpha, 0.0);
#else
	out0 = vec4(col, 1.0);
#endif
}
)";

inline void replace(std::string& str, const char* target, const char* source)
{
	int start = 0;
	size_t target_len = strlen(target);
	size_t source_len = strlen(source);
	while (true)
	{
		size_t pos = str.find(target, start);
		if (pos == std::string::npos) break;
		str.replace(pos, target_len, source);
		start = pos + source_len;
	}
}

void StandardRoutine::s_generate_shaders(const Options& options, Bindings& bindings, std::string& s_vertex, std::string& s_frag)
{
	s_vertex = g_vertex;
	s_frag = g_frag_part0 + g_frag_part1 + g_frag_part2;

	std::string defines = "";

	if (options.has_indices)
	{		
		defines += "#define HAS_INDICES 1\n";
		bindings.location_attrib_indices = 0;
		{
			char line[64];
			sprintf(line, "#define LOCATION_ATTRIB_INDICES %d\n", bindings.location_attrib_indices);
			defines += line;
		}
	}
	else
	{	
		defines += "#define HAS_INDICES 0\n";
		bindings.location_attrib_indices = -1;
	}
	
	if (options.has_lightmap)
	{
		defines += "#define HAS_LIGHTMAP 1\n";
		bindings.location_attrib_atlas_indices = bindings.location_attrib_indices + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_ATTRIB_ATLAS_INDICES %d\n", bindings.location_attrib_atlas_indices);
			defines += line;
		}

	}
	else
	{
		defines += "#define HAS_LIGHTMAP 0\n";
		bindings.location_attrib_atlas_indices = bindings.location_attrib_indices;
	}
	
	{
		bindings.location_tex_pos = 0;
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_POS %d\n", bindings.location_tex_pos);
			defines += line;
		}
	}

	{
		bindings.location_tex_vert_norm = bindings.location_tex_pos + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_VERT_NORM %d\n", bindings.location_tex_vert_norm);
			defines += line;
		}
	}

	{
		bindings.binding_camera = 0;
		{
			char line[64];
			sprintf(line, "#define BINDING_CAMERA %d\n", bindings.binding_camera);
			defines += line;
		}
	}

	{
		bindings.binding_model = bindings.binding_camera + 1;
		{
			char line[64];
			sprintf(line, "#define BINDING_MODEL %d\n", bindings.binding_model);
			defines += line;
		}
	}

	{
		bindings.location_varying_viewdir = 0;
		{
			char line[64];
			sprintf(line, "#define LOCATION_VARYING_VIEWDIR %d\n", bindings.location_varying_viewdir);
			defines += line;
		}
	}

	{
		bindings.location_varying_norm = bindings.location_varying_viewdir + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_VARYING_NORM %d\n", bindings.location_varying_norm);
			defines += line;
		}
	}

	if (options.alpha_mode == AlphaMode::Opaque)
	{
		defines += "#define IS_OPAQUE 1\n";
	}
	else
	{
		defines += "#define IS_OPAQUE 0\n";
	}

	if (options.alpha_mode == AlphaMode::Mask)
	{
		defines += "#define ALPHA_MASK 1\n";
	}
	else
	{
		defines += "#define ALPHA_MASK 0\n";
	}

	if (options.alpha_mode == AlphaMode::Blend)
	{
		if (options.is_highlight_pass)
		{
			defines += "#define ALPHA_BLEND 0\n";
			defines += "#define IS_HIGHTLIGHT 1\n";
		}
		else
		{
			defines += "#define ALPHA_BLEND 1\n";
			defines += "#define IS_HIGHTLIGHT 0\n";
		}
	}
	else
	{
		defines += "#define ALPHA_BLEND 0\n";
		defines += "#define IS_HIGHTLIGHT 0\n";
	}

	if (options.specular_glossiness)
	{
		defines += "#define SPECULAR_GLOSSINESS 1\n";
	}
	else
	{
		defines += "#define SPECULAR_GLOSSINESS 0\n";
	}

	{
		bindings.binding_material = bindings.binding_model + 1;
		{
			char line[64];
			sprintf(line, "#define BINDING_MATERIAL %d\n", bindings.binding_material);
			defines += line;
		}
	}

	if (options.has_color)
	{
		defines += "#define HAS_COLOR 1\n";
		bindings.location_tex_vert_color = bindings.location_tex_vert_norm + 1;
		bindings.location_varying_color = bindings.location_varying_norm + 1;

		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_VERT_COLOR %d\n", bindings.location_tex_vert_color);
			defines += line;
		}

		{
			char line[64];
			sprintf(line, "#define LOCATION_VARYING_COLOR %d\n", bindings.location_varying_color);
			defines += line;
		}
	}
	else
	{
		defines += "#define HAS_COLOR 0\n";
		bindings.location_tex_vert_color = bindings.location_tex_vert_norm;
		bindings.location_varying_color = bindings.location_varying_norm;
	}

	bool has_uv = options.has_color_texture || options.has_metalness_map || options.has_roughness_map
		|| options.has_normal_map || options.has_emissive_map || options.has_specular_map || options.has_glossiness_map;

	if (has_uv)
	{
		defines += "#define HAS_UV 1\n";
		bindings.location_tex_uv = bindings.location_tex_vert_color + 1;
		bindings.location_varying_uv = bindings.location_varying_color + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_UV %d\n", bindings.location_tex_uv);
			defines += line;
		}
		{
			char line[64];
			sprintf(line, "#define LOCATION_VARYING_UV %d\n", bindings.location_varying_uv);
			defines += line;
		}
	}
	else
	{
		defines += "#define HAS_UV 0\n";
		bindings.location_tex_uv = bindings.location_tex_vert_color;
		bindings.location_varying_uv = bindings.location_varying_color;
	}

	if (options.has_lightmap)
	{
		bindings.location_tex_atlas_uv = bindings.location_tex_uv + 1;
		bindings.location_varying_atlas_uv = bindings.location_varying_uv + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_ATLAS_UV %d\n", bindings.location_tex_atlas_uv);
			defines += line;
		}
		{
			char line[64];
			sprintf(line, "#define LOCATION_VARYING_ATLAS_UV %d\n", bindings.location_varying_atlas_uv);
			defines += line;
		}
	}
	else
	{
		bindings.location_tex_atlas_uv = bindings.location_tex_uv;
		bindings.location_varying_atlas_uv = bindings.location_varying_uv;
	}

	if (options.has_color_texture)
	{
		defines += "#define HAS_COLOR_TEX 1\n";

		bindings.location_tex_color = bindings.location_tex_atlas_uv + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_COLOR %d\n", bindings.location_tex_color);
			defines += line;
		}
	}
	else
	{
		defines += "#define HAS_COLOR_TEX 0\n";
		bindings.location_tex_color = bindings.location_tex_atlas_uv;
	}

	if (options.has_metalness_map)
	{
		defines += "#define HAS_METALNESS_MAP 1\n";

		bindings.location_tex_metalness = bindings.location_tex_color + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_METALNESS %d\n", bindings.location_tex_metalness);
			defines += line;
		}
	}
	else
	{
		defines += "#define HAS_METALNESS_MAP 0\n";
		bindings.location_tex_metalness = bindings.location_tex_color;
	}

	if (options.has_roughness_map)
	{
		defines += "#define HAS_ROUGHNESS_MAP 1\n";
		bindings.location_tex_roughness = bindings.location_tex_metalness + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_ROUGHNESS %d\n", bindings.location_tex_roughness);
			defines += line;
		}
	}
	else
	{
		defines += "#define HAS_ROUGHNESS_MAP 0\n";
		bindings.location_tex_roughness = bindings.location_tex_metalness;
	}

	if (options.has_normal_map)
	{
		defines += "#define HAS_NORMAL_MAP 1\n";
		bindings.location_tex_normal = bindings.location_tex_roughness + 1;
		bindings.location_tex_tangent = bindings.location_tex_normal + 1;
		bindings.location_varying_tangent = bindings.location_varying_atlas_uv + 1;
		bindings.location_tex_bitangent = bindings.location_tex_tangent + 1;
		bindings.location_varying_bitangent = bindings.location_varying_tangent + 1;

		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_NORMAL %d\n", bindings.location_tex_normal);
			defines += line;
		}

		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_TANGENT %d\n", bindings.location_tex_tangent);
			defines += line;
		}
		{
			char line[64];
			sprintf(line, "#define LOCATION_VARYING_TANGENT %d\n", bindings.location_varying_tangent);
			defines += line;
		}
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_BITANGENT %d\n", bindings.location_tex_bitangent);
			defines += line;
		}
		{
			char line[64];
			sprintf(line, "#define LOCATION_VARYING_BITANGENT %d\n", bindings.location_varying_bitangent);
			defines += line;
		}
	}
	else
	{
		defines += "#define HAS_NORMAL_MAP 0\n";
		bindings.location_tex_normal = bindings.location_tex_roughness;
		bindings.location_tex_tangent = bindings.location_tex_normal;
		bindings.location_varying_tangent = bindings.location_varying_atlas_uv;
		bindings.location_tex_bitangent = bindings.location_tex_tangent;
		bindings.location_varying_bitangent = bindings.location_varying_tangent;
	}

	{
		bindings.location_varying_world_pos = bindings.location_varying_bitangent + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_VARYING_WORLD_POS %d\n", bindings.location_varying_world_pos);
			defines += line;
		}
	}

	if (options.has_emissive_map)
	{
		defines += "#define HAS_EMISSIVE_MAP 1\n";
		bindings.location_tex_emissive = bindings.location_tex_bitangent + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_EMISSIVE %d\n", bindings.location_tex_emissive);
			defines += line;
		}
	}
	else
	{
		defines += "#define HAS_EMISSIVE_MAP 0\n";
		bindings.location_tex_emissive = bindings.location_tex_bitangent;
	}

	if (options.has_specular_map)
	{
		defines += "#define HAS_SPECULAR_MAP 1\n";
		bindings.location_tex_specular = bindings.location_tex_emissive + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_SPECULAR %d\n", bindings.location_tex_specular);
			defines += line;
		}
	}
	else
	{
		defines += "#define HAS_SPECULAR_MAP 0\n";
		bindings.location_tex_specular = bindings.location_tex_emissive;
	}

	if (options.has_glossiness_map)
	{
		defines += "#define HAS_GLOSSINESS_MAP 1\n";
		bindings.location_tex_glossiness = bindings.location_tex_specular + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_GLOSSINESS %d\n", bindings.location_tex_glossiness);
			defines += line;
		}
	}
	else
	{
		defines += "#define HAS_GLOSSINESS_MAP 0\n";
		bindings.location_tex_glossiness = bindings.location_tex_specular;
	}

	{
		char line[64];
		sprintf(line, "#define NUM_DIRECTIONAL_LIGHTS %d\n", options.num_directional_lights);
		defines += line;
	}

	if (options.num_directional_lights > 0)
	{
		bindings.binding_directional_lights = bindings.binding_material + 1;
		{
			char line[64];
			sprintf(line, "#define BINDING_DIRECTIONAL_LIGHTS %d\n", bindings.binding_directional_lights);
			defines += line;
		}
	}
	else
	{
		bindings.binding_directional_lights = bindings.binding_material;
	}

	{
		char line[64];
		sprintf(line, "#define NUM_DIRECTIONAL_SHADOWS %d\n", options.num_directional_shadows);
		defines += line;
	}

	bindings.location_tex_directional_shadow = bindings.location_tex_glossiness + options.num_directional_shadows;
	bindings.location_tex_directional_shadow_depth = bindings.location_tex_directional_shadow + options.num_directional_shadows;

	if (options.num_directional_shadows > 0)
	{
		bindings.binding_directional_shadows = bindings.binding_directional_lights + 1;
		{
			char line[64];
			sprintf(line, "#define BINDING_DIRECTIONAL_SHADOWS %d\n", bindings.binding_directional_shadows);
			defines += line;
		}
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_DIRECTIONAL_SHADOW %d\n", bindings.location_tex_directional_shadow - options.num_directional_shadows + 1);
			defines += line;
		}
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_DIRECTIONAL_SHADOW_DEPTH %d\n", bindings.location_tex_directional_shadow_depth - options.num_directional_shadows + 1);
			defines += line;
		}
	}
	else
	{
		bindings.binding_directional_shadows = bindings.binding_directional_lights;
	}

	if (options.has_lightmap)
	{		
		bindings.location_tex_lightmap = bindings.location_tex_directional_shadow_depth + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_LIGHTMAP %d\n", bindings.location_tex_lightmap);
			defines += line;
		}
	}
	else
	{
		bindings.location_tex_lightmap = bindings.location_tex_directional_shadow_depth;
	}

	replace(s_vertex, "#DEFINES#", defines.c_str());
	replace(s_frag, "#DEFINES#", defines.c_str());
}

StandardRoutine::StandardRoutine(const Options& options) : m_options(options)
{
	std::string s_vertex, s_frag;
	s_generate_shaders(options, m_bindings, s_vertex, s_frag);

	GLShader vert_shader(GL_VERTEX_SHADER, s_vertex.c_str());
	GLShader frag_shader(GL_FRAGMENT_SHADER, s_frag.c_str());
	m_prog = (std::unique_ptr<GLProgram>)(new GLProgram(vert_shader, frag_shader));
}


void StandardRoutine::_render_common(const RenderParams& params)
{
	const MeshStandardMaterial& material = *(MeshStandardMaterial*)params.material_list[params.primitive->material_idx];
	const GeometrySet& geo = params.primitive->geometry[params.primitive->geometry.size() - 1];

	glBindBufferBase(GL_UNIFORM_BUFFER, m_bindings.binding_camera, params.constant_camera->m_id);
	glBindBufferBase(GL_UNIFORM_BUFFER, m_bindings.binding_model, params.constant_model->m_id);
	glBindBufferBase(GL_UNIFORM_BUFFER, m_bindings.binding_material, material.constant_material.m_id);

	if (m_options.num_directional_lights > 0)
	{
		glBindBufferBase(GL_UNIFORM_BUFFER, m_bindings.binding_directional_lights, params.lights->constant_directional_lights->m_id);
	}

	if (m_options.num_directional_shadows > 0)
	{
		glBindBufferBase(GL_UNIFORM_BUFFER, m_bindings.binding_directional_shadows, params.lights->constant_directional_shadows->m_id);
	}

	int texture_idx = 0;
	{
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_BUFFER, geo.pos_buf->tex_id);
		glUniform1i(m_bindings.location_tex_pos, texture_idx);
		texture_idx++;
	}

	{
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_BUFFER, geo.normal_buf->tex_id);
		glUniform1i(m_bindings.location_tex_vert_norm, texture_idx);
		texture_idx++;
	}
	
	if (m_options.has_color)
	{
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_BUFFER, params.primitive->color_buf->tex_id);
		glUniform1i(m_bindings.location_tex_vert_color, texture_idx);
		texture_idx++;
	}

	bool has_uv = m_options.has_color_texture || m_options.has_metalness_map || m_options.has_roughness_map
		|| m_options.has_normal_map || m_options.has_emissive_map || m_options.has_specular_map || m_options.has_glossiness_map;
	if (has_uv)
	{
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_BUFFER, params.primitive->uv_buf->tex_id);
		glUniform1i(m_bindings.location_tex_uv, texture_idx);
		texture_idx++;
	}

	if (m_options.has_lightmap)
	{
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_BUFFER, params.primitive->lightmap_uv_buf->tex_id);
		glUniform1i(m_bindings.location_tex_atlas_uv, texture_idx);
		texture_idx++;
	}


	if (m_options.has_normal_map)
	{
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_BUFFER, geo.tangent_buf->tex_id);
		glUniform1i(m_bindings.location_tex_tangent, texture_idx);
		texture_idx++;

		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_BUFFER, geo.bitangent_buf->tex_id);
		glUniform1i(m_bindings.location_tex_bitangent, texture_idx);
		texture_idx++;
	}

	if (m_options.has_color_texture)
	{
		const GLTexture2D& tex = *params.tex_list[material.tex_idx_map];
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_2D, tex.tex_id);
		glUniform1i(m_bindings.location_tex_color, texture_idx);
		texture_idx++;
	}

	if (m_options.has_metalness_map)
	{
		const GLTexture2D& tex = *params.tex_list[material.tex_idx_metalnessMap];
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_2D, tex.tex_id);
		glUniform1i(m_bindings.location_tex_metalness, texture_idx);
		texture_idx++;
	}

	if (m_options.has_roughness_map)
	{
		const GLTexture2D& tex = *params.tex_list[material.tex_idx_roughnessMap];
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_2D, tex.tex_id);
		glUniform1i(m_bindings.location_tex_roughness, texture_idx);
		texture_idx++;
	}

	if (m_options.has_normal_map)
	{
		const GLTexture2D& tex = *params.tex_list[material.tex_idx_normalMap];
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_2D, tex.tex_id);
		glUniform1i(m_bindings.location_tex_normal, texture_idx);
		texture_idx++;
	}

	if (m_options.has_emissive_map)
	{
		const GLTexture2D& tex = *params.tex_list[material.tex_idx_emissiveMap];
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_2D, tex.tex_id);
		glUniform1i(m_bindings.location_tex_emissive, texture_idx);
		texture_idx++;
	}

	if (m_options.has_specular_map)
	{
		const GLTexture2D& tex = *params.tex_list[material.tex_idx_specularMap];
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_2D, tex.tex_id);
		glUniform1i(m_bindings.location_tex_specular, texture_idx);
		texture_idx++;
	}

	if (m_options.has_glossiness_map)
	{
		const GLTexture2D& tex = *params.tex_list[material.tex_idx_glossinessMap];
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_2D, tex.tex_id);
		glUniform1i(m_bindings.location_tex_glossiness, texture_idx);
		texture_idx++;
	}

	if (m_options.num_directional_shadows > 0)
	{
		std::vector<int> values(m_options.num_directional_shadows);
		for (int i = 0; i < m_options.num_directional_shadows; i++)
		{
			glActiveTexture(GL_TEXTURE0 + texture_idx);
			glBindTexture(GL_TEXTURE_2D, params.lights->directional_shadow_texs[i]);
			values[i] = texture_idx;
			texture_idx++;
		}
		int start_idx = m_bindings.location_tex_directional_shadow - m_options.num_directional_shadows + 1;
		int start_idx_depth = m_bindings.location_tex_directional_shadow_depth - m_options.num_directional_shadows + 1;
		glUniform1iv(start_idx, m_options.num_directional_shadows, values.data());
		glUniform1iv(start_idx_depth, m_options.num_directional_shadows, values.data());
	}	

	if (m_options.has_lightmap)
	{
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_2D, params.tex_lightmap->tex_id);
		glUniform1i(m_bindings.location_tex_lightmap, texture_idx);
		texture_idx++;

		glBindBuffer(GL_ARRAY_BUFFER, params.primitive->lightmap_indices->m_id);
		glVertexAttribIPointer(m_bindings.location_attrib_atlas_indices, 1, GL_UNSIGNED_INT, 0, nullptr);
		glEnableVertexAttribArray(m_bindings.location_attrib_atlas_indices);
	}
}

void StandardRoutine::render(const RenderParams& params)
{
	const MeshStandardMaterial& material = *(MeshStandardMaterial*)params.material_list[params.primitive->material_idx];

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	if (m_options.alpha_mode == AlphaMode::Mask)
	{
		glDepthMask(GL_TRUE);
	}
	else
	{
		glDepthMask(GL_FALSE);
	}

	if (material.doubleSided)
	{
		glDisable(GL_CULL_FACE);
	}
	else
	{
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
	}

	glUseProgram(m_prog->m_id);

	_render_common(params);	

	if (params.primitive->index_buf != nullptr)
	{
		glBindBuffer(GL_ARRAY_BUFFER, params.primitive->index_buf->m_id);
		if (params.primitive->type_indices == 1)
		{			
			glVertexAttribIPointer(m_bindings.location_attrib_indices, 1, GL_UNSIGNED_BYTE, 0, nullptr);
		}
		else if (params.primitive->type_indices == 2)
		{
			glVertexAttribIPointer(m_bindings.location_attrib_indices, 1, GL_UNSIGNED_SHORT, 0, nullptr);
		}
		else if (params.primitive->type_indices == 4)
		{
			glVertexAttribIPointer(m_bindings.location_attrib_indices, 1, GL_UNSIGNED_INT, 0, nullptr);
		}
		glEnableVertexAttribArray(m_bindings.location_attrib_indices);

		glDrawArrays(GL_TRIANGLES, 0, params.primitive->num_face * 3);
	}
	else
	{
		glDrawArrays(GL_TRIANGLES, 0, params.primitive->num_pos);
	}

	glUseProgram(0);
}


void StandardRoutine::render_batched(const RenderParams& params, const std::vector<int>& first_lst, const std::vector<int>& count_lst)
{
	const MeshStandardMaterial& material = *(MeshStandardMaterial*)params.material_list[params.primitive->material_idx];

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	if (m_options.alpha_mode == AlphaMode::Mask)
	{
		glDepthMask(GL_TRUE);
	}
	else
	{
		glDepthMask(GL_FALSE);
	}

	if (material.doubleSided)
	{
		glDisable(GL_CULL_FACE);
	}
	else
	{
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
	}

	glUseProgram(m_prog->m_id);

	_render_common(params);

	glBindBuffer(GL_ARRAY_BUFFER, params.primitive->index_buf->m_id);
	glVertexAttribIPointer(m_bindings.location_attrib_indices, 1, GL_UNSIGNED_INT, 0, nullptr);
	glEnableVertexAttribArray(m_bindings.location_attrib_indices);	

	for (int i = 0; i < (int)first_lst.size(); i++)
	{		
		glDrawArrays(GL_TRIANGLES, first_lst[i], count_lst[i]);
	}

	//glMultiDrawArrays(GL_TRIANGLES, first_lst.data(), count_lst.data(), (int)first_lst.size());

	glUseProgram(0);

}

