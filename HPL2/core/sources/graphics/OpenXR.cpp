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
		mbEnabled = apConfig->GetBool("VR", "Enabled", false);
		mfIPD = apConfig->GetFloat("VR", "IPD", 0.064f);
		mfNearZ = apConfig->GetFloat("VR", "NearZ", 0.1f);
		mfFarZ = apConfig->GetFloat("VR", "FarZ", 1000.0f);
	}

	if (!mbEnabled)
	{
		Log("OpenXR: VR disabled in config\n");
		return true;
	}

	// Initialize OpenXR - minimal stub implementation
	Log("OpenXR: Initializing (stub)...\n");
	
	// Try to create instance
	XrInstanceCreateInfo createInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
	strcpy(createInfo.applicationInfo.applicationName, "Amnesia: The Dark Descent");
	strcpy(createInfo.applicationInfo.engineName, "HPL2 Engine");
	createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
	createInfo.applicationInfo.applicationVersion = 1;
	createInfo.applicationInfo.engineVersion = 2;

	const char* enabledExtensions[] = {
		"XR_KHR_opengl_enable",
		"XR_KHR_swapchain",
	};

	createInfo.enabledExtensionCount = sizeof(enabledExtensions) / sizeof(enabledExtensions[0]);
	createInfo.enabledExtensionNames = enabledExtensions;

	XrResult result = xrCreateInstance(&createInfo, &mInstance);
	if (XR_FAILED(result))
	{
		Log("OpenXR: Failed to create instance: %s\n", XrResultToString(result));
		mbEnabled = false;
		return true; // Don't fail the whole build, just disable VR
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

	Log("OpenXR: System found\n");
	mbAvailable = true;
	mbInitialized = true;
	
	Log("OpenXR: Initialized (stub - full VR rendering not implemented)\n");
	return true;
}

//-----------------------------------------------------------------------

void cOpenXR::Shutdown()
{
	if (!mbInitialized)
		return;

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

	mbAvailable = false;
	mbInitialized = false;

	Log("OpenXR: Shutdown\n");
}

//-----------------------------------------------------------------------

void cOpenXR::Update(float afTimeStep)
{
	// Stub - no-op
}

//-----------------------------------------------------------------------

void cOpenXR::BeginFrame()
{
	// Stub - no-op
}

//-----------------------------------------------------------------------

void cOpenXR::EndFrame()
{
	// Stub - no-op
}

//-----------------------------------------------------------------------

void cOpenXR::GetViewMatrices(cMatrixf& avLeftView, cMatrixf& avRightView, cMatrixf& avLeftProj, cMatrixf& avRightProj)
{
	avLeftView = cMatrixf::Identity;
	avRightView = cMatrixf::Identity;
	avLeftProj = cMatrixf::Identity;
	avRightProj = cMatrixf::Identity;
}

//-----------------------------------------------------------------------

cVector2l cOpenXR::GetRecommendedRenderTargetSize() const
{
	return cVector2l(0, 0);
}

//-----------------------------------------------------------------------

bool cOpenXR::GetControllerState(int alHand, cVector3f& avPos, cQuaternion& aqRot, float& afTrigger, float& afGrip, bool& abMenu, bool& abGripPressed)
{
	return false;
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