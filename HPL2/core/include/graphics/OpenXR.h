/*
 * Copyright © 2009-2020 Frictional Games
 * Copyright © 2024 OpenXR VR Support
 * 
 * This file is part of Amnesia: The Dark Descent.
 * 
 * Amnesia: The Dark Descent is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version. 
 * 
 * Amnesia: The Dark Descent is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Amnesia: The Dark Descent.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef HPL_OPENXR_H
#define HPL_OPENXR_H

#include "math/MathTypes.h"
#include "system/SystemTypes.h"
#include "system/LowLevelSystem.h"

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#if defined(__linux__)
#include <X11/Xlib.h>
#include <GL/glx.h>
#endif

// XrSwapchainImageOpenGLKHR is defined in XR_KHR_opengl_enable extension
// which may not be in the standard headers on all systems
struct XrSwapchainImageOpenGLKHR
{
    XrStructureType    type;
    void*              next;
    uint32_t           image;
};

// XrGraphicsBindingOpenGLKHR is defined in XR_KHR_opengl_enable extension
struct XrGraphicsBindingOpenGLKHR
{
    XrStructureType    type;
    void*              next;
#if defined(_WIN32)
    HDC                hDC;
    HGLRC              hGLRC;
#elif defined(__linux__)
    Display*           xDisplay;
    GLXFBConfig        glxFBConfig;
    GLXDrawable        glxDrawable;
    GLXContext         glxContext;
#endif
};

#ifndef XR_TYPE_GRAPHICS_BINDING_OPENGL_KHR
#define XR_TYPE_GRAPHICS_BINDING_OPENGL_KHR 1000023004
#endif

namespace hpl {

	class iLowLevelGraphics;
	class iLowLevelSystem;
	class cConfigFile;

	//-----------------------------------------------------------------------

	class cOpenXR
	{
	public:
		cOpenXR(iLowLevelSystem* apLowLevelSystem, iLowLevelGraphics* apLowLevelGraphics);
		~cOpenXR();

		bool Init(cConfigFile* apConfig);
		void Shutdown();

		bool IsAvailable() const { return mbAvailable; }
		bool IsEnabled() const { return mbEnabled; }
		bool IsRunning() const { return mbSessionRunning; }

		void Update(float afTimeStep);
		void BeginFrame();
		void EndFrame();

		// Get view matrices for stereo rendering
		void GetViewMatrices(cMatrixf& avLeftView, cMatrixf& avRightView, cMatrixf& avLeftProj, cMatrixf& avRightProj);

		// Render target size for each eye
		cVector2l GetRecommendedRenderTargetSize() const;

		// Controller input
		bool GetControllerState(int alHand, cVector3f& avPos, cQuaternion& aqRot, float& afTrigger, float& afGrip, bool& abMenu, bool& abGripPressed);

		// Configuration
		void SetEnabled(bool abEnabled) { mbEnabled = abEnabled; }
		void SetIPD(float afIPD) { mfIPD = afIPD; }
		void SetNearZ(float afNear) { mfNearZ = afNear; }
		void SetFarZ(float afFar) { mfFarZ = afFar; }

	private:
		// OpenXR instance and system
		XrInstance mInstance = XR_NULL_HANDLE;
		XrSystemId mSystemId = XR_NULL_SYSTEM_ID;
		XrSession mSession = XR_NULL_HANDLE;
		XrSpace mReferenceSpace = XR_NULL_HANDLE;
		XrSpace mLocalSpace = XR_NULL_HANDLE;

		// View configuration
		XrViewConfigurationType mViewConfigType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		std::vector<XrViewConfigurationView> mViewConfigViews;
		std::vector<XrView> mViews;

		// Swapchain for each eye
		struct SwapchainData {
			XrSwapchain swapchain = XR_NULL_HANDLE;
			std::vector<XrSwapchainImageOpenGLKHR*> images;
			uint32_t imageIndex = 0;
			int width = 0;
			int height = 0;
		};
		std::vector<SwapchainData> mSwapchains;

		// Frame state
		XrFrameState mFrameState = { XR_TYPE_FRAME_STATE };
		XrFrameWaitInfo mFrameWaitInfo = { XR_TYPE_FRAME_WAIT_INFO };
		XrFrameBeginInfo mFrameBeginInfo = { XR_TYPE_FRAME_BEGIN_INFO };
		XrFrameEndInfo mFrameEndInfo = { XR_TYPE_FRAME_END_INFO };

		// View locate info
		XrViewLocateInfo mViewLocateInfo = { XR_TYPE_VIEW_LOCATE_INFO };

		// Composition layer
		std::vector<XrCompositionLayerProjectionView> mProjectionLayerViews;
		XrCompositionLayerProjection mProjectionLayer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };

		// Input
		XrActionSet mActionSet = XR_NULL_HANDLE;
		XrAction mPoseAction = XR_NULL_HANDLE;
		XrAction mTriggerAction = XR_NULL_HANDLE;
		XrAction mGripAction = XR_NULL_HANDLE;
		XrAction mMenuAction = XR_NULL_HANDLE;
		XrAction mGripClickAction = XR_NULL_HANDLE;

		XrPath mLeftHandPath = XR_NULL_PATH;
		XrPath mRightHandPath = XR_NULL_PATH;
		XrPath mLeftHandGripPath = XR_NULL_PATH;
		XrPath mRightHandGripPath = XR_NULL_PATH;

		// Hand spaces
		std::vector<XrSpace> mHandSpaces;

		// Settings
		bool mbAvailable = false;
		bool mbEnabled = false;
		bool mbSessionRunning = false;
		bool mbInitialized = false;

		float mfIPD = 0.064f;
		float mfNearZ = 0.1f;
		float mfFarZ = 1000.0f;

		cVector2l mvRenderTargetSize;

		iLowLevelSystem* mpLowLevelSystem;
		iLowLevelGraphics* mpLowLevelGraphics;

		// Internal methods
		bool BeginSession();
		void EndSession();

		bool CreateSwapchains();
		void DestroySwapchains();

		void SetupProjectionLayerViews();
		void UpdateViewConfig();

		void PollEvents();

		// Helper
		static const char* XrResultToString(XrResult result);
		void CheckXrResult(XrResult result, const char* operation);
	};

};

#endif // HPL_OPENXR_H