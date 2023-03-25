#pragma once

#include <memory>
#include <unordered_map>
#include "renderers/bvh_routines/CompWeightedOIT.h"
#include "renderers/bvh_routines/CompSkyBox.h"
#include "renderers/bvh_routines/CompHemisphere.h"
#include "renderers/bvh_routines/BVHDepthOnly.h"
#include "renderers/bvh_routines/BVHRoutine.h"
#include "renderers/bvh_routines/LightmapUpdate.h"
#include "renderers/bvh_routines/LightmapFilter.h"

class Scene;
class Camera;
class BVHRenderTarget;
class SimpleModel;
class GLTFModel;
class DirectionalLight;
class DirectionalLightShadow;

class Lightmap;
class LightmapRenderTarget;
class LightmapRayList;

class BVHRenderer
{
public:
	void render(Scene& scene, Camera& camera, BVHRenderTarget& target);
	void render_lightmap(Scene& scene, LightmapRayList& lmrl, BVHRenderTarget& target);
	void update_lightmap(const BVHRenderTarget& source, const LightmapRayList& lmrl, const Lightmap& lightmap, int id_start_texel, float mix_rate = 1.0f);
	void filter_lightmap(const LightmapRenderTarget& atlas, const Lightmap& lightmap);

private:
	std::unique_ptr<CompWeightedOIT> oit_resolver;

	void check_bvh(SimpleModel* model);
	void check_bvh(GLTFModel* model);

	enum class Pass
	{
		Opaque,
		Alpha
	};

	std::unique_ptr<CompSkyBox> SkyBoxDraw;
	std::unique_ptr<CompHemisphere> HemisphereDraw;

	std::unique_ptr<BVHDepthOnly> DepthRenderer;
	void render_depth_primitive(const BVHDepthOnly::RenderParams& params);
	void render_depth_model(Camera* p_camera, SimpleModel* model, BVHRenderTarget& target);
	void render_depth_model(Camera* p_camera, GLTFModel* model, BVHRenderTarget& target);

	std::unordered_map<uint64_t, std::unique_ptr<BVHRoutine>> routine_map;
	BVHRoutine* get_routine(const BVHRoutine::Options& options);

	void render_primitive(const BVHRoutine::RenderParams& params, Pass pass);
	void render_model(Camera* p_camera, const Lights& lights, SimpleModel* model, Pass pass, BVHRenderTarget& target);
	void render_model(Camera* p_camera, const Lights& lights, GLTFModel* model, Pass pass, BVHRenderTarget& target);

	///////////// Render to Lightmap ////////////////

	std::unique_ptr<CompSkyBox> LightmapSkyBoxDraw;
	std::unique_ptr<CompHemisphere> LightmapHemisphereDraw;

	std::unique_ptr<BVHDepthOnly> LightmapDepthRenderer;
	void render_lightmap_depth_primitive(const BVHDepthOnly::RenderParams& params);
	void render_lightmap_depth_model(LightmapRayList& lmrl, SimpleModel* model, BVHRenderTarget& target);
	void render_lightmap_depth_model(LightmapRayList& lmrl, GLTFModel* model, BVHRenderTarget& target);

	std::unordered_map<uint64_t, std::unique_ptr<BVHRoutine>> lightmap_routine_map;
	BVHRoutine* get_lightmap_routine(const BVHRoutine::Options& options);

	void render_lightmap_primitive(const BVHRoutine::RenderParams& params, Pass pass);
	void render_lightmap_model(LightmapRayList& lmrl, const Lights& lights, SimpleModel* model, Pass pass, BVHRenderTarget& target);
	void render_lightmap_model(LightmapRayList& lmrl, const Lights& lights, GLTFModel* model, Pass pass, BVHRenderTarget& target);

	std::unique_ptr<LightmapUpdate> LightmapUpdater;
	std::unique_ptr<LightmapFilter> LightmapFiltering;
};
