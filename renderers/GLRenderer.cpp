#include <GL/glew.h>
#include "crc64/crc64.h"
#include "scenes/Scene.h"
#include "cameras/Camera.h"
#include "cameras/PerspectiveCamera.h"
#include "GLRenderTarget.h"
#include "LightmapRenderTarget.h"
#include "GLRenderer.h"

#include "backgrounds/Background.h"
#include "models/ModelComponents.h"
#include "models/SimpleModel.h"
#include "models/GLTFModel.h"

#include "materials/MeshStandardMaterial.h"
#include "lights/DirectionalLight.h"
#include "lights/DirectionalLightShadow.h"

#include "LightmapRayList.h"

GLRenderer::GLRenderer()
{

}

GLRenderer::~GLRenderer()
{

}

void GLRenderer::update_model(SimpleModel* model)
{
	model->updateConstant();
}

void GLRenderer::update_model(GLTFModel* model)
{
	model->updateMeshConstants();
}


StandardRoutine* GLRenderer::get_routine(const StandardRoutine::Options& options)
{
	uint64_t hash = crc64(0, (const unsigned char*)&options, sizeof(StandardRoutine::Options));
	auto iter = routine_map.find(hash);
	if (iter == routine_map.end())
	{
		routine_map[hash] = std::unique_ptr<StandardRoutine>(new StandardRoutine(options));
	}
	return routine_map[hash].get();
}


inline void toViewAABB(const glm::mat4& MV, const glm::vec3& min_pos, const glm::vec3& max_pos, glm::vec3& min_pos_out, glm::vec3& max_pos_out)
{
	glm::vec4 view_pos[8];
	view_pos[0] = MV * glm::vec4(min_pos.x, min_pos.y, min_pos.z, 1.0f);
	view_pos[1] = MV * glm::vec4(max_pos.x, min_pos.y, min_pos.z, 1.0f);
	view_pos[2] = MV * glm::vec4(min_pos.x, max_pos.y, min_pos.z, 1.0f);
	view_pos[3] = MV * glm::vec4(max_pos.x, max_pos.y, min_pos.z, 1.0f);
	view_pos[4] = MV * glm::vec4(min_pos.x, min_pos.y, max_pos.z, 1.0f);
	view_pos[5] = MV * glm::vec4(max_pos.x, min_pos.y, max_pos.z, 1.0f);
	view_pos[6] = MV * glm::vec4(min_pos.x, max_pos.y, max_pos.z, 1.0f);
	view_pos[7] = MV * glm::vec4(max_pos.x, max_pos.y, max_pos.z, 1.0f);

	min_pos_out = { FLT_MAX, FLT_MAX, FLT_MAX };
	max_pos_out = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

	for (int k = 0; k < 8; k++)
	{
		glm::vec4 pos = view_pos[k];
		if (pos.x < min_pos_out.x) min_pos_out.x = pos.x;
		if (pos.x > max_pos_out.x) max_pos_out.x = pos.x;
		if (pos.y < min_pos_out.y) min_pos_out.y = pos.y;
		if (pos.y > max_pos_out.y) max_pos_out.y = pos.y;
		if (pos.z < min_pos_out.z) min_pos_out.z = pos.z;
		if (pos.z > max_pos_out.z) max_pos_out.z = pos.z;
	}
}

inline bool visible(const glm::mat4& MV, const glm::mat4& P, const glm::vec3& min_pos, const glm::vec3& max_pos)
{
	glm::vec3 min_pos_view, max_pos_view;
	toViewAABB(MV, min_pos, max_pos, min_pos_view, max_pos_view);

	glm::mat4 invP = glm::inverse(P);
	glm::vec4 view_far = invP * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
	view_far /= view_far.w;
	glm::vec4 view_near = invP * glm::vec4(0.0f, 0.0f, -1.0f, 1.0f);
	view_near /= view_near.w;

	if (min_pos_view.z > view_near.z) return false;
	if (max_pos_view.z < view_far.z) return false;
	if (min_pos_view.z < view_far.z)
	{
		min_pos_view.z = view_far.z;
	}

	glm::vec4 min_pos_proj = P * glm::vec4(min_pos_view.x, min_pos_view.y, min_pos_view.z, 1.0f);
	min_pos_proj /= min_pos_proj.w;

	glm::vec4 max_pos_proj = P * glm::vec4(max_pos_view.x, max_pos_view.y, min_pos_view.z, 1.0f);
	max_pos_proj /= max_pos_proj.w;

	return  max_pos_proj.x >= -1.0f && min_pos_proj.x <= 1.0f && max_pos_proj.y >= -1.0f && min_pos_proj.y <= 1.0f;
}

