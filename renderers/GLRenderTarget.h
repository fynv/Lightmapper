#pragma once

#include <memory>
#include <renderers/GLUtils.h>
#include "renderers/routines/WeightedOIT.h"

class GLTexture2D;
class Image;
class GLRenderTarget
{
public:
	GLRenderTarget(bool default_buffer, bool msaa);
	~GLRenderTarget();

	bool msaa() const
	{
		return m_fbo_msaa != (unsigned)(-1);
	}

	int m_width = -1;
	int m_height = -1;
	
	std::unique_ptr<GLTexture2D> m_tex_video;
	std::unique_ptr<GLTexture2D> m_tex_msaa;
	std::unique_ptr<GLTexture2D> m_tex_depth;

	unsigned m_fbo_video = 0;
	unsigned m_fbo_msaa = -1;
	bool update_framebuffers(int width, int height);

	WeightedOIT::Buffers m_OITBuffers;
	void update_oit_buffers();

	void bind_buffer();
	void resolve_msaa();
	void blit_buffer(int width_wnd, int height_wnd, int margin);

	void GetImage(Image& image);

};

