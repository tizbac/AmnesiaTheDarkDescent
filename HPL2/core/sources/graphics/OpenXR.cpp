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

#include "graphics/OpenXR.h"

#include "system/LowLevelSystem.h"
#include "resources/ConfigFile.h"
#include "system/String.h"
#include "graphics/LowLevelGraphics.h"
#include "system/Platform.h"
#include "math/Math.h"

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <cstring>

#if defined(__linux__)
#include <X11/Xlib.h>
#include <GL/glx.h>
#include <GL/gl.h>
#endif

namespace hpl {

//-----------------------------------------------------------------------

cOpenXR::cOpenXR(iLowLevelSystem* apLowLevelSystem, iLowLevelGraphics* apLowLevelGraphics)
	: mpLowLevelSystem(apLowLevelSystem)
	, mpLowLevelGraphics(apLowLevelGraphics)
	, mInstance(XR_NULL_HANDLE)
	, mSystemId(XR_NULL_SYSTEM_ID)
	, mSession(XR_NULL_HANDLE)
	, mReferenceSpace(XR_NULL_HANDLE)
	, mLocalSpace(XR_NULL_HANDLE)
	, mActionSet(XR_NULL_HANDLE)
	, mPoseAction(XR_NULL_HANDLE)
	, mTriggerAction(XR_NULL_HANDLE)
	, mGripAction(XR_NULL_HANDLE)
	, mMenuAction(XR_NULL_HANDLE)
	, mGripClickAction(XR_NULL_HANDLE)
	, mLeftHandPath(XR_NULL_PATH)
	, mRightHandPath(XR_NULL_PATH)
	, mLeftHandGripPath(XR_NULL_PATH)
	, mRightHandGripPath(XR_NULL_PATH)
	, mbAvailable(false)
	, mbEnabled(false)
	, mbSessionRunning(false)
	, mbInitialized(false)
	, mfIPD(0.064f)
	, mfNearZ(0.1f)
	, mfFarZ(1000.0f)
	, mvRenderTargetSize(0, 0)
{
}

//-----------------------------------------------------------------------

cOpenXR::~cOpenXR()
{
	Shutdown();
}

//-----------------------------------------------------------------------

bool cOpenXR::Init(cConfigFile* apConfig)
{
	if (mbInitialized)
		return true;

	// Check if VR is enabled in config
	if (apConfig)
	{
		mbEnabled = apConfig->GetBool("VR", "Enabled", true);
		mfIPD = apConfig->GetFloat("VR", "IPD", 0.064f);
		mfNearZ = apConfig->GetFloat("VR", "NearZ", 0.1f);
		mfFarZ = apConfig->GetFloat("VR", "FarZ", 1000.0f);
	}

	if (!mbEnabled)
	{
		Log("OpenXR: VR disabled in config\n");
		return true;
	}

	// Initialize OpenXR
	Log("OpenXR: Initializing...\n");
	
	// First, query available extensions
	uint32_t extCount = 0;
	xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr);
	std::vector<XrExtensionProperties> extProps(extCount, { XR_TYPE_EXTENSION_PROPERTIES });
	xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, extProps.data());

	bool hasKHRPlatform = false;
	bool hasKHRSwapchain = false;
	
	for (const auto& ext : extProps)
	{
		if (strcmp(ext.extensionName, "XR_KHR_opengl_enable") == 0)
			hasKHRPlatform = true;
		else if (strcmp(ext.extensionName, "XR_KHR_swapchain") == 0)
			hasKHRSwapchain = true;
	}

	if (!hasKHRPlatform)
	{
		Log("OpenXR: XR_KHR_opengl_enable not available\n");
		mbEnabled = false;
		return true;
	}

	// Try to create instance with available extensions
	const char* enabledExtensions[2];
	uint32_t enabledExtCount = 0;
	enabledExtensions[enabledExtCount++] = "XR_KHR_opengl_enable";
	
	if (hasKHRSwapchain)
	{
		enabledExtensions[enabledExtCount++] = "XR_KHR_swapchain";
	}
	else
	{
		Log("OpenXR: XR_KHR_swapchain not available, continuing without swapchain support\n");
	}

	// Try to create instance
	XrInstanceCreateInfo createInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
	strcpy(createInfo.applicationInfo.applicationName, "Amnesia: The Dark Descent");
	strcpy(createInfo.applicationInfo.engineName, "HPL2 Engine");
	createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
	createInfo.applicationInfo.applicationVersion = 1;
	createInfo.applicationInfo.engineVersion = 2;

	createInfo.enabledExtensionCount = enabledExtCount;
	createInfo.enabledExtensionNames = enabledExtensions;

	XrResult result = xrCreateInstance(&createInfo, &mInstance);
	if (XR_FAILED(result))
	{
		Log("OpenXR: Failed to create instance: %s\n", XrResultToString(result));
		mbEnabled = false;
		return true;
	}

	Log("OpenXR: Instance created\n");

	// Get system
	XrSystemGetInfo sysInfo = { XR_TYPE_SYSTEM_GET_INFO };
	sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

	result = xrGetSystem(mInstance, &sysInfo, &mSystemId);
	if (XR_FAILED(result))
	{
		Log("OpenXR: Failed to get system: %s\n", XrResultToString(result));
		Shutdown();
		mbEnabled = false;
		return true;
	}

	// Get view configuration views
	uint32_t viewCount = 0;
	xrEnumerateViewConfigurationViews(mInstance, mSystemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
	mViewConfigViews.resize(viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
	xrEnumerateViewConfigurationViews(mInstance, mSystemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, mViewConfigViews.data());

	Log("OpenXR: System found, %d views\n", viewCount);
	mbAvailable = true;
	mbInitialized = true;
	
	Log("OpenXR: Initialized\n");
	return true;
}

//-----------------------------------------------------------------------

void cOpenXR::Shutdown()
{
	if (!mbInitialized)
		return;

	EndSession();
	DestroySwapchains();
	
	if (mReferenceSpace != XR_NULL_HANDLE)
		xrDestroySpace(mReferenceSpace);
	if (mLocalSpace != XR_NULL_HANDLE)
		xrDestroySpace(mLocalSpace);
	if (mSession != XR_NULL_HANDLE)
		xrDestroySession(mSession);
	if (mInstance != XR_NULL_HANDLE)
		xrDestroyInstance(mInstance);

	mInstance = XR_NULL_HANDLE;
	mSystemId = XR_NULL_SYSTEM_ID;
	mSession = XR_NULL_HANDLE;
	mReferenceSpace = XR_NULL_HANDLE;
	mLocalSpace = XR_NULL_HANDLE;
	mActionSet = XR_NULL_HANDLE;
	mPoseAction = XR_NULL_HANDLE;
	mTriggerAction = XR_NULL_HANDLE;
	mGripAction = XR_NULL_HANDLE;
	mMenuAction = XR_NULL_HANDLE;
	mGripClickAction = XR_NULL_HANDLE;
	mLeftHandPath = XR_NULL_PATH;
	mRightHandPath = XR_NULL_PATH;
	mLeftHandGripPath = XR_NULL_PATH;
	mRightHandGripPath = XR_NULL_PATH;

	mbAvailable = false;
	mbInitialized = false;
	mbSessionRunning = false;

	Log("OpenXR: Shutdown\n");
}

//-----------------------------------------------------------------------

bool cOpenXR::BeginSession()
{
	if (mbSessionRunning || !mbAvailable)
		return false;

	// Create graphics binding
	XrGraphicsBindingOpenGLKHR glBinding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_KHR };
	
#if defined(_WIN32)
	glBinding.hDC = wglGetCurrentDC();
	glBinding.hGLRC = wglGetCurrentContext();
#elif defined(__linux__)
	glBinding.xDisplay = glXGetCurrentDisplay();
	glBinding.glxFBConfig = 0;
	glBinding.glxDrawable = glXGetCurrentDrawable();
	glBinding.glxContext = glXGetCurrentContext();
#endif

	XrSessionCreateInfo sessionInfo = { XR_TYPE_SESSION_CREATE_INFO };
	sessionInfo.next = &glBinding;
	sessionInfo.systemId = mSystemId;

	XrResult result = xrCreateSession(mInstance, &sessionInfo, &mSession);
	if (XR_FAILED(result))
	{
		Log("OpenXR: Failed to create session: %s\n", XrResultToString(result));
		return false;
	}

	// Create reference space
	XrReferenceSpaceCreateInfo refSpaceInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	refSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	refSpaceInfo.poseInReferenceSpace = {{0, 0, 0, 1}, {0, 0, 0}};

	result = xrCreateReferenceSpace(mSession, &refSpaceInfo, &mReferenceSpace);
	if (XR_FAILED(result))
	{
		Log("OpenXR: Failed to create reference space: %s\n", XrResultToString(result));
		return false;
	}

	// Create local space
	refSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	result = xrCreateReferenceSpace(mSession, &refSpaceInfo, &mLocalSpace);
	if (XR_FAILED(result))
	{
		Log("OpenXR: Failed to create local space: %s\n", XrResultToString(result));
		return false;
	}

	// Create swapchains
	if (!CreateSwapchains())
	{
		return false;
	}

	// Begin session
	XrSessionBeginInfo beginInfo = { XR_TYPE_SESSION_BEGIN_INFO };
	beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

	result = xrBeginSession(mSession, &beginInfo);
	if (XR_FAILED(result))
	{
		Log("OpenXR: Failed to begin session: %s\n", XrResultToString(result));
		return false;
	}

	mbSessionRunning = true;
	Log("OpenXR: Session started\n");
	return true;
}

