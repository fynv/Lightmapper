#pragma once

#include <memory>
#include "renderers/GLUtils.h"
#include "lights/Light.h"

struct ConstDirectionalLight
{
	glm::vec4 color;
	glm::vec4 origin;
	glm::vec4 direction;
	int has_shadow;
	int padding[3];
};

class DirectionalLightShadow;
class DirectionalLight : public Light
{
public:
	DirectionalLight();
	~DirectionalLight();
	Object3D* target = nullptr;

	std::unique_ptr<DirectionalLightShadow> shadow;
	void setShadow(bool enable, int map_width, int map_height);
	void setShadowProjection(float left, float right, float bottom, float top, float zNear, float zFar);
	void SetShadowRadius(float radius);

	glm::vec3 direction();
	void makeConst(ConstDirectionalLight& const_light);

	GLDynBuffer m_constant;
	void updateConstant();

	void lookAtTarget();

};