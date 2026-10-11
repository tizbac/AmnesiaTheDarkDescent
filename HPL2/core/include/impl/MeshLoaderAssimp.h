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

#ifndef HPL_MESH_LOADER_ASSIMP_H
#define HPL_MESH_LOADER_ASSIMP_H

#include "math/MathTypes.h"
#include "resources/MeshLoader.h"
#include "graphics/VertexBuffer.h"
#include "physics/PhysicsJoint.h"
#include "system/MemoryManager.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/mesh.h>
#include <assimp/anim.h>
#include <assimp/material.h>
#include <assimp/camera.h>
#include <assimp/light.h>
#include <assimp/metadata.h>

namespace hpl {

	class cMesh;
	class cNode3D;
	class iVertexBuffer;
	class cBone;
	class cAnimation;
	class cAnimationTrack;
	class cSkeleton;
	class cWorld;
	class cSector;
	class cMeshEntity;
	class iPhysicsBody;
	class cMeshJoint;
	class cSystem;
	class cMeshLoaderMSH;

	//------------------------------------------------------------
	// HELP CLASSES (similar to Collada loader)

	class cAssimpImage
	{
	public:
		tString msId;
		tString msName;
		tString msSource;
	};

	typedef std::vector<cAssimpImage> tAssimpImageVec;
	
	//------------------------------------------------
	
	class cAssimpTexture
	{
	public:
		tString msId;
		tString msName;
		tString msImage;
	};

	typedef std::vector<cAssimpTexture> tAssimpTextureVec;
	
	//------------------------------------------------

	class cAssimpMaterial
	{
	public:
		tString msId;
		tString msName;
		tString msTexture;
		cColor mDiffuseColor;
	};

	typedef std::vector<cAssimpMaterial> tAssimpMaterialVec;
	
	//------------------------------------------------
	
	class cAssimpLight
	{
	public:
		tString msId;
		tString msName;
		tString msType;
		cColor mDiffuseColor;
		float mfAngle;
	};

	typedef std::vector<cAssimpLight> tAssimpLightVec;

	//------------------------------------------------

	class cAssimpVtxArray
	{
	public:
		cAssimpVtxArray() : mbIsInVertex(false){}	
	
		tString msId;
		tString msType;
		bool mbIsInVertex;
		tVector3fVec mvArray;
	};

	typedef std::vector<cAssimpVtxArray> tAssimpVtxArrayVec;

	class cAssimpVtxIndex
	{
	public:
        int mlVtx;
		int mlNorm;
		int mlTex;
	};
	
	typedef std::vector<cAssimpVtxIndex> tAssimpVtxIndexVec;

	//------------------------------------------------

	class cAssimpExtraVtx
	{
	public:
		cAssimpExtraVtx(int alVtx,int alNorm,int alTex,int alNewVtx)
		{
			mlVtx = alVtx;
			mlNorm = alNorm;
			mlTex = alTex;
			mlNewVtx = alNewVtx;
		}

		cAssimpExtraVtx(){}

		int mlVtx;
		int mlNorm;
		int mlTex;
		int mlNewVtx;

		bool Equals(const cAssimpVtxIndex& aData)
		{
			if(mlVtx == aData.mlVtx && mlNorm == aData.mlNorm && mlTex == aData.mlTex)
			{
				return true;
			}
			return false;
		}
	};

	typedef std::list<cAssimpExtraVtx> tAssimpExtraVtxList;
	typedef tAssimpExtraVtxList::iterator tAssimpExtraVtxListIt;
	typedef std::vector<tAssimpExtraVtxList> tAssimpExtraVtxListVec;

	//------------------------------------------------

	class cAssimpGeometry
	{
	public:
		cAssimpGeometry() : mlPosArrayIdx(-1),mlNormArrayIdx(-1),mlTexArrayIdx(-1),
						mlPosIdxNum(-1),mlNormIdxNum(-1),mlTexIdxNum(-1)  {}