//-----------------------------------------------------------------------

void cOpenXR::EndSession()
{
	if (!mbSessionRunning)
		return;

	if (mSession != XR_NULL_HANDLE)
	{
		xrEndSession(mSession);
	}

	DestroySwapchains();

	if (mReferenceSpace != XR_NULL_HANDLE)
	{
		xrDestroySpace(mReferenceSpace);
		mReferenceSpace = XR_NULL_HANDLE;
	}
	if (mLocalSpace != XR_NULL_HANDLE)
	{
		xrDestroySpace(mLocalSpace);
		mLocalSpace = XR_NULL_HANDLE;
	}
	if (mSession != XR_NULL_HANDLE)
	{
		xrDestroySession(mSession);
		mSession = XR_NULL_HANDLE;
	}

	mbSessionRunning = false;
	Log("OpenXR: Session ended\n");
}

//-----------------------------------------------------------------------

bool cOpenXR::CreateSwapchains()
{
	if (mViewConfigViews.empty())
		return false;

	mvRenderTargetSize.x = mViewConfigViews[0].recommendedImageRectWidth;
	mvRenderTargetSize.y = mViewConfigViews[0].recommendedImageRectHeight;

	// Create swapchain for each view (stereo)
	mSwapchains.resize(mViewConfigViews.size());

	for (size_t i = 0; i < mViewConfigViews.size(); ++i)
	{
		XrSwapchainCreateInfo swapchainInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
		swapchainInfo.arraySize = 1;
		swapchainInfo.format = GL_RGBA;  // Use standard GL_RGBA format
		swapchainInfo.width = mViewConfigViews[i].recommendedImageRectWidth;
		swapchainInfo.height = mViewConfigViews[i].recommendedImageRectHeight;
		swapchainInfo.mipCount = 1;
		swapchainInfo.faceCount = 1;
		swapchainInfo.sampleCount = mViewConfigViews[i].recommendedSwapchainSampleCount;
		swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;

		XrResult result = xrCreateSwapchain(mSession, &swapchainInfo, &mSwapchains[i].swapchain);
		if (XR_FAILED(result))
		{
			Log("OpenXR: Failed to create swapchain %zu: %s\n", i, XrResultToString(result));
			return false;
		}

		// Get swapchain images
		uint32_t imageCount = 0;
		xrEnumerateSwapchainImages(mSwapchains[i].swapchain, 0, &imageCount, nullptr);
		mSwapchains[i].images.resize(imageCount);
		for (uint32_t j = 0; j < imageCount; ++j)
		{
			mSwapchains[i].images[j] = new XrSwapchainImageOpenGLKHR{ XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR };
		}
		xrEnumerateSwapchainImages(mSwapchains[i].swapchain, imageCount, &imageCount, reinterpret_cast<XrSwapchainImageBaseHeader*>(mSwapchains[i].images.data()));

		mSwapchains[i].width = mViewConfigViews[i].recommendedImageRectWidth;
		mSwapchains[i].height = mViewConfigViews[i].recommendedImageRectHeight;

		Log("OpenXR: Swapchain %zu created with %d images (%dx%d)\n", i, imageCount, mSwapchains[i].width, mSwapchains[i].height);
	}

	return true;
}