void GLRenderer::render_primitive(const StandardRoutine::RenderParams& params, Pass pass)
{
	const MeshStandardMaterial* material = params.material_list[params.primitive->material_idx];
	const Lights* lights = params.lights;
	
	StandardRoutine::Options options;
	options.has_indices = params.primitive->index_buf != nullptr;
	options.has_lightmap = params.tex_lightmap != nullptr;
	options.alpha_mode = material->alphaMode;
	options.is_highlight_pass = pass == Pass::Highlight;
	options.specular_glossiness = material->specular_glossiness;
	options.has_color = params.primitive->color_buf != nullptr;
	options.has_color_texture = material->tex_idx_map >= 0;
	options.has_metalness_map = material->tex_idx_metalnessMap >= 0;
	options.has_roughness_map = material->tex_idx_roughnessMap >= 0;
	options.has_normal_map = material->tex_idx_normalMap >= 0;
	options.has_emissive_map = material->tex_idx_emissiveMap >= 0;
	options.has_specular_map = material->tex_idx_specularMap >= 0;
	options.has_glossiness_map = material->tex_idx_glossinessMap >= 0;
	options.num_directional_lights = lights->num_directional_lights;
	options.num_directional_shadows = lights->num_directional_shadows;	
	StandardRoutine* routine = get_routine(options);
	routine->render(params);
}


void GLRenderer::render_primitives(const StandardRoutine::RenderParams& params, Pass pass, const std::vector<int>& first_lst, const std::vector<int>& count_lst)
{
	const MeshStandardMaterial* material = params.material_list[params.primitive->material_idx];
	const Lights* lights = params.lights;

	StandardRoutine::Options options;
	options.has_indices = true;
	options.has_lightmap = params.tex_lightmap != nullptr;
	options.alpha_mode = material->alphaMode;
	options.is_highlight_pass = pass == Pass::Highlight;
	options.specular_glossiness = material->specular_glossiness;
	options.has_color = params.primitive->color_buf != nullptr;
	options.has_color_texture = material->tex_idx_map >= 0;
	options.has_metalness_map = material->tex_idx_metalnessMap >= 0;
	options.has_roughness_map = material->tex_idx_roughnessMap >= 0;
	options.has_normal_map = material->tex_idx_normalMap >= 0;
	options.has_emissive_map = material->tex_idx_emissiveMap >= 0;
	options.has_specular_map = material->tex_idx_specularMap >= 0;
	options.has_glossiness_map = material->tex_idx_glossinessMap >= 0;
	options.num_directional_lights = lights->num_directional_lights;
	options.num_directional_shadows = lights->num_directional_shadows;	
	StandardRoutine* routine = get_routine(options);
	routine->render_batched(params, first_lst, count_lst);

}


void GLRenderer::render_model(Camera* p_camera, const Lights& lights, SimpleModel* model, GLRenderTarget& target, Pass pass)
{
	const GLTexture2D* tex = &model->texture;
	if (model->repl_texture != nullptr)
	{
		tex = model->repl_texture;
	}

	const MeshStandardMaterial* material = &model->material;

	if (pass == Pass::Opaque)
	{
		if (material->alphaMode == AlphaMode::Blend) return;
	}
	else if (pass == Pass::Alpha || pass == Pass::Highlight)
	{
		if (material->alphaMode != AlphaMode::Blend) return;
	}

	StandardRoutine::RenderParams params;
	params.tex_list = &tex;
	params.material_list = &material;
	params.constant_camera = &p_camera->m_constant;
	params.constant_model = &model->m_constant;
	params.primitive = &model->geometry;
	params.lights = &lights;
	params.tex_lightmap = nullptr;
	if (model->lightmap != nullptr)
	{
		params.tex_lightmap = model->lightmap->lightmap.get();
	}

	render_primitive(params, pass);
}


