//----------------------------------------------------------------------
// ZENIMAX MEDIA PROPRIETARY INFORMATION
//
// This software is developed and/or supplied under the terms of a license
// or non-disclosure agreement with ZeniMax Media Inc. and may not be copied
// or disclosed except in accordance with the terms of that agreement.
//
// Copyright (c) 2020 ZeniMax Media Incorporated.
// All Rights Reserved.
//
// ZeniMax Media Incorporated, Rockville, Maryland 20850
// http://www.zenimax.com
//
// FILE ShaderModel.cpp
// AUTHOR Christian Roy
// OWNER Christian Roy
// DATE 5/13/2020
//----------------------------------------------------------------------

#include "AppPCH.h"
#include "ShaderModel.h"

#include <SharedTools/Qt/QtSharedIncludesBegin.h>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QWidget>
#include <SharedTools/Qt/Utility/QtSharedToolsFunctions.h>
#include <SharedTools/Qt/Widgets/PropertyEditor/QtGenericEditorModelShared.h>
#include <SharedTools/Qt/Widgets/PropertyEditor/GenericEditorBuilder.h>
#include <SharedTools/Qt/QtSharedIncludesEnd.h>

#include <BSMaterial/BSMaterialDB.h>
#include <BSMaterial/BSMaterialFwd.h>
#include <BSMaterial/BSMaterialLayeredMaterial.h>
#include <BSSystem/BSFixedString.h>
#include <BSSystem/FilePathUtilities.h>
#include <Perforce/BGSCSPerforce.h>
#include <SharedTools/Qt/Widgets/PropertyEditor/AttributeProcessor.h>
#include <SharedTools/Qt/Widgets/PropertyEditor/DeleteProcessor.h>
#include <SharedTools/Qt/Widgets/PropertyEditor/RuleProcessor.h>
#include <SharedTools/Qt/Widgets/PropertyEditor/RuleTemplateManager.h>

extern INISetting sMaterialDefaultChangeListDesc;
INISetting bMaterialSuperUser("bMaterialSuperUser:MaterialLayering", false);

namespace
{
	// Template category as well as sub folder name.
	constexpr char ShaderModelsTemplateCategoryC[] = { "ShaderModels" };

	// Default values & Specific Shader Model business rules by name
	constexpr char DefaultShaderModelC[] = { "Experimental" };
	const BSFixedString ExperimentalShaderModelC("Experimental");
	const BSFixedString BaseMaterialShaderModelC("BaseMaterial");

	// Meta attributes
	constexpr char ShaderModelMetaLockedC[] = { "Locked" };
	constexpr char ShaderModelMetaSwitchableC[] = { "Switchable" };
	constexpr char ShaderModelMetaRootMaterialC[] = { "RootMaterial" };
	constexpr char ShaderModelMetaDisplayNameC[] = { "DisplayName" };
	constexpr char ShaderModelMetaDisableLOD[] = { "DisableLOD" };

	// Set if Experimental materials are editable in the tool.
	INISetting bExperimentalMaterials("bExperimentalMaterials:ShaderModels", true);
}