//-----------------------------------------------------------------------

void cOpenXR::DestroySwapchains()
{
	for (auto& swapchain : mSwapchains)
	{
		if (swapchain.swapchain != XR_NULL_HANDLE)
		{
			for (auto image : swapchain.images)
				delete image;
			swapchain.images.clear();
			xrDestroySwapchain(swapchain.swapchain);
			swapchain.swapchain = XR_NULL_HANDLE;
		}
	}
	mSwapchains.clear();
}

//-----------------------------------------------------------------------


//-----------------------------------------------------------------------

void cOpenXR::BeginFrame()
{
	if (!mbAvailable || !mbEnabled || !mbSessionRunning)
		return;

	XrFrameWaitInfo frameWaitInfo = { XR_TYPE_FRAME_WAIT_INFO };
	XrFrameState frameState = { XR_TYPE_FRAME_STATE };

	XrResult result = xrWaitFrame(mSession, &frameWaitInfo, &mFrameState);
	if (XR_FAILED(result))
	{
		if (result == XR_ERROR_SESSION_NOT_RUNNING)
			mbSessionRunning = false;
		return;
	}

	result = xrBeginFrame(mSession, nullptr);
	if (XR_FAILED(result))
	{
		return;
	}

	// Acquire swapchain images
	for (size_t i = 0; i < mSwapchains.size(); ++i)
	{
		XrSwapchainImageAcquireInfo acquireInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
		result = xrAcquireSwapchainImage(mSwapchains[i].swapchain, &acquireInfo, &mSwapchains[i].imageIndex);
		if (XR_FAILED(result))
			return;

		XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
		waitInfo.timeout = XR_INFINITE_DURATION;
		result = xrWaitSwapchainImage(mSwapchains[i].swapchain, &waitInfo);
		if (XR_FAILED(result))
			return;
	}
}