void GLRenderer::render_model(Camera* p_camera, const Lights& lights, GLTFModel* model, GLRenderTarget& target, Pass pass)
{
	std::vector<const GLTexture2D*> tex_lst(model->m_textures.size());
	for (size_t i = 0; i < tex_lst.size(); i++)
	{
		auto iter = model->m_repl_textures.find(i);
		if (iter != model->m_repl_textures.end())
		{
			tex_lst[i] = iter->second;
		}
		else
		{
			tex_lst[i] = model->m_textures[i].get();
		}
	}

	std::vector<const MeshStandardMaterial*> material_lst(model->m_materials.size());
	for (size_t i = 0; i < material_lst.size(); i++)
		material_lst[i] = model->m_materials[i].get();

	if (model->batched_mesh != nullptr)
	{
		std::vector<std::vector<int>> first_lists(material_lst.size());
		std::vector<std::vector<int>> count_lists(material_lst.size());
		for (size_t i = 0; i < model->m_meshs.size(); i++)
		{
			Mesh& mesh = model->m_meshs[i];
			glm::mat4 matrix = model->matrixWorld;
			if (mesh.node_id >= 0 && mesh.skin_id < 0)
			{
				Node& node = model->m_nodes[mesh.node_id];
				matrix *= node.g_trans;
			}
			glm::mat4 MV = p_camera->matrixWorldInverse * matrix;

			for (size_t j = 0; j < mesh.primitives.size(); j++)
			{
				Primitive& primitive = mesh.primitives[j];
				if (!visible(MV, p_camera->projectionMatrix, primitive.min_pos, primitive.max_pos)) continue;

				int idx_list = primitive.material_idx;
				first_lists[idx_list].push_back(model->batch_map[i][j]);
				count_lists[idx_list].push_back(primitive.num_face * 3);
			}
		}

		{
			Mesh& mesh = *model->batched_mesh;
			for (size_t j = 0; j < mesh.primitives.size(); j++)
			{
				Primitive& primitive = mesh.primitives[j];
				int idx_material = primitive.material_idx;

				std::vector<int>& material_firsts = first_lists[idx_material];
				std::vector<int>& material_counts = count_lists[idx_material];
				if (material_firsts.size() < 1) continue;

				const MeshStandardMaterial* material = material_lst[idx_material];
				if (pass == Pass::Opaque)
				{
					if (material->alphaMode == AlphaMode::Blend) continue;
				}
				else if (pass == Pass::Alpha || pass == Pass::Highlight)
				{
					if (material->alphaMode != AlphaMode::Blend) continue;
				}

				StandardRoutine::RenderParams params;
				params.tex_list = tex_lst.data();
				params.material_list = material_lst.data();
				params.constant_camera = &p_camera->m_constant;
				params.constant_model = mesh.model_constant.get();
				params.primitive = &primitive;
				params.lights = &lights;
				params.tex_lightmap = nullptr;
				if (model->lightmap != nullptr)
				{
					params.tex_lightmap = model->lightmap->lightmap.get();
				}

				render_primitives(params, pass, material_firsts, material_counts);
			}

		}

	}
	else
	{
		for (size_t i = 0; i < model->m_meshs.size(); i++)
		{
			Mesh& mesh = model->m_meshs[i];
			glm::mat4 matrix = model->matrixWorld;
			if (mesh.node_id >= 0 && mesh.skin_id < 0)
			{
				Node& node = model->m_nodes[mesh.node_id];
				matrix *= node.g_trans;
			}
			glm::mat4 MV = p_camera->matrixWorldInverse * matrix;

			for (size_t j = 0; j < mesh.primitives.size(); j++)
			{
				Primitive& primitive = mesh.primitives[j];
				if (!visible(MV, p_camera->projectionMatrix, primitive.min_pos, primitive.max_pos)) continue;

				const MeshStandardMaterial* material = material_lst[primitive.material_idx];
				if (pass == Pass::Opaque)
				{
					if (material->alphaMode == AlphaMode::Blend) continue;
				}
				else if (pass == Pass::Alpha || pass == Pass::Highlight)
				{
					if (material->alphaMode != AlphaMode::Blend) continue;
				}

				StandardRoutine::RenderParams params;
				params.tex_list = tex_lst.data();
				params.material_list = material_lst.data();
				params.constant_camera = &p_camera->m_constant;
				params.constant_model = mesh.model_constant.get();
				params.primitive = &primitive;
				params.lights = &lights;
				params.tex_lightmap = nullptr;
				if (model->lightmap != nullptr)
				{					
					params.tex_lightmap = model->lightmap->lightmap.get();
				}
				render_primitive(params, pass);
			}
		}
	}

}

DirectionalShadowCast* GLRenderer::get_shadow_caster(const DirectionalShadowCast::Options& options)
{
	uint64_t hash = crc64(0, (const unsigned char*)&options, sizeof(DirectionalShadowCast::Options));
	auto iter = directional_shadow_caster_map.find(hash);
	if (iter == directional_shadow_caster_map.end())
	{
		directional_shadow_caster_map[hash] = std::unique_ptr<DirectionalShadowCast>(new DirectionalShadowCast(options));
	}
	return directional_shadow_caster_map[hash].get();
}


void GLRenderer::render_shadow_primitive(const DirectionalShadowCast::RenderParams& params)
{
	const MeshStandardMaterial* material = params.material_list[params.primitive->material_idx];

	DirectionalShadowCast::Options options;
	options.alpha_mode = material->alphaMode;
	options.has_color = params.primitive->color_buf != nullptr;
	options.has_color_texture = material->tex_idx_map >= 0;
	DirectionalShadowCast* shadow_caster = get_shadow_caster(options);
	shadow_caster->render(params);
}

void GLRenderer::render_shadow_primitives(const DirectionalShadowCast::RenderParams& params, const std::vector<void*>& offset_lst, const std::vector<int>& count_lst)
{
	const MeshStandardMaterial* material = params.material_list[params.primitive->material_idx];

	DirectionalShadowCast::Options options;
	options.alpha_mode = material->alphaMode;
	options.has_color = params.primitive->color_buf != nullptr;
	options.has_color_texture = material->tex_idx_map >= 0;
	DirectionalShadowCast* shadow_caster = get_shadow_caster(options);
	shadow_caster->render_batched(params, offset_lst, count_lst);
}


void GLRenderer::render_shadow_model(DirectionalLightShadow* shadow, SimpleModel* model)
{
	glm::mat4 view_matrix = glm::inverse(shadow->m_light->matrixWorld);
	if (!visible(view_matrix * model->matrixWorld, shadow->m_light_proj_matrix, model->geometry.min_pos, model->geometry.max_pos)) return;

	const GLTexture2D* tex = &model->texture;
	if (model->repl_texture != nullptr)
	{
		tex = model->repl_texture;
	}

	const MeshStandardMaterial* material = &model->material;

	DirectionalShadowCast::RenderParams params;
	params.force_cull = shadow->m_force_cull;
	params.tex_list = &tex;
	params.material_list = &material;
	params.constant_shadow = &shadow->constant_shadow;
	params.constant_model = &model->m_constant;
	params.primitive = &model->geometry;
	render_shadow_primitive(params);
}


