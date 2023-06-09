#include <GL/glew.h>
#include "crc64/crc64.h"
#include "GLUtils.h"
#include "BVHRenderer.h"
#include "BVHRenderTarget.h"
#include "cameras/Camera.h"
#include "cameras/PerspectiveCamera.h"
#include "scenes/Scene.h"
#include "backgrounds/Background.h"
#include "models/ModelComponents.h"
#include "models/SimpleModel.h"
#include "models/GLTFModel.h"
#include "lights/DirectionalLight.h"
#include "lights/DirectionalLightShadow.h"
#include "renderers/LightmapRenderTarget.h"

void BVHRenderer::check_bvh(SimpleModel* model)
{
	if (model->geometry.cwbvh == nullptr)
	{
		model->geometry.cwbvh = std::unique_ptr<CWBVH>(new CWBVH(&model->geometry, model->matrixWorld));
	}
}

void BVHRenderer::check_bvh(GLTFModel* model)
{
	if (model->batched_mesh == nullptr)
	{
		model->batch_primitives();
	}

	//for (size_t i = 0; i < model->m_meshs.size(); i++)
	{
		//Mesh& mesh = model->m_meshs[i];
		Mesh& mesh = *model->batched_mesh;
		glm::mat4 matrix = model->matrixWorld;
		if (mesh.node_id >= 0 && mesh.skin_id < 0)
		{
			Node& node = model->m_nodes[mesh.node_id];
			matrix *= node.g_trans;
		}

		for (size_t j = 0; j < mesh.primitives.size(); j++)
		{
			Primitive& primitive = mesh.primitives[j];
			if (primitive.cwbvh == nullptr)
			{
				primitive.cwbvh = std::unique_ptr<CWBVH>(new CWBVH(&primitive, matrix));
			}
		}
	}
}



void BVHRenderer::render_depth_primitive(const BVHDepthOnly::RenderParams& params)
{
	const Primitive* prim = params.primitive;
	if (DepthRenderer == nullptr)
	{
		DepthRenderer = std::unique_ptr<BVHDepthOnly>(new BVHDepthOnly);
	}
	DepthRenderer->render(params);
}


void BVHRenderer::render_depth_model(Camera* p_camera, SimpleModel* model, BVHRenderTarget& target)
{
	const MeshStandardMaterial* material = &model->material;
	if (material->alphaMode != AlphaMode::Opaque) return;

	BVHDepthOnly::RenderParams params;
	params.material_list = &material;
	params.primitive = &model->geometry;
	params.target = &target;
	params.constant_camera = &p_camera->m_constant;
	render_depth_primitive(params);
}


void BVHRenderer::render_depth_model(Camera* p_camera, GLTFModel* model, BVHRenderTarget& target)
{
	std::vector<const MeshStandardMaterial*> material_lst(model->m_materials.size());
	for (size_t i = 0; i < material_lst.size(); i++)
		material_lst[i] = model->m_materials[i].get();

	//for (size_t i = 0; i < model->m_meshs.size(); i++)
	{
		//Mesh& mesh = model->m_meshs[i];
		Mesh& mesh = *model->batched_mesh;

		for (size_t j = 0; j < mesh.primitives.size(); j++)
		{
			Primitive& primitive = mesh.primitives[j];

			const MeshStandardMaterial* material = material_lst[primitive.material_idx];
			if (material->alphaMode != AlphaMode::Opaque) continue;

			BVHDepthOnly::RenderParams params;
			params.material_list = material_lst.data();
			params.primitive = &primitive;
			params.target = &target;
			params.constant_camera = &p_camera->m_constant;
			render_depth_primitive(params);
		}
	}
}


BVHRoutine* BVHRenderer::get_routine(const BVHRoutine::Options& options)
{
	uint64_t hash = crc64(0, (const unsigned char*)&options, sizeof(BVHRoutine::Options));
	auto iter = routine_map.find(hash);
	if (iter == routine_map.end())
	{
		routine_map[hash] = std::unique_ptr<BVHRoutine>(new BVHRoutine(options));
	}
	return routine_map[hash].get();
}