		void Clear()
		{
			mvIndices.clear();
			for(int i=0; i<(int)mvArrayVec.size();i++)
			{
				mvArrayVec[i].mvArray.clear();
			}
		}	

		tString msId;
		tString msName;	
		tVertexVec mvVertexVec;
		tUIntVec mvIndexVec;
		tAssimpExtraVtxListVec mvExtraVtxVec;
		tFloatVec mvTangents;
		tString msMaterial;

		// Used when loading from geometry element
		tAssimpVtxArrayVec mvArrayVec;
		tAssimpVtxIndexVec mvIndices;
		
		int mlPosIdxNum;
		int mlNormIdxNum;
		int mlTexIdxNum;

		int mlPosArrayIdx;
		int mlNormArrayIdx;
		int mlTexArrayIdx;
	};

	typedef std::vector<cAssimpGeometry> tAssimpGeometryVec;
	
	//------------------------------------------------
	
	class cAssimpJointPair
	{
	public:
		cAssimpJointPair(){}
		cAssimpJointPair(int alJoint, int alWeight)
		{
			mlJoint = alJoint;
			mlWeight = alWeight;
		}

		int mlJoint;
		int mlWeight;
	};

	typedef std::list<cAssimpJointPair> tAssimpJointPairList;
	typedef tAssimpJointPairList::iterator tAssimpJointPairListIt;
	typedef std::vector<tAssimpJointPairList> tAssimpJointPairListVec;

	class cAssimpController
	{
	public:
		cAssimpController() : mlJointPairIdx(-1), mlWeightPairIdx(-1){}

		tString msTarget;
		tString msId;
		cMatrixf m_mtxBindShapeMatrix;
		int mlJointPairIdx;
		int mlWeightPairIdx;
		tStringVec mvJoints;
		tFloatVec mvWeights;
		tMatrixfVec mvMatrices;
		tAssimpJointPairListVec mvPairs;
	};

	typedef std::vector<cAssimpController> tAssimpControllerVec;

	//------------------------------------------------
	
	class cAssimpChannel
	{
	public:
		tString msId;
		tString msTarget;
		tString msSource;
	};

	typedef std::vector<cAssimpChannel> tAssimpChannelVec;

	class cAssimpSampler
	{
	public:
		tString msId;
		tString msTimeArray;
		tString msValueArray;
		tString msTarget;
	};
	
	typedef std::vector<cAssimpSampler> tAssimpSamplerVec;
	
	class cAssimpAnimSource
	{
	public:
		tString msId;
		tFloatVec mvValues;
	};

	typedef std::vector<cAssimpAnimSource> tAssimpAnimSourceVec;

	class cAssimpAnimation
	{
	public:
		tString msId;
		tString msTargetNode;
		tAssimpChannelVec mvChannels;
		tAssimpSamplerVec mvSamplers;
		tAssimpAnimSourceVec mvSources;

		tFloatVec* GetSourceVec(const tString& asId)
		{
			for(size_t i=0; i< mvSources.size(); i++)
			{
				if(mvSources[i].msId == asId)
				{
					return &mvSources[i].mvValues;
				}
			}
			return NULL;
		}
	};

	typedef std::vector<cAssimpAnimation> tAssimpAnimationVec;

	//------------------------------------------------
	
	class cAssimpTransform
	{
	public:
		tString msSid;
		tString msType;
		tFloatVec mvValues;
	};

	typedef std::list<cAssimpTransform> tAssimpTransformList;
	typedef tAssimpTransformList::iterator tAssimpTransformListIt;

	class cAssimpNode;
	typedef std::list<cAssimpNode*> tAssimpNodeList;
	typedef tAssimpNodeList::iterator tAssimpNodeListIt;

	class cAssimpNode
	{
	public:
		cAssimpNode() : mlCount(0), pParent(NULL), mvScale(1,1,1), msInstanceMaterial("") {}
		
