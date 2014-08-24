//##########################################################################
//#                                                                        #
//#                            CLOUDCOMPARE                                #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU General Public License as published by  #
//#  the Free Software Foundation; version 2 of the License.               #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
//#                                                                        #
//##########################################################################

#ifdef CC_FBX_SUPPORT

#include "FBXFilter.h"

//qCC_db
#include <ccPointCloud.h>
#include <ccMesh.h>
#include <ccNormalVectors.h>
#include <ccMaterialSet.h>

//FBX SDK
#include <fbxsdk.h>

//Qt
#include <QFileInfo>
#include <QDir>
#include <QMap>

//System
#include <vector>
#include <assert.h>

// Converts a CC mesh to an FBX mesh
static FbxNode* ToFbxMesh(ccGenericMesh* mesh, FbxScene* pScene, QString filename, size_t meshIndex)
{
	if (!mesh)
		return 0;

	FbxNode* lNode = FbxNode::Create(pScene,qPrintable(mesh->getName()));
	FbxMesh* lMesh = FbxMesh::Create(pScene, qPrintable(mesh->getName()));
	lNode->SetNodeAttribute(lMesh);


	ccGenericPointCloud* cloud = mesh->getAssociatedCloud();
	if (!cloud)
		return 0;
	unsigned vertCount = cloud->size();
	unsigned faceCount = mesh->size();

	// Create control points.
	{
		lMesh->InitControlPoints(vertCount);
		FbxVector4* lControlPoints = lMesh->GetControlPoints();

		for (unsigned i=0; i<vertCount; ++i)
		{
			const CCVector3* P = cloud->getPoint(i);
			lControlPoints[i] = FbxVector4(P->x,P->y,P->z);
		}
	}

	ccMesh* asCCMesh = 0;
	if (mesh->isA(CC_TYPES::MESH))
		asCCMesh = static_cast<ccMesh*>(mesh);

	// normals
	if (mesh->hasNormals())
	{
		FbxGeometryElementNormal* lGeometryElementNormal = lMesh->CreateElementNormal();
		if (mesh->hasTriNormals())
		{
			// We want to have one normal per vertex of each polygon,
			// so we set the mapping mode to eByPolygonVertex.
			lGeometryElementNormal->SetMappingMode(FbxGeometryElement::eByPolygonVertex);
			lGeometryElementNormal->SetReferenceMode(FbxGeometryElement::eIndexToDirect);
			lGeometryElementNormal->GetIndexArray().SetCount(faceCount*3);
			
			if (asCCMesh)
			{
				NormsIndexesTableType* triNorms = asCCMesh->getTriNormsTable();
				assert(triNorms);
				for (unsigned i=0; i<triNorms->currentSize(); ++i)
				{
					const CCVector3& N = ccNormalVectors::GetNormal(triNorms->getValue(i));
					FbxVector4 Nfbx(N.x,N.y,N.z);
					lGeometryElementNormal->GetDirectArray().Add(Nfbx);
				}
				for (unsigned j=0; j<faceCount; ++j)
				{
					int i1,i2,i3;
					asCCMesh->getTriangleNormalIndexes(j,i1,i2,i3);
					lGeometryElementNormal->GetIndexArray().SetAt(static_cast<int>(j)*3+0, i1);
					lGeometryElementNormal->GetIndexArray().SetAt(static_cast<int>(j)*3+1, i2);
					lGeometryElementNormal->GetIndexArray().SetAt(static_cast<int>(j)*3+2, i3);
				}
			}
			else
			{
				for (unsigned j=0; j<faceCount; ++j)
				{
					//we can't use the 'NormsIndexesTable' so we save all the normals of all the vertices
					CCVector3 Na,Nb,Nc;
					lGeometryElementNormal->GetDirectArray().Add(FbxVector4(Na.x,Na.y,Na.z));
					lGeometryElementNormal->GetDirectArray().Add(FbxVector4(Nb.x,Nb.y,Nb.z));
					lGeometryElementNormal->GetDirectArray().Add(FbxVector4(Nc.x,Nc.y,Nc.z));
					
					mesh->getTriangleNormals(j,Na,Nb,Nc);
					lGeometryElementNormal->GetIndexArray().SetAt(static_cast<int>(j)*3+0, static_cast<int>(j)*3+0);
					lGeometryElementNormal->GetIndexArray().SetAt(static_cast<int>(j)*3+1, static_cast<int>(j)*3+1);
					lGeometryElementNormal->GetIndexArray().SetAt(static_cast<int>(j)*3+2, static_cast<int>(j)*3+2);
				}
			}
		}
		else
		{
			// We want to have one normal for each vertex (or control point),
			// so we set the mapping mode to eByControlPoint.
			lGeometryElementNormal->SetMappingMode(FbxGeometryElement::eByControlPoint);
			// The first method is to set the actual normal value
			// for every control point.
			lGeometryElementNormal->SetReferenceMode(FbxGeometryElement::eDirect);
			for (unsigned i=0; i<vertCount; ++i)
			{
				const CCVector3& N = cloud->getPointNormal(i);
				FbxVector4 Nfbx(N.x,N.y,N.z);
				lGeometryElementNormal->GetDirectArray().Add(Nfbx);
			}
		}
	}
	else
	{
		ccLog::Warning("[FBX] Mesh has no normal! You can manually compute them (select it then call \"Edit > Normals > Compute\")");
	}

	// colors
	if (cloud->hasColors())
	{
		FbxGeometryElementVertexColor* lGeometryElementVertexColor = lMesh->CreateElementVertexColor();
		lGeometryElementVertexColor->SetMappingMode(FbxGeometryElement::eByControlPoint);
		lGeometryElementVertexColor->SetReferenceMode(FbxGeometryElement::eDirect);
		for (unsigned i=0; i<vertCount; ++i)
		{
			const colorType* C = cloud->getPointColor(i);
			FbxColor col(	static_cast<double>(C[0])/MAX_COLOR_COMP,
							static_cast<double>(C[1])/MAX_COLOR_COMP,
							static_cast<double>(C[2])/MAX_COLOR_COMP,
							1.0 );
			lGeometryElementVertexColor->GetDirectArray().Add(col);
		}
	}

	// Set material mapping.
	bool hasMaterial = false;
	if (asCCMesh && asCCMesh->hasMaterials())
	{
		FbxString lShadingName  = "Phong";

		//directory to save textures
		QFileInfo info(filename);
		QString textDirName = info.baseName() + QString(".fbm");
		QDir baseDir = info.absoluteDir();
		QDir texDir = QDir(baseDir.absolutePath() + QString("/") + textDirName);

		static const char gDiffuseElementName[] = "DiffuseUV";

		// Create UV for Diffuse channel
		{
			FbxGeometryElementUV* lUVDiffuseElement = lMesh->CreateElementUV(gDiffuseElementName);
			assert(lUVDiffuseElement != 0);
			lUVDiffuseElement->SetMappingMode(FbxGeometryElement::eByPolygonVertex);
			lUVDiffuseElement->SetReferenceMode(FbxGeometryElement::eIndexToDirect);

			//fill Direct Array
			{
				const TextureCoordsContainer* texCoords = asCCMesh->getTexCoordinatesTable();
				assert(texCoords);
				unsigned count = texCoords->currentSize();
				lUVDiffuseElement->GetDirectArray().SetCount(static_cast<int>(count));
				for (unsigned i=0; i<count; ++i)
				{
					const float* uv = texCoords->getValue(i);
					lUVDiffuseElement->GetDirectArray().SetAt(i,FbxVector2(uv[0],uv[1]));
				}
			}

			//fill Indexes Array
			{
				unsigned triCount = asCCMesh->size();
				lUVDiffuseElement->GetIndexArray().SetCount(static_cast<int>(3*triCount));
				assert(asCCMesh->hasPerTriangleTexCoordIndexes());
				for (unsigned j=0; j<triCount; ++j)
				{
					int t1=0, t2=0, t3=0;
					asCCMesh->getTriangleTexCoordinatesIndexes(j, t1, t2, t3);

					lUVDiffuseElement->GetIndexArray().SetAt(j*3+0,t1);
					lUVDiffuseElement->GetIndexArray().SetAt(j*3+1,t2);
					lUVDiffuseElement->GetIndexArray().SetAt(j*3+2,t3);
				}
			}
		}

		//Textures used in this file
		QMap<QString,QString> texFilenames;

		const ccMaterialSet* matSet = asCCMesh->getMaterialSet();
		size_t matCount = matSet->size();
		for (size_t i=0; i<matCount; ++i)
		{
			const ccMaterial& mat = matSet->at(i);
			FbxSurfacePhong *lMaterial = FbxSurfacePhong::Create(pScene, qPrintable(mat.name));

			lMaterial->Emissive.Set(FbxDouble3(mat.emission[0],mat.emission[1],mat.emission[2]));
			//lMaterial->EmissiveFactor = mat.emission[3];
			lMaterial->Ambient.Set(FbxDouble3(mat.ambient[0],mat.ambient[1],mat.ambient[2]));
			//lMaterial->AmbientFactor = mat.ambient[3];
			lMaterial->Diffuse.Set(FbxDouble3(mat.diffuseFront[0],mat.diffuseFront[1],mat.diffuseFront[2]));
			//lMaterial->DiffuseFactor = mat.diffuseFront[3];
			lMaterial->Specular.Set(FbxDouble3(mat.specular[0],mat.specular[1],mat.specular[2]));
			//lMaterial->SpecularFactor = mat.specular[3];
			lMaterial->Shininess = mat.shininessFront;
			lMaterial->ShadingModel.Set(lShadingName);

			if (mat.hasTexture())
			{
				QString texFilename = mat.getAbsoluteFilename();
				
				//texture has not already been processed
				if (!texFilenames.contains(texFilename))
				{
					//if necessary, we (try to) create a subfolder to store textures
					if (!texDir.exists())
					{
						texDir = baseDir;
						if (texDir.mkdir(textDirName))
						{
							texDir.cd(textDirName);
						}
						else
						{
							textDirName = QString();
							ccLog::Warning("[FBX] Failed to create subfolder '%1' to store texture files (files will be stored next to the .fbx file)");
						}
					}

					QFileInfo fileInfo(texFilename);
					QString baseTexName = fileInfo.fileName();
					//add extension
					QString extension = QFileInfo(texFilename).suffix();
					if (fileInfo.suffix().isEmpty())
						baseTexName += QString(".png");

					QString absoluteFilename = texDir.absolutePath() + QString("/") + baseTexName;
					ccLog::PrintDebug(QString("[FBX] Material '%1' texture: %2").arg(mat.name).arg(absoluteFilename));

					texFilenames[texFilename] = absoluteFilename;
				}
				//mat.texture.save(absoluteFilename);

				// Set texture properties.
				FbxFileTexture* lTexture = FbxFileTexture::Create(pScene,"DiffuseTexture");
				assert(!texFilenames[texFilename].isEmpty());
				lTexture->SetFileName(qPrintable(texFilenames[texFilename]));
				lTexture->SetTextureUse(FbxTexture::eStandard);
				lTexture->SetMappingType(FbxTexture::eUV);
				lTexture->SetMaterialUse(FbxFileTexture::eModelMaterial);
				lTexture->SetSwapUV(false);
				lTexture->SetTranslation(0.0, 0.0);
				lTexture->SetScale(1.0, 1.0);
				lTexture->SetRotation(0.0, 0.0);
				lTexture->UVSet.Set(FbxString(gDiffuseElementName)); // Connect texture to the proper UV

				// don't forget to connect the texture to the corresponding property of the material
				lMaterial->Diffuse.ConnectSrcObject(lTexture);
			}

			int matIndex = lNode->AddMaterial(lMaterial);
			assert(matIndex  == static_cast<int>(i));
		}

		//don't forget to save the texture files
		{
			for (QMap<QString,QString>::ConstIterator it = texFilenames.begin(); it != texFilenames.end(); ++it)
			{
				const QImage image = ccMaterial::GetTexture(it.key());
				image.mirrored().save(it.value());
			}
			
			texFilenames.clear(); //don't need this anymore!
		}

		// Create 'triangle to material index' mapping
		{
			FbxGeometryElementMaterial* lMaterialElement = lMesh->CreateElementMaterial();
			lMaterialElement->SetMappingMode(FbxGeometryElement::eByPolygon);
			lMaterialElement->SetReferenceMode(FbxGeometryElement::eIndexToDirect);

			//set the proper material mapping
			//unsigned triCount = asCCMesh->size();
			//lMaterialElement->GetIndexArray().SetCount(static_cast<int>(triCount));
			//assert(asCCMesh->hasPerTriangleMtlIndexes());
			//for (unsigned j=0; j<triCount; ++j)
			//{
			//	int matIndex = asCCMesh->getTriangleMtlIndex(j);
			//	lMaterialElement->GetIndexArray().SetAt(j,matIndex);
			//}
		}

		hasMaterial = true;
	}

	// Create polygons
	{
		for (unsigned j=0; j<faceCount; ++j)
		{
			const CCLib::TriangleSummitsIndexes* tsi = mesh->getTriangleIndexes(j);

			int matIndex = hasMaterial ? asCCMesh->getTriangleMtlIndex(j) : -1;
			lMesh->BeginPolygon(matIndex);
			lMesh->AddPolygon(tsi->i1);
			lMesh->AddPolygon(tsi->i2);
			lMesh->AddPolygon(tsi->i3);
			lMesh->EndPolygon();
		}
	}

	return lNode;
}