void BVHRenderer::render_primitive(const BVHRoutine::RenderParams& params, Pass pass)
{
	const MeshStandardMaterial* material = params.material_list[params.primitive->material_idx];
	const Lights* lights = params.lights;

	BVHRoutine::Options options;	
	options.has_lightmap = params.tex_lightmap != nullptr;
	options.alpha_mode = material->alphaMode;
	options.specular_glossiness = material->specular_glossiness;
	options.has_color = params.primitive->color_buf != nullptr;
	options.has_color_texture = material->tex_idx_map >= 0;
	options.has_metalness_map = material->tex_idx_metalnessMap >= 0;
	options.has_roughness_map = material->tex_idx_roughnessMap >= 0;
	options.has_emissive_map = material->tex_idx_emissiveMap >= 0;
	options.has_specular_map = material->tex_idx_specularMap >= 0;
	options.has_glossiness_map = material->tex_idx_glossinessMap >= 0;
	options.num_directional_lights = lights->num_directional_lights;
	options.num_directional_shadows = lights->num_directional_shadows;	
	BVHRoutine* routine = get_routine(options);
	routine->render(params);
}


void BVHRenderer::render_model(Camera* p_camera, const Lights& lights, SimpleModel* model, Pass pass, BVHRenderTarget& target)
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
	else if (pass == Pass::Alpha)
	{
		if (material->alphaMode != AlphaMode::Blend) return;
	}

	BVHRoutine::RenderParams params;
	params.tex_list = &tex;
	params.material_list = &material;
	params.constant_model = &model->m_constant;
	params.primitive = &model->geometry;	
	params.lights = &lights;
	params.tex_lightmap = nullptr;
	if (model->lightmap != nullptr)
	{
		params.tex_lightmap = model->lightmap->lightmap.get();
	}

	params.target = &target;
	params.constant_camera = &p_camera->m_constant;

	render_primitive(params, pass);
}


void BVHRenderer::render_model(Camera* p_camera, const Lights& lights, GLTFModel* model, Pass pass, BVHRenderTarget& target)
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

	//for (size_t i = 0; i < model->m_meshs.size(); i++)
	{
		//Mesh& mesh = model->m_meshs[i];
		Mesh& mesh = *model->batched_mesh;

		for (size_t j = 0; j < mesh.primitives.size(); j++)
		{
			Primitive& primitive = mesh.primitives[j];

			const MeshStandardMaterial* material = material_lst[primitive.material_idx];
			if (pass == Pass::Opaque)
			{
				if (material->alphaMode == AlphaMode::Blend) continue;
			}
			else if (pass == Pass::Alpha)
			{
				if (material->alphaMode != AlphaMode::Blend) continue;
			}

			BVHRoutine::RenderParams params;
			params.tex_list = tex_lst.data();
			params.material_list = material_lst.data();
			params.constant_model = mesh.model_constant.get();
			params.primitive = &primitive;
			params.lights = &lights;
			params.tex_lightmap = nullptr;
			if (model->lightmap != nullptr)
			{
				params.tex_lightmap = model->lightmap->lightmap.get();
			}

			params.target = &target;
			params.constant_camera = &p_camera->m_constant;

			render_primitive(params, pass);
		}
	}

}


