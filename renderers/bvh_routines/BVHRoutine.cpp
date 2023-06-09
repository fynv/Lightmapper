#include <GL/glew.h>
#include "models/ModelComponents.h"
#include "lights/DirectionalLight.h"
#include "BVHRoutine.h"
#include "renderers/BVHRenderTarget.h"
#include "renderers/LightmapRayList.h"

static std::string g_compute_part0 =
R"(#version 430

uint ray_get_octant_inv4(in vec3 ray_direction)
{
	return (ray_direction.x < 0.0 ? 0 : 0x04040404) |
		(ray_direction.y < 0.0 ? 0 : 0x02020202) |
		(ray_direction.z < 0.0 ? 0 : 0x01010101);
}

struct BVH8Node
{
	vec4 node_0;
	vec4 node_1;
	vec4 node_2;
	vec4 node_3;
	vec4 node_4; 
};

layout (location = 0) uniform samplerBuffer uTexBVH8;
layout (location = 1) uniform samplerBuffer uTexTriangles;
layout (location = 2) uniform isamplerBuffer uTexIndices;

#DEFINES#

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


struct Ray
{
	vec3 origin;
	float tmin;
	vec3 direction;	
	float tmax;
};

struct Intersection
{
	int triangle_index;
	float t;
	float u;
	float v;
};

uint extract_byte(uint x, uint i) 
{
	return (x >> (i * 8)) & 0xff;
}

uint sign_extend_s8x4(uint x) 
{
	return ((x >> 7) & 0x01010101) * 0xff;
}

uint bvh8_node_intersect(in Ray ray, uint oct_inv4, in BVH8Node node)
{
	vec3 p = node.node_0.xyz;
	
	uint e_imask = floatBitsToUint(node.node_0.w);
	uint e_x = extract_byte(e_imask, 0);
	uint e_y = extract_byte(e_imask, 1);
	uint e_z = extract_byte(e_imask, 2);

	vec3 adjusted_ray_direction_inv = vec3(
		uintBitsToFloat(e_x << 23) / ray.direction.x,
		uintBitsToFloat(e_y << 23) / ray.direction.y,
		uintBitsToFloat(e_z << 23) / ray.direction.z
	);

	vec3 adjusted_ray_origin = (p - ray.origin) / ray.direction;

	uint hit_mask = 0;

	for (int i = 0; i < 2; i++) 
	{
		uint meta4 = floatBitsToUint(i == 0 ? node.node_1.z : node.node_1.w);
		
		uint is_inner4   = (meta4 & (meta4 << 1)) & 0x10101010;
		uint inner_mask4 = sign_extend_s8x4(is_inner4 << 3);
		uint bit_index4  = (meta4 ^ (oct_inv4 & inner_mask4)) & 0x1f1f1f1f;
		uint child_bits4 = (meta4 >> 5) & 0x07070707;

		// Select near and far planes based on ray octant
		uint q_lo_x = floatBitsToUint(i == 0 ? node.node_2.x : node.node_2.y);
		uint q_hi_x = floatBitsToUint(i == 0 ? node.node_2.z : node.node_2.w);

		uint q_lo_y = floatBitsToUint(i == 0 ? node.node_3.x : node.node_3.y);
		uint q_hi_y = floatBitsToUint(i == 0 ? node.node_3.z : node.node_3.w);

		uint q_lo_z = floatBitsToUint(i == 0 ? node.node_4.x : node.node_4.y);
		uint q_hi_z = floatBitsToUint(i == 0 ? node.node_4.z : node.node_4.w);

		uint x_min = ray.direction.x < 0.0 ? q_hi_x : q_lo_x;
		uint x_max = ray.direction.x < 0.0 ? q_lo_x : q_hi_x;

		uint y_min = ray.direction.y < 0.0 ? q_hi_y : q_lo_y;
		uint y_max = ray.direction.y < 0.0 ? q_lo_y : q_hi_y;

		uint z_min = ray.direction.z < 0.0 ? q_hi_z : q_lo_z;
		uint z_max = ray.direction.z < 0.0 ? q_lo_z : q_hi_z;

		for (int j = 0; j < 4; j++) 
		{
			// Extract j-th byte
			vec3 tmin3 = vec3(float(extract_byte(x_min, j)), float(extract_byte(y_min, j)), float(extract_byte(z_min, j)));
			vec3 tmax3 = vec3(float(extract_byte(x_max, j)), float(extract_byte(y_max, j)), float(extract_byte(z_max, j)));

			// Account for grid origin and scale
			tmin3 = tmin3 * adjusted_ray_direction_inv + adjusted_ray_origin;
			tmax3 = tmax3 * adjusted_ray_direction_inv + adjusted_ray_origin;

			float tmin = max(max(tmin3.x, tmin3.y), max(tmin3.z, ray.tmin));
			float tmax = min(min(tmax3.x, tmax3.y), min(tmax3.z, ray.tmax));

			bool intersected = tmin < tmax;
			if (intersected) 
			{
				uint child_bits = extract_byte(child_bits4, j);
				uint bit_index  = extract_byte(bit_index4,  j);
				hit_mask |= child_bits << bit_index;
			}
		}
	}

	return hit_mask;
}