void GLRenderer::render_shadow_model(DirectionalLightShadow* shadow, GLTFModel* model)
{
	glm::mat4 view_matrix = glm::inverse(shadow->m_light->matrixWorld);

	std::vector<const GLTexture2D*> tex_lst(model->m_textures.size());
	for (size_t i = 0; i < tex_lst.size(); i++)
	{
		auto iter = model->m_repl_textures.find(i);
		if (iter != model->m_repl_textures.end())
		{
			tex_lst[i] = iter->second;
		}
		else
		{
			tex_lst[i] = model->m_textures[i].get();
		}
	}

	std::vector<const MeshStandardMaterial*> material_lst(model->m_materials.size());
	for (size_t i = 0; i < material_lst.size(); i++)
		material_lst[i] = model->m_materials[i].get();

	if (model->batched_mesh != nullptr)
	{
		std::vector<std::vector<void*>> offset_lists(material_lst.size());
		std::vector<std::vector<int>> count_lists(material_lst.size());
		for (size_t i = 0; i < model->m_meshs.size(); i++)
		{
			Mesh& mesh = model->m_meshs[i];
			glm::mat4 matrix = model->matrixWorld;
			if (mesh.node_id >= 0 && mesh.skin_id < 0)
			{
				Node& node = model->m_nodes[mesh.node_id];
				matrix *= node.g_trans;
			}
			glm::mat4 MV = view_matrix * matrix;

			for (size_t j = 0; j < mesh.primitives.size(); j++)
			{
				Primitive& primitive = mesh.primitives[j];
				if (!visible(MV, shadow->m_light_proj_matrix, primitive.min_pos, primitive.max_pos)) continue;

				int idx_list = primitive.material_idx;
				offset_lists[idx_list].push_back((void*)(model->batch_map[i][j] * sizeof(int)));
				count_lists[idx_list].push_back(primitive.num_face * 3);
			}
		}

		{
			Mesh& mesh = *model->batched_mesh;
			for (size_t j = 0; j < mesh.primitives.size(); j++)
			{
				Primitive& primitive = mesh.primitives[j];
				int idx_material = primitive.material_idx;

				std::vector<void*>& material_offsets = offset_lists[idx_material];
				std::vector<int>& material_counts = count_lists[idx_material];
				if (material_offsets.size() < 1) continue;

				const MeshStandardMaterial* material = material_lst[primitive.material_idx];

				DirectionalShadowCast::RenderParams params;
				params.force_cull = shadow->m_force_cull;
				params.tex_list = tex_lst.data();
				params.material_list = material_lst.data();
				params.constant_shadow = &shadow->constant_shadow;
				params.constant_model = mesh.model_constant.get();
				params.primitive = &primitive;
				render_shadow_primitives(params, material_offsets, material_counts);
			}
		}

	}
	else
	{
		for (size_t i = 0; i < model->m_meshs.size(); i++)
		{
			Mesh& mesh = model->m_meshs[i];
			glm::mat4 matrix = model->matrixWorld;
			if (mesh.node_id >= 0 && mesh.skin_id < 0)
			{
				Node& node = model->m_nodes[mesh.node_id];
				matrix *= node.g_trans;
			}
			glm::mat4 MV = view_matrix * matrix;

			for (size_t j = 0; j < mesh.primitives.size(); j++)
			{
				Primitive& primitive = mesh.primitives[j];
				if (!visible(MV, shadow->m_light_proj_matrix, primitive.min_pos, primitive.max_pos)) continue;

				const MeshStandardMaterial* material = material_lst[primitive.material_idx];

				DirectionalShadowCast::RenderParams params;
				params.force_cull = shadow->m_force_cull;
				params.tex_list = tex_lst.data();
				params.material_list = material_lst.data();
				params.constant_shadow = &shadow->constant_shadow;
				params.constant_model = mesh.model_constant.get();
				params.primitive = &primitive;
				render_shadow_primitive(params);
			}
		}
	}
}


void GLRenderer::render_depth_primitive(const DepthOnly::RenderParams& params)
{
	if (DepthRenderer == nullptr)
	{
		DepthRenderer = std::unique_ptr<DepthOnly>(new DepthOnly);
	}
	DepthRenderer->render(params);
}

void GLRenderer::render_depth_primitives(const DepthOnly::RenderParams& params, const std::vector<void*>& offset_lst, const std::vector<int>& count_lst)
{
	if (DepthRenderer == nullptr)
	{
		DepthRenderer = std::unique_ptr<DepthOnly>(new DepthOnly);
	}
	DepthRenderer->render_batched(params, offset_lst, count_lst);

}

void GLRenderer::render_depth_model(Camera* p_camera, SimpleModel* model)
{
	glm::mat4 view_matrix = p_camera->matrixWorldInverse;
	if (!visible(view_matrix * model->matrixWorld, p_camera->projectionMatrix, model->geometry.min_pos, model->geometry.max_pos)) return;

	const MeshStandardMaterial* material = &model->material;
	if (material->alphaMode != AlphaMode::Opaque) return;

	DepthOnly::RenderParams params;
	params.material_list = &material;
	params.constant_camera = &p_camera->m_constant;
	params.constant_model = &model->m_constant;
	params.primitive = &model->geometry;
	render_depth_primitive(params);
}