		tString msId;
		tString msName;
		tString msSid;
		tString msType;
		tString msInstanceMaterial;
		tString msSource;
		bool mbSourceIsFile;
		cMatrixf m_mtxTransform;
		cMatrixf m_mtxWorldTransform;
		cAssimpNode *pParent;
		cVector3f mvScale;
		int mlCount;
		tAssimpNodeList mlstChildren;
		tAssimpTransformList mlstTransforms;

		cAssimpNode* CreateChild()
		{
			cAssimpNode *pNode = hplNew( cAssimpNode, () );
			mlstChildren.push_back(pNode);
			pNode->pParent = this;
			return pNode;
		}

        cAssimpTransform* GetTransform(const tString &asSid)
		{
			tAssimpTransformListIt it = mlstTransforms.begin();
			for(; it != mlstTransforms.end(); it++)
			{
				 cAssimpTransform &Trans = *it;
				 if(Trans.msSid == asSid)
				 {
					 return &Trans;
				 }
			}
			return NULL;
		}
	};

	class cAssimpScene
	{
	public:
		cAssimpScene()
		{
			mRoot.m_mtxTransform = cMatrixf::Identity;
			mRoot.m_mtxWorldTransform = cMatrixf::Identity;
			mfDeltaTime = 0;
		}

		~cAssimpScene()
		{
			STLDeleteAll(mlstNodes);
		}

		void ResetNodes()
		{
			STLDeleteAll(mlstNodes);
			mRoot.mlstChildren.clear();
		}

		cAssimpNode* GetNode(const tString asId)
		{
			tAssimpNodeListIt it = mlstNodes.begin();
			for(; it != mlstNodes.end();++it)
			{	
				cAssimpNode *pNode = *it;
				if(pNode->msId == asId || pNode->msName == asId)
				{
					return pNode;
				}
			}
			return NULL;
		}

		cAssimpNode* GetNodeFromSource(const tString asSource)
		{
			tAssimpNodeListIt it = mlstNodes.begin();
			for(; it != mlstNodes.end();++it)
			{	
				cAssimpNode *pNode = *it;
				if(pNode->msSource == asSource)
				{
					return pNode;
				}
			}
			return NULL;
		}

		float mfStartTime;
		float mfEndTime;
		float mfDeltaTime;
		cAssimpNode mRoot;
		tAssimpNodeList mlstNodes;
	};

	//------------------------------------------------
	
	class cMeshLoaderAssimp : public iMeshLoader
	{
	public:
		cMeshLoaderAssimp(iLowLevelGraphics *apLowLevelGraphics, cMeshLoaderMSH *apMeshLoaderMSH, bool abLoadAndSaveMSHFormat);
		~cMeshLoaderAssimp();

		void SetLoadAndSaveMSHFormat(bool abX){ mbLoadAndSaveMSHFormat = abX; }

		cMesh* LoadMesh(const tWString& asFile, tMeshLoadFlag aFlags);
		bool SaveMesh(cMesh* apMesh,const tWString& asFile){return false;}

		cAnimation* LoadAnimation(const tWString& asFile);
		bool SaveAnimation(cAnimation* apAnimation, const tWString& asFile){return false;}

		static void SetConvertUnit(bool abX) { mbConvertUnit = abX; }
		static bool GetConvertUnit() { return mbConvertUnit; }

	private:
		cMeshLoaderMSH *mpMeshLoaderMSH;
		bool mbLoadAndSaveMSHFormat;
		float mfUnitScale;
		bool mbZToY;
		cMatrixf m_mtxZToY;
		static bool mbConvertUnit;
		tWorldLoadFlag mFlags;

		Assimp::Importer mImporter;

		bool mbLog;
		bool mbLowLog;