bool g_front_facing;
vec3 g_face_normal;

bool triangle_intersect(int triangle_id, in Ray ray, out float t, out float u, out float v)
{
	vec3 pos0 = texelFetch(uTexTriangles, triangle_id*3).xyz;
	vec3 edge1 = texelFetch(uTexTriangles, triangle_id*3 + 1).xyz;
	vec3 edge2 = texelFetch(uTexTriangles, triangle_id*3 + 2).xyz;
	
	vec3 h = cross(ray.direction, edge2);
	float a = dot(edge1, h);

	if (a==0.0 ||  (uDoubleSided==0 && a<0.0)) return false;
	//if (a==0.0) return false;

	g_front_facing = a>0.0;
	
	float f = 1.0 / a;
	vec3 s = ray.origin - pos0;
	u = f * dot(s, h);

	if (u < 0.0 || u > 1.0) return false;
	
	vec3 q = cross(s, edge1);
	v = f * dot(ray.direction, q);

	if (v < 0.0 || (u + v)> 1.0) return false;
	t = f * dot(edge2, q);

	if (t <= ray.tmin) return false;	
	if (t > ray.tmax) return false;

	g_face_normal = normalize(cross(edge1, edge2));
	return true;
}

#define BVH_STACK_SIZE 32
#define SHARED_STACK_SIZE 8
#define LOCAL_STACK_SIZE (BVH_STACK_SIZE - SHARED_STACK_SIZE)
shared uvec2 shared_stack_bvh8[SHARED_STACK_SIZE*64];

#define SHARED_STACK_INDEX(offset) ((gl_LocalInvocationID.y * SHARED_STACK_SIZE + offset) * 32 + gl_LocalInvocationID.x)

void stack_push(inout uvec2 stack[LOCAL_STACK_SIZE], inout int stack_size, in uvec2 item) {	

	if (stack_size < SHARED_STACK_SIZE) 
	{
		shared_stack_bvh8[SHARED_STACK_INDEX(stack_size)] = item;
	} 
	else 
	{
		stack[stack_size - SHARED_STACK_SIZE] = item;
	}
	stack_size++;
}

uvec2 stack_pop(in uvec2 stack[LOCAL_STACK_SIZE], inout int stack_size) 
{
	stack_size--;
	if (stack_size < SHARED_STACK_SIZE) 
	{
		return shared_stack_bvh8[SHARED_STACK_INDEX(stack_size)];
	} 
	else 
	{
		return stack[stack_size - SHARED_STACK_SIZE];
	}
}

Ray g_ray;
Intersection g_ray_hit;

void intersect()
{
	g_ray_hit.triangle_index = -1;
	g_ray_hit.t = g_ray.tmax;
	g_ray_hit.u = 0.0;
	g_ray_hit.v = 0.0;
	
	uvec2 stack[LOCAL_STACK_SIZE]; 
	int stack_size = 0;

	uint oct_inv4 = ray_get_octant_inv4(g_ray.direction);
	uvec2 current_group = uvec2(0, 0x80000000);
	
	while (stack_size > 0 || current_group.y!=0)
	{
		uvec2 triangle_group;
		if ((current_group.y & 0xff000000)!=0)
		{
			uint hits_imask = current_group.y;
			int child_index_offset = findMSB(hits_imask);
			uint child_index_base   = current_group.x;

			// Remove n from current_group;
			current_group.y &= ~(1 << child_index_offset);

			// If the node group is not yet empty, push it on the stack
			if ((current_group.y & 0xff000000)!=0) 
			{
				stack_push(stack, stack_size, current_group);
			}

			uint slot_index     = (child_index_offset - 24) ^ (oct_inv4 & 0xff);
			uint relative_index = bitCount(hits_imask & ~(0xffffffff << slot_index));

			uint child_node_index = child_index_base + relative_index;
			BVH8Node node;
			node.node_0 = texelFetch(uTexBVH8, int(child_node_index*5));
			node.node_1 = texelFetch(uTexBVH8, int(child_node_index*5 + 1));
			node.node_2 = texelFetch(uTexBVH8, int(child_node_index*5 + 2));
			node.node_3 = texelFetch(uTexBVH8, int(child_node_index*5 + 3));
			node.node_4 = texelFetch(uTexBVH8, int(child_node_index*5 + 4));
			uint hitmask = bvh8_node_intersect(g_ray, oct_inv4, node);

			uint imask = extract_byte(floatBitsToUint(node.node_0.w), 3);			

			current_group.x = floatBitsToUint(node.node_1.x); // Child    base offset
			triangle_group.x = floatBitsToUint(node.node_1.y); // Triangle base offset

			current_group.y = (hitmask & 0xff000000) | imask;
			triangle_group.y = (hitmask & 0x00ffffff);
		}
		else 
		{
			triangle_group = current_group;
			current_group  = uvec2(0);
		}

		while (triangle_group.y != 0)
		{
			int triangle_index = findMSB(triangle_group.y);
			triangle_group.y &= ~(1 << triangle_index);

			int tri_idx = int(triangle_group.x + triangle_index);
			float t,u,v;
			if (triangle_intersect(tri_idx, g_ray, t, u, v))
			{
				g_ray_hit.triangle_index = texelFetch(uTexIndices, tri_idx).x;
				g_ray_hit.t = t;
				g_ray_hit.u = u;
				g_ray_hit.v = v;

				g_ray.tmax = t;
			}
		}			

		if ((current_group.y & 0xff000000) == 0) 
		{
			if (stack_size == 0) break;
			current_group = stack_pop(stack, stack_size);			
		}
	}
}