static bool SaveScene(FbxManager* pManager, FbxDocument* pScene, const char* pFilename, int pFileFormat = -1, bool pEmbedMedia = false)
{
	// Create an exporter
	FbxExporter* lExporter = FbxExporter::Create(pManager, "");

	if( pFileFormat < 0 || pFileFormat >= pManager->GetIOPluginRegistry()->GetWriterFormatCount() )
	{
		// Write in fall back format in less no ASCII format found
		pFileFormat = pManager->GetIOPluginRegistry()->GetNativeWriterFormat();

		//Try to export in ASCII if possible
		int lFormatIndex, lFormatCount = pManager->GetIOPluginRegistry()->GetWriterFormatCount();

		for (lFormatIndex=0; lFormatIndex<lFormatCount; lFormatIndex++)
		{
			if (pManager->GetIOPluginRegistry()->WriterIsFBX(lFormatIndex))
			{
				FbxString lDesc = pManager->GetIOPluginRegistry()->GetWriterFormatDescription(lFormatIndex);
				const char *lASCII = "ascii";
				if (lDesc.Find(lASCII)>=0)
				{
					pFileFormat = lFormatIndex;
					break;
				}
			}
		} 
	}

	// Set the export states. By default, the export states are always set to 
	// true except for the option eEXPORT_TEXTURE_AS_EMBEDDED. The code below 
	// shows how to change these states
	(*(pManager->GetIOSettings())).SetBoolProp(EXP_FBX_MATERIAL,		true);
	(*(pManager->GetIOSettings())).SetBoolProp(EXP_FBX_TEXTURE,			true);
	(*(pManager->GetIOSettings())).SetBoolProp(EXP_FBX_EMBEDDED,		pEmbedMedia);
	(*(pManager->GetIOSettings())).SetBoolProp(EXP_FBX_SHAPE,			true);
	(*(pManager->GetIOSettings())).SetBoolProp(EXP_FBX_GOBO,			true);
	(*(pManager->GetIOSettings())).SetBoolProp(EXP_FBX_ANIMATION,		true);
	(*(pManager->GetIOSettings())).SetBoolProp(EXP_FBX_GLOBAL_SETTINGS,	true);

	// Initialize the exporter by providing a filename
	if(lExporter->Initialize(pFilename, pFileFormat, pManager->GetIOSettings()) == false)
	{
		ccLog::Warning("[FBX] Call to FbxExporter::Initialize() failed");
		ccLog::Warning("[FBX] Error returned: %s", lExporter->GetStatus().GetErrorString());
		return false;
	}

	// Export the scene
	bool lStatus = lExporter->Export(pScene); 

	// Destroy the exporter
	lExporter->Destroy();

	return lStatus;
}

