#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/ResourceManager.h"

#include "glRenderDeviceRender.h"

#include "glTextureUtils.h"

CRT::CRT			()
{
	pSurface		= NULL;
	dwWidth			= 0;
	dwHeight		= 0;
	fmt				= D3DFMT_UNKNOWN;
}
CRT::~CRT			()
{
	destroy			();

	// release external reference
	DEV->_DeleteRT	(this);
}

void CRT::create	(LPCSTR Name, u32 w, u32 h,	D3DFORMAT f, u32 SampleCount )
{
	if (pSurface)	return;

	R_ASSERT(Name && Name[0] && w && h);
	_order = CPU::GetCLK();	//Device.GetTimerGlobal()->GetElapsed_clk();

	//HRESULT		_hr;

	dwWidth		= w;
	dwHeight	= h;
	fmt			= f;

	// Get caps
	GLint max_width, max_height;
	CHK_GL(glGetIntegerv(GL_MAX_FRAMEBUFFER_WIDTH, &max_width));
	CHK_GL(glGetIntegerv(GL_MAX_FRAMEBUFFER_HEIGHT, &max_height));

	// Check width-and-height of render target surface
	if (w>max_width)		return;
	if (h>max_height)		return;

	DEV->Evict();

	glGenTextures(1, &pSurface);
	CHK_GL(glBindTexture(GL_TEXTURE_2D, pSurface));
	CHK_GL(glTexStorage2D(GL_TEXTURE_2D, 1, glTextureUtils::ConvertTextureFormat(fmt), w, h));

	pTexture	= DEV->_CreateTexture	(Name);
	pTexture->surface_set(GL_TEXTURE_2D, pSurface);
}

void CRT::destroy		()
{
	if (pTexture._get())	{
		pTexture->surface_set(GL_TEXTURE_2D, 0);
		pTexture = NULL;
	}
	CHK_GL(glDeleteTextures(1, &pSurface));
}
void CRT::reset_begin	()
{
	destroy		();
}
void CRT::reset_end		()
{
	create		(*cName,dwWidth,dwHeight,fmt);
}
void resptrcode_crt::create(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount)
{
	_set			(DEV->_CreateRT(Name,w,h,f));
}