void GLRenderer::render_depth_model(Camera* p_camera, GLTFModel* model)
{
	glm::mat4 view_matrix = p_camera->matrixWorldInverse;

	std::vector<const MeshStandardMaterial*> material_lst(model->m_materials.size());
	for (size_t i = 0; i < material_lst.size(); i++)
		material_lst[i] = model->m_materials[i].get();

	if (model->batched_mesh != nullptr)
	{
		std::vector<std::vector<void*>> offset_lists(material_lst.size());
		std::vector<std::vector<int>> count_lists(material_lst.size());
		for (size_t i = 0; i < model->m_meshs.size(); i++)
		{
			Mesh& mesh = model->m_meshs[i];
			glm::mat4 matrix = model->matrixWorld;
			if (mesh.node_id >= 0 && mesh.skin_id < 0)
			{
				Node& node = model->m_nodes[mesh.node_id];
				matrix *= node.g_trans;
			}
			glm::mat4 MV = p_camera->matrixWorldInverse * matrix;

			for (size_t j = 0; j < mesh.primitives.size(); j++)
			{
				Primitive& primitive = mesh.primitives[j];
				if (!visible(MV, p_camera->projectionMatrix, primitive.min_pos, primitive.max_pos)) continue;

				int idx_list = primitive.material_idx;
				offset_lists[idx_list].push_back((void*)(model->batch_map[i][j] * sizeof(int)));
				count_lists[idx_list].push_back(primitive.num_face * 3);
			}
		}

		{
			Mesh& mesh = *model->batched_mesh;
			for (size_t j = 0; j < mesh.primitives.size(); j++)
			{
				Primitive& primitive = mesh.primitives[j];
				int idx_material = primitive.material_idx;

				std::vector<void*>& material_offsets = offset_lists[idx_material];
				std::vector<int>& material_counts = count_lists[idx_material];
				if (material_offsets.size() < 1) continue;

				const MeshStandardMaterial* material = material_lst[idx_material];
				if (material->alphaMode != AlphaMode::Opaque) continue;

				DepthOnly::RenderParams params;
				params.material_list = material_lst.data();
				params.constant_camera = &p_camera->m_constant;
				params.constant_model = mesh.model_constant.get();
				params.primitive = &primitive;
				render_depth_primitives(params, material_offsets, material_counts);

			}

		}
	}
	else
	{

		for (size_t i = 0; i < model->m_meshs.size(); i++)
		{
			Mesh& mesh = model->m_meshs[i];
			glm::mat4 matrix = model->matrixWorld;
			if (mesh.node_id >= 0 && mesh.skin_id < 0)
			{
				Node& node = model->m_nodes[mesh.node_id];
				matrix *= node.g_trans;
			}
			glm::mat4 MV = view_matrix * matrix;

			for (size_t j = 0; j < mesh.primitives.size(); j++)
			{
				Primitive& primitive = mesh.primitives[j];
				if (!visible(MV, p_camera->projectionMatrix, primitive.min_pos, primitive.max_pos)) continue;

				const MeshStandardMaterial* material = material_lst[primitive.material_idx];
				if (material->alphaMode != AlphaMode::Opaque) continue;

				DepthOnly::RenderParams params;
				params.material_list = material_lst.data();
				params.constant_camera = &p_camera->m_constant;
				params.constant_model = mesh.model_constant.get();
				params.primitive = &primitive;
				render_depth_primitive(params);
			}
		}
	}
}