layout (std140, binding = BINDING_MODEL) uniform Model
{
	mat4 uModelMat;
	mat4 uNormalMat;
};

layout (location = LOCATION_TEX_FACES) uniform usamplerBuffer uTexFaces;

#if HAS_LIGHTMAP
layout (location = LOCATION_TEX_ATLAS_INDICES) uniform usamplerBuffer uTexAtlasIndices;
#endif

layout (std430, binding = BINDING_POSITIONS) buffer Positions
{
	vec4 positions[];
};

layout (std430, binding = BINDING_NORMALS) buffer Normals
{
	vec4 normals[];
};

#if HAS_COLOR
layout (std430, binding = BINDING_COLORS) buffer Colors
{
	vec4 colors[];
};
#endif

#if HAS_UV
layout (std430, binding = BINDING_UV) buffer UVs
{
	vec2 uvs[];
};
#endif

#if HAS_LIGHTMAP
layout (std430, binding = BINDING_ATLAS_UV) buffer AtlasUVs
{
	vec2 atlas_uvs[];
};
#endif

float g_norm_z;
vec3 gViewDir;
vec3 gWorldPos;
vec3 gNorm;

#if HAS_COLOR
vec4 gColor;
#endif

#if HAS_UV
vec2 gUV;
#endif

#if HAS_LIGHTMAP
vec2 gAtlasUV;
#endif

void interpolate_variants()
{
	int face_id = g_ray_hit.triangle_index;
	float u = g_ray_hit.u;
	float v = g_ray_hit.v;
	int vert_idx0 = int(texelFetch(uTexFaces, face_id*3).x);
	int vert_idx1 = int(texelFetch(uTexFaces, face_id*3 + 1).x);
	int vert_idx2 = int(texelFetch(uTexFaces, face_id*3 + 2).x);

	vec3 pos0 = positions[vert_idx0].xyz;
	vec3 pos1 = positions[vert_idx1].xyz;
	vec3 pos2 = positions[vert_idx2].xyz;
	vec3 position =  (1.0 - u - v) * pos0 + u * pos1 + v * pos2;
	gWorldPos = vec3(uModelMat * vec4(position, 1.0));
	
	vec3 norm0 = normals[vert_idx0].xyz;
	vec3 norm1 = normals[vert_idx1].xyz;
	vec3 norm2 = normals[vert_idx2].xyz;
	vec3 normal =  (1.0 - u - v) * norm0 + u * norm1 + v * norm2;
	gNorm = normalize(vec3(uNormalMat * vec4(normal, 0.0)));		

#if HAS_COLOR
	vec4 color0 = colors[vert_idx0];
	vec4 color1 = colors[vert_idx1];
	vec4 color2 = colors[vert_idx2];
	gColor = (1.0 - u - v) * color0 + u * color1 + v * color2;
#endif

#if HAS_UV
	{
		vec2 uv0 = uvs[vert_idx0];
		vec2 uv1 = uvs[vert_idx1];
		vec2 uv2 = uvs[vert_idx2];
		gUV = (1.0 - u - v) * uv0 + u * uv1 + v * uv2;
	}
#endif

#if HAS_LIGHTMAP
	{
		int vert_idx0 = int(texelFetch(uTexAtlasIndices, face_id*3).x);
		int vert_idx1 = int(texelFetch(uTexAtlasIndices, face_id*3 + 1).x);
		int vert_idx2 = int(texelFetch(uTexAtlasIndices, face_id*3 + 2).x);
		vec2 uv0 = atlas_uvs[vert_idx0];
		vec2 uv1 = atlas_uvs[vert_idx1];
		vec2 uv2 = atlas_uvs[vert_idx2];
		gAtlasUV = (1.0 - u - v) * uv0 + u * uv1 + v * uv2;
	}
#endif
}

