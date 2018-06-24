// 
//---------------------------------------------------------------------------
//
// Copyright(C) 2015 Christopher Bruns
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//--------------------------------------------------------------------------
//
/*
** gl_stereo3d.cpp
** Stereoscopic 3D API
**
*/

#include "gl_load/gl_system.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/renderer/gl_renderbuffers.h"
#include "hwrenderer/utility/hw_vrmodes.h"
#include "gl/system/gl_framebuffer.h"
#include "gl/renderer/gl_postprocessstate.h"
#include "hwrenderer/postprocessing/hw_presentshader.h"
#include "hwrenderer/postprocessing/hw_present3dRowshader.h"

EXTERN_CVAR(Int, vr_mode)
EXTERN_CVAR(Float, vid_saturation)
EXTERN_CVAR(Float, vid_brightness)
EXTERN_CVAR(Float, vid_contrast)
EXTERN_CVAR(Int, gl_satformula)

//==========================================================================
//
//
//
//==========================================================================

static void PresentAnaglyph(bool r, bool g, bool b)
{
	GLRenderer->mBuffers->BindOutputFB();
	GLRenderer->ClearBorders();

	glColorMask(r, g, b, 1);
	GLRenderer->mBuffers->BindEyeTexture(0, 0);
 	GLRenderer->DrawPresentTexture(screen->mOutputLetterbox, true);

	glColorMask(!r, !g, !b, 1);
	GLRenderer->mBuffers->BindEyeTexture(1, 0);
	GLRenderer->DrawPresentTexture(screen->mOutputLetterbox, true);

	glColorMask(1, 1, 1, 1);
}

//==========================================================================
//
//
//
//==========================================================================

static void PresentSideBySide()
{
	GLRenderer->mBuffers->BindOutputFB();
	GLRenderer->ClearBorders();

	// Compute screen regions to use for left and right eye views
	int leftWidth = screen->mOutputLetterbox.width / 2;
	int rightWidth = screen->mOutputLetterbox.width - leftWidth;
	IntRect leftHalfScreen = screen->mOutputLetterbox;
	leftHalfScreen.width = leftWidth;
	IntRect rightHalfScreen = screen->mOutputLetterbox;
	rightHalfScreen.width = rightWidth;
	rightHalfScreen.left += leftWidth;

	GLRenderer->mBuffers->BindEyeTexture(0, 0);
	GLRenderer->DrawPresentTexture(leftHalfScreen, true);

	GLRenderer->mBuffers->BindEyeTexture(1, 0);
	GLRenderer->DrawPresentTexture(rightHalfScreen, true);
}


//==========================================================================
//
//
//
//==========================================================================

static void PresentTopBottom()
{
	GLRenderer->mBuffers->BindOutputFB();
	GLRenderer->ClearBorders();

	// Compute screen regions to use for left and right eye views
	int topHeight = screen->mOutputLetterbox.height / 2;
	int bottomHeight = screen->mOutputLetterbox.height - topHeight;
	IntRect topHalfScreen = screen->mOutputLetterbox;
	topHalfScreen.height = topHeight;
	topHalfScreen.top = topHeight;
	IntRect bottomHalfScreen = screen->mOutputLetterbox;
	bottomHalfScreen.height = bottomHeight;
	bottomHalfScreen.top = 0;

	GLRenderer->mBuffers->BindEyeTexture(0, 0);
	GLRenderer->DrawPresentTexture(topHalfScreen, true);

	GLRenderer->mBuffers->BindEyeTexture(1, 0);
	GLRenderer->DrawPresentTexture(bottomHalfScreen, true);
}

//==========================================================================
//
//
//
//==========================================================================

