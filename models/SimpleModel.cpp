#include <GL/glew.h>
#include "SimpleModel.h"
#include "materials/MeshStandardMaterial.h"
#include "renderers/LightmapRenderTarget.h"
#include "renderers/GLRenderer.h"

struct ModelConst
{
	glm::mat4 ModelMat;
	glm::mat4 NormalMat;
};

SimpleModel::SimpleModel() : m_constant(sizeof(ModelConst), GL_UNIFORM_BUFFER)
{
	glm::u8vec3 white = { 255, 255, 255 };
	texture.load_memory_bgr(1, 1, (uint8_t*)&white, true);
	material.tex_idx_map = 0;
	set_color({ 0.8f, 0.8f, 0.8f });
	geometry.material_idx = 0;
}

SimpleModel::~SimpleModel()
{


}

void SimpleModel::updateConstant()
{
	ModelConst c;
	c.ModelMat = matrixWorld;
	c.NormalMat = glm::transpose(glm::inverse(matrixWorld));
	m_constant.upload(&c);
}

void SimpleModel::set_color(const glm::vec3& color)
{
	material.color = glm::vec4(color, 1.0f);
	material.update_uniform();
}


void SimpleModel::set_metalness(float metalness)
{
	material.metallicFactor = metalness;
	material.update_uniform();
}


void SimpleModel::set_roughness(float roughness)
{
	material.roughnessFactor = roughness;
	material.update_uniform();
}

void SimpleModel::init_lightmap(GLRenderer* renderer, int texelsPerUnit)
{
	std::vector<Primitive*> primitives(1);
	std::vector<glm::mat4> trans(1);
	primitives[0] = &geometry;
	trans[0] = glm::identity<glm::mat4>();

	lightmap = std::unique_ptr<Lightmap>(new Lightmap(primitives, trans, texelsPerUnit));

	lightmap_target = std::unique_ptr<LightmapRenderTarget>(new LightmapRenderTarget);
	lightmap_target->update_framebuffer(lightmap->width, lightmap->height);
	renderer->rasterize_atlas(this);

#if 0
	{
		std::vector<glm::vec3> dump(lightmap->width * lightmap->height);

		glBindTexture(GL_TEXTURE_2D, lightmap_target->m_tex_normal->tex_id);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_FLOAT, dump.data());
		glBindTexture(GL_TEXTURE_2D, 0);

		char filename[64];
		sprintf(filename, "dump_%s.raw", name.c_str());

		printf("%s %d %d\n", filename, lightmap->width, lightmap->height);

		FILE* fp = fopen(filename, "wb");

		for (int i = 0; i < lightmap->width * lightmap->height; i++)
		{
			glm::u8vec3 value_u8 = glm::u8vec3((dump[i] * 0.5f + 0.5f) * 255.0f + 0.5f);
			fwrite(&value_u8, 3, 1, fp);
		}

		fclose(fp);

	}
#endif

	glm::vec4 zero = { 0.0f, 0.0f, 0.0f, 0.0f };
	glClearTexImage(lightmap->lightmap->tex_id, 0, GL_RGBA, GL_FLOAT, &zero);

	std::vector<float> alpha_mask(lightmap->width * lightmap->height);

	glBindTexture(GL_TEXTURE_2D, lightmap_target->m_tex_position->tex_id);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_ALPHA, GL_FLOAT, alpha_mask.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	std::vector<glm::u16vec2> lst_valid;

	for (int i = 0; i < lightmap->width * lightmap->height; i++)
	{
		float f = alpha_mask[i];
		if (f > 0.5f)
		{
			uint16_t x = (uint16_t)(i % lightmap->width);
			uint16_t y = (uint16_t)(i / lightmap->width);
			lst_valid.push_back({ x,y });
		}
	}

	lightmap_target->count_valid = (int)lst_valid.size();
	lightmap_target->valid_list = std::unique_ptr<TextureBuffer>(new TextureBuffer(sizeof(glm::u16vec2) * lightmap_target->count_valid, GL_RG16UI));
	lightmap_target->valid_list->upload(lst_valid.data());

}