void BVHRenderer::render(Scene& scene, Camera& camera, BVHRenderTarget& target)
{
	bool has_alpha = false;
	bool has_opaque = false;

	for (size_t i = 0; i < scene.simple_models.size(); i++)
	{
		SimpleModel* model = scene.simple_models[i];
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

	for (size_t i = 0; i < scene.gltf_models.size(); i++)
	{
		GLTFModel* model = scene.gltf_models[i];
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

	while (scene.background != nullptr)
	{
		{
			ColorBackground* bg = dynamic_cast<ColorBackground*>(scene.background);
			if (bg != nullptr)
			{
				glm::vec4 color = { bg->color.r, bg->color.g, bg->color.b, 1.0f };
				glClearTexImage(target.m_tex_video->tex_id, 0, GL_RGBA, GL_FLOAT, &color);
				break;
			}
		}
		{
			CubeBackground* bg = dynamic_cast<CubeBackground*>(scene.background);
			if (bg != nullptr)
			{				
				if (SkyBoxDraw == nullptr)
				{
					SkyBoxDraw = std::unique_ptr<CompSkyBox>(new CompSkyBox);
				}
				SkyBoxDraw->render(&camera.m_constant, &bg->cubemap, &target);
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
					HemisphereDraw = std::unique_ptr<CompHemisphere>(new CompHemisphere);
				}
				HemisphereDraw->render(&camera.m_constant, &bg->m_constant, &target);
				break;
			}
		}
		break;
	}

	for (size_t i = 0; i < scene.simple_models.size(); i++)
	{
		SimpleModel* model = scene.simple_models[i];
		check_bvh(model);
	}

	for (size_t i = 0; i < scene.gltf_models.size(); i++)
	{
		GLTFModel* model = scene.gltf_models[i];
		check_bvh(model);
	}

	Lights& lights = scene.lights;

	float max_depth = FLT_MAX;
	glClearTexImage(target.m_tex_depth->tex_id, 0, GL_RED, GL_FLOAT, &max_depth);

	if (has_opaque)
	{
		// depth-prepass
		for (size_t i = 0; i < scene.simple_models.size(); i++)
		{
			SimpleModel* model = scene.simple_models[i];
			render_depth_model(&camera, model, target);
		}

		for (size_t i = 0; i < scene.gltf_models.size(); i++)
		{
			GLTFModel* model = scene.gltf_models[i];
			render_depth_model(&camera, model, target);
		}

		// opaque
		for (size_t i = 0; i < scene.simple_models.size(); i++)
		{
			SimpleModel* model = scene.simple_models[i];
			render_model(&camera, lights, model, Pass::Opaque, target);
		}

		for (size_t i = 0; i < scene.gltf_models.size(); i++)
		{
			GLTFModel* model = scene.gltf_models[i];
			render_model(&camera, lights, model, Pass::Opaque, target);
		}
	}

	if (has_alpha)
	{
		target.update_oit_buffers();

		if (oit_resolver == nullptr)
		{
			oit_resolver = std::unique_ptr<CompWeightedOIT>(new CompWeightedOIT);
		}
		oit_resolver->PreDraw(target.m_OITBuffers);

		for (size_t i = 0; i < scene.simple_models.size(); i++)
		{
			SimpleModel* model = scene.simple_models[i];
			render_model(&camera, lights, model, Pass::Alpha, target);
		}

		for (size_t i = 0; i < scene.gltf_models.size(); i++)
		{
			GLTFModel* model = scene.gltf_models[i];
			render_model(&camera, lights, model, Pass::Alpha, target);
		}

		oit_resolver->PostDraw(&target);
	}
}

void BVHRenderer::render_lightmap_depth_primitive(const BVHDepthOnly::RenderParams& params)
{
	const Primitive* prim = params.primitive;
	if (LightmapDepthRenderer == nullptr)
	{
		LightmapDepthRenderer = std::unique_ptr<BVHDepthOnly>(new BVHDepthOnly(2));
	}
	LightmapDepthRenderer->render(params);
}

void BVHRenderer::render_lightmap_depth_model(LightmapRayList& lmrl, SimpleModel* model, BVHRenderTarget& target)
{
	const MeshStandardMaterial* material = &model->material;
	if (material->alphaMode != AlphaMode::Opaque) return;

	BVHDepthOnly::RenderParams params;
	params.material_list = &material;
	params.primitive = &model->geometry;
	params.target = &target;
	params.lmrl = &lmrl;
	render_lightmap_depth_primitive(params);
}

void BVHRenderer::render_lightmap_depth_model(LightmapRayList& lmrl, GLTFModel* model, BVHRenderTarget& target)
{
	std::vector<const MeshStandardMaterial*> material_lst(model->m_materials.size());
	for (size_t i = 0; i < material_lst.size(); i++)
		material_lst[i] = model->m_materials[i].get();

	//for (size_t i = 0; i < model->m_meshs.size(); i++)
	{
		//Mesh& mesh = model->m_meshs[i];
		Mesh& mesh = *model->batched_mesh;

		for (size_t j = 0; j < mesh.primitives.size(); j++)
		{
			Primitive& primitive = mesh.primitives[j];

			const MeshStandardMaterial* material = material_lst[primitive.material_idx];
			if (material->alphaMode != AlphaMode::Opaque) continue;

			BVHDepthOnly::RenderParams params;
			params.material_list = material_lst.data();
			params.primitive = &primitive;
			params.target = &target;
			params.lmrl = &lmrl;
			render_lightmap_depth_primitive(params);
		}
	}
}


BVHRoutine* BVHRenderer::get_lightmap_routine(const BVHRoutine::Options& options)
{
	uint64_t hash = crc64(0, (const unsigned char*)&options, sizeof(BVHRoutine::Options));
	auto iter = lightmap_routine_map.find(hash);
	if (iter == lightmap_routine_map.end())
	{
		lightmap_routine_map[hash] = std::unique_ptr<BVHRoutine>(new BVHRoutine(options));
	}
	return lightmap_routine_map[hash].get();
}


void BVHRenderer::render_lightmap_primitive(const BVHRoutine::RenderParams& params, Pass pass)
{
	const MeshStandardMaterial* material = params.material_list[params.primitive->material_idx];
	const Lights* lights = params.lights;

	BVHRoutine::Options options;
	options.target_mode = 2;
	options.has_lightmap = params.tex_lightmap != nullptr;
	options.alpha_mode = material->alphaMode;
	options.specular_glossiness = material->specular_glossiness;
	options.has_color = params.primitive->color_buf != nullptr;
	options.has_color_texture = material->tex_idx_map >= 0;
	options.has_metalness_map = material->tex_idx_metalnessMap >= 0;
	options.has_roughness_map = material->tex_idx_roughnessMap >= 0;
	options.has_emissive_map = material->tex_idx_emissiveMap >= 0;
	options.has_specular_map = material->tex_idx_specularMap >= 0;
	options.has_glossiness_map = material->tex_idx_glossinessMap >= 0;
	options.num_directional_lights = lights->num_directional_lights;
	options.num_directional_shadows = lights->num_directional_shadows;	
	BVHRoutine* routine = get_lightmap_routine(options);
	routine->render(params);
}

void BVHRenderer::render_lightmap_model(LightmapRayList& lmrl, const Lights& lights, SimpleModel* model, Pass pass, BVHRenderTarget& target)
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
	else if (pass == Pass::Alpha)
	{
		if (material->alphaMode != AlphaMode::Blend) return;
	}

	BVHRoutine::RenderParams params;
	params.tex_list = &tex;
	params.material_list = &material;
	params.constant_model = &model->m_constant;
	params.primitive = &model->geometry;
	params.lights = &lights;
	params.tex_lightmap = nullptr;
	if (model->lightmap != nullptr)
	{		
		params.tex_lightmap = model->lightmap->lightmap.get();
	}

	params.target = &target;
	params.lmrl = &lmrl;

	render_lightmap_primitive(params, pass);
}