static void prepareInterleavedPresent(FPresentShaderBase& shader)
{
	GLRenderer->mBuffers->BindOutputFB();
	GLRenderer->ClearBorders();


	// Bind each eye texture, for composition in the shader
	GLRenderer->mBuffers->BindEyeTexture(0, 0);
	GLRenderer->mBuffers->BindEyeTexture(1, 1);

	glActiveTexture(GL_TEXTURE0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glActiveTexture(GL_TEXTURE1);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	const IntRect& box = screen->mOutputLetterbox;
	glViewport(box.left, box.top, box.width, box.height);

	shader.Bind(NOQUEUE);

	if (GLRenderer->framebuffer->IsHWGammaActive())
	{
		shader.Uniforms->InvGamma = 1.0f;
		shader.Uniforms->Contrast = 1.0f;
		shader.Uniforms->Brightness = 0.0f;
		shader.Uniforms->Saturation = 1.0f;
	}
	else
	{
		shader.Uniforms->InvGamma = 1.0f / clamp<float>(Gamma, 0.1f, 4.f);
		shader.Uniforms->Contrast = clamp<float>(vid_contrast, 0.1f, 3.f);
		shader.Uniforms->Brightness = clamp<float>(vid_brightness, -0.8f, 0.8f);
		shader.Uniforms->Saturation = clamp<float>(vid_saturation, -15.0f, 15.0f);
		shader.Uniforms->GrayFormula = static_cast<int>(gl_satformula);
	}
	shader.Uniforms->Scale = {
		screen->mScreenViewport.width / (float)GLRenderer->mBuffers->GetWidth(),
		screen->mScreenViewport.height / (float)GLRenderer->mBuffers->GetHeight()
	};
	shader.Uniforms.Set();
}

//==========================================================================
//
//
//
//==========================================================================

static void PresentColumnInterleaved()
{
	FGLPostProcessState savedState;
	savedState.SaveTextureBindings(2);
	prepareInterleavedPresent(*GLRenderer->mPresent3dColumnShader);

	// Compute absolute offset from top of screen to top of current display window
	// because we need screen-relative, not window-relative, scan line parity

	// Todo:
	//auto clientoffset = screen->GetClientOffset();
	//auto windowHOffset = clientoffset.X % 2;
	int windowHOffset = 0;

	GLRenderer->mPresent3dColumnShader->Uniforms->WindowPositionParity = windowHOffset;
	GLRenderer->mPresent3dColumnShader->Uniforms.Set();

	GLRenderer->RenderScreenQuad();
}

//==========================================================================
//
//
//
//==========================================================================

static void PresentRowInterleaved()
{
	FGLPostProcessState savedState;
	savedState.SaveTextureBindings(2);
	prepareInterleavedPresent(*GLRenderer->mPresent3dRowShader);

	// Todo:
	//auto clientoffset = screen->GetClientOffset();
	//auto windowVOffset = clientoffset.Y % 2;
	int windowVOffset = 0;

	GLRenderer->mPresent3dRowShader->Uniforms->WindowPositionParity =
		(windowVOffset
			+ screen->mOutputLetterbox.height + 1 // +1 because of origin at bottom
			) % 2;

	GLRenderer->mPresent3dRowShader->Uniforms.Set();
	GLRenderer->RenderScreenQuad();
}

//==========================================================================
//
//
//
//==========================================================================

static void PresentCheckerInterleaved()
{
	FGLPostProcessState savedState;
	savedState.SaveTextureBindings(2);
	prepareInterleavedPresent(*GLRenderer->mPresent3dCheckerShader);

	// Compute absolute offset from top of screen to top of current display window
	// because we need screen-relative, not window-relative, scan line parity

	//auto clientoffset = screen->GetClientOffset();
	//auto windowHOffset = clientoffset.X % 2;
	//auto windowVOffset = clientoffset.Y % 2;
	int windowHOffset = 0;
	int windowVOffset = 0;

	GLRenderer->mPresent3dCheckerShader->Uniforms->WindowPositionParity =
		(windowVOffset
			+ windowHOffset
			+ screen->mOutputLetterbox.height + 1 // +1 because of origin at bottom
			) % 2; // because we want the top pixel offset, but gl_FragCoord.y is the bottom pixel offset

	GLRenderer->mPresent3dCheckerShader->Uniforms.Set();
	GLRenderer->RenderScreenQuad();
}

//==========================================================================
//
// Sometimes the stereo render context is not ready immediately at start up
//
//==========================================================================

bool QuadStereoCheckInitialRenderContextState()
{
	// Keep trying until we see at least one good OpenGL context to render to
	static bool bQuadStereoSupported = false;
	static bool bDecentContextWasFound = false;
	static int contextCheckCount = 0;
	if ((!bDecentContextWasFound) && (contextCheckCount < 200))
	{
		contextCheckCount += 1;
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);  // This question is about the main screen display context
		GLboolean supportsStereo, supportsBuffered;
		glGetBooleanv(GL_DOUBLEBUFFER, &supportsBuffered);
		if (supportsBuffered) // Finally, a useful OpenGL context
		{
			// This block will be executed exactly ONCE during a game run
			bDecentContextWasFound = true; // now we can stop checking every frame...
										   // Now check whether this context supports hardware stereo
			glGetBooleanv(GL_STEREO, &supportsStereo);
			bQuadStereoSupported = supportsStereo && supportsBuffered;
		}
	}
	return bQuadStereoSupported;
}

//==========================================================================
//
//
//
//==========================================================================

static void PresentQuadStereo()
{
	if (QuadStereoCheckInitialRenderContextState())
	{
		GLRenderer->mBuffers->BindOutputFB();

		glDrawBuffer(GL_BACK_LEFT);
		GLRenderer->ClearBorders();
		GLRenderer->mBuffers->BindEyeTexture(0, 0);
		GLRenderer->DrawPresentTexture(screen->mOutputLetterbox, true);

		glDrawBuffer(GL_BACK_RIGHT);
		GLRenderer->ClearBorders();
		GLRenderer->mBuffers->BindEyeTexture(1, 0);
		GLRenderer->DrawPresentTexture(screen->mOutputLetterbox, true);

		glDrawBuffer(GL_BACK);
	}
	else
	{
		GLRenderer->mBuffers->BindOutputFB();
		GLRenderer->ClearBorders();
		GLRenderer->mBuffers->BindEyeTexture(0, 0);
		GLRenderer->DrawPresentTexture(screen->mOutputLetterbox, true);
	}
}


void FGLRenderer::PresentStereo()
{
	switch (vr_mode)
	{
	default:
		return;

	case VR_GREENMAGENTA:
		PresentAnaglyph(false, true, true);
		break;

	case VR_REDCYAN:
		PresentAnaglyph(true, false, false);
		break;

	case VR_AMBERBLUE:
		PresentAnaglyph(true, true, false);
		break;

	case VR_SIDEBYSIDEFULL:
	case VR_SIDEBYSIDESQUISHED:
		PresentSideBySide();
		break;

	case VR_TOPBOTTOM:
		PresentTopBottom();
		break;

	case VR_ROWINTERLEAVED:
		PresentRowInterleaved();
		break;

	case VR_COLUMNINTERLEAVED:
		PresentColumnInterleaved();
		break;

	case VR_CHECKERINTERLEAVED:
		PresentCheckerInterleaved();
		break;

	case VR_QUADSTEREO:
		PresentQuadStereo();
		break;
	}
}