/// --------------------------------------------------------------------------------
/// <summary>
/// ShaderModel utility functions.
/// </summary>
/// --------------------------------------------------------------------------------
namespace SharedTools
{
	/// <summary> Handles creation of a new Shader Model rule template and its associated root material. </summary>
	/// <param name="apParent">UI Parent widget for dialog box modal interactions</param>
	/// <param name="aOutShaderModelName">OUT : the created ShaderModel name</param>
	/// <param name="aOutShaderModelFileName">OUT : the created ShaderModel FileName, for saving post creation ShaderModel file changes.</param>
	/// <param name="aOutCreatedRootMaterial">OUT : the created root material associated to new ShaderModel</param>
	/// <returns> bool </returns>
	bool CreateNewShaderModel(QWidget* apParent, BSFixedString& aOutShaderModelName, BSFixedString& aOutShaderModelFileName, BSMaterial::LayeredMaterialID& aOutCreatedRootMaterial)
	{
		using namespace QtPropertyEditor;

		bool created = false;

		BSFilePathString startPath;
		FilePathUtilities::Join(TemplateManager::QInstance().QRuleTemplateRootFolder(), ShaderModelsTemplateCategoryC, startPath);

		// Step 1 : Select a filename with open dialog
		bool loop = true;
		while (loop)
		{
			QString absoluteFilename = QFileDialog::getSaveFileName(apParent, "Create New Shader Model file", startPath.QString(), "Shader Model json files (*.json)", nullptr, QFileDialog::DontConfirmOverwrite);
			if (absoluteFilename.isEmpty())
			{
				// Dialog was canceled
				loop = false;
			}
			else
			{
				QFileInfo file(absoluteFilename);
				QString relFilename = QDir::current().relativeFilePath(absoluteFilename);
				relFilename = QDir::toNativeSeparators(relFilename);
				absoluteFilename = QDir::toNativeSeparators(absoluteFilename);
				aOutShaderModelFileName = BSFixedString(absoluteFilename.toLatin1().data());

				// Does a shader model file exist on disk ?
				if (file.exists())
				{
					// Warn the user this file already exist and selecting existing file as new is not supported.
					QMessageBox::warning(apParent, "Shader Model overwrite not supported.",
						"The shader model filename you selected already exist.\n"
						"Please pick a new unique filename for your shader model.",
						QMessageBox::Ok);
				}
				else
				{
					// Does a root material with that name already exist ?
					aOutShaderModelName = BSFixedString(file.baseName().toLatin1().data());
					BSMaterial::LayeredMaterialID existingMaterial = BSMaterial::GetLayeredMaterial(aOutShaderModelName);

					// We prevent creating a new shader model with an existing material name.
					if (existingMaterial.QValid())
					{
						// Warn to the user a material with that name already exist.
						QMessageBox::warning(apParent, "Material name already in use",
							"The shader model filename you selected already exist as a Material name.\n"
							"Please pick a new unique filename for your shader model.",
							QMessageBox::Ok);
					}
					else
					{
						bool success = false;

						// Modify this Raw string json for initial rules. The following adds a single layer akin to base material.
						auto baseTemplateRules = R"(
						[
							{
								"Class": "BSMaterial::LayeredMaterialID",
								"Rules": 
								[
									{
										"From": "*",
										"Op": "Remove"
									},			
									{
										"From": "Layer1",
										"Op": "Add"
									}
								]
							}
						]
						)"_json;

						// Create the new Shader Model template file
						nlohmann::json& newShaderModelJson = TemplateManager::QInstance().CreateTemplate(ShaderModelsTemplateCategoryC, aOutShaderModelName.QString(), success);
						BSASSERT(success, "Shader Model template already exist, but has no disk file with that name.");
						// Add default elements to TemplateRules node.
						newShaderModelJson[TemplateManager::pJson_TemplateRulesC] = baseTemplateRules;
						// Set the root material name to ShaderModel name for starter. (They should ideally share the same name but the link is enforced via meta data.)
						nlohmann::json newMetaDataObj;
						newMetaDataObj[ShaderModelMetaRootMaterialC] = aOutShaderModelName.QString();
						newShaderModelJson[TemplateManager::pJson_TemplateMetaDataC] = newMetaDataObj;
						TemplateManager::QInstance().SaveTemplateToFile(ShaderModelsTemplateCategoryC, aOutShaderModelName.QString(), aOutShaderModelFileName.QString());

						// Add new Json SM file to perforce if you mapped Data/EditorFiles/... in your P4 data workspace folder.
						BSPerforce::ConnectionSmartPtr spperforce;
						CSPerforce::Perforce::QInstance().QPerforce(spperforce);
						if (spperforce)
						{
							spperforce->AddFile(aOutShaderModelFileName.QString());
						}

						created = true;

						// All conditions good to create the associated root Layered material.
						aOutCreatedRootMaterial = BSMaterial::CreateLayeredMaterial(aOutShaderModelName);

						SetLayeredMaterialShaderModel(aOutCreatedRootMaterial, BSMaterial::ShaderModelComponent(aOutShaderModelName));

						// Rename any inherited sub objects, we must flush to ensure all pending creates are executed
						BSMaterial::Flush();

						loop = false;
					}
				}
			}
		}