CC_FILE_ERROR FBXFilter::saveToFile(ccHObject* entity, QString filename)
{
	if (!entity)
		return CC_FERR_BAD_ARGUMENT;
	
	std::vector<ccGenericMesh*> meshes;
	if (entity->isKindOf(CC_TYPES::MESH))
	{
		meshes.push_back(static_cast<ccGenericMesh*>(entity));
	}
	else if (entity->isA(CC_TYPES::HIERARCHY_OBJECT))
	{
		for (unsigned i=0; i<entity->getChildrenNumber(); ++i)
		{
			ccHObject* child = entity->getChild(i);
			if (child->isKindOf(CC_TYPES::MESH))
				meshes.push_back(static_cast<ccGenericMesh*>(child));
		}
	}

	if (meshes.empty())
	{
		return CC_FERR_NO_SAVE;
	}

	//The first thing to do is to create the FBX Manager which is the object allocator for almost all the classes in the SDK
	FbxManager* lSdkManager = FbxManager::Create();
	if( !lSdkManager )
	{
		ccLog::Warning("[FBX] Error: Unable to create FBX Manager!");
		return CC_FERR_CONSOLE_ERROR;
	}
	else
	{
		ccLog::Print("[FBX] Autodesk FBX SDK version %s", lSdkManager->GetVersion());
	}

	//Create an IOSettings object. This object holds all import/export settings.
	FbxIOSettings* ios = FbxIOSettings::Create(lSdkManager, IOSROOT);
	lSdkManager->SetIOSettings(ios);

	//Load plugins from the executable directory (optional)
	//FbxString lPath = FbxGetApplicationDirectory();
	//lSdkManager->LoadPluginsDirectory(lPath.Buffer());

	//Create an FBX scene. This object holds most objects imported/exported from/to files.
	FbxScene* lScene = FbxScene::Create(lSdkManager, "My Scene");
	if( !lScene )
	{
		ccLog::Warning("[FBX] Error: Unable to create FBX scene!");
		return CC_FERR_CONSOLE_ERROR;
	}

	// create scene info
	{
		FbxDocumentInfo* sceneInfo = FbxDocumentInfo::Create(lSdkManager,"SceneInfo");
		sceneInfo->mTitle = qPrintable(QString("Mesh: ") + (meshes.size() == 1 ? meshes[0]->getName() : QString("Multiple meshes")));
		sceneInfo->mAuthor = "CloudCompare";
		sceneInfo->mRevision = "rev. 1.0";
		sceneInfo->mKeywords = "cloudcompare mesh";

		// we need to add the sceneInfo before calling AddThumbNailToScene because
		// that function is asking the scene for the sceneInfo.
		lScene->SetSceneInfo(sceneInfo);
	}

	//create thumbnail
	//{
	//	FbxThumbnail* lThumbnail = FbxThumbnail::Create(lScene,"");

	//	lThumbnail->SetDataFormat(FbxThumbnail::eRGB_24);
	//	lThumbnail->SetSize(FbxThumbnail::e64x64);
	//	lThumbnail->SetThumbnailImage(cSceneThumbnail);

	//	if (lScene->GetSceneInfo())
	//	{
	//		lScene->GetSceneInfo()->SetSceneThumbnail(lThumbnail);
	//	}
	//}

	// Build the node tree.
	FbxNode* lRootNode = lScene->GetRootNode();
	{
		for (size_t i=0; i<meshes.size(); ++i)
		{
			FbxNode* meshNode = ToFbxMesh(meshes[i],lScene,filename,i);
			if (meshNode)
				lRootNode->AddChild(meshNode);
			else
				ccLog::Warning(QString("[FBX] Failed to convert mesh '%1' to FBX mesh/node!").arg(meshes[i]->getName()));
		}
	}

	// Save the scene.
	bool lResult = SaveScene(lSdkManager, lScene, qPrintable(filename));

	// Destroy all objects created by the FBX SDK.
	if( lSdkManager )
		lSdkManager->Destroy();

	return lResult ? CC_FERR_NO_ERROR : CC_FERR_CONSOLE_ERROR;
}

