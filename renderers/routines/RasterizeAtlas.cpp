#include <GL/glew.h>
#include "models/ModelComponents.h"
#include "lights/DirectionalLight.h"
#include "RasterizeAtlas.h"

static std::string g_vertex =
R"(#version 430

#DEFINES#

#if HAS_INDICES
layout (location = LOCATION_ATTRIB_INDICES) in uint aInd;
#endif

layout (location = LOCATION_ATTRIB_ATLAS_INDICES) in uint aAtlasInd;

layout (location = LOCATION_TEX_POS) uniform samplerBuffer uTexPos;
layout (location = LOCATION_TEX_VERT_NORM) uniform samplerBuffer uTexNorm;

layout (std140, binding = BINDING_MODEL) uniform Model
{
	mat4 uModelMat;
	mat4 uNormalMat;
};

layout (location = LOCATION_VARYING_WORLD_POS) out vec3 vWorldPos;
layout (location = LOCATION_VARYING_NORM) out vec3 vNorm;

#if HAS_UV
layout (location = LOCATION_TEX_UV) uniform samplerBuffer uTexUV;
layout (location = LOCATION_VARYING_UV) out vec2 vUV;
#endif

layout (location = LOCATION_TEX_ATLAS_UV) uniform samplerBuffer uTexAtlasUV;

#if HAS_NORMAL_MAP
layout (location = LOCATION_TEX_TANGENT) uniform samplerBuffer uTexTangent;
layout (location = LOCATION_VARYING_TANGENT) out vec3 vTangent;
layout (location = LOCATION_TEX_BITANGENT) uniform samplerBuffer uTexBitangent;
layout (location = LOCATION_VARYING_BITANGENT) out vec3 vBitangent;
#endif


void main()
{
#if HAS_INDICES
	int index = int(aInd);
#else
	int index = int(gl_VertexID);
#endif
	int atlas_index = int(aAtlasInd);

	vec4 wolrd_pos = uModelMat * texelFetch(uTexPos, index);
	vWorldPos = wolrd_pos.xyz;

	vec4 world_norm = uNormalMat * texelFetch(uTexNorm, index);
	vNorm = world_norm.xyz;

#if HAS_UV
	vUV = texelFetch(uTexUV, index).xy;
#endif

#if HAS_NORMAL_MAP
	vec4 world_tangent = uModelMat * texelFetch(uTexTangent, index);
	vTangent =  world_tangent.xyz;

	vec4 world_bitangent = uModelMat * texelFetch(uTexBitangent, index);
	vBitangent =  world_bitangent.xyz;
#endif

	vec2 atlas_uv = texelFetch(uTexAtlasUV, atlas_index).xy;
	gl_Position = vec4(atlas_uv * 2.0 - 1.0, 0.0, 1.0);
}
)";

static std::string g_frag =
R"(#version 430
#DEFINES#

layout (location = LOCATION_VARYING_WORLD_POS) in vec3 vWorldPos;
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

#if HAS_UV
layout (location = LOCATION_VARYING_UV) in vec2 vUV;
#endif


#if HAS_NORMAL_MAP
layout (location = LOCATION_TEX_NORMAL) uniform sampler2D uTexNormal;
layout (location = LOCATION_VARYING_TANGENT) in vec3 vTangent;
layout (location = LOCATION_VARYING_BITANGENT) in vec3 vBitangent;
#endif

layout (location = 0) out vec4 out_pos;
layout (location = 1) out vec4 out_norm;

