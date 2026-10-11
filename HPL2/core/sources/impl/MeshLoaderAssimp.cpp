/*
 * Copyright © 2009-2020 Frictional Games
 * Copyright © 2024 Open Source Replacement
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

#include "impl/MeshLoaderAssimp.h"

#include "system/LowLevelSystem.h"
#include "system/String.h"
#include "system/Platform.h"

#include "resources/MaterialManager.h"
#include "resources/MeshManager.h"
#include "resources/Resources.h"
#include "resources/FileSearcher.h"

#include "graphics/LowLevelGraphics.h"
#include "graphics/VertexBuffer.h"
#include "graphics/Mesh.h"
#include "graphics/SubMesh.h"

#include "graphics/Material.h"
#include "graphics/Skeleton.h"
#include "graphics/Bone.h"
#include "graphics/Animation.h"
#include "graphics/AnimationTrack.h"

#include "impl/MeshLoaderMSH.h"

#include "math/Math.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/mesh.h>
#include <assimp/anim.h>
#include <assimp/material.h>
#include <assimp/camera.h>
#include <assimp/light.h>
#include <assimp/metadata.h>

namespace hpl {

bool cMeshLoaderAssimp::mbConvertUnit = true;

//-----------------------------------------------------------------------

cMeshLoaderAssimp::cMeshLoaderAssimp(iLowLevelGraphics *apLowLevelGraphics, cMeshLoaderMSH *apMeshLoaderMSH, bool abLoadAndSaveMSHFormat) : iMeshLoader(apLowLevelGraphics)
{
	mpMeshLoaderMSH = apMeshLoaderMSH;
	mbLoadAndSaveMSHFormat = abLoadAndSaveMSHFormat;

	AddSupportedExtension("fbx");
	AddSupportedExtension("dae");
	AddSupportedExtension("obj");
	AddSupportedExtension("3ds");
	AddSupportedExtension("gltf");
	AddSupportedExtension("glb");

	mbZToY = true;
	m_mtxZToY = cMatrixf::Identity;
	m_mtxZToY.m[1][1] = 0;
	m_mtxZToY.m[2][2] = 0;
	m_mtxZToY.m[1][2] = 1;
	m_mtxZToY.m[2][1] = -1;

	mbLog = true;
	mbLowLog = false;
}

//-----------------------------------------------------------------------

cMeshLoaderAssimp::~cMeshLoaderAssimp()
{
}

//-----------------------------------------------------------------------

cMatrixf cMeshLoaderAssimp::AiMatrixToMatrix(const aiMatrix4x4& aMatrix)
{
	cMatrixf mtx;
	for(int i=0; i<4; i++)
		for(int j=0; j<4; j++)
			mtx.m[i][j] = aMatrix[i][j];
	return mtx;
}

cVector3f cMeshLoaderAssimp::AiVector3ToVector3(const aiVector3D& aVec)
{
	return cVector3f(aVec.x, aVec.y, aVec.z);
}

cQuaternion cMeshLoaderAssimp::AiQuaternionToQuaternion(const aiQuaternion& aQuat)
{
	return cQuaternion(aQuat.w, aQuat.x, aQuat.y, aQuat.z);
}

// Helper: multiply matrix (3x3 rotation part) with vector
static cVector3f MatrixMulVec3(const cMatrixf& aMatrix, const cVector3f& aVec)
{
	return cVector3f(
		aMatrix.m[0][0]*aVec.x + aMatrix.m[0][1]*aVec.y + aMatrix.m[0][2]*aVec.z,
		aMatrix.m[1][0]*aVec.x + aMatrix.m[1][1]*aVec.y + aMatrix.m[1][2]*aVec.z,
		aMatrix.m[2][0]*aVec.x + aMatrix.m[2][1]*aVec.y + aMatrix.m[2][2]*aVec.z
	);
}

//-----------------------------------------------------------------------

cMesh* cMeshLoaderAssimp::LoadMesh(const tWString& asFile, tMeshLoadFlag aFlags)
{
	/////////////////////////////////////////////////
	// TRY USING MSH LOADER
	if(mbLoadAndSaveMSHFormat)
	{
		tWString sMSHFile = cString::SetFileExtW(asFile, _W("msh"));
		cDate currentDate = cPlatform::FileModifiedDate(asFile);
		cDate mshDate = cPlatform::FileModifiedDate(sMSHFile);
		
		if(cResources::GetForceCacheLoadingAndSkipSaving() || mshDate > currentDate || cPlatform::FileExists(asFile)==false)
		{
			cMesh *pMesh = mpMeshLoaderMSH->LoadMesh(sMSHFile, aFlags);
			if(pMesh)
			{
				pMesh->SetFullPath(asFile);
				return pMesh;
			}
		}
	}

	std::string filePath = cString::To8Char(asFile);
	
	const aiScene* pScene = mImporter.ReadFile(filePath,
		aiProcess_Triangulate |
		aiProcess_GenSmoothNormals |
		aiProcess_CalcTangentSpace |
		aiProcess_JoinIdenticalVertices |
		aiProcess_SortByPType |
		aiProcess_LimitBoneWeights |
		aiProcess_ValidateDataStructure |
		aiProcess_ImproveCacheLocality |
		aiProcess_RemoveRedundantMaterials |
		aiProcess_FindDegenerates |
		aiProcess_FindInvalidData |
		aiProcess_GenUVCoords |
		aiProcess_TransformUVCoords |
		aiProcess_FlipUVs |
		aiProcess_FlipWindingOrder);

	if(!pScene)
	{
		Error("Failed to load mesh '%s': %s\n", filePath.c_str(), mImporter.GetErrorString());
		return NULL;
	}

	tAssimpImageVec lstImages;
	tAssimpTextureVec lstTextures;
	tAssimpMaterialVec lstMaterials;
	tAssimpLightVec lstLights;
	tAssimpGeometryVec lstGeometries;
	tAssimpControllerVec lstControllers;
	tAssimpAnimationVec lstAnimations;
	cAssimpScene assimpScene;

	if(!FillStructuresFromAssimp(pScene,
		&lstImages, &lstTextures, &lstMaterials, &lstLights,
		&lstGeometries, &lstControllers, &lstAnimations, &assimpScene))
	{
		return NULL;
	}

	// Create skeleton
	cSkeleton* pSkeleton = new cSkeleton();
	
	// Build bone hierarchy from node structure
	for(size_t i=0; i<assimpScene.mlstNodes.size(); i++)
	{
		cAssimpNode* pNode = *std::next(assimpScene.mlstNodes.begin(), i);
		if(pNode->pParent == &assimpScene.mRoot)
		{
			CreateSkeletonBone(pNode, pSkeleton->GetRootBone());
		}
	}

	// Check if any bones where found
	if(pSkeleton->GetRootBone()->GetChildIterator().HasNext() == false)
	{
		delete pSkeleton;
		pSkeleton = NULL;
	}

	// Load scene hierarchy and meshes
	cNode3D* pRootNode = NULL; // Will be created by CreateHierarchyNodes
	CreateHierarchyNodes(NULL, NULL, &assimpScene.mRoot, lstGeometries);

	// Create the mesh
	cMesh *pMesh = hplNew( cMesh, (cString::To8Char(asFile), asFile, mpMaterialManager, mpAnimationManager) );
	
	if(pSkeleton) pMesh->SetSkeleton(pSkeleton);

	// Add animations
	for(size_t i=0; i<lstAnimations.size(); i++)
	{
		cAnimationTrack* pTrack = CreateAnimTrack(NULL, pSkeleton, lstAnimations[i], &assimpScene);
		// Note: Animation creation is complex, simplified for now
	}

	/////////////////////////////////////////////////
	// SAVE MSH FORMAT
	if(cResources::GetForceCacheLoadingAndSkipSaving()==false && mbLoadAndSaveMSHFormat)
	{
		tWString sMSHFile = cString::SetFileExtW(asFile, _W("msh"));
		mpMeshLoaderMSH->SaveMesh(pMesh, sMSHFile);
	}
		
	return pMesh;
}

//-----------------------------------------------------------------------

cAnimation* cMeshLoaderAssimp::LoadAnimation(const tWString& asFile)
{
	/////////////////////////////////////////////////
	// TRY USING MSH LOADER
	if(mbLoadAndSaveMSHFormat)
	{
		tWString sMSHFile = cString::SetFileExtW(asFile, _W("anm"));
		cDate currentDate = cPlatform::FileModifiedDate(asFile);
		cDate mshDate = cPlatform::FileModifiedDate(sMSHFile);

		if(	cResources::GetForceCacheLoadingAndSkipSaving() ||
			mshDate > currentDate || cPlatform::FileExists(asFile)==false)
		{
			cAnimation *pAnim = mpMeshLoaderMSH->LoadAnimation(sMSHFile);
			if(pAnim)
			{
				pAnim->SetFullPath(asFile);
				return pAnim;
			}
		}
	}

	std::string filePath = cString::To8Char(asFile);
	
	const aiScene* pScene = mImporter.ReadFile(filePath,
		aiProcess_Triangulate |
		aiProcess_GenSmoothNormals |
		aiProcess_CalcTangentSpace |
		aiProcess_LimitBoneWeights |
		aiProcess_ValidateDataStructure);

	if(!pScene)
	{
		Error("Failed to load animation '%s': %s\n", filePath.c_str(), mImporter.GetErrorString());
		return NULL;
	}

	tAssimpImageVec lstImages;
	tAssimpTextureVec lstTextures;
	tAssimpMaterialVec lstMaterials;
	tAssimpLightVec lstLights;
	tAssimpGeometryVec lstGeometries;
	tAssimpControllerVec lstControllers;
	tAssimpAnimationVec lstAnimations;
	cAssimpScene assimpScene;

	if(!FillStructuresFromAssimp(pScene,
		&lstImages, &lstTextures, &lstMaterials, &lstLights,
		&lstGeometries, &lstControllers, &lstAnimations, &assimpScene))
	{
		return NULL;
	}

	// Create skeleton for animation
	cSkeleton* pSkeleton = new cSkeleton();
	for(size_t i=0; i<assimpScene.mlstNodes.size(); i++)
	{
		cAssimpNode* pNode = *std::next(assimpScene.mlstNodes.begin(), i);
		if(pNode->pParent == &assimpScene.mRoot)
		{
			CreateSkeletonBone(pNode, pSkeleton->GetRootBone());
		}
	}

	// Calculate local matrices after controller bind matrix
	if(pSkeleton)
	{
		cBoneIterator boneIt = pSkeleton->GetRootBone()->GetChildIterator();
		while(boneIt.HasNext())
		{
			cMatrixf mtxIdentity = cMatrixf::Identity;
			CalcLocalMatrixAfterControllerBindMatrixRec(boneIt.Next(), mtxIdentity, mtxIdentity, 0);
		}
	}

	// Load animations from assimp
	cAnimation* pAnimation = NULL;
	if(!lstAnimations.empty())
	{
		pAnimation = hplNew( cAnimation, (lstAnimations[0].msId.c_str(), asFile, cString::GetFileName(cString::To8Char(asFile))) );
		pAnimation->SetLength(assimpScene.mfDeltaTime > 0 ? assimpScene.mfDeltaTime : 1.0f);
		
		for(size_t i=0; i<lstAnimations.size(); i++)
		{
			cAnimationTrack* pTrack = CreateAnimTrack(pAnimation, pSkeleton, lstAnimations[i], &assimpScene);
		}
	}

	/////////////////////////////////////////////////
	// SAVE MSH FORMAT
	if(cResources::GetForceCacheLoadingAndSkipSaving()==false && mbLoadAndSaveMSHFormat)
	{
		tWString sMSHFile = cString::SetFileExtW(asFile, _W("anm"));
		mpMeshLoaderMSH->SaveAnimation(pAnimation, sMSHFile);
	}

	if(pSkeleton) hplDelete(pSkeleton);

	return pAnimation;
}

//-----------------------------------------------------------------------

void cMeshLoaderAssimp::CleanUpNonAnimatedNodes(cAnimation *apAnimation, cSkeleton *apSkeleton, cAssimpScene* apAssimpScene )
{
	// Simplified - remove nodes that don't have animation tracks
}

//-----------------------------------------------------------------------

tString cMeshLoaderAssimp::GetParentName(cAssimpNode *apNode, cAssimpScene* apAssimpScene, tAssimpGeometryVec *apAssimpGeometries)
{
	if(apNode->pParent == NULL || apNode->pParent == &apAssimpScene->mRoot)
		return "";
	return apNode->pParent->msName;
}

//-----------------------------------------------------------------------

void cMeshLoaderAssimp::CreateMeshJoint(cMeshJoint* apJoint, ePhysicsJointType aJointType, cBoundingVolume &aBV, 
										tStringVec &avStrings, cAssimpNode* apNode, cAssimpScene &aAssimpScene,
										tAssimpGeometryVec &avAssimpGeom)
{
	// Physics joint creation - simplified
}

//-----------------------------------------------------------------------

void cMeshLoaderAssimp::CreateHierarchyNodes(cMesh *apMesh, cNode3D* mpParentNode, 
	cAssimpNode* apAssimpNode, 
	tAssimpGeometryVec &avAssimpGeom)
{
	// Create HPL2 node hierarchy from Assimp nodes
	for(tAssimpNodeListIt it = apAssimpNode->mlstChildren.begin(); it != apAssimpNode->mlstChildren.end(); ++it)
	{
		cAssimpNode* pChildNode = *it;
		
		// Check if this node has geometry
		cAssimpGeometry* pGeom = GetGeometry(pChildNode->msSource, avAssimpGeom);
		if(pGeom && !pGeom->mvVertexVec.empty())
		{
			// Create submesh/node for this geometry
			iVertexBuffer* pVtxBuffer = CreateVertexBuffer(*pGeom, eVertexBufferUsageType_Static);
			if(pVtxBuffer)
			{
				// Node creation would happen here
			}
		}
		
		// Recurse
		CreateHierarchyNodes(apMesh, mpParentNode, pChildNode, avAssimpGeom);
	}
}

//-----------------------------------------------------------------------

cAssimpGeometry* cMeshLoaderAssimp::GetGeometry(const tString& asId, tAssimpGeometryVec &avGeomVec)
{
	for(size_t i=0; i<avGeomVec.size(); i++)
	{
		if(avGeomVec[i].msId == asId || avGeomVec[i].msName == asId)
			return &avGeomVec[i];
	}
	return NULL;
}

//-----------------------------------------------------------------------

cAssimpLight* cMeshLoaderAssimp::GetLight(const tString& asId, tAssimpLightVec &avLightVec)
{
	for(size_t i=0; i<avLightVec.size(); i++)
	{
		if(avLightVec[i].msId == asId || avLightVec[i].msName == asId)
			return &avLightVec[i];
	}
	return NULL;
}

//-----------------------------------------------------------------------

cAnimationTrack* cMeshLoaderAssimp::CreateAnimTrack(cAnimation *apAnimation, cSkeleton *apSkeleton,
						cAssimpAnimation &aAnim, cAssimpScene *apScene)
{
	// Simplified animation track creation
	if(!apAnimation || !apSkeleton) return NULL;
	
	cBone* pBone = apSkeleton->GetBoneByName(aAnim.msTargetNode);
	if(!pBone) return NULL;

	cAnimationTrack* pTrack = apAnimation->CreateTrack(aAnim.msTargetNode.c_str(), 
		eAnimTransformFlag_Translate | eAnimTransformFlag_Rotate | eAnimTransformFlag_Scale);
	
	// Extract keyframes from Assimp animation data
	// This is a simplified version - real implementation would process channels/samplers
	
	return pTrack;
}

//-----------------------------------------------------------------------

void cMeshLoaderAssimp::CalcLocalMatrixAfterControllerBindMatrixRec(cBone* apBone, cMatrixf a_mtxParentGlobal, cMatrixf a_mtxParentGlobalUnscaled, int alDepth)
{
	// Calculate local matrix from global and parent global
	if(apBone->GetValue() != 0)
	{
		cMatrixf mtxGlobal = apBone->GetLocalTransform();
		cMatrixf mtxInvParent = cMath::MatrixInverse(a_mtxParentGlobal);
		cMatrixf mtxLocal = cMath::MatrixMul(mtxInvParent, mtxGlobal);
		apBone->SetTransform(mtxLocal);
		a_mtxParentGlobal = mtxGlobal;

		// unscaled
		cMatrixf mtxGlobalUnscaled = apBone->GetLocalTransformUnscaled();
		cMatrixf mtxInvParentUnscaled = cMath::MatrixInverse(a_mtxParentGlobalUnscaled);
		cMatrixf mtxLocalUnscaled = cMath::MatrixMul(mtxInvParentUnscaled, mtxGlobalUnscaled);
		apBone->SetTransformUnscaled(mtxLocalUnscaled);
		a_mtxParentGlobalUnscaled = mtxGlobalUnscaled;
	}
	else
	{
		a_mtxParentGlobal = apBone->GetWorldTransform();
		a_mtxParentGlobalUnscaled = apBone->GetWorldTransformUnscaled();
	}
	
	cBoneIterator it = apBone->GetChildIterator();
	while(it.HasNext())
	{
		CalcLocalMatrixAfterControllerBindMatrixRec(it.Next(), a_mtxParentGlobal, a_mtxParentGlobalUnscaled, alDepth+1);
	}
}

//-----------------------------------------------------------------------

void cMeshLoaderAssimp::CreateSkeletonBone(cAssimpNode* apAssimpNode, cBone* apParentBone)
{
	// Check if this node is a bone (has mesh with skinning data or is marked as joint)
	bool isBone = false;
	// In Assimp, bones are typically nodes that are referenced by meshes with vertex weights
	
	cBone* pNewBone = apParentBone->CreateChildBone(apAssimpNode->msName.c_str(), apAssimpNode->msName.c_str());
	
	// Set initial transform from Assimp node (convert aiMatrix4x4 to cMatrixf)
	aiMatrix4x4 aiMtx;
	for(int r=0; r<4; r++)
		for(int c=0; c<4; c++)
			aiMtx[r][c] = apAssimpNode->m_mtxTransform.m[r][c];
	cMatrixf mtxLocal = AiMatrixToMatrix(aiMtx);
	pNewBone->SetTransform(mtxLocal);
	pNewBone->SetTransformUnscaled(mtxLocal);
	pNewBone->SetValue(0); // Will be set to 1 if linked to geometry
	
	// Recurse for children
	for(tAssimpNodeListIt it = apAssimpNode->mlstChildren.begin(); it != apAssimpNode->mlstChildren.end(); ++it)
	{
		CreateSkeletonBone(*it, pNewBone);
	}
}

//-----------------------------------------------------------------------

iVertexBuffer *cMeshLoaderAssimp::CreateVertexBuffer(cAssimpGeometry & aGeometry, 
														eVertexBufferUsageType aUsageType)
{
	if(aGeometry.mvVertexVec.empty() || aGeometry.mvIndexVec.empty())
		return NULL;

	iVertexBuffer *pVtxBuffer = mpLowLevelGraphics->CreateVertexBuffer(
		eVertexBufferType_Hardware, eVertexBufferDrawType_Tri,
		aUsageType, (int)aGeometry.mvVertexVec.size(), (int)aGeometry.mvIndexVec.size());

	pVtxBuffer->CreateElementArray(eVertexBufferElement_Position, eVertexBufferElementFormat_Float, 4);
	pVtxBuffer->CreateElementArray(eVertexBufferElement_Normal, eVertexBufferElementFormat_Float, 3);
	pVtxBuffer->CreateElementArray(eVertexBufferElement_Texture0, eVertexBufferElementFormat_Float, 3);
	pVtxBuffer->CreateElementArray(eVertexBufferElement_Color0, eVertexBufferElementFormat_Float, 4);
	pVtxBuffer->CreateElementArray(eVertexBufferElement_Texture1Tangent, eVertexBufferElementFormat_Float, 4);

	// Add vertices
	for(size_t i=0; i<aGeometry.mvVertexVec.size(); i++)
	{
		cVertex &vtx = aGeometry.mvVertexVec[i];
		pVtxBuffer->AddVertexVec3f(eVertexBufferElement_Position, vtx.pos);
		pVtxBuffer->AddVertexColor(eVertexBufferElement_Color0, vtx.col);
		pVtxBuffer->AddVertexVec3f(eVertexBufferElement_Normal, vtx.norm);
		pVtxBuffer->AddVertexVec3f(eVertexBufferElement_Texture0, vtx.tex);
		pVtxBuffer->AddVertexVec3f(eVertexBufferElement_Texture1Tangent, vtx.tan);
	}

	// Add indices
	for(size_t i=0; i<aGeometry.mvIndexVec.size(); i++)
	{
		pVtxBuffer->AddIndex(aGeometry.mvIndexVec[i]);
	}

	pVtxBuffer->Compile(0);
	return pVtxBuffer;
}

//-----------------------------------------------------------------------

bool cMeshLoaderAssimp::FillStructuresFromAssimp(const aiScene* apScene,
							tAssimpImageVec *apAssimpImageVec,
							tAssimpTextureVec *apAssimpTextureVec,
							tAssimpMaterialVec *apAssimpMaterialVec,
							tAssimpLightVec *apAssimpLightVec,
							tAssimpGeometryVec *apAssimpGeometryVec,
							tAssimpControllerVec *apAssimpControllerVec,
							tAssimpAnimationVec *apAssimpAnimVec,
							cAssimpScene *apAssimpScene)
{
	// Load materials
	LoadAssimpMaterials(apScene, *apAssimpMaterialVec);
	
	// Load meshes and controllers (skinning)
	LoadAssimpMeshes(apScene, *apAssimpGeometryVec, apAssimpControllerVec);
	
	// Load node hierarchy
	LoadAssimpNodeHierarchy(apScene->mRootNode, &apAssimpScene->mRoot, apAssimpScene, apScene);
	
	// Load animations
	LoadAssimpAnimations(apScene, *apAssimpAnimVec, apAssimpScene);
	
	return true;
}

//-----------------------------------------------------------------------

void cMeshLoaderAssimp::LoadAssimpNodeHierarchy(const aiNode* apNode, cAssimpNode* apParentNode, cAssimpScene* apScene, const aiScene* apAiScene)
{
	cAssimpNode* pNewNode = apParentNode->CreateChild();
	pNewNode->msId = apNode->mName.C_Str();
	pNewNode->msName = apNode->mName.C_Str();
	
	// Convert Assimp matrix to HPL2 matrix
	aiMatrix4x4 mtx = apNode->mTransformation;
	pNewNode->m_mtxTransform = AiMatrixToMatrix(mtx);
	
	// Compute world transform
	if(apParentNode == &apScene->mRoot)
		pNewNode->m_mtxWorldTransform = pNewNode->m_mtxTransform;
	else
		pNewNode->m_mtxWorldTransform = cMath::MatrixMul(apParentNode->m_mtxWorldTransform, pNewNode->m_mtxTransform);
	
	// Process meshes attached to this node
	for(unsigned int i=0; i<apNode->mNumMeshes; i++)
	{
		unsigned int meshIndex = apNode->mMeshes[i];
		if(meshIndex < apAiScene->mNumMeshes)
		{
			// Link geometry to this node
			// Geometry will be loaded separately in LoadAssimpMeshes
			pNewNode->msSource = "mesh_" + cString::ToString(meshIndex);
			pNewNode->msInstanceMaterial = "material_" + cString::ToString(apAiScene->mMeshes[meshIndex]->mMaterialIndex);
		}
	}
	
	apScene->mlstNodes.push_back(pNewNode);
	
	// Recurse children
	for(unsigned int i=0; i<apNode->mNumChildren; i++)
	{
		LoadAssimpNodeHierarchy(apNode->mChildren[i], pNewNode, apScene, apAiScene);
	}
}

//-----------------------------------------------------------------------

void cMeshLoaderAssimp::LoadAssimpAnimations(const aiScene* apScene, tAssimpAnimationVec &avAnimations,
								cAssimpScene *apAssimpScene)
{
	for(unsigned int i=0; i<apScene->mNumAnimations; i++)
	{
		const aiAnimation* pAnim = apScene->mAnimations[i];
		
		cAssimpAnimation anim;
		anim.msId = pAnim->mName.C_Str();
		anim.msTargetNode = pAnim->mName.C_Str();
		
		// Calculate duration
		double duration = pAnim->mDuration;
		double ticksPerSecond = pAnim->mTicksPerSecond > 0 ? pAnim->mTicksPerSecond : 25.0;
		apAssimpScene->mfDeltaTime = (float)(duration / ticksPerSecond);
		
		// Process animation channels
		for(unsigned int j=0; j<pAnim->mNumChannels; j++)
		{
			const aiNodeAnim* pChannel = pAnim->mChannels[j];
			
			cAssimpChannel channel;
			channel.msId = pChannel->mNodeName.C_Str();
			channel.msTarget = pChannel->mNodeName.C_Str();
			
			// Create samplers for position, rotation, scale
			if(pChannel->mNumPositionKeys > 0)
			{
				cAssimpSampler sampler;
				sampler.msId = "pos_" + channel.msId;
				sampler.msTarget = channel.msTarget;
				
				cAssimpAnimSource source;
				source.msId = "time_" + sampler.msId;
				for(unsigned int k=0; k<pChannel->mNumPositionKeys; k++)
				{
					source.mvValues.push_back((float)(pChannel->mPositionKeys[k].mTime / ticksPerSecond));
				}
				anim.mvSources.push_back(source);
				
				source.msId = "value_" + sampler.msId;
				for(unsigned int k=0; k<pChannel->mNumPositionKeys; k++)
				{
					const aiVector3D& v = pChannel->mPositionKeys[k].mValue;
					source.mvValues.push_back(v.x);
					source.mvValues.push_back(v.y);
					source.mvValues.push_back(v.z);
				}
				anim.mvSources.push_back(source);
				
				anim.mvSamplers.push_back(sampler);
				anim.mvChannels.push_back(channel);
			}
			
			if(pChannel->mNumRotationKeys > 0)
			{
				cAssimpSampler sampler;
				sampler.msId = "rot_" + channel.msId;
				sampler.msTarget = channel.msTarget;
				
				cAssimpAnimSource source;
				source.msId = "time_" + sampler.msId;
				for(unsigned int k=0; k<pChannel->mNumRotationKeys; k++)
				{
					source.mvValues.push_back((float)(pChannel->mRotationKeys[k].mTime / ticksPerSecond));
				}
				anim.mvSources.push_back(source);
				
				source.msId = "value_" + sampler.msId;
				for(unsigned int k=0; k<pChannel->mNumRotationKeys; k++)
				{
					const aiQuaternion& q = pChannel->mRotationKeys[k].mValue;
					source.mvValues.push_back(q.x);
					source.mvValues.push_back(q.y);
					source.mvValues.push_back(q.z);
					source.mvValues.push_back(q.w);
				}
				anim.mvSources.push_back(source);
				
				anim.mvSamplers.push_back(sampler);
				anim.mvChannels.push_back(channel);
			}
			
			if(pChannel->mNumScalingKeys > 0)
			{
				cAssimpSampler sampler;
				sampler.msId = "scale_" + channel.msId;
				sampler.msTarget = channel.msTarget;
				
				cAssimpAnimSource source;
				source.msId = "time_" + sampler.msId;
				for(unsigned int k=0; k<pChannel->mNumScalingKeys; k++)
				{
					source.mvValues.push_back((float)(pChannel->mScalingKeys[k].mTime / ticksPerSecond));
				}
				anim.mvSources.push_back(source);
				
				source.msId = "value_" + sampler.msId;
				for(unsigned int k=0; k<pChannel->mNumScalingKeys; k++)
				{
					const aiVector3D& v = pChannel->mScalingKeys[k].mValue;
					source.mvValues.push_back(v.x);
					source.mvValues.push_back(v.y);
					source.mvValues.push_back(v.z);
				}
				anim.mvSources.push_back(source);
				
				anim.mvSamplers.push_back(sampler);
				anim.mvChannels.push_back(channel);
			}
		}
		
		avAnimations.push_back(anim);
	}
}

//-----------------------------------------------------------------------

void cMeshLoaderAssimp::LoadAssimpMaterials(const aiScene* apScene, tAssimpMaterialVec &avAssimpMaterialVec)
{
	for(unsigned int i=0; i<apScene->mNumMaterials; i++)
	{
		const aiMaterial* pMat = apScene->mMaterials[i];
		
		cAssimpMaterial mat;
		mat.msId = "material_" + cString::ToString(i);
		
		aiString name;
		if(pMat->Get(AI_MATKEY_NAME, name) == AI_SUCCESS)
			mat.msName = name.C_Str();
		else
			mat.msName = mat.msId;
		
		// Diffuse color
		aiColor3D diffuse(1,1,1);
		pMat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
		mat.mDiffuseColor = cColor(diffuse.r, diffuse.g, diffuse.b, 1.0f);
		
		// Texture
		aiString texPath;
		if(pMat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS)
		{
			mat.msTexture = texPath.C_Str();
		}
		
		avAssimpMaterialVec.push_back(mat);
	}
}

//-----------------------------------------------------------------------

void cMeshLoaderAssimp::LoadAssimpMeshes(const aiScene* apScene, tAssimpGeometryVec &avAssimpGeometryVec,
							tAssimpControllerVec *apAssimpControllerVec)
{
	for(unsigned int i=0; i<apScene->mNumMeshes; i++)
	{
		const aiMesh* pMesh = apScene->mMeshes[i];
		
		cAssimpGeometry geom;
		geom.msId = "mesh_" + cString::ToString(i);
		geom.msName = pMesh->mName.C_Str();
		
		if(pMesh->mMaterialIndex < apScene->mNumMaterials)
		{
			geom.msMaterial = "material_" + cString::ToString(pMesh->mMaterialIndex);
		}
		
		// Vertices
		geom.mvVertexVec.resize(pMesh->mNumVertices);
		geom.mvIndexVec.reserve(pMesh->mNumFaces * 3);
		
		// Positions
		for(unsigned int v=0; v<pMesh->mNumVertices; v++)
		{
			cVector3f pos = AiVector3ToVector3(pMesh->mVertices[v]);
			if(mbZToY) pos = MatrixMulVec3(m_mtxZToY, pos);
			geom.mvVertexVec[v].pos = pos;
			geom.mvVertexVec[v].col = cColor(1,1,1,1);
			geom.mvVertexVec[v].tex = cVector3f(0,0,0);
			geom.mvVertexVec[v].norm = cVector3f(0,1,0);
			geom.mvVertexVec[v].tan = cVector3f(1,0,0);
		}
		
		// Normals
		if(pMesh->HasNormals())
		{
			for(unsigned int v=0; v<pMesh->mNumVertices; v++)
			{
				cVector3f norm = AiVector3ToVector3(pMesh->mNormals[v]);
				if(mbZToY) norm = MatrixMulVec3(m_mtxZToY, norm);
				norm.Normalize();
				geom.mvVertexVec[v].norm = norm;
			}
		}
		
		// Tangents
		if(pMesh->HasTangentsAndBitangents())
		{
			for(unsigned int v=0; v<pMesh->mNumVertices; v++)
			{
				cVector3f tan = AiVector3ToVector3(pMesh->mTangents[v]);
				if(mbZToY) tan = MatrixMulVec3(m_mtxZToY, tan);
				tan.Normalize();
				geom.mvVertexVec[v].tan = tan;
			}
		}
		
		// UV coordinates
		if(pMesh->HasTextureCoords(0))
		{
			for(unsigned int v=0; v<pMesh->mNumVertices; v++)
			{
				cVector3f uv(AiVector3ToVector3(pMesh->mTextureCoords[0][v]));
				uv.y = 1.0f - uv.y; // Flip V
				geom.mvVertexVec[v].tex = uv;
			}
		}
		
		// Indices (faces)
		for(unsigned int f=0; f<pMesh->mNumFaces; f++)
		{
			const aiFace& face = pMesh->mFaces[f];
			for(unsigned int j=0; j<face.mNumIndices; j++)
			{
				geom.mvIndexVec.push_back(face.mIndices[j]);
			}
		}
		
		// Vertex weights (skinning)
		if(pMesh->HasBones() && apAssimpControllerVec)
		{
			cAssimpController controller;
			controller.msId = "controller_" + cString::ToString(i);
			controller.msTarget = geom.msName;
			
			// Bind shape matrix (identity for now)
			controller.m_mtxBindShapeMatrix = cMatrixf::Identity;
			
			for(unsigned int b=0; b<pMesh->mNumBones; b++)
			{
				const aiBone* pBone = pMesh->mBones[b];
				
				controller.mvJoints.push_back(pBone->mName.C_Str());
				
				// Bind pose matrix (offset matrix)
				cMatrixf bindMatrix = AiMatrixToMatrix(pBone->mOffsetMatrix);
				controller.mvMatrices.push_back(bindMatrix);
				
				// Weights
				for(unsigned int w=0; w<pBone->mNumWeights; w++)
				{
					const aiVertexWeight& weight = pBone->mWeights[w];
					controller.mvWeights.push_back(weight.mWeight);
					
					cAssimpJointPair pair(b, weight.mWeight);
					controller.mvPairs.push_back(tAssimpJointPairList());
					controller.mvPairs.back().push_back(pair);
				}
			}
			
			apAssimpControllerVec->push_back(controller);
		}
		
		avAssimpGeometryVec.push_back(geom);
	}
}

//-----------------------------------------------------------------------

void cMeshLoaderAssimp::SplitVertices(cAssimpGeometry &aGeometry, tAssimpExtraVtxListVec &avExtraVtxVec, 
							tVertexVec &avVertexVec, tUIntVec &avIndexVec)
{
	// Handle vertex splitting for multiple UV sets - simplified
	avExtraVtxVec.resize(avVertexVec.size());
}

//-----------------------------------------------------------------------

void cMeshLoaderAssimp::FillVertexVec(const char* apChars, tVector3fVec &avVtxVec, int alElements, int alVtxCount)
{
	// Not used with Assimp
}

//-----------------------------------------------------------------------

tString cMeshLoaderAssimp::GetTopString(const tString asPath)
{
	size_t pos = asPath.find_last_of("/\\");
	if(pos != tString::npos)
		return asPath.substr(pos+1);
	return asPath;
}

//-----------------------------------------------------------------------

tString cMeshLoaderAssimp::GetMaterialTextureFile(const tString &asMaterial, tAssimpMaterialVec &avAssimpMaterialVec,
																tAssimpTextureVec &avAssimpTextureVec,
																tAssimpImageVec &avAssimpImageVec)
{
	for(size_t i=0; i<avAssimpMaterialVec.size(); i++)
	{
		if(avAssimpMaterialVec[i].msName == asMaterial || avAssimpMaterialVec[i].msId == asMaterial)
		{
			return avAssimpMaterialVec[i].msTexture;
		}
	}
	return "";
}

//-----------------------------------------------------------------------

cVector3f cMeshLoaderAssimp::GetVectorPos(const cVector3f &avVec)
{
	return avVec;
}

cVector3f cMeshLoaderAssimp::GetVectorNormal(const cVector3f &avVec)
{
	return avVec;
}

cMatrixf cMeshLoaderAssimp::GetMatrix(const cMatrixf &amtxA)
{
	return amtxA;
}

cVector3f cMeshLoaderAssimp::GetVectorPosFromPtr(float *apVec)
{
	return cVector3f(apVec[0], apVec[1], apVec[2]);
}

cVector3f cMeshLoaderAssimp::GetVectorRotationFromPtr(float *apVec)
{
	return cVector3f(apVec[0], apVec[1], apVec[2]);
}

cVector3f cMeshLoaderAssimp::GetVectorScaleFromPtr(float *apVec)
{
	return cVector3f(apVec[0], apVec[1], apVec[2]);
}

};