#if HAS_COLOR_TEX
layout (location = LOCATION_TEX_COLOR) uniform sampler2D uTexColor;
#endif

#if HAS_METALNESS_MAP
layout (location = LOCATION_TEX_METALNESS) uniform sampler2D uTexMetalness;
#endif

#if HAS_ROUGHNESS_MAP
layout (location = LOCATION_TEX_ROUGHNESS) uniform sampler2D uTexRoughness;
#endif

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

float luminance(in vec3 color)
{
	return color.x * 0.2126 + color.y * 0.7152 + color.z *0.0722;
}

struct DirectionalLight
{	
	vec4 color;
	vec4 origin;
	vec4 direction;
	int has_shadow;
	float diffuse_thresh;
	float diffuse_high;
	float diffuse_low;
	float specular_thresh;
	float specular_high;
	float specular_low;
};


#if NUM_DIRECTIONAL_LIGHTS>0
layout (std140, binding = BINDING_DIRECTIONAL_LIGHTS) uniform DirectionalLights
{
	DirectionalLight uDirectionalLights[NUM_DIRECTIONAL_LIGHTS];
};
#endif

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

vec3 computeShadowCoords(in mat4 VPSB)
{
	vec4 shadowCoords = VPSB * vec4(gWorldPos, 1.0);
	return shadowCoords.xyz;
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
#endif


#if HAS_LIGHTMAP
layout (location = LOCATION_TEX_LIGHTMAP) uniform sampler2D uTexLightmap;
#endif
)";

static std::string g_compute_part1 =
R"(
vec4 out0;

#if ALPHA_BLEND
vec4 out_oit_col;
float out_oit_reveal;
#endif

bool calc_shading()
{
	interpolate_variants();
	vec4 base_color = uColor;
#if HAS_COLOR
	base_color *= gColor;
#endif

	float tex_alpha = 1.0;

#if HAS_COLOR_TEX
	vec4 tex_color = texture(uTexColor, gUV);
	tex_alpha = tex_color.w;
	base_color *= tex_color;
#endif

#if ALPHA_MASK
	base_color.w = base_color.w > uAlphaCutoff ? 1.0 : 0.0;
#endif

#if ALPHA_MASK || ALPHA_BLEND
	if (base_color.w == 0.0) return false;
#endif

#if SPECULAR_GLOSSINESS
	vec3 specularFactor = uSpecularGlossiness.xyz;
#if HAS_SPECULAR_MAP	
	specularFactor *= texture( uTexSpecular, gUV ).xyz;
#endif
	float glossinessFactor = uSpecularGlossiness.w;
#if HAS_GLOSSINESS_MAP
	glossinessFactor *= texture( uTexGlossiness, gUV ).w;
#endif

#else
	float metallicFactor = uMetallicFactor;
	float roughnessFactor = uRoughnessFactor;

#if HAS_METALNESS_MAP
	metallicFactor *= texture(uTexMetalness, gUV).z;
#endif

#if HAS_ROUGHNESS_MAP
	roughnessFactor *= texture(uTexRoughness, gUV).y;
#endif

#endif	
	
	vec3 norm = gNorm;
	if (uDoubleSided!=0 && !g_front_facing)
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

	material.specularF90 = 1.0;

	vec3 emissive = uEmissive.xyz;
#if HAS_EMISSIVE_MAP
	emissive *= texture(uTexEmissive, gUV).xyz;
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
			l_shadow = computeShadowCoef(shadow.VPSBMat, uDirectionalShadowTex[shadow_id]);
			shadow_id++;
		}
#endif
		IncidentLight directLight = IncidentLight(light_source.color.xyz * l_shadow, light_source.direction.xyz, true);	

		float dotNL =  saturate(dot(norm, directLight.direction));
		vec3 irradiance = dotNL * directLight.color;

		diffuse += irradiance * BRDF_Lambert( material.diffuseColor );
		specular += irradiance * BRDF_GGX( directLight.direction, gViewDir, norm, material.specularColor, material.specularF90, material.roughness );
	}
#endif

#if HAS_LIGHTMAP
	{
		vec4 lm = texture(uTexLightmap, gAtlasUV);
		vec3 light_color = lm.w>0.0 ? lm.xyz/lm.w : vec3(0.0);
		diffuse += material.diffuseColor * light_color;
		specular += material.specularColor * light_color;
	}
#endif

	vec3 col = emissive + specular;	

#if ALPHA_BLEND
	out0 = vec4(col*tex_alpha, 0.0);
	col += diffuse;

	float alpha = base_color.w;
	float a = min(1.0, alpha) * 8.0 + 0.01;
	float b = -g_norm_z * 0.95 + 1.0;
	float weight = clamp(a * a * a * 1e8 * b * b * b, 1e-2, 3e2);

	out_oit_col = vec4(col * alpha, alpha) * weight;
	out_oit_reveal = alpha;
