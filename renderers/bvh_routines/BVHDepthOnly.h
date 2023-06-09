#pragma once

#include <memory>
#include <string>

#include "materials/MeshStandardMaterial.h"
#include "renderers/GLUtils.h"

class Primitive;
class BVHRenderTarget;
class LightmapRayList;
class BVHDepthOnly
{
public:
	BVHDepthOnly(int target_mode = 0);

	struct RenderParams
	{
		const MeshStandardMaterial** material_list;
		const Primitive* primitive;
		const BVHRenderTarget* target;
		const GLDynBuffer* constant_camera;		
		const LightmapRayList* lmrl;
	};

	void render(const RenderParams& params);

private:
	int m_target_mode;
	std::unique_ptr<GLProgram> m_prog;

};