void main()
{
	out_pos = vec4(vWorldPos, 1.0);	
	vec3 dx = dFdx(vWorldPos);
	vec3 dy = dFdy(vWorldPos);
	vec3 N = normalize(cross(dx, dy));
	if (length(N)>0.0)
	{
		if (!gl_FrontFacing) N = -N;
		out_pos += vec4(N,0.0) * 0.001;
	}
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
	out_norm = vec4(norm, 0.0);
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

void RasterizeAtlas::s_generate_shaders(const Options& options, Bindings& bindings, std::string& s_vertex, std::string& s_frag)
{
	s_vertex = g_vertex;
	s_frag = g_frag;

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

	{
		bindings.location_attrib_atlas_indices = bindings.location_attrib_indices + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_ATTRIB_ATLAS_INDICES %d\n", bindings.location_attrib_atlas_indices);
			defines += line;
		}
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
		bindings.binding_model = 0;
		{
			char line[64];
			sprintf(line, "#define BINDING_MODEL %d\n", bindings.binding_model);
			defines += line;
		}
	}

	{
		bindings.location_varying_world_pos = 0;
		{
			char line[64];
			sprintf(line, "#define LOCATION_VARYING_WORLD_POS %d\n", bindings.location_varying_world_pos);
			defines += line;
		}
	}

	{
		bindings.location_varying_norm = bindings.location_varying_world_pos + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_VARYING_NORM %d\n", bindings.location_varying_norm);
			defines += line;
		}
	}

	{
		bindings.binding_material = bindings.binding_model + 1;
		{
			char line[64];
			sprintf(line, "#define BINDING_MATERIAL %d\n", bindings.binding_material);
			defines += line;
		}
	}

	bool has_uv = options.has_normal_map;

	if (has_uv)
	{
		defines += "#define HAS_UV 1\n";
		bindings.location_tex_uv = bindings.location_tex_vert_norm + 1;
		bindings.location_varying_uv = bindings.location_varying_norm + 1;
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
		bindings.location_tex_uv = bindings.location_tex_vert_norm;
		bindings.location_varying_uv = bindings.location_varying_norm;
	}

	{
		bindings.location_tex_atlas_uv = bindings.location_tex_uv + 1;
		{
			char line[64];
			sprintf(line, "#define LOCATION_TEX_ATLAS_UV %d\n", bindings.location_tex_atlas_uv);
			defines += line;
		}
	}

	if (options.has_normal_map)
	{
		defines += "#define HAS_NORMAL_MAP 1\n";
		bindings.location_tex_normal = bindings.location_tex_atlas_uv + 1;
		bindings.location_tex_tangent = bindings.location_tex_normal + 1;
		bindings.location_varying_tangent = bindings.location_varying_uv + 1;
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
		bindings.location_tex_normal = bindings.location_tex_atlas_uv;
		bindings.location_tex_tangent = bindings.location_tex_normal;
		bindings.location_varying_tangent = bindings.location_varying_uv;
		bindings.location_tex_bitangent = bindings.location_tex_tangent;
		bindings.location_varying_bitangent = bindings.location_varying_tangent;
	}

	replace(s_vertex, "#DEFINES#", defines.c_str());
	replace(s_frag, "#DEFINES#", defines.c_str());
}

RasterizeAtlas::RasterizeAtlas(const Options& options) : m_options(options)
{
	std::string s_vertex, s_frag;
	s_generate_shaders(options, m_bindings, s_vertex, s_frag);

	GLShader vert_shader(GL_VERTEX_SHADER, s_vertex.c_str());
	GLShader frag_shader(GL_FRAGMENT_SHADER, s_frag.c_str());
	m_prog = (std::unique_ptr<GLProgram>)(new GLProgram(vert_shader, frag_shader));
}

void RasterizeAtlas::render(const RenderParams& params)
{
	const MeshStandardMaterial& material = *(MeshStandardMaterial*)params.material_list[params.primitive->material_idx];
	const GeometrySet& geo = params.primitive->geometry[params.primitive->geometry.size() - 1];

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);	

	glUseProgram(m_prog->m_id);

	glBindBufferBase(GL_UNIFORM_BUFFER, m_bindings.binding_model, params.constant_model->m_id);
	glBindBufferBase(GL_UNIFORM_BUFFER, m_bindings.binding_material, material.constant_material.m_id);

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

	bool has_uv = m_options.has_normal_map;
	if (has_uv)
	{
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_BUFFER, params.primitive->uv_buf->tex_id);
		glUniform1i(m_bindings.location_tex_uv, texture_idx);
		texture_idx++;
	}

	{
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_BUFFER, params.primitive->lightmap_uv_buf->tex_id);
		glUniform1i(m_bindings.location_tex_atlas_uv, texture_idx);
		texture_idx++;
	}

	if (m_options.has_normal_map)
	{
		const GLTexture2D& tex = *params.tex_list[material.tex_idx_normalMap];
		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_2D, tex.tex_id);
		glUniform1i(m_bindings.location_tex_normal, texture_idx);
		texture_idx++;

		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_BUFFER, geo.tangent_buf->tex_id);
		glUniform1i(m_bindings.location_tex_tangent, texture_idx);
		texture_idx++;

		glActiveTexture(GL_TEXTURE0 + texture_idx);
		glBindTexture(GL_TEXTURE_BUFFER, geo.bitangent_buf->tex_id);
		glUniform1i(m_bindings.location_tex_bitangent, texture_idx);
		texture_idx++;		
	}

	glBindBuffer(GL_ARRAY_BUFFER, params.primitive->lightmap_indices->m_id);
	glVertexAttribIPointer(m_bindings.location_attrib_atlas_indices, 1, GL_UNSIGNED_INT, 0, nullptr);
	glEnableVertexAttribArray(m_bindings.location_attrib_atlas_indices);
	
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