#else
	col += diffuse;
	out0 = vec4(col, 1.0);	
#endif
	return true;
}

layout (binding=0, r32f) uniform image2D uImgDepth;
layout (binding=1, rgba16f) uniform image2D uImgColor;

#if ALPHA_BLEND
layout (binding=2, rgba16f) uniform image2D uImgOITColor;
layout (binding=3, r8) uniform image2D uImgOITReveal;
#endif

layout(local_size_x = 32, local_size_y = 2) in;

ivec2 g_id_io;
vec3 g_origin;
vec3 g_dir;
float g_tmin;
float g_tmax;

void render()
{
	float tmax = imageLoad(uImgDepth, g_id_io).x;
	g_tmax = min(tmax, g_tmax);
	
	while (g_tmin < g_tmax)
	{
		g_ray.origin = g_origin;
		g_ray.direction = g_dir;
		g_ray.tmin = g_tmin;
		g_ray.tmax = g_tmax;

		intersect();
		if (g_ray_hit.triangle_index < 0) break;

		g_norm_z = 1.0 - 1.0/(g_ray_hit.t + 1.0);
		gViewDir = -g_dir;		
		if (calc_shading())
		{
#if ALPHA_BLEND
			vec4 base = imageLoad(uImgColor, g_id_io);
			imageStore(uImgColor, g_id_io, base + out0);
			
			vec4 base_col =  imageLoad(uImgOITColor, g_id_io);
			imageStore(uImgOITColor, g_id_io, base_col + out_oit_col);

			float base_reveal = imageLoad(uImgOITReveal, g_id_io).x;
			imageStore(uImgOITReveal, g_id_io, vec4((1-out_oit_reveal)*base_reveal));
#else
			imageStore(uImgColor, g_id_io, out0);
#if ALPHA_MASK
			imageStore(uImgDepth, g_id_io, vec4(g_ray_hit.t));
#endif
			break;
#endif
		}
		g_tmin = g_ray_hit.t;
	}
}

#if TO_CAMERA

layout (std140, binding = BINDING_CAMERA) uniform Camera
{
	mat4 uProjMat;
	mat4 uViewMat;	
	mat4 uInvProjMat;
	mat4 uInvViewMat;	
	vec3 uEyePos;
};

void main()
{
	ivec2 size = imageSize(uImgDepth);
	ivec2 id = ivec3(gl_GlobalInvocationID).xy;	
	if (id.x>= size.x || id.y >=size.y) return;

	ivec2 screen = ivec2(id.x, id.y);
	vec4 clip0 = vec4((vec2(screen) + 0.5)/vec2(size)*2.0-1.0, -1.0, 1.0);
	vec4 clip1 = vec4((vec2(screen) + 0.5)/vec2(size)*2.0-1.0, 1.0, 1.0);
	vec4 view0 = uInvProjMat * clip0; view0 /= view0.w;
	vec4 view1 = uInvProjMat * clip1; view1 /= view1.w;
	vec3 world0 = vec3(uInvViewMat*view0);
	vec3 world1 = vec3(uInvViewMat*view1);	
	vec3 dir = normalize(world0 - uEyePos);

	g_id_io = id;
	g_origin = uEyePos;
	g_dir = dir;
	g_tmin = length(world0 - uEyePos);
	g_tmax = length(world1 - uEyePos);
	
	render();
}
#elif TO_LIGHTMAP
layout (std140, binding = BINDING_LIGHTMAP_RAY_LIST) uniform LightmapRayList
{
	int uTexelBegin;
	int uTexelEnd;
	int uNumRays;
	int uTexelsPerRow;
	int uNumRows;
	int uJitter;
};

layout (location = LOCATION_TEX_LIGHTMAP_POS) uniform sampler2D uTexPosition;
layout (location = LOCATION_TEX_LIGHTMAP_NORM) uniform sampler2D uTexNormal;
layout (location = LOCATION_TEX_LIGHTMAP_VALID_LIST) uniform usamplerBuffer uValidList;

uint InitRandomSeed(uint val0, uint val1)
{
	uint v0 = val0, v1 = val1, s0 = 0u;

	for (uint n = 0u; n < 16u; n++)
	{
		s0 += 0x9e3779b9u;
		v0 += ((v1 << 4) + 0xa341316cu) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4u);
		v1 += ((v0 << 4) + 0xad90777du) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761eu);
	}

	return v0;
}

uint RandomInt(inout uint seed)
{
    return (seed = 1664525u * seed + 1013904223u);
}

float RandomFloat(inout uint seed)
{
	return (float(RandomInt(seed) & 0x00FFFFFFu) / float(0x01000000));
}