void BVHRenderer::render_lightmap_model(LightmapRayList& lmrl, const Lights& lights, GLTFModel* model, Pass pass, BVHRenderTarget& target)
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

	//for (size_t i = 0; i < model->m_meshs.size(); i++)
	{
		//Mesh& mesh = model->m_meshs[i];
		Mesh& mesh = *model->batched_mesh;

		for (size_t j = 0; j < mesh.primitives.size(); j++)
		{
			Primitive& primitive = mesh.primitives[j];

			const MeshStandardMaterial* material = material_lst[primitive.material_idx];
			if (pass == Pass::Opaque)
			{
				if (material->alphaMode == AlphaMode::Blend) continue;
			}
			else if (pass == Pass::Alpha)
			{
				if (material->alphaMode != AlphaMode::Blend) continue;
			}

			BVHRoutine::RenderParams params;
			params.tex_list = tex_lst.data();
			params.material_list = material_lst.data();
			params.constant_model = mesh.model_constant.get();
			params.primitive = &primitive;
			params.lights = &lights;
			params.tex_lightmap = nullptr;
			if (model->lightmap != nullptr)
			{
				params.tex_lightmap = model->lightmap->lightmap.get();
			}

			params.target = &target;
			params.lmrl = &lmrl;

			render_lightmap_primitive(params, pass);
		}
	}
}