QString GetAttributeTypeName(FbxNodeAttribute::EType type)
{
	switch(type)
	{ 
	case FbxNodeAttribute::eUnknown: return("unidentified");
	case FbxNodeAttribute::eNull: return("null"); 
	case FbxNodeAttribute::eMarker: return("marker"); 
	case FbxNodeAttribute::eSkeleton: return("skeleton"); 
	case FbxNodeAttribute::eMesh: return("mesh"); 
	case FbxNodeAttribute::eNurbs: return("nurbs"); 
	case FbxNodeAttribute::ePatch: return("patch"); 
	case FbxNodeAttribute::eCamera: return("camera"); 
	case FbxNodeAttribute::eCameraStereo: return("stereo"); 
	case FbxNodeAttribute::eCameraSwitcher: return("camera switcher"); 
	case FbxNodeAttribute::eLight: return("light"); 
	case FbxNodeAttribute::eOpticalReference: return("optical reference"); 
	case FbxNodeAttribute::eOpticalMarker: return("marker"); 
	case FbxNodeAttribute::eNurbsCurve: return("nurbs curve"); 
	case FbxNodeAttribute::eTrimNurbsSurface: return("trim nurbs surface"); 
	case FbxNodeAttribute::eBoundary: return("boundary"); 
	case FbxNodeAttribute::eNurbsSurface: return("nurbs surface"); 
	case FbxNodeAttribute::eShape: return("shape"); 
	case FbxNodeAttribute::eLODGroup: return("lodgroup"); 
	case FbxNodeAttribute::eSubDiv: return("subdiv"); 
	default:
		break;
	}
	
	return("unknown"); 
}