//-----------------------------------------------------------------------

void cOpenXR::EndFrame()
{
	if (!mbAvailable || !mbEnabled || !mbSessionRunning)
		return;

	// Release swapchain images
	for (size_t i = 0; i < mSwapchains.size(); ++i)
	{
		XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
		xrReleaseSwapchainImage(mSwapchains[i].swapchain, &releaseInfo);
	}

	// Setup projection layer views
	SetupProjectionLayerViews();

	// Submit frame
	mFrameEndInfo = { XR_TYPE_FRAME_END_INFO };
	mFrameEndInfo.displayTime = mFrameState.predictedDisplayTime;
	mFrameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	mFrameEndInfo.layerCount = 1;
	mFrameEndInfo.layers = (const XrCompositionLayerBaseHeader**)&mProjectionLayer;

	XrResult result = xrEndFrame(mSession, &mFrameEndInfo);
	if (XR_FAILED(result))
	{
		if (result == XR_ERROR_SESSION_NOT_RUNNING)
			mbSessionRunning = false;
	}
}

//-----------------------------------------------------------------------

void cOpenXR::SetupProjectionLayerViews()
{
	mProjectionLayerViews.resize(mViews.size());

	for (size_t i = 0; i < mViews.size(); ++i)
	{
		mProjectionLayerViews[i] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
		mProjectionLayerViews[i].pose = mViews[i].pose;
		mProjectionLayerViews[i].fov = mViews[i].fov;
		mProjectionLayerViews[i].subImage.swapchain = mSwapchains[i].swapchain;
		mProjectionLayerViews[i].subImage.imageRect = { {0, 0}, { (int32_t)mSwapchains[i].width, (int32_t)mSwapchains[i].height } };
		mProjectionLayerViews[i].subImage.imageArrayIndex = 0;
	}

	mProjectionLayer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	mProjectionLayer.space = mReferenceSpace;
	mProjectionLayer.viewCount = (uint32_t)mProjectionLayerViews.size();
	mProjectionLayer.views = mProjectionLayerViews.data();
}