vec3 RandomDirection(inout uint seed)
{
	float z = RandomFloat(seed) * 2.0 - 1.0;
	float xy = sqrt(1.0 - z*z);
	float alpha = RandomFloat(seed) * PI * 2.0;
	return vec3(xy * cos(alpha), xy * sin(alpha), z);
}

vec3 RandomDiffuse(inout uint seed, in vec3 base_dir)
{
	vec3 dir = RandomDirection(seed);
	float d = dot(dir, base_dir);
	vec3 c = d * base_dir;
	vec3 s = dir - c;
	float z2 = clamp(abs(d), 0.0, 1.0);
	float xy = sqrt(1.0 - z2);	
	vec3 s_dir =  sqrt(z2) * base_dir;
	if (length(s)>0.0)
	{		
		s_dir += xy * normalize(s);
	}
	return s_dir;
}

void main()
{
	ivec2 local_id = ivec3(gl_LocalInvocationID).xy;	
	ivec2 group_id = ivec3(gl_WorkGroupID).xy;
	g_id_io = ivec2(local_id.x + local_id.y * 32 + group_id.x * 64, group_id.y);
	int idx_texel_out = g_id_io.x/uNumRays + g_id_io.y*uTexelsPerRow;	
	int idx_texel_in = idx_texel_out + uTexelBegin;
	if (idx_texel_in >= uTexelEnd) return;

	int idx_ray = g_id_io.x % uNumRays;

	ivec2 texel_coord = ivec2(texelFetch(uValidList, idx_texel_in).xy);	
	g_origin = texelFetch(uTexPosition, texel_coord, 0).xyz;
	vec3 norm = texelFetch(uTexNormal, texel_coord, 0).xyz;
	uint seed = InitRandomSeed(uJitter, idx_texel_out * uNumRays +  idx_ray);
	g_dir = RandomDiffuse(seed, norm);	

	g_tmin = 0.001;	
	g_tmax = 3.402823466e+38;
	
	render();
}
#endif
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

