#pragma once

#include <memory>
#include <unordered_map>
#include "renderers/routines/StandardRoutine.h"
#include "renderers/routines/WeightedOIT.h"
#include "renderers/routines/DirectionalShadowCast.h"
#include "renderers/routines/DrawSkyBox.h"
#include "renderers/routines/DrawHemisphere.h"
#include "renderers/routines/DepthOnly.h"
#include "renderers/routines/DrawTexture.h"
#include "renderers/routines/RasterizeAtlas.h"

#include "BVHRenderer.h"
#include "BVHRenderTarget.h"

class Scene;
class Camera;
class GLRenderTarget;
class Lightmap;
class LightmapRenderTarget;
class SimpleModel;
class GLTFModel;
class DirectionalLight;
class DirectionalLightShadow;

class GLRenderer
{
public:
	GLRenderer();
	~GLRenderer();

	void render(Scene& scene, Camera& camera, GLRenderTarget& target);
	void rasterize_atlas(SimpleModel* model);
	void rasterize_atlas(GLTFModel* model);

	int updateLightmap(Scene& scene, Lightmap& lm, LightmapRenderTarget& src, int start_texel, int num_directions = 64);
	void filterLightmap(Lightmap& lm, LightmapRenderTarget& src);
	
	void renderTexture(GLTexture2D* tex, int x, int y, int width, int height, GLRenderTarget& target, bool flipY = true, float alpha = 1.0f);

private:
	std::unique_ptr<WeightedOIT> oit_resolvers[2];

	void update_model(SimpleModel* model);
	void update_model(GLTFModel* model);

	enum class Pass
	{
		Opaque,
		Highlight,
		Alpha
	};

	std::unordered_map<uint64_t, std::unique_ptr<StandardRoutine>> routine_map;
	StandardRoutine* get_routine(const StandardRoutine::Options& options);

	void render_primitive(const StandardRoutine::RenderParams& params, Pass pass);
	void render_primitives(const StandardRoutine::RenderParams& params, Pass pass, const std::vector<int>& first_lst, const std::vector<int>& count_lst); // batched
	void render_model(Camera* p_camera, const Lights& lights, SimpleModel* model, GLRenderTarget& target, Pass pass);	
	void render_model(Camera* p_camera, const Lights& lights, GLTFModel* model, GLRenderTarget& target, Pass pass);

	// directional shadow maps
	std::unordered_map<uint64_t, std::unique_ptr<DirectionalShadowCast>> directional_shadow_caster_map;
	DirectionalShadowCast* get_shadow_caster(const DirectionalShadowCast::Options& options);

	void render_shadow_primitive(const DirectionalShadowCast::RenderParams& params);
	void render_shadow_primitives(const DirectionalShadowCast::RenderParams& params, const std::vector<void*>& offset_lst, const std::vector<int>& count_lst);  // batched
	void render_shadow_model(DirectionalLightShadow* shadow, SimpleModel* model);
	void render_shadow_model(DirectionalLightShadow* shadow, GLTFModel* model);

	std::unique_ptr<DrawSkyBox> SkyBoxDraw;
	std::unique_ptr<DrawHemisphere> HemisphereDraw;

	std::unique_ptr<DepthOnly> DepthRenderer;
	void render_depth_primitive(const DepthOnly::RenderParams& params);
	void render_depth_primitives(const DepthOnly::RenderParams& params, const std::vector<void*>& offset_lst, const std::vector<int>& count_lst);  // batched
	void render_depth_model(Camera* p_camera, SimpleModel* model);
	void render_depth_model(Camera* p_camera, GLTFModel* model);

	void _pre_render(Scene& scene);

	void _render_scene(Scene& scene, Camera& camera, GLRenderTarget& target);
	void _render(Scene& scene, Camera& camera, GLRenderTarget& target);

	std::unique_ptr<DrawTexture> TextureDraw;

	std::unordered_map<uint64_t, std::unique_ptr<RasterizeAtlas>> atlas_rasterizer_map;
	RasterizeAtlas* get_atlas_rasterizer(const RasterizeAtlas::Options& options);

	void rasterize_atlas_primitive(const RasterizeAtlas::RenderParams& params);

	BVHRenderer bvh_renderer;
	BVHRenderTarget bvh_target;
	void _render_bvh(Scene& scene, Camera& camera, GLRenderTarget& target);

};