//-----------------------------------------------------------------------


//-----------------------------------------------------------------------

void cOpenXR::PollEvents()
{
	XrEventDataBuffer eventData = { XR_TYPE_EVENT_DATA_BUFFER };
	XrResult result = xrPollEvent(mInstance, &eventData);
	
	while (result == XR_SUCCESS)
	{
		switch (eventData.type)
		{
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
			{
				XrEventDataSessionStateChanged* sessionState = (XrEventDataSessionStateChanged*)&eventData;
				Log("OpenXR: Session state changed to %d\n", sessionState->state);

				switch (sessionState->state)
				{
					case XR_SESSION_STATE_READY:
						if (!mbSessionRunning)
							BeginSession();
						break;
					case XR_SESSION_STATE_STOPPING:
						EndSession();
						break;
					case XR_SESSION_STATE_EXITING:
					case XR_SESSION_STATE_LOSS_PENDING:
						mbEnabled = false;
						break;
				}
				break;
			}
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
				mbEnabled = false;
				break;
		}

		result = xrPollEvent(mInstance, &eventData);
	}

	if (!mbSessionRunning)
		return;
}

//-----------------------------------------------------------------------

void cOpenXR::UpdateViewConfig()
{
	mViewLocateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
	mViewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	mViewLocateInfo.displayTime = mFrameState.predictedDisplayTime;
	mViewLocateInfo.space = mReferenceSpace;

	mViews.resize(mViewConfigViews.size());
	for (auto& view : mViews)
	{
		view = { XR_TYPE_VIEW };
	}

	uint32_t viewCount = (uint32_t)mViews.size();
	XrViewState viewState = { XR_TYPE_VIEW_STATE };
	XrResult result = xrLocateViews(mSession, &mViewLocateInfo, &viewState, viewCount, &viewCount, nullptr);
	if (XR_SUCCEEDED(result))
	{
		result = xrLocateViews(mSession, &mViewLocateInfo, &viewState, viewCount, &viewCount, mViews.data());
	}
}

//-----------------------------------------------------------------------

