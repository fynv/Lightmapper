#pragma once

#include <string>

#include "materials/MeshStandardMaterial.h"
#include "lights/Lights.h"
#include "renderers/GLUtils.h"

class Primitive;
class StandardRoutine
{
public:
	struct Options
	{
		Options()
		{
			memset(this, 0, sizeof(Options));
		}
		AlphaMode alpha_mode = AlphaMode::Opaque;
		bool is_highlight_pass = false;
		bool has_indices = false;
		bool has_lightmap = false;
		bool specular_glossiness = false;
		bool has_color = false;
		bool has_color_texture = false;
		bool has_metalness_map = false;
		bool has_roughness_map = false;
		bool has_normal_map = false;
		bool has_emissive_map = false;
		bool has_specular_map = false;
		bool has_glossiness_map = false;
		int num_directional_lights = 0;
		int num_directional_shadows = 0;		
	};
	StandardRoutine(const Options& options);

	struct RenderParams
	{
		const GLTexture2D** tex_list;
		const MeshStandardMaterial** material_list;
		const GLDynBuffer* constant_camera;
		const GLDynBuffer* constant_model;
		const Primitive* primitive;
		const Lights* lights;
		const GLTexture2D* tex_lightmap;
	};

	void render(const RenderParams& params);
	void render_batched(const RenderParams& params, const std::vector<int>& first_lst, const std::vector<int>& count_lst);

private:
	Options m_options;

	struct Bindings
	{	
		int location_attrib_indices;
		int location_attrib_atlas_indices;
		int location_tex_pos;
		int location_tex_vert_norm;
		int binding_camera;
		int binding_model;
		int location_varying_viewdir;
		int location_varying_norm;
		int binding_material;
		int location_tex_vert_color;
		int location_varying_color;
		int location_tex_uv;
		int location_varying_uv;
		int location_tex_atlas_uv;
		int location_varying_atlas_uv;
		int location_tex_color;
		int location_tex_metalness;
		int location_tex_roughness;
		int location_tex_normal;
		int location_tex_tangent;
		int location_varying_tangent;
		int location_tex_bitangent;
		int location_varying_bitangent;
		int location_tex_emissive;
		int location_tex_specular;
		int location_tex_glossiness;
		int location_varying_world_pos;
		int binding_directional_lights;
		int binding_directional_shadows;
		int location_tex_directional_shadow;
		int location_tex_directional_shadow_depth;		
		int location_tex_lightmap;
	};

	Bindings m_bindings;

	static void s_generate_shaders(const Options& options, Bindings& bindings, std::string& s_vertex, std::string& s_frag);

	std::unique_ptr<GLProgram> m_prog;

	void _render_common(const RenderParams& params);
};