void GLRenderer::_pre_render(Scene& scene)
{
	scene.clear_lists();

	auto* p_scene = &scene;
	scene.traverse([p_scene](Object3D* obj) {
		do
		{
			{
				SimpleModel* model = dynamic_cast<SimpleModel*>(obj);
				if (model)
				{
					p_scene->simple_models.push_back(model);
					break;
				}
			}
			{
				GLTFModel* model = dynamic_cast<GLTFModel*>(obj);
				if (model)
				{
					p_scene->gltf_models.push_back(model);
					break;
				}
			}
			{
				DirectionalLight* light = dynamic_cast<DirectionalLight*>(obj);
				if (light)
				{
					light->lookAtTarget();
					p_scene->directional_lights.push_back(light);
					break;
				}
			}
		} while (false);

		obj->updateWorldMatrix(false, false);
	});

	// update models
	for (size_t i = 0; i < scene.simple_models.size(); i++)
	{
		SimpleModel* model = scene.simple_models[i];
		update_model(model);
	}

	for (size_t i = 0; i < scene.gltf_models.size(); i++)
	{
		GLTFModel* model = scene.gltf_models[i];
		update_model(model);
	}

	// update lights
	for (size_t i = 0; i < scene.directional_lights.size(); i++)
	{
		DirectionalLight* light = scene.directional_lights[i];
		if (light->shadow != nullptr)
		{
			light->shadow->updateMatrices();

			
			glBindFramebuffer(GL_FRAMEBUFFER, light->shadow->m_lightFBO);
			glViewport(0, 0, light->shadow->m_map_width, light->shadow->m_map_height);
			const float one = 1.0f;
			glDepthMask(GL_TRUE);
			glClearBufferfv(GL_DEPTH, 0, &one);			

			for (size_t j = 0; j < scene.simple_models.size(); j++)
			{
				SimpleModel* model = scene.simple_models[j];
				render_shadow_model(light->shadow.get(), model);
			}
			for (size_t j = 0; j < scene.gltf_models.size(); j++)
			{
				GLTFModel* model = scene.gltf_models[j];
				render_shadow_model(light->shadow.get(), model);
			}

		}
	}

	// update light constants
	Lights& lights = scene.lights;
	lights.directional_shadow_texs.clear();

	std::vector<ConstDirectionalLight> const_directional_lights(scene.directional_lights.size());
	std::vector<ConstDirectionalShadow> const_directional_shadows;
	for (size_t i = 0; i < scene.directional_lights.size(); i++)
	{
		DirectionalLight* light = scene.directional_lights[i];
		ConstDirectionalLight& const_light = const_directional_lights[i];
		light->makeConst(const_light);

		if (light->shadow != nullptr)
		{
			lights.directional_shadow_texs.push_back(light->shadow->m_lightTex);
			ConstDirectionalShadow constShadow;
			light->shadow->makeConst(constShadow);
			const_directional_shadows.push_back(std::move(constShadow));
		}
	}

	{
		if (lights.num_directional_lights != (int)const_directional_lights.size())
		{
			lights.num_directional_lights = (int)const_directional_lights.size();
			lights.constant_directional_lights = nullptr;
			if (lights.num_directional_lights > 0)
			{
				lights.constant_directional_lights = std::unique_ptr<GLDynBuffer>(new GLDynBuffer(const_directional_lights.size() * sizeof(ConstDirectionalLight), GL_UNIFORM_BUFFER));
			}
			lights.hash_directional_lights = 0;

		}
		if (lights.num_directional_lights > 0)
		{
			uint64_t hash = crc64(0, (unsigned char*)const_directional_lights.data(), const_directional_lights.size() * sizeof(ConstDirectionalLight));
			if (hash != lights.hash_directional_lights)
			{
				lights.hash_directional_lights = hash;
				lights.constant_directional_lights->upload(const_directional_lights.data());
			}
		}
	}

	{
		if (lights.num_directional_shadows != (int)const_directional_shadows.size())
		{
			lights.num_directional_shadows = (int)const_directional_shadows.size();
			lights.constant_directional_shadows = nullptr;
			if (lights.num_directional_shadows > 0)
			{
				lights.constant_directional_shadows = std::unique_ptr<GLDynBuffer>(new GLDynBuffer(const_directional_shadows.size() * sizeof(ConstDirectionalShadow), GL_UNIFORM_BUFFER));
			}
			lights.hash_directional_shadows = 0;
		}
		if (lights.num_directional_shadows > 0)
		{
			uint64_t hash = crc64(0, (unsigned char*)const_directional_shadows.data(), const_directional_shadows.size() * sizeof(ConstDirectionalShadow));
			if (hash != lights.hash_directional_shadows)
			{
				lights.hash_directional_shadows = hash;
				lights.constant_directional_shadows->upload(const_directional_shadows.data());
			}
		}
	}

}