//converts a FBX mesh to a CC mesh
static ccMesh* FromFbxMesh(FbxMesh* fbxMesh, FileIOFilter::LoadParameters& parameters)
{
	if (!fbxMesh)
		return 0;

	int polyCount = fbxMesh->GetPolygonCount();
	//fbxMesh->GetLayer(
	unsigned triCount = 0;
	unsigned polyVertCount = 0; //different from vertCount (vertices can be counted multiple times here!)
	//as we can't load all polygons (yet ;) we already look if we can load any!
	{
		unsigned skipped = 0;
		for (int i=0; i<polyCount; ++i)
		{
			int pSize = fbxMesh->GetPolygonSize(i);

			if (pSize == 3)
			{
				++triCount;
				polyVertCount += 3;
			}
			else if (pSize == 4)
			{
				triCount += 2;
				polyVertCount += 4;
			}
			else
			{
				++skipped;
			}
		}

		if (triCount == 0)
		{
			ccLog::Warning(QString("[FBX] No triangle or quad found in mesh '%1'! (polygons with more than 4 vertices are not supported for the moment)").arg(fbxMesh->GetName()));
			return 0;
		}
		else if (skipped != 0)
		{
			ccLog::Warning(QString("[FBX] Some polygons in mesh '%1' were ignored (%2): polygons with more than 4 vertices are not supported for the moment)").arg(fbxMesh->GetName()).arg(skipped));
			return 0;
		}
	}

	int vertCount = fbxMesh->GetControlPointsCount();
	if (vertCount <= 0)
	{
		ccLog::Warning(QString("[FBX] Mesh '%1' has no vetex or no polygon?!").arg(fbxMesh->GetName()));
		return 0;
	}

	ccPointCloud* vertices = new ccPointCloud("vertices");
	ccMesh* mesh = new ccMesh(vertices);
	mesh->setName(fbxMesh->GetName());
	mesh->addChild(vertices);
	vertices->setEnabled(false);
	
	if (!mesh->reserve(static_cast<unsigned>(triCount)) || !vertices->reserve(vertCount))
	{
		ccLog::Warning(QString("[FBX] Not enough memory to load mesh '%1'!").arg(fbxMesh->GetName()));
		delete mesh;
		return 0;
	}

	//colors
	{
		for (int l=0; l<fbxMesh->GetElementVertexColorCount(); l++)
		{
			FbxGeometryElementVertexColor* vertColor = fbxMesh->GetElementVertexColor(l);
			//CC can only handle per-vertex colors
			if (vertColor->GetMappingMode() == FbxGeometryElement::eByControlPoint)
			{
				if (vertColor->GetReferenceMode() == FbxGeometryElement::eDirect
					|| vertColor->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
				{
					if (vertices->reserveTheRGBTable())
					{
						switch (vertColor->GetReferenceMode())
						{
						case FbxGeometryElement::eDirect:
							{
								for (int i=0; i<vertCount; ++i)
								{
									FbxColor c = vertColor->GetDirectArray().GetAt(i);
									vertices->addRGBColor(	static_cast<colorType>(c.mRed	* MAX_COLOR_COMP),
															static_cast<colorType>(c.mGreen	* MAX_COLOR_COMP),
															static_cast<colorType>(c.mBlue	* MAX_COLOR_COMP) );
								}
							}
							break;
						case FbxGeometryElement::eIndexToDirect:
							{
								for (int i=0; i<vertCount; ++i)
								{
									int id = vertColor->GetIndexArray().GetAt(i);
									FbxColor c = vertColor->GetDirectArray().GetAt(id);
									vertices->addRGBColor(	static_cast<colorType>(c.mRed	* MAX_COLOR_COMP),
															static_cast<colorType>(c.mGreen	* MAX_COLOR_COMP),
															static_cast<colorType>(c.mBlue	* MAX_COLOR_COMP) );
								}
							}
							break;
						default:
							assert(false);
							break;
						}

						vertices->showColors(true);
						mesh->showColors(true);
						break; //no need to look for other color fields (we won't be able to handle them!
					}
					else
					{
						ccLog::Warning(QString("[FBX] Not enough memory to load mesh '%1' colors!").arg(fbxMesh->GetName()));
					}
				}
				else
				{
					ccLog::Warning(QString("[FBX] Color field #%i of mesh '%1' will be ignored (unhandled type)").arg(l).arg(fbxMesh->GetName()));
				}
			}
			else
			{
				ccLog::Warning(QString("[FBX] Color field #%i of mesh '%1' will be ignored (unhandled type)").arg(l).arg(fbxMesh->GetName()));
			}
		}
	}

	//normals can be per vertices or per-triangle
	int perPointNormals = -1;
	int perVertexNormals = -1;
	int perPolygonNormals = -1;
	{
		for (int j=0; j<fbxMesh->GetElementNormalCount(); j++)
		{
			FbxGeometryElementNormal* leNormals = fbxMesh->GetElementNormal(j);
			switch(leNormals->GetMappingMode())
			{
			case FbxGeometryElement::eByControlPoint:
				perPointNormals = j;
				break;
			case FbxGeometryElement::eByPolygonVertex:
				perVertexNormals = j;
				break;
			case FbxGeometryElement::eByPolygon:
				perPolygonNormals = j;
				break;
			default:
				//not handled
				break;
			}
		}
	}

	//per-point normals
	if (perPointNormals >= 0)
	{
		FbxGeometryElementNormal* leNormals = fbxMesh->GetElementNormal(perPointNormals);
		FbxLayerElement::EReferenceMode refMode = leNormals->GetReferenceMode();
		const FbxLayerElementArrayTemplate<FbxVector4>& normals = leNormals->GetDirectArray();
		assert(normals.GetCount() == vertCount);
		if (normals.GetCount() != vertCount)
		{
			ccLog::Warning(QString("[FBX] Wrong number of normals on mesh '%1'!").arg(fbxMesh->GetName()));
			perPointNormals = -1;
		}
		else if (!vertices->reserveTheNormsTable())
		{
			ccLog::Warning(QString("[FBX] Not enough memory to load mesh '%1' normals!").arg(fbxMesh->GetName()));
			perPointNormals = -1;
		}
		else
		{
			//import normals
			for (int i=0; i<vertCount; ++i)
			{
				int id = refMode != FbxGeometryElement::eDirect ? leNormals->GetIndexArray().GetAt(i) : i;
				FbxVector4 N = normals.GetAt(id);
				//convert to CC-structure
				CCVector3 Npc(	static_cast<PointCoordinateType>(N.Buffer()[0]),
								static_cast<PointCoordinateType>(N.Buffer()[1]),
								static_cast<PointCoordinateType>(N.Buffer()[2]) );
				vertices->addNorm(Npc);
			}
			vertices->showNormals(true);
			mesh->showNormals(true);
			//no need to import the other normals (if any)
			perVertexNormals = -1;
			perPolygonNormals = -1;
		}
	}

	//per-triangle normals
	NormsIndexesTableType* normsTable = 0;
	if (perVertexNormals >= 0 || perPolygonNormals >= 0)
	{
		normsTable = new NormsIndexesTableType();
		if (!normsTable->reserve(polyVertCount) || !mesh->reservePerTriangleNormalIndexes())
		{
			ccLog::Warning(QString("[FBX] Not enough memory to load mesh '%1' normals!").arg(fbxMesh->GetName()));
			normsTable->release();
			normsTable = 0;
		}
		else
		{
			mesh->setTriNormsTable(normsTable);
			mesh->addChild(normsTable);
			vertices->showNormals(true);
			mesh->showNormals(true);
		}
	}

	//materials
	ccMaterialSet* materials = 0;
	{
		FbxNode* lNode = fbxMesh->GetNode();
		int lMaterialCount = lNode ? lNode->GetMaterialCount() : 0;
		for (int i=0; i<lMaterialCount; i++)
		{
			FbxSurfaceMaterial *lBaseMaterial = lNode->GetMaterial(i);

			bool isLambert = lBaseMaterial->GetClassId().Is(FbxSurfaceLambert::ClassId);
			bool isPhong = lBaseMaterial->GetClassId().Is(FbxSurfacePhong::ClassId);
			if (isLambert || isPhong)
			{
				ccMaterial mat(lBaseMaterial->GetName());

				FbxSurfaceLambert* lLambertMat = static_cast<FbxSurfaceLambert*>(lBaseMaterial);
			
				for (int k=0; k<3; ++k)
				{
					mat.ambient[k] = lLambertMat->Ambient.Get()[k];
					mat.diffuseFront[k] = mat.diffuseBack[k] = lLambertMat->Diffuse.Get()[k];
					mat.emission[k] = lLambertMat->Emissive.Get()[k];

					if (isPhong)
					{
						FbxSurfacePhong* lPhongMat = static_cast<FbxSurfacePhong*>(lBaseMaterial);
						mat.specular[k] = lPhongMat->Specular.Get()[k];
						mat.shininessFront = mat.shininessBack = lPhongMat->Shininess;
					}
				}

				//import associated texture (if any)
				{
					int lTextureIndex;
					FBXSDK_FOR_EACH_TEXTURE(lTextureIndex)
					{
						FbxProperty lProperty = lBaseMaterial->FindProperty(FbxLayerElement::sTextureChannelNames[lTextureIndex]);
						if( lProperty.IsValid() )
						{
							int lTextureCount = lProperty.GetSrcObjectCount<FbxTexture>();
							FbxTexture* texture = 0; //we can handle only one texture per material! We'll take the non layered one by default (if any)
							for (int j = 0; j < lTextureCount; ++j)
							{
								//Here we have to check if it's layeredtextures, or just textures:
								FbxLayeredTexture *lLayeredTexture = lProperty.GetSrcObject<FbxLayeredTexture>(j);
								if (lLayeredTexture)
								{
									//we don't handle layered textures!
									/*int lNbTextures = lLayeredTexture->GetSrcObjectCount<FbxTexture>();
									for (int k=0; k<lNbTextures; ++k)
									{
										FbxTexture* lTexture = lLayeredTexture->GetSrcObject<FbxTexture>(k);
										if(lTexture)
										{
										}
									}
									//*/
								}
								else
								{
									//non-layered texture
									FbxTexture* lTexture = lProperty.GetSrcObject<FbxTexture>(j);
									if(lTexture)
									{
										//we take the first non layered texture by default
										texture = lTexture;
										break;
									}
								}
							}

							if (texture)
							{
								FbxFileTexture *lFileTexture = FbxCast<FbxFileTexture>(texture);
								if (lFileTexture)
								{
									const char* texAbsoluteFilename = lFileTexture->GetFileName();
									ccLog::PrintDebug(QString("[FBX] Texture absolue filename: %1").arg(texAbsoluteFilename));
									if (texAbsoluteFilename != 0 && texAbsoluteFilename[0] != 0)
									{
										if (!mat.setTexture(texAbsoluteFilename))
										{
											ccLog::Warning(QString("[FBX] Failed to load texture file: %1").arg(texAbsoluteFilename));
										}
									}
								}
							}
						}
					}
				}

				if (!materials)
				{
					materials = new ccMaterialSet("materials");
					mesh->addChild(materials);
				}
				materials->addMaterial(mat);
			}
			else
			{
				ccLog::Warning(QString("[FBX] Material '%1' has an unhandled type").arg(lBaseMaterial->GetName()));
			}
		}
	}

	//import textures UV
	TextureCoordsContainer* vertTexUVTable = 0;
	bool hasTexUVIndexes = false;
	{
		for (int l=0; l<fbxMesh->GetElementUVCount(); ++l)
		{
			FbxGeometryElementUV* leUV = fbxMesh->GetElementUV(l);
			//per-point UV coordinates
			if (leUV->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
			{
				vertTexUVTable = new TextureCoordsContainer();
				int uvCount = leUV->GetDirectArray().GetCount();

				if (!vertTexUVTable->reserve(uvCount) || !mesh->reservePerTriangleTexCoordIndexes())
				{
					vertTexUVTable->release();
					vertTexUVTable = 0;
					ccLog::Warning(QString("[FBX] Not enough memory to load mesh '%1' UV coordinates!").arg(fbxMesh->GetName()));
				}
				else
				{
					FbxLayerElement::EReferenceMode refMode = leUV->GetReferenceMode();
					for (int i=0; i<uvCount; ++i)
					{
						FbxVector2 uv = leUV->GetDirectArray().GetAt(i);
						//convert to CC-structure
						float uvf[2] = {static_cast<float>(uv.Buffer()[0]),
										static_cast<float>(uv.Buffer()[1])};
						vertTexUVTable->addElement(uvf);
					}

					int indexCount = leUV->GetIndexArray().GetCount();
					if (refMode == FbxGeometryElement::eIndexToDirect)
					{
						hasTexUVIndexes = true;
						//for (int i=0; i<polyCount; ++i)
						//{
						//	mesh->addTriangleTexCoordIndexes(leUV->GetIndexArray().GetAt(3*i),leUV->GetIndexArray().GetAt(3*i+1),leUV->GetIndexArray().GetAt(3*i+2));
						//}
					}
					else if (refMode == FbxGeometryElement::eDirect)
					{
						//for (int i=0; i<polyCount; ++i)
						//{
						//	mesh->addTriangleTexCoordIndexes(3*i,3*i+1,3*i+2);
						//}
					}
					else
					{
						ccLog::Warning(QString("[FBX] UV coordinates for mesh '%1' are encoded in an unhandled mode!") .arg(fbxMesh->GetName()));
						vertTexUVTable->release();
						vertTexUVTable = 0;
					}
				}

				if (vertTexUVTable)
					break; //no need to look to the other UV fields (can't handle them!)
			}
		}
	}

	//import polygons
	{
		int uvIndex = 0;
		for (int i=0; i<polyCount; ++i)
		{
			int pSize = fbxMesh->GetPolygonSize(i);

			if (pSize > 4)
			{
				//not handled for the moment
				if (!hasTexUVIndexes)
					uvIndex += pSize;
				continue;
			}
			//we split quads into two triangles

			//vertex indices
			int i1 = fbxMesh->GetPolygonVertex(i, 0);
			int i2 = fbxMesh->GetPolygonVertex(i, 1);
			int i3 = fbxMesh->GetPolygonVertex(i, 2);
			mesh->addTriangle(i1,i2,i3);

			int i4 = -1;
			if (pSize == 4)
			{
				i4 = fbxMesh->GetPolygonVertex(i, 3);
				mesh->addTriangle(i1,i3,i4);
			}

			if (vertTexUVTable)
			{
				if (hasTexUVIndexes)
				{
					i1 = fbxMesh->GetTextureUVIndex(i, 0);
					if (i1 > uvIndex)
						uvIndex = i1;
					i2 = fbxMesh->GetTextureUVIndex(i, 1);
					if (i2 > uvIndex)
						uvIndex = i2;
					i3 = fbxMesh->GetTextureUVIndex(i, 2);
					if (i3 > uvIndex)
						uvIndex = i3;
				}
				else
				{
					i1 = uvIndex++;
					i2 = uvIndex++;
					i3 = uvIndex++;
				}
				mesh->addTriangleTexCoordIndexes(i1,i2,i3);
				if (pSize == 4)
				{
					if (hasTexUVIndexes)
					{
						i4 = fbxMesh->GetTextureUVIndex(i, 3);
						if (i4 > uvIndex)
							uvIndex = i4;
					}
					else
					{
						i4 = uvIndex++;
					}
					mesh->addTriangleTexCoordIndexes(i1,i3,i4);
				}

				if (uvIndex >= static_cast<int>(vertTexUVTable->currentSize()))
				{
					ccLog::Warning(QString("[FBX] Mesh '%1': UV coordinates indexes mismatch!") .arg(fbxMesh->GetName()));
					vertTexUVTable->release();
					vertTexUVTable = 0;
				}
			}

			//per-triangle normals
			if (normsTable)
			{
				int nIndex = static_cast<int>(normsTable->currentSize());
				for (int j=0; j<pSize; ++j)
				{
					FbxVector4 N;
					fbxMesh->GetPolygonVertexNormal(i, j, N);
					CCVector3 Npc(	static_cast<PointCoordinateType>(N.Buffer()[0]),
									static_cast<PointCoordinateType>(N.Buffer()[1]),
									static_cast<PointCoordinateType>(N.Buffer()[2]) );
					normsTable->addElement(ccNormalVectors::GetNormIndex(Npc.u));
				}

				mesh->addTriangleNormalIndexes(nIndex,nIndex+1,nIndex+2);
				if (pSize == 4)
					mesh->addTriangleNormalIndexes(nIndex,nIndex+2,nIndex+3);
			}
		}
		
		if (vertTexUVTable)
		{
			mesh->setTexCoordinatesTable(vertTexUVTable);
			mesh->addChild(vertTexUVTable);
		}

		if (mesh->size() == 0)
		{
			ccLog::Warning(QString("[FBX] No triangle found in mesh '%1'! (only triangles are supported for the moment)").arg(fbxMesh->GetName()));
			delete mesh;
			return 0;
		}
	}

	//import vertices
	{
		const FbxVector4* fbxVertices = fbxMesh->GetControlPoints();
		assert(vertices && fbxVertices);
		CCVector3d Pshift(0,0,0);
		for (int i=0; i<vertCount; ++i, ++fbxVertices)
		{
			const double* P = fbxVertices->Buffer();
			assert(P[3] == 0);

			//coordinate shift management
			if (i == 0)
			{
				if (FileIOFilter::HandleGlobalShift(P,Pshift,parameters))
				{
					vertices->setGlobalShift(Pshift);
					ccLog::Warning("[FBX] Mesh has been recentered! Translation: (%.2f,%.2f,%.2f)",Pshift.x,Pshift.y,Pshift.z);
				}
			}

			CCVector3 PV(	static_cast<PointCoordinateType>(P[0] + Pshift.x),
							static_cast<PointCoordinateType>(P[1] + Pshift.y),
							static_cast<PointCoordinateType>(P[2] + Pshift.z) );

			vertices->addPoint(PV);
		}
	}

	//import material mapping (AFTER LOADING THE POLYGONS!)
	if (materials)
	{
		int fbxMatCount = fbxMesh->GetElementMaterialCount();
		for (int i=0; i<fbxMatCount; ++i)
		{
			FbxGeometryElementMaterial* lMaterialElement = fbxMesh->GetElementMaterial(i);
			if (	lMaterialElement->GetMappingMode() == FbxGeometryElement::eByPolygon
				&&	lMaterialElement->GetReferenceMode() == FbxGeometryElement::eIndexToDirect
				&&	lMaterialElement->GetIndexArray().GetCount() == fbxMesh->GetPolygonCount())
			{
				if (mesh->reservePerTriangleMtlIndexes())
				{
					int maxMaterialIndex = static_cast<int>(materials->size());
					int matElemCount = lMaterialElement->GetIndexArray().GetCount();
					for (int j=0; j<matElemCount; ++j)
					{
						int mtlIndex = lMaterialElement->GetIndexArray().GetAt(j);
						mesh->addTriangleMtlIndex(mtlIndex < maxMaterialIndex ? mtlIndex : -1);
					}
				}
				else
				{
					ccLog::Warning("[FBX] Not enough memory to load materials!");
				}
				break;
			}
		}

		if (mesh->hasPerTriangleMtlIndexes())
		{
			mesh->setMaterialSet(materials);
			//mesh->addChild(materials);
			mesh->showMaterials(true);
		}
		else
		{
			//we failed to load material mapping! No need to kepp the materials...
			mesh->removeChild(materials);
			//materials->release();
			materials = 0;
		}
	}

	return mesh;
}


CC_FILE_ERROR FBXFilter::loadFile(QString filename, ccHObject& container, LoadParameters& parameters)
{
	// Initialize the SDK manager. This object handles memory management.
	FbxManager* lSdkManager = FbxManager::Create();

	// Create the IO settings object.
	FbxIOSettings *ios = FbxIOSettings::Create(lSdkManager, IOSROOT);
	lSdkManager->SetIOSettings(ios);
	
	// Import options determine what kind of data is to be imported.
	// True is the default, but here we�ll set some to true explicitly, and others to false.
	//(*(lSdkManager->GetIOSettings())).SetBoolProp(IMP_FBX_MATERIAL,	true);
	//(*(lSdkManager->GetIOSettings())).SetBoolProp(IMP_FBX_TEXTURE,	true);
	
	// Create an importer using the SDK manager.
	FbxImporter* lImporter = FbxImporter::Create(lSdkManager,"");

	CC_FILE_ERROR result = CC_FERR_NO_ERROR;
	
	// Use the first argument as the filename for the importer.
	if (!lImporter->Initialize(qPrintable(filename), -1, lSdkManager->GetIOSettings()))
	{ 
		ccLog::Warning(QString("[FBX] Error: %1").arg(lImporter->GetStatus().GetErrorString()));
		result = CC_FERR_READING;
	}
	else
	{
		// Create a new scene so that it can be populated by the imported file.
		FbxScene* lScene = FbxScene::Create(lSdkManager,"myScene");

		// Import the contents of the file into the scene.
		if (lImporter->Import(lScene))
		{
			// Print the nodes of the scene and their attributes recursively.
			// Note that we are not printing the root node because it should
			// not contain any attributes.
			FbxNode* lRootNode = lScene->GetRootNode();
			std::vector<FbxNode*> nodes;
			nodes.push_back(lRootNode);

			while (!nodes.empty())
			{
				FbxNode* lNode = nodes.back();
				nodes.pop_back();

				const char* nodeName = lNode->GetName();
#ifdef _DEBUG
				ccLog::Print(QString("Node: %1 - %2 properties").arg(nodeName).arg(lNode->GetNodeAttributeCount()));
#endif
				// scan the node's attributes.
				for(int i=0; i<lNode->GetNodeAttributeCount(); i++)
				{
					FbxNodeAttribute* pAttribute = lNode->GetNodeAttributeByIndex(i);
					FbxNodeAttribute::EType type = pAttribute->GetAttributeType();
#ifdef _DEBUG
					ccLog::Print(QString("\tProp. #%1").arg(GetAttributeTypeName(type)));
#endif

					switch(type)
					{ 
					case FbxNodeAttribute::eMesh:
						{
							ccMesh* mesh = FromFbxMesh(static_cast<FbxMesh*>(pAttribute),parameters);
							if (mesh)
							{
								//apply transformation
								FbxAMatrix& transform = lNode->EvaluateGlobalTransform();
								ccGLMatrix mat;
								float* data = mat.data();
								for (int c=0; c<4; ++c)
								{
									FbxVector4 C = transform.GetColumn(c);
									*data++ = static_cast<float>(C[0]);
									*data++ = static_cast<float>(C[1]);
									*data++ = static_cast<float>(C[2]);
									*data++ = static_cast<float>(C[3]);
								}
								mesh->applyGLTransformation_recursive(&mat);

								if (mesh->getName().isEmpty())
									mesh->setName(nodeName);

								container.addChild(mesh);
							}
						}
						break;

					case FbxNodeAttribute::eUnknown: 
					case FbxNodeAttribute::eNull:
					case FbxNodeAttribute::eMarker:
					case FbxNodeAttribute::eSkeleton:
					case FbxNodeAttribute::eNurbs:
					case FbxNodeAttribute::ePatch:
					case FbxNodeAttribute::eCamera:
					case FbxNodeAttribute::eCameraStereo:
					case FbxNodeAttribute::eCameraSwitcher:
					case FbxNodeAttribute::eLight:
					case FbxNodeAttribute::eOpticalReference:
					case FbxNodeAttribute::eOpticalMarker:
					case FbxNodeAttribute::eNurbsCurve:
					case FbxNodeAttribute::eTrimNurbsSurface:
					case FbxNodeAttribute::eBoundary:
					case FbxNodeAttribute::eNurbsSurface:
					case FbxNodeAttribute::eShape:
					case FbxNodeAttribute::eLODGroup:
					case FbxNodeAttribute::eSubDiv:
					default:
						//not handled yet
						break;
					}
				}

				// Recursively add the children.
				for(int j=0; j<lNode->GetChildCount(); j++)
				{
					nodes.push_back(lNode->GetChild(j));
				}
			}
		}
	}

	// The file is imported, so get rid of the importer.
	lImporter->Destroy();
	// Destroy the SDK manager and all the other objects it was handling.
	lSdkManager->Destroy();

	return container.getChildrenNumber() == 0 ? CC_FERR_NO_LOAD : CC_FERR_NO_ERROR;
}

#endif
