#pragma once

#include <string>

#include "materials/MeshStandardMaterial.h"
#include "lights/Lights.h"
#include "renderers/GLUtils.h"

class Primitive;
class RasterizeAtlas
{
public:
	struct Options
	{
		Options()
		{
			memset(this, 0, sizeof(Options));
		}
		bool has_indices = false;		
		bool has_normal_map = false;		
	};
	RasterizeAtlas(const Options& options);

	struct RenderParams
	{
		const GLTexture2D** tex_list;
		const MeshStandardMaterial** material_list;
		const GLDynBuffer* constant_model;
		const Primitive* primitive;
	};

	void render(const RenderParams& params);

private:
	Options m_options;

	struct Bindings
	{
		int location_attrib_indices;
		int location_attrib_atlas_indices;
		int location_tex_pos;
		int location_tex_vert_norm;
		int binding_model;
		int location_varying_world_pos;
		int location_varying_norm;
		int binding_material;
		int location_tex_uv;
		int location_varying_uv;
		int location_tex_atlas_uv;
		int location_tex_normal;
		int location_tex_tangent;
		int location_varying_tangent;
		int location_tex_bitangent;
		int location_varying_bitangent;
	};

	Bindings m_bindings;

	static void s_generate_shaders(const Options& options, Bindings& bindings, std::string& s_vertex, std::string& s_frag);

	std::unique_ptr<GLProgram> m_prog;
};