void BVHRoutine::s_generate_shaders(const Options& options, Bindings& bindings, std::string& s_compute)
{		
	s_compute = g_compute_part0 + g_compute_part1;

	std::string defines = "";

	if (options.target_mode == 0)
	{
		defines += "#define TO_CAMERA 1\n";
	}
	else
	{
		defines += "#define TO_CAMERA 0\n";
	}

	if (options.target_mode == 2)
	{
		defines += "#define TO_LIGHTMAP 1\n";
	}
	else
	{
		defines += "#define TO_LIGHTMAP 0\n";
	}
	
	{
		bindings.binding_material = 0;
		{
			char line[64];
			sprintf(line, "#define BINDING_MATERIAL %d\n", bindings.binding_material);
			defines += line;
		}
	}

	{
		bindings.binding_model = bindings.binding_material + 1;
		{
			char line[64];
			sprintf(line, "#define BINDING_MODEL %d\n", bindings.binding_model);
			defines += line;
		}
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
		defines += "#define ALPHA_BLEND 1\n";		
	}
	else
	{
		defines += "#define ALPHA_BLEND 0\n";		
	}

	{
		bindings.location_tex_faces = 3;
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_FACES %d\n", bindings.location_tex_faces);
			defines += line;
		}
	}
	
	if (options.has_lightmap)
	{
		defines += "#define HAS_LIGHTMAP 1\n";
		bindings.location_tex_atlas_indices = bindings.location_tex_faces + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_ATLAS_INDICES %d\n", bindings.location_tex_atlas_indices);
			defines += line;
		}

	}
	else
	{
		defines += "#define HAS_LIGHTMAP 0\n";
		bindings.location_tex_atlas_indices = bindings.location_tex_faces;
	}

	{
		bindings.binding_positions = bindings.binding_model + 1;
		{
			char line[64];
			sprintf(line, "#define BINDING_POSITIONS %d\n", bindings.binding_positions);
			defines += line;
		}
	}

	{
		bindings.binding_normals = bindings.binding_positions + 1;
		{
			char line[64];
			sprintf(line, "#define BINDING_NORMALS %d\n", bindings.binding_normals);
			defines += line;
		}
	}

	if (options.has_color)
	{
		defines += "#define HAS_COLOR 1\n";
		bindings.binding_colors = bindings.binding_normals + 1;
		{
			char line[64];
			sprintf(line, "#define BINDING_COLORS %d\n", bindings.binding_colors);
			defines += line;
		}
	}
	else
	{
		defines += "#define HAS_COLOR 0\n";
		bindings.binding_colors = bindings.binding_normals;
	}

	bool has_uv = options.has_color_texture || options.has_metalness_map || options.has_roughness_map
		|| options.has_emissive_map || options.has_specular_map || options.has_glossiness_map;

	if (has_uv)
	{		
		defines += "#define HAS_UV 1\n";
		bindings.binding_uv = bindings.binding_colors + 1;
		{
			char line[64];
			sprintf(line, "#define BINDING_UV %d\n", bindings.binding_uv);
			defines += line;
		}
	}
	else
	{		
		defines += "#define HAS_UV 0\n";
		bindings.binding_uv = bindings.binding_colors;
	}

	if (options.has_lightmap)
	{
		bindings.binding_atlas_uv = bindings.binding_uv + 1;
		{
			char line[64];
			sprintf(line, "#define BINDING_ATLAS_UV %d\n", bindings.binding_atlas_uv);
			defines += line;
		}
	}
	else
	{
		bindings.binding_atlas_uv = bindings.binding_uv;
	}

	if (options.has_color_texture)
	{
		defines += "#define HAS_COLOR_TEX 1\n";

		bindings.location_tex_color = bindings.location_tex_atlas_indices + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_COLOR %d\n", bindings.location_tex_color);
			defines += line;
		}
	}
	else
	{
		defines += "#define HAS_COLOR_TEX 0\n";
		bindings.location_tex_color = bindings.location_tex_atlas_indices;
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

	if (options.has_emissive_map)
	{
		defines += "#define HAS_EMISSIVE_MAP 1\n";
		bindings.location_tex_emissive = bindings.location_tex_roughness + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_EMISSIVE %d\n", bindings.location_tex_emissive);
			defines += line;
		}
	}
	else
	{
		defines += "#define HAS_EMISSIVE_MAP 0\n";
		bindings.location_tex_emissive = bindings.location_tex_roughness;
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
		bindings.binding_directional_lights = bindings.binding_atlas_uv + 1;
		{
			char line[64];
			sprintf(line, "#define BINDING_DIRECTIONAL_LIGHTS %d\n", bindings.binding_directional_lights);
			defines += line;
		}
	}
	else
	{
		bindings.binding_directional_lights = bindings.binding_atlas_uv;
	}

	{
		char line[64];
		sprintf(line, "#define NUM_DIRECTIONAL_SHADOWS %d\n", options.num_directional_shadows);
		defines += line;
	}

	bindings.location_tex_directional_shadow = bindings.location_tex_glossiness + options.num_directional_shadows;

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
	}
	else
	{
		bindings.binding_directional_shadows = bindings.binding_directional_lights;
	}

	if (options.has_lightmap)
	{
		bindings.location_tex_lightmap = bindings.location_tex_directional_shadow + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_LIGHTMAP %d\n", bindings.location_tex_lightmap);
			defines += line;
		}
	}
	else
	{
		bindings.location_tex_lightmap = bindings.location_tex_directional_shadow;
	}

	if (options.target_mode == 0)
	{
		bindings.binding_camera = bindings.binding_directional_shadows + 1;
		{
			char line[64];
			sprintf(line, "#define BINDING_CAMERA %d\n", bindings.binding_camera);
			defines += line;
		}
	}
	else if (options.target_mode == 2)
	{
		bindings.binding_lightmap_ray_list = bindings.binding_directional_shadows + 1;
		bindings.location_tex_lightmap_pos = bindings.location_tex_lightmap + 1;
		bindings.location_tex_lightmap_norm = bindings.location_tex_lightmap_pos + 1;
		bindings.location_tex_lightmap_valid_list = bindings.location_tex_lightmap_norm + 1;

		{
			char line[64];
			sprintf(line, "#define BINDING_LIGHTMAP_RAY_LIST %d\n", bindings.binding_lightmap_ray_list);
			defines += line;
		}

		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_LIGHTMAP_POS %d\n", bindings.location_tex_lightmap_pos);
			defines += line;
		}
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_LIGHTMAP_NORM %d\n", bindings.location_tex_lightmap_norm);
			defines += line;
		}
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_LIGHTMAP_VALID_LIST %d\n", bindings.location_tex_lightmap_valid_list);
			defines += line;
		}
	}

	replace(s_compute, "#DEFINES#", defines.c_str());
}

BVHRoutine::BVHRoutine(const Options& options) : m_options(options)
{
	std::string s_compute;
	s_generate_shaders(options, m_bindings, s_compute);

	GLShader comp_shader(GL_COMPUTE_SHADER, s_compute.c_str());
	m_prog = (std::unique_ptr<GLProgram>)(new GLProgram(comp_shader));
}