		return created;
	}

	/// <summary> Calculate ShaderModel State such as number of visible layers and blenders. </summary>
	/// <param name="arMaterialRootNode"> Material ModelNode root for property recursion. </param>
	/// <param name="arState"> OUT: Calculated state </param>
	void CalculateShaderModelState(QtPropertyEditor::ModelNode& arMaterialRootNode, ShaderModelState& arState)
	{
		using namespace QtPropertyEditor;

		// Count the number of Allowed Layers & Blenders by the Material ShaderModel
		arState = { 0 };

		arMaterialRootNode.ApplyRecursively([&arState](ModelNode& arChild)
			{
				if (arChild.QModel())
				{
					// Test for Layer
					BSMaterial::LayerID layerID;
					BSMaterial::BlenderID blenderID;
					if (arChild.GetNativeValue(BSReflection::Ptr(&layerID)))
					{
						// Total available layers
						arState.LayerCount++;
						if (layerID.QValid())
						{
							// used layer slot
							arState.LayersInUse++;
						}
					}
					// Try it as a blender
					else if (arChild.GetNativeValue(BSReflection::Ptr(&blenderID)))
					{
						arState.BlenderCount++;
					}
				}
			});
	}

	/// <summary> Migrate visible properties of the new material that has just been switched to a new Shader Model parent. </summary>
	/// <param name="arMaterialPropertyEditorRootNode"> The first node to Material loaded in the Tool Property Editor </param>
	/// <param name="aShaderModelRootMaterial"> The new Shader Model Root Material to migrate to</param>
	void MigrateShaderModelProperties(QtPropertyEditor::ModelNode& arMaterialPropertyEditorRootNode,
		BSMaterial::LayeredMaterialID aShaderModelRootMaterial)
	{
		using namespace QtPropertyEditor;

		auto applyProcessors = [](ModelNode& arNode, const BSFixedString& aShaderModelToApply) {

			DeleteProcessor cleanupProcessor; // Mandatory last processor

			// Try to find the shader model processor and apply it on Model Node hierarchy.
			std::shared_ptr<RuleProcessor> shaderModelProcessor = SharedTools::GetShaderModelRuleProcessor(aShaderModelToApply);
			BSASSERT(shaderModelProcessor != nullptr, "SwitchShaderModel : Cannot find shader model (%s) processor.", aShaderModelToApply.QString());
			shaderModelProcessor->Process(arNode);

			// Apply node clean up (delete) as last processor, processing nodes that were set to hidden.
			cleanupProcessor.Process(arNode);
		};

		// Create Model Node hierarchy for Destination Material with Shader Model to compare against.
		ModelNode destRootNode;
		GenericEditorBuilder destVisitor(destRootNode);
		destVisitor.Visit(BSReflection::ObjectPtr(&aShaderModelRootMaterial));
		BSMaterial::ShaderModelComponent destSMComponent = BSMaterial::GetLayeredMaterialShaderModel(aShaderModelRootMaterial);
		applyProcessors(destRootNode, destSMComponent.FileName);

		// iterate the source model node visible properties after processors have been applied. Find their equivalent dataPath properties
		// in simulated destination Material visible properties. If a data path is not found, revert the property to data parent value (default), 
		// else leave the property intact.
		arMaterialPropertyEditorRootNode.ForEach([&destRootNode](ModelNode& arChild) {

			BSContainer::ForEachResult result = BSContainer::Continue;

			bool propertyIsValid = false;
			BSString displayedPropertyName(arChild.GetViewPath());
			BSString srcPropertyDataPath(arChild.GetDataPath());
			// Process all properties except root which is the LayeredMaterial ID document.
			if (!srcPropertyDataPath.IsEmpty())
			{
				ModelNode* pnodeInDestination = destRootNode.FindDataPath(srcPropertyDataPath.QString());
				if (pnodeInDestination)
				{
					propertyIsValid = pnodeInDestination->QState() != ModelNode::ReadOnly;
					BSWARNING(WARN_EDITOR, "Property %s (DataPath: %s) found and can be migrated", displayedPropertyName.QString(), srcPropertyDataPath.QString());
				}
				else
				{
					// Skip children if node is not found.
					result = BSContainer::SkipChildren;
				}

				// Revert invalid properties to new parents default value.
				if (!propertyIsValid)
				{
					BSWARNING(WARN_EDITOR, "Property %s (DataPath: %s) is not editable in the new shader model.", displayedPropertyName.QString(), srcPropertyDataPath.QString());

					if (arChild.QHasDataParent())
					{
						BSVERIFY(arChild.SetValue(arChild.GetParentValue()));

						// Report if the value is an Object and is non null. It is supposed to be zeroed out as parent should not have layers/Blenders by default.
						BSComponentDB2::ID id;
						BSWARNING_IF(arChild.Get(id) && id != BSComponentDB2::NullIDC, WARN_MATERIALS, "We copied a sub object from our shader model root material");
					}
					else
					{
						BSReflection::Any theDefault(*arChild.GetDataType());
						BSVERIFY(arChild.SetNativeValue(theDefault.MakePointer()));
					}
				}
			}
			return result;
			});
	}

	/// <summary> Is ShaderModel of Material classified as a Base Material ? </summary>
	/// <param name="aMaterialID"> Layered Material to Test shader model name </param>
	/// <returns> True if Base Material, false otherwise. </returns>
	bool IsBaseMaterial(BSMaterial::LayeredMaterialID aMaterialID)
	{
		BSMaterial::ShaderModelComponent smComponent = BSMaterial::GetLayeredMaterialShaderModel(aMaterialID);
		return smComponent.FileName == BaseMaterialShaderModelC;
	}

	/// <summary> Is ShaderModel classified as pre-production Experimental ? </summary>
	/// <param name="aShaderModelName">ShaderModel name</param>
	/// <returns> True if experimental, false otherwise. </returns>
	bool IsExperimental(const BSFixedString& aShaderModelName)
	{
		return aShaderModelName == ExperimentalShaderModelC;
	}

	/// <summary> Can we display and let user use the Shader Model and its Materials. </summary>
	/// <returns> True if usable, false otherwise. </returns>
	bool GetShaderModelAllowed(bool aShaderModelIsExperimental)
	{
		bool isAllowed = true;
		// If ShaderModel is Experimental, we must check if its usable by user.
		if (aShaderModelIsExperimental)
		{
			isAllowed = bExperimentalMaterials.Bool();
		}
		return isAllowed;
	}

	/// <summary> Get ShaderModel Watchable source json file Folder </summary>
	/// <returns> Absolute loose file path for editor files shader models. </returns>
	BSFilePathString GetShaderModelWatchFolder()
	{
		BSFilePathString ruleTemplateSourcePath;
		BSFilePathString watchPath;
		BSFilePathString NormalizedFinalPath;
		FilePathUtilities::Join(QDir::current().absolutePath().toLatin1().data(),
			QtPropertyEditor::TemplateManager::QInstance().QRuleTemplateRootFolder(), ruleTemplateSourcePath);
		FilePathUtilities::Join(ruleTemplateSourcePath, ShaderModelsTemplateCategoryC, watchPath);
		// Make sure all slashes are normalized
		FilePathUtilities::NormPath(watchPath.QString(), NormalizedFinalPath);
		return NormalizedFinalPath;
	}

	/// <summary> Save ShaderModel to File </summary>
	/// <param name="aShaderModelAbsoluteFilePath">Absolute filename to save to.</param>
	void SaveShaderModelToFile(const BSFixedString& aShaderModelAbsoluteFilePath)
	{
		QFileInfo file(aShaderModelAbsoluteFilePath.QString());
		BSFixedString shaderModelName = BSFixedString(file.baseName().toLatin1().data());
		QtPropertyEditor::TemplateManager::QInstance().SaveTemplateToFile(ShaderModelsTemplateCategoryC, shaderModelName.QString(), aShaderModelAbsoluteFilePath.QString());
	}

	/// <summary> Get the ShaderModel Template List </summary>
	/// <returns> List of all ShaderModel template names loaded. </returns>
	stl::vector<std::string> GetShaderModelTemplateList()
	{
		stl::vector<std::string> shaderModels;
		QtPropertyEditor::TemplateManager::QInstance().GetTemplateList(ShaderModelsTemplateCategoryC, shaderModels);
		return shaderModels;
	}

	/// <summary> Test if ShaderModel is Locked. This means cannot create new material from root SM material, and full inheritance should be prevented. </summary>
	/// <param name="aShaderModelName">The shader model name to get the shader root material.</param>
	/// <returns> Rule processor associated with this ShaderModel </returns>
	std::shared_ptr<QtPropertyEditor::RuleProcessor> GetShaderModelRuleProcessor(const BSFixedString& aShaderModelName)
	{
		return QtPropertyEditor::TemplateManager::QInstance().GetRuleProcessor(ShaderModelsTemplateCategoryC, aShaderModelName.QString());
	}

	/// <summary> Get the ShaderModel metadata tag that links to a root material. </summary>
	/// <param name="aShaderModelName">The shader model name to get the shader root material.</param>
	/// <returns> Root material name </returns>
	BSFixedString GetShaderModelRootMaterial(const BSFixedString& aShaderModelName)
	{
		// Empty shader model/not found defaults to Experimental shader model.
		std::string rootMaterialName(DefaultShaderModelC);
		if (!aShaderModelName.QEmpty())
		{
			rootMaterialName = QtPropertyEditor::TemplateManager::QInstance().GetMetaDataValue<std::string>(ShaderModelsTemplateCategoryC,
				aShaderModelName.QString(), ShaderModelMetaRootMaterialC);
		}

		return BSFixedString(rootMaterialName.c_str());
	}

	/// <summary> Set ShaderModel RootMaterial Metadata </summary>
	/// <param name="aShaderModelName">ShaderModel name to set Metadata</param>
	/// <param name="aRootMaterialName">RootMaterial name value to set.</param>
	void SetShaderModelRootMaterial(const BSFixedString& aShaderModelName, const BSFixedString& aRootMaterialName)
	{
		BSVERIFY(QtPropertyEditor::TemplateManager::QInstance().SetMetaDataValue(ShaderModelsTemplateCategoryC, aShaderModelName.QString(),
			ShaderModelMetaRootMaterialC, std::string(aRootMaterialName.QString())));
	}

	/// <summary> Test if ShaderModel is Locked. This means cannot create new material from root SM material, and full inheritance should be prevented. </summary>
	/// <param name="aShaderModelName">The shader model name to check locked status.</param>
	/// <returns> True if locked, false otherwise. </returns>
	bool GetShaderModelLocked(const BSFixedString& aShaderModelName)
	{
		bool isLocked = false;

		// Ignore locked status if we are a super user.
		const bool isSuperUser = bMaterialSuperUser.Bool();
		if (!isSuperUser)
		{
			if (!aShaderModelName.QEmpty())
			{
				isLocked = QtPropertyEditor::TemplateManager::QInstance().GetMetaDataValue<bool>(ShaderModelsTemplateCategoryC, aShaderModelName.QString(), ShaderModelMetaLockedC);
			}
			else
			{
				// Empty or nullptr shader models are treated as locked. (All materials should have one)
				isLocked = true;
			}
		}
		return isLocked;
	}

	/// <summary> Test if ShaderModel permit a child material to be Switched into another material of a different shader model.</summary>
	/// <param name="aShaderModelName">The shader model name to check switchable status.</param>
	/// <returns> True if switchable, false otherwise. </returns>
	bool GetShaderModelSwitchable(const BSFixedString& aShaderModelName)
	{
		// By default all shader model can freely switch to other shader model. Look for specified exceptions in meta data.
		bool isSwitchable = true;

		// Super user can always switch shader model materials.
		const bool isSuperUser = bMaterialSuperUser.Bool();
		if (!isSuperUser)
		{
			if (!aShaderModelName.QEmpty())
			{
				isSwitchable = QtPropertyEditor::TemplateManager::QInstance().GetMetaDataValue<bool>(ShaderModelsTemplateCategoryC, aShaderModelName.QString(), ShaderModelMetaSwitchableC, true);
			}
		}
		return isSwitchable;
	}

	/// <summary> Get the ShaderModel DisplayName MetaData value. If none exist, return the name of the Shader Model. </summary>
	/// <param name="aShaderModelName">Name of the ShaderModel to get the DisplayName.</param>
	/// <returns> DisplayName if any. Otherwise Shader Model Name. </returns>
	BSFixedString GetShaderModelDisplayName(const BSFixedString& aShaderModelName)
	{
		const char* pdisplayName = nullptr;
		std::string displayNameMeta = QtPropertyEditor::TemplateManager::QInstance().GetMetaDataValue<std::string>(ShaderModelsTemplateCategoryC, aShaderModelName.QString(), ShaderModelMetaDisplayNameC);
		if (displayNameMeta.empty())
		{
			// There are no display name alias, just re-use the name for the UI.
			pdisplayName = aShaderModelName.QString();
		}
		else
		{
			pdisplayName = displayNameMeta.c_str();
		}
		return BSFixedString(pdisplayName);
	}

	/// <summary> 
	/// Get a Map of ShaderModel DisplayName Alias to use in the UI instead of the data name. 
	/// If no Display Name is present, we simply default to present the name of the Shader Model.
	/// </summary>
	/// <param name="arDisplayNameMap">OUT : Map of Name to DisplayNames</param>
	void GetShaderModelDisplayNameMap(stl::scrap_unordered_map<BSFixedString, BSFixedString>& arDisplayNameMap)
	{
		arDisplayNameMap.clear();
		stl::vector<std::string> shaderModels = GetShaderModelTemplateList();
		for (const auto& shaderModelName : shaderModels)
		{
			BSFixedString name(shaderModelName.c_str());
			arDisplayNameMap[name] = GetShaderModelDisplayName(name);
		}
	}

	/// <summary> Utility function to resolve to the corresponding ShaderModel from DisplayNameMap (see GetShaderModelDisplayNameMap). </summary>
	/// <param name="aDisplayName"> Display Name to resolve to ShaderModel Name.</param>
	/// <returns> Corresponding ShaderModel Name. </returns>
	const BSFixedString ResolveShaderModelDisplayName(const BSFixedString& aDisplayName, const stl::scrap_unordered_map<BSFixedString, BSFixedString>& aDisplayNameMap)
	{
		const char* pfound = nullptr;
		stl::vector<std::string> shaderModels = GetShaderModelTemplateList();
		for (const auto& rentry : aDisplayNameMap)
		{
			if (rentry.second.Compare(aDisplayName) == 0)
			{
				// We want the key (Shader Model name)
				pfound = rentry.first.QString();
				break;
			}
		}
		BSASSERT(pfound != nullptr, "You have supplied a DisplayName that cannot be resolved to ShaderModel name. This should never happen.");
		return BSFixedString(pfound);
	}

	/// <summary> Test if ShaderModel uses LOD materials. </summary>
	/// <param name="aShaderModelName">The shader model name to check.</param>
	/// <returns> True if LODs are enabled. </returns>
	bool GetShaderModelUsesLevelOfDetail(const BSFixedString& aShaderModelName)
	{
		bool enabled = true;
		if (!aShaderModelName.QEmpty())
		{			
			enabled = 
				!QtPropertyEditor::TemplateManager::QInstance().GetMetaDataValue<bool>
				(
					ShaderModelsTemplateCategoryC,
					aShaderModelName.QString(),
					ShaderModelMetaDisableLOD
				);
		}
		return enabled;
	}

}