void cOpenXR::GetViewMatrices(cMatrixf& avLeftView, cMatrixf& avRightView, cMatrixf& avLeftProj, cMatrixf& avRightProj)
{
	if (!mbAvailable || !mbEnabled || mViews.size() < 2)
	{
		avLeftView = cMatrixf::Identity;
		avRightView = cMatrixf::Identity;
		avLeftProj = cMatrixf::Identity;
		avRightProj = cMatrixf::Identity;
		return;
	}

	// Convert OpenXR poses to view matrices
	// Left eye
	XrPosef leftPose = mViews[0].pose;
	cMatrixf leftView = cMatrixf::Identity;
	cQuaternion leftOrient(leftPose.orientation.w, leftPose.orientation.x, leftPose.orientation.y, leftPose.orientation.z);
	leftOrient.ToRotationMatrix(leftView);
	cVector3f leftPos(leftPose.position.x, leftPose.position.y, leftPose.position.z);
	leftView.m[3][0] = leftPos.x;
	leftView.m[3][1] = leftPos.y;
	leftView.m[3][2] = leftPos.z;
	avLeftView = hpl::cMath::MatrixInverse(leftView);

	// Right eye
	XrPosef rightPose = mViews[1].pose;
	cMatrixf rightView = cMatrixf::Identity;
	cQuaternion rightOrient(rightPose.orientation.w, rightPose.orientation.x, rightPose.orientation.y, rightPose.orientation.z);
	rightOrient.ToRotationMatrix(rightView);
	cVector3f rightPos(rightPose.position.x, rightPose.position.y, rightPose.position.z);
	rightView.m[3][0] = rightPos.x;
	rightView.m[3][1] = rightPos.y;
	rightView.m[3][2] = rightPos.z;
	avRightView = hpl::cMath::MatrixInverse(rightView);

	// Projection matrices from FOV
	XrFovf leftFov = mViews[0].fov;
	XrFovf rightFov = mViews[1].fov;

	avLeftProj = hpl::cMath::MatrixPerspectiveProjection(mfNearZ, mfFarZ, leftFov.angleLeft + leftFov.angleRight, (leftFov.angleLeft + leftFov.angleRight) / (leftFov.angleUp + leftFov.angleDown), false);

	avRightProj = hpl::cMath::MatrixPerspectiveProjection(mfNearZ, mfFarZ, rightFov.angleLeft + rightFov.angleRight, (rightFov.angleLeft + rightFov.angleRight) / (rightFov.angleUp + rightFov.angleDown), false);
}

//-----------------------------------------------------------------------

cVector2l cOpenXR::GetRecommendedRenderTargetSize() const
{
	return mvRenderTargetSize;
}

//-----------------------------------------------------------------------