		void CleanUpNonAnimatedNodes(cAnimation *apAnimation, cSkeleton *apSkeleton, cAssimpScene* apAssimpScene );
		tString GetParentName(cAssimpNode *apNode, cAssimpScene* apAssimpScene, tAssimpGeometryVec *apAssimpGeometries);
		void CreateMeshJoint(cMeshJoint* apJoint,ePhysicsJointType aJointType,cBoundingVolume &aBV, 
							tStringVec &avStrings,cAssimpNode* apNode,cAssimpScene &aAssimpScene,
							tAssimpGeometryVec &avAssimpGeom);
		void CreateHierarchyNodes(cMesh *apMesh, cNode3D* mpParentNode, 
			cAssimpNode* apAssimpNode, 
			tAssimpGeometryVec &avAssimpGeom);
		cAssimpGeometry* GetGeometry(const tString& asId, tAssimpGeometryVec &avGeomVec);
		cAssimpLight* GetLight(const tString& asId, tAssimpLightVec &avLightVec);
		cAnimationTrack* CreateAnimTrack(cAnimation *apAnimation , cSkeleton *apSkeleton,
						cAssimpAnimation &aAnim, cAssimpScene *apScene);
		void CalcLocalMatrixAfterControllerBindMatrixRec(cBone* apBone, cMatrixf a_mtxParentGlobal, cMatrixf a_mtxParentGlobalUnscaled, int alDepth);
		void CreateSkeletonBone(cAssimpNode* apAssimpNode, cBone* apParentBone);
		iVertexBuffer *CreateVertexBuffer(cAssimpGeometry & aGeometry, 
											eVertexBufferUsageType aUsageType);

		bool FillStructuresFromAssimp(const aiScene* apScene,
							tAssimpImageVec *apAssimpImageVec,
							tAssimpTextureVec *apAssimpTextureVec,
							tAssimpMaterialVec *apAssimpMaterialVec,
							tAssimpLightVec *apAssimpLightVec,
							tAssimpGeometryVec *apAssimpGeometryVec,
							tAssimpControllerVec *apAssimpControllerVec,
							tAssimpAnimationVec *apAssimpAnimVec,
							cAssimpScene *apAssimpScene);

		void LoadAssimpScene(const aiScene* apScene, cAssimpScene *apAssimpScene,
								tAssimpLightVec *apAssimpLightVec);
		void LoadAssimpAnimations(const aiScene* apScene, tAssimpAnimationVec &avAnimations,
								cAssimpScene *apAssimpScene);
		void LoadAssimpMaterials(const aiScene* apScene, tAssimpMaterialVec &avAssimpMaterialVec);
		void LoadAssimpMeshes(const aiScene* apScene, tAssimpGeometryVec &avAssimpGeometryVec,
							tAssimpControllerVec *apAssimpControllerVec);
		void LoadAssimpNodeHierarchy(const aiNode* apNode, cAssimpNode* apParentNode, cAssimpScene* apScene, const aiScene* apAiScene);

		void SplitVertices(cAssimpGeometry &aGeometry,tAssimpExtraVtxListVec &avExtraVtxVec, 
							tVertexVec &avVertexVec, tUIntVec &avIndexVec);

		void FillVertexVec(const char* apChars,tVector3fVec &avVtxVec, int alElements, int alVtxCount);
		tString GetTopString(const tString asPath);
		tString GetMaterialTextureFile(const tString &asMaterial, tAssimpMaterialVec &avAssimpMaterialVec,
																tAssimpTextureVec &avAssimpTextureVec,
																tAssimpImageVec &avAssimpImageVec);
		cVector3f GetVectorPos(const cVector3f &avVec);
		cVector3f GetVectorNormal(const cVector3f &avVec);
		cMatrixf GetMatrix(const cMatrixf &amtxA);
		cVector3f GetVectorPosFromPtr(float *apVec);
		cVector3f GetVectorRotationFromPtr(float *apVec);
		cVector3f GetVectorScaleFromPtr(float *apVec);

		cMatrixf AiMatrixToMatrix(const aiMatrix4x4& aMatrix);
		cVector3f AiVector3ToVector3(const aiVector3D& aVec);
		cQuaternion AiQuaternionToQuaternion(const aiQuaternion& aQuat);
	};

};

#endif // HPL_MESH_LOADER_ASSIMP_H