void BVHRoutine::render(const RenderParams& params)
{
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	const MeshStandardMaterial& material = *(MeshStandardMaterial*)params.material_list[params.primitive->material_idx];
	const GeometrySet& geo = params.primitive->geometry[params.primitive->geometry.size() - 1];

	const CWBVH* bvh = params.primitive->cwbvh.get();
	const BVHRenderTarget* target = params.target;

	int width = target->m_width;
	int height = target->m_height;

	glUseProgram(m_prog->m_id);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_BUFFER, bvh->m_tex_bvh8.tex_id);
	glUniform1i(0, 0);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_BUFFER, bvh->m_tex_triangles.tex_id);
	glUniform1i(1, 1);

	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_BUFFER, bvh->m_tex_indices.tex_id);
	glUniform1i(2, 2);

	glBindBufferBase(GL_UNIFORM_BUFFER, m_bindings.binding_material, material.constant_material.m_id);
	glBindBufferBase(GL_UNIFORM_BUFFER, m_bindings.binding_model, params.constant_model->m_id);

	int texture_idx = 3;
	{		
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_BUFFER, params.primitive->index_buf->tex_id);
		glUniform1i(m_bindings.location_tex_faces, texture_idx);
		texture_idx++;
	}

	if (m_options.has_lightmap)
	{
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_BUFFER, params.primitive->lightmap_indices->tex_id);
		glUniform1i(m_bindings.location_tex_atlas_indices, texture_idx);
		texture_idx++;
	}

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, m_bindings.binding_positions, geo.pos_buf->m_id);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, m_bindings.binding_normals, geo.normal_buf->m_id);

	if (m_options.has_color)
	{
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, m_bindings.binding_colors, params.primitive->color_buf->m_id);
	}

	bool has_uv = m_options.has_color_texture || m_options.has_metalness_map || m_options.has_roughness_map
		|| m_options.has_emissive_map || m_options.has_specular_map || m_options.has_glossiness_map;

	if (has_uv)
	{		
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, m_bindings.binding_uv, params.primitive->uv_buf->m_id);		
	}

	if (m_options.has_lightmap)
	{
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, m_bindings.binding_atlas_uv, params.primitive->lightmap_uv_buf->m_id);
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

	if (m_options.num_directional_lights > 0)
	{
		glBindBufferBase(GL_UNIFORM_BUFFER, m_bindings.binding_directional_lights, params.lights->constant_directional_lights->m_id);
	}

	if (m_options.num_directional_shadows > 0)
	{		
		glBindBufferBase(GL_UNIFORM_BUFFER, m_bindings.binding_directional_shadows, params.lights->constant_directional_shadows->m_id);

		std::vector<int> values(m_options.num_directional_shadows);
		for (int i = 0; i < m_options.num_directional_shadows; i++)
		{
			glActiveTexture(GL_TEXTURE0 + texture_idx);			
			glBindTexture(GL_TEXTURE_2D, params.lights->directional_shadow_texs[i]);
			values[i] = texture_idx;
			texture_idx++;
		}
		int start_idx = m_bindings.location_tex_directional_shadow - m_options.num_directional_shadows + 1;
		glUniform1iv(start_idx, m_options.num_directional_shadows, values.data());
	}

	if (m_options.has_lightmap)
	{		
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_2D, params.tex_lightmap->tex_id);
		glUniform1i(m_bindings.location_tex_lightmap, texture_idx);
		texture_idx++;
	}

	glBindImageTexture(0, target->m_tex_depth->tex_id, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32F);
	glBindImageTexture(1, target->m_tex_video->tex_id, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);

	if (m_options.alpha_mode == AlphaMode::Blend)
	{
		glBindImageTexture(2, target->m_OITBuffers.m_tex_col->tex_id, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
		glBindImageTexture(3, target->m_OITBuffers.m_tex_reveal->tex_id, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R8);
	}

	if (m_options.target_mode == 0)
	{
		glBindBufferBase(GL_UNIFORM_BUFFER, m_bindings.binding_camera, params.constant_camera->m_id);

		glm::ivec2 blocks = { (width + 31) / 32, (height + 1) / 2 };
		glDispatchCompute(blocks.x, blocks.y, 1);
	}
	else if (m_options.target_mode == 2)
	{
		glBindBufferBase(GL_UNIFORM_BUFFER, m_bindings.binding_lightmap_ray_list, params.lmrl->m_constant.m_id);		

		{
			glActiveTexture(GL_TEXTURE0 + texture_idx);
			glBindTexture(GL_TEXTURE_2D, params.lmrl->source->m_tex_position->tex_id);
			glUniform1i(m_bindings.location_tex_lightmap_pos, texture_idx);
			texture_idx++;
		}

		{
			glActiveTexture(GL_TEXTURE0 + texture_idx);
			glBindTexture(GL_TEXTURE_2D, params.lmrl->source->m_tex_normal->tex_id);
			glUniform1i(m_bindings.location_tex_lightmap_norm, texture_idx);
			texture_idx++;
		}

		{
			glActiveTexture(GL_TEXTURE0 + texture_idx);
			glBindTexture(GL_TEXTURE_BUFFER, params.lmrl->source->valid_list->tex_id);
			glUniform1i(m_bindings.location_tex_lightmap_valid_list, texture_idx);
			texture_idx++;
		}

		glm::ivec2 blocks = { (width + 63) / 64, height };
		glDispatchCompute(blocks.x, blocks.y, 1);

	}
	
	glUseProgram(0);
}