void BVHRenderer::render_lightmap(Scene& scene, LightmapRayList& lmrl, BVHRenderTarget& target)
{
	bool has_alpha = false;
	bool has_opaque = false;

	for (size_t i = 0; i < scene.simple_models.size(); i++)
	{
		SimpleModel* model = scene.simple_models[i];
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

	for (size_t i = 0; i < scene.gltf_models.size(); i++)
	{
		GLTFModel* model = scene.gltf_models[i];
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

	while (scene.background != nullptr)
	{
		{
			ColorBackground* bg = dynamic_cast<ColorBackground*>(scene.background);
			if (bg != nullptr)
			{
				glm::vec4 color = { bg->color.r, bg->color.g, bg->color.b, 1.0f };
				glClearTexImage(target.m_tex_video->tex_id, 0, GL_RGBA, GL_FLOAT, &color);
				break;
			}
		}
		{
			CubeBackground* bg = dynamic_cast<CubeBackground*>(scene.background);
			if (bg != nullptr)
			{
				if (LightmapSkyBoxDraw == nullptr)
				{
					LightmapSkyBoxDraw = std::unique_ptr<CompSkyBox>(new CompSkyBox(2));
				}
				LightmapSkyBoxDraw->render(&lmrl, &bg->cubemap, &target);
				break;
			}
		} 
		{			
			HemisphereBackground* bg = dynamic_cast<HemisphereBackground*>(scene.background);
			if (bg != nullptr)
			{
				bg->updateConstant();
				if (LightmapHemisphereDraw == nullptr)
				{
					LightmapHemisphereDraw = std::unique_ptr<CompHemisphere>(new CompHemisphere(2));
				}				
				LightmapHemisphereDraw->render(&lmrl, &bg->m_constant, &target);
				break;
			}
		}
		break;
	}


	for (size_t i = 0; i < scene.simple_models.size(); i++)
	{
		SimpleModel* model = scene.simple_models[i];
		check_bvh(model);
	}

	for (size_t i = 0; i < scene.gltf_models.size(); i++)
	{
		GLTFModel* model = scene.gltf_models[i];
		check_bvh(model);
	}

	Lights& lights = scene.lights;

	float max_depth = FLT_MAX;
	glClearTexImage(target.m_tex_depth->tex_id, 0, GL_RED, GL_FLOAT, &max_depth);

	if (has_opaque)
	{
		// depth-prepass
		for (size_t i = 0; i < scene.simple_models.size(); i++)
		{
			SimpleModel* model = scene.simple_models[i];
			render_lightmap_depth_model(lmrl, model, target);
		}

		for (size_t i = 0; i < scene.gltf_models.size(); i++)
		{
			GLTFModel* model = scene.gltf_models[i];
			render_lightmap_depth_model(lmrl, model, target);
		}	

		// opaque
		for (size_t i = 0; i < scene.simple_models.size(); i++)
		{
			SimpleModel* model = scene.simple_models[i];
			render_lightmap_model(lmrl, lights, model, Pass::Opaque, target);
		}

		for (size_t i = 0; i < scene.gltf_models.size(); i++)
		{
			GLTFModel* model = scene.gltf_models[i];
			render_lightmap_model(lmrl, lights, model, Pass::Opaque, target);
		}
	}

	if (has_alpha)
	{
		target.update_oit_buffers();

		if (oit_resolver == nullptr)
		{
			oit_resolver = std::unique_ptr<CompWeightedOIT>(new CompWeightedOIT);
		}
		oit_resolver->PreDraw(target.m_OITBuffers);

		for (size_t i = 0; i < scene.simple_models.size(); i++)
		{
			SimpleModel* model = scene.simple_models[i];
			render_lightmap_model(lmrl, lights, model, Pass::Alpha, target);
		}

		for (size_t i = 0; i < scene.gltf_models.size(); i++)
		{
			GLTFModel* model = scene.gltf_models[i];
			render_lightmap_model(lmrl, lights, model, Pass::Alpha, target);
		}

		oit_resolver->PostDraw(&target);
	}
}

void BVHRenderer::update_lightmap(const BVHRenderTarget& source, const LightmapRayList& lmrl, const Lightmap& lightmap, int id_start_texel, float mix_rate)
{
	if (LightmapUpdater == nullptr)
	{
		LightmapUpdater = std::unique_ptr<LightmapUpdate>(new LightmapUpdate);
	}

	LightmapUpdate::RenderParams params;
	params.mix_rate = mix_rate;
	params.source = &source;
	params.lmrl = &lmrl;
	params.target = &lightmap;
	LightmapUpdater->update(params);
}

void BVHRenderer::filter_lightmap(const LightmapRenderTarget& atlas, const Lightmap& lightmap)
{
	int width = lightmap.width;
	int height = lightmap.height;
	float texel_size = 1.0f / (float)(lightmap.texels_per_unit);
	Lightmap tmp(width, height);

	if (LightmapFiltering == nullptr)
	{
		LightmapFiltering = std::unique_ptr<LightmapFilter>(new LightmapFilter);
	}
	
	{
		LightmapFilter::RenderParams params;
		params.width = width;
		params.height = height;
		params.texel_size = texel_size;
		params.light_map_in = lightmap.lightmap.get();
		params.light_map_out = tmp.lightmap.get();
		params.atlas_position = atlas.m_tex_position.get();
		LightmapFiltering->filter(params);
	}

	{
		LightmapFilter::RenderParams params;
		params.width = width;
		params.height = height;
		params.texel_size = texel_size;
		params.light_map_in = tmp.lightmap.get();
		params.light_map_out = lightmap.lightmap.get(); 
		params.atlas_position = atlas.m_tex_position.get();
		LightmapFiltering->filter(params);
	}
}