void GLRenderer::_render_scene(Scene& scene, Camera& camera, GLRenderTarget& target)
{
	camera.updateMatrixWorld(false);
	camera.updateConstant();

	// model culling
	std::vector<SimpleModel*> simple_models = scene.simple_models;
	std::vector<GLTFModel*> gltf_models = scene.gltf_models;

	bool has_alpha = false;
	bool has_opaque = false;

	for (size_t i = 0; i < simple_models.size(); i++)
	{
		SimpleModel* model = simple_models[i];
		if (!visible(camera.matrixWorldInverse * model->matrixWorld, camera.projectionMatrix, model->geometry.min_pos, model->geometry.max_pos))
		{			
			simple_models.erase(simple_models.begin() + i);
			i--;
		}
		else
		{			
			const MeshStandardMaterial* material = &model->material;
			if (material->alphaMode == AlphaMode::Blend)
			{
				has_alpha = true;
			}
			else
			{
				has_opaque = true;
			}
		}
	}

	for (size_t i = 0; i < gltf_models.size(); i++)
	{
		GLTFModel* model = gltf_models[i];
		if (!visible(camera.matrixWorldInverse * model->matrixWorld, camera.projectionMatrix, model->m_min_pos, model->m_max_pos))
		{
			gltf_models.erase(gltf_models.begin() + i);
			i--;
		}
		else
		{
			size_t num_materials = model->m_materials.size();
			for (size_t i = 0; i < num_materials; i++)
			{
				const MeshStandardMaterial* material = model->m_materials[i].get();
				if (material->alphaMode == AlphaMode::Blend)
				{
					has_alpha = true;
				}
				else
				{
					has_opaque = true;
				}
			}
		}
	}

	// render scene
	target.bind_buffer();
	glEnable(GL_FRAMEBUFFER_SRGB);
	glViewport(0, 0, target.m_width, target.m_height);

	while (scene.background != nullptr)
	{
		{
			ColorBackground* bg = dynamic_cast<ColorBackground*>(scene.background);
			if (bg != nullptr)
			{
				glClearColor(bg->color.r, bg->color.g, bg->color.b, 1.0f);
				glClear(GL_COLOR_BUFFER_BIT);
				break;
			}
			{
				CubeBackground* bg = dynamic_cast<CubeBackground*>(scene.background);
				if (bg != nullptr)
				{
					if (SkyBoxDraw == nullptr)
					{
						SkyBoxDraw = std::unique_ptr<DrawSkyBox>(new DrawSkyBox);
					}
					SkyBoxDraw->render(&camera.m_constant, &bg->cubemap);
					break;
				}
			}
			{
				HemisphereBackground* bg = dynamic_cast<HemisphereBackground*>(scene.background);
				if (bg != nullptr)
				{
					bg->updateConstant();
					if (HemisphereDraw == nullptr)
					{
						HemisphereDraw = std::unique_ptr<DrawHemisphere>(new DrawHemisphere);
					}
					HemisphereDraw->render(&camera.m_constant, &bg->m_constant);
					break;
				}
			}
		}		
		break;
	}

	Lights& lights = scene.lights;

	glDepthMask(GL_TRUE);
	glClearDepth(1.0f);
	glClear(GL_DEPTH_BUFFER_BIT);

	if (has_opaque)
	{
		glDisable(GL_BLEND);

		// depth-prepass
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		for (size_t i = 0; i < simple_models.size(); i++)
		{
			SimpleModel* model = simple_models[i];
			render_depth_model(&camera, model);
		}

		for (size_t i = 0; i < gltf_models.size(); i++)
		{
			GLTFModel* model = gltf_models[i];
			render_depth_model(&camera, model);
		}

		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	}

	if (has_opaque)
	{
		// opaque
		for (size_t i = 0; i < simple_models.size(); i++)
		{
			SimpleModel* model = simple_models[i];
			render_model(&camera, lights, model, target, Pass::Opaque);
		}

		for (size_t i = 0; i < gltf_models.size(); i++)
		{
			GLTFModel* model = gltf_models[i];
			render_model(&camera, lights, model, target, Pass::Opaque);
		}

	}


	glDepthMask(GL_FALSE);

	if (has_alpha)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);

		for (size_t i = 0; i < simple_models.size(); i++)
		{
			SimpleModel* model = simple_models[i];
			render_model(&camera, lights, model, target, Pass::Highlight);
		}

		for (size_t i = 0; i < gltf_models.size(); i++)
		{
			GLTFModel* model = gltf_models[i];
			render_model(&camera, lights, model, target, Pass::Highlight);
		}
		
		target.update_oit_buffers();
		if (!target.msaa())
		{
			if (oit_resolvers[0] == nullptr)
			{
				oit_resolvers[0] = std::unique_ptr<WeightedOIT>(new WeightedOIT(false));
			}
			oit_resolvers[0]->PreDraw(target.m_OITBuffers);
		}
		else
		{
			if (oit_resolvers[1] == nullptr)
			{
				oit_resolvers[1] = std::unique_ptr<WeightedOIT>(new WeightedOIT(true));
			}
			oit_resolvers[1]->PreDraw(target.m_OITBuffers);
		}

		for (size_t i = 0; i < simple_models.size(); i++)
		{
			SimpleModel* model = simple_models[i];
			render_model(&camera, lights, model, target, Pass::Alpha);
		}

		target.bind_buffer();

		if (!target.msaa())
		{
			oit_resolvers[0]->PostDraw(target.m_OITBuffers);
		}
		else
		{
			oit_resolvers[1]->PostDraw(target.m_OITBuffers);
		}
	}
}

void GLRenderer::_render(Scene& scene, Camera& camera, GLRenderTarget& target)
{
	_render_scene(scene, camera, target);		
	if (target.msaa())
	{
		target.resolve_msaa();
	}
}

GLRenderTarget* g_target = nullptr;

void GLRenderer::render(Scene& scene, Camera& camera, GLRenderTarget& target)
{
	_pre_render(scene);
	_render(scene, camera, target);			
	//_render_bvh(scene, camera, target);

	g_target = &target;
}

RasterizeAtlas* GLRenderer::get_atlas_rasterizer(const RasterizeAtlas::Options& options)
{
	uint64_t hash = crc64(0, (const unsigned char*)&options, sizeof(RasterizeAtlas::Options));
	auto iter = routine_map.find(hash);
	if (iter == routine_map.end())
	{
		atlas_rasterizer_map[hash] = std::unique_ptr<RasterizeAtlas>(new RasterizeAtlas(options));
	}
	return atlas_rasterizer_map[hash].get();
}

void GLRenderer::rasterize_atlas_primitive(const RasterizeAtlas::RenderParams & params)
{
	const MeshStandardMaterial* material = params.material_list[params.primitive->material_idx];

	RasterizeAtlas::Options options;
	options.has_indices = params.primitive->index_buf != nullptr;
	options.has_normal_map = material->tex_idx_normalMap >= 0;
	RasterizeAtlas* routine = get_atlas_rasterizer(options);

	glLineWidth(2.0f);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	routine->render(params);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	routine->render(params);
}