bool cOpenXR::GetControllerState(int alHand, cVector3f& avPos, cQuaternion& aqRot, float& afTrigger, float& afGrip, bool& abMenu, bool& abGripPressed)
{
	if (!mbAvailable || !mbEnabled || alHand < 0 || alHand >= 2)
		return false;

	if (mHandSpaces[alHand] == XR_NULL_HANDLE)
		return false;

	XrSpaceLocation spaceLoc = { XR_TYPE_SPACE_LOCATION };
	XrResult result = xrLocateSpace(mHandSpaces[alHand], mReferenceSpace, mFrameState.predictedDisplayTime, &spaceLoc);
	if (XR_FAILED(result) || (spaceLoc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0)
		return false;

	// Position
	avPos = cVector3f(spaceLoc.pose.position.x, spaceLoc.pose.position.y, spaceLoc.pose.position.z);

	// Orientation
	aqRot = cQuaternion(spaceLoc.pose.orientation.w, spaceLoc.pose.orientation.x, spaceLoc.pose.orientation.y, spaceLoc.pose.orientation.z);

	// Controller state (simplified)
	afTrigger = 0.0f;
	afGrip = 0.0f;
	abMenu = false;
	abGripPressed = false;

	return true;
}

//-----------------------------------------------------------------------


//-----------------------------------------------------------------------




//-----------------------------------------------------------------------




//-----------------------------------------------------------------------

const char* cOpenXR::XrResultToString(XrResult result)
{
	switch (result)
	{
		case XR_SUCCESS: return "XR_SUCCESS";
		case XR_ERROR_VALIDATION_FAILURE: return "XR_ERROR_VALIDATION_FAILURE";
		case XR_ERROR_RUNTIME_FAILURE: return "XR_ERROR_RUNTIME_FAILURE";
		case XR_ERROR_OUT_OF_MEMORY: return "XR_ERROR_OUT_OF_MEMORY";
		case XR_ERROR_API_VERSION_UNSUPPORTED: return "XR_ERROR_API_VERSION_UNSUPPORTED";
		case XR_ERROR_INITIALIZATION_FAILED: return "XR_ERROR_INITIALIZATION_FAILED";
		case XR_ERROR_FUNCTION_UNSUPPORTED: return "XR_ERROR_FUNCTION_UNSUPPORTED";
		case XR_ERROR_FEATURE_UNSUPPORTED: return "XR_ERROR_FEATURE_UNSUPPORTED";
		case XR_ERROR_EXTENSION_NOT_PRESENT: return "XR_ERROR_EXTENSION_NOT_PRESENT";
		case XR_ERROR_LIMIT_REACHED: return "XR_ERROR_LIMIT_REACHED";
		case XR_ERROR_SIZE_INSUFFICIENT: return "XR_ERROR_SIZE_INSUFFICIENT";
		case XR_ERROR_HANDLE_INVALID: return "XR_ERROR_HANDLE_INVALID";
		case XR_ERROR_INSTANCE_LOST: return "XR_ERROR_INSTANCE_LOST";
		case XR_ERROR_SESSION_LOST: return "XR_ERROR_SESSION_LOST";
		case XR_ERROR_REFERENCE_SPACE_UNSUPPORTED: return "XR_ERROR_REFERENCE_SPACE_UNSUPPORTED";
		case XR_ERROR_POSE_INVALID: return "XR_ERROR_POSE_INVALID";
		case XR_ERROR_LAYER_INVALID: return "XR_ERROR_LAYER_INVALID";
		case XR_ERROR_LAYER_LIMIT_EXCEEDED: return "XR_ERROR_LAYER_LIMIT_EXCEEDED";
		case XR_ERROR_SWAPCHAIN_RECT_INVALID: return "XR_ERROR_SWAPCHAIN_RECT_INVALID";
		case XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED: return "XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED";
		case XR_ERROR_ACTION_TYPE_MISMATCH: return "XR_ERROR_ACTION_TYPE_MISMATCH";
		case XR_ERROR_SESSION_NOT_RUNNING: return "XR_ERROR_SESSION_NOT_RUNNING";
		case XR_ERROR_SESSION_NOT_STOPPING: return "XR_ERROR_SESSION_NOT_STOPPING";
		case XR_ERROR_TIME_INVALID: return "XR_ERROR_TIME_INVALID";
		case XR_ERROR_FILE_ACCESS_ERROR: return "XR_ERROR_FILE_ACCESS_ERROR";
		case XR_ERROR_FILE_CONTENTS_INVALID: return "XR_ERROR_FILE_CONTENTS_INVALID";
		case XR_ERROR_FORM_FACTOR_UNSUPPORTED: return "XR_ERROR_FORM_FACTOR_UNSUPPORTED";
		case XR_ERROR_FORM_FACTOR_UNAVAILABLE: return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
		case XR_ERROR_API_LAYER_NOT_PRESENT: return "XR_ERROR_API_LAYER_NOT_PRESENT";
		case XR_ERROR_CALL_ORDER_INVALID: return "XR_ERROR_CALL_ORDER_INVALID";
		case XR_ERROR_GRAPHICS_DEVICE_INVALID: return "XR_ERROR_GRAPHICS_DEVICE_INVALID";
#if defined(XR_ERROR_ANDROID_THREAD_SETTINGS_ID_INVALID_KHR)
		case XR_ERROR_ANDROID_THREAD_SETTINGS_ID_INVALID_KHR: return "XR_ERROR_ANDROID_THREAD_SETTINGS_ID_INVALID_KHR";
#endif
#if defined(XR_ERROR_ANDROID_THREAD_SETTINGS_FAILURE_KHR)
		case XR_ERROR_ANDROID_THREAD_SETTINGS_FAILURE_KHR: return "XR_ERROR_ANDROID_THREAD_SETTINGS_FAILURE_KHR";
#endif
		default: return "UNKNOWN_XR_ERROR";
	}
}

//-----------------------------------------------------------------------

void cOpenXR::CheckXrResult(XrResult result, const char* operation)
{
	if (XR_FAILED(result))
	{
		Error("OpenXR: %s failed: %s\n", operation, XrResultToString(result));
	}
}

};