void GLRenderer::rasterize_atlas(SimpleModel* model)
{
	model->updateWorldMatrix(false, false);
	update_model(model);

	glBindFramebuffer(GL_FRAMEBUFFER, model->lightmap_target->m_fbo);

	const GLenum drawBuffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glDrawBuffers(2, drawBuffers);
	glViewport(0, 0, model->lightmap_target->m_width, model->lightmap_target->m_height);

	float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glClearBufferfv(GL_COLOR, 0, zero);
	glClearBufferfv(GL_COLOR, 1, zero);

	const GLTexture2D* tex = &model->texture;
	if (model->repl_texture != nullptr)
	{
		tex = model->repl_texture;
	}

	const MeshStandardMaterial* material = &model->material;

	RasterizeAtlas::RenderParams params;
	params.tex_list = &tex;
	params.material_list = &material;
	params.constant_model = &model->m_constant;
	params.primitive = &model->geometry;
	rasterize_atlas_primitive(params);
}

void GLRenderer::rasterize_atlas(GLTFModel* model)
{
	model->updateWorldMatrix(false, false);
	update_model(model);

	glBindFramebuffer(GL_FRAMEBUFFER, model->lightmap_target->m_fbo);

	const GLenum drawBuffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glDrawBuffers(2, drawBuffers);
	glViewport(0, 0, model->lightmap_target->m_width, model->lightmap_target->m_height);

	float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glClearBufferfv(GL_COLOR, 0, zero);
	glClearBufferfv(GL_COLOR, 1, zero);

	std::vector<const GLTexture2D*> tex_lst(model->m_textures.size());
	for (size_t i = 0; i < tex_lst.size(); i++)
	{
		auto iter = model->m_repl_textures.find(i);
		if (iter != model->m_repl_textures.end())
		{
			tex_lst[i] = iter->second;
		}
		else
		{
			tex_lst[i] = model->m_textures[i].get();
		}
	}

	std::vector<const MeshStandardMaterial*> material_lst(model->m_materials.size());
	for (size_t i = 0; i < material_lst.size(); i++)
		material_lst[i] = model->m_materials[i].get();

	if (model->batched_mesh != nullptr)
	{
		Mesh& mesh = *model->batched_mesh;
		for (size_t j = 0; j < mesh.primitives.size(); j++)
		{
			Primitive& primitive = mesh.primitives[j];

			RasterizeAtlas::RenderParams params;
			params.tex_list = tex_lst.data();
			params.material_list = material_lst.data();
			params.constant_model = mesh.model_constant.get();
			params.primitive = &primitive;
			rasterize_atlas_primitive(params);
		}
	}
	else
	{
		for (size_t i = 0; i < model->m_meshs.size(); i++)
		{
			Mesh& mesh = model->m_meshs[i];
			for (size_t j = 0; j < mesh.primitives.size(); j++)
			{
				Primitive& primitive = mesh.primitives[j];				

				RasterizeAtlas::RenderParams params;
				params.tex_list = tex_lst.data();
				params.material_list = material_lst.data();				
				params.constant_model = mesh.model_constant.get();
				params.primitive = &primitive;
				rasterize_atlas_primitive(params);
			}
		}
	}
}

void GLRenderer::renderTexture(GLTexture2D* tex, int x, int y, int width, int height, GLRenderTarget& target, bool flipY, float alpha)
{
	if (TextureDraw == nullptr)
	{
		TextureDraw = std::unique_ptr<DrawTexture>(new DrawTexture(false, flipY));
	}
	glBindFramebuffer(GL_FRAMEBUFFER, target.m_fbo_video);
	glEnable(GL_FRAMEBUFFER_SRGB);
	TextureDraw->render(tex->tex_id, x, target.m_height - (y + height), width, height, alpha < 1.0f, alpha);
}

void GLRenderer::_render_bvh(Scene& scene, Camera& camera, GLRenderTarget& target)
{
	camera.updateMatrixWorld(false);
	camera.updateConstant();

	bvh_target.update(target.m_width, target.m_height);
	bvh_renderer.render(scene, camera, bvh_target);
	renderTexture(bvh_target.m_tex_video.get(), 0, 0, target.m_width, target.m_height, target, false);
}


int GLRenderer::updateLightmap(Scene& scene, Lightmap& lm, LightmapRenderTarget& src, int start_texel, int num_directions)
{
	int max_texels = (1 << 17) / num_directions;
	if (max_texels < 1) max_texels = 1;

	int num_texels = src.count_valid - start_texel;
	if (num_texels > max_texels) num_texels = max_texels;

	int width = 512;
	if (width < num_directions) width = num_directions;

	int texels_per_row = width / num_directions;

	int height = (num_texels + texels_per_row - 1) / texels_per_row;	
	
	BVHRenderTarget bvh_target;
	bvh_target.update(width, height);

	LightmapRayList lmrl(&src, &bvh_target, start_texel, start_texel + num_texels, num_directions);
	bvh_renderer.render_lightmap(scene, lmrl, bvh_target);

	bvh_renderer.update_lightmap(bvh_target, lmrl, lm, start_texel, 1.0f);

	/*if (g_target != nullptr)
	{
		renderTexture(bvh_target.m_tex_video.get(), 10, 10, 512, 256, *g_target);
	}*/

	return num_texels;

}

void GLRenderer::filterLightmap(Lightmap& lm, LightmapRenderTarget& src)
{
	bvh_renderer.filter_lightmap(src, lm);
}
