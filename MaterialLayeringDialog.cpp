//----------------------------------------------------------------------
// ZENIMAX MEDIA PROPRIETARY INFORMATION
//
// This software is developed and/or supplied under the terms of a license
// or non-disclosure agreement with ZeniMax Media Inc. and may not be copied
// or disclosed except in accordance with the terms of that agreement.
//
// Copyright (c) 2019 ZeniMax Media Incorporated.
// All Rights Reserved.
//
// ZeniMax Media Incorporated, Rockville, Maryland 20850
// http://www.zenimax.com
//
// FILE 	MaterialLayeringDialog.cpp
// OWNER 	Christian Roy
// DATE 	2019-02-25
//----------------------------------------------------------------------

#include "AppPCH.h"
#include "MaterialLayeringDialog.h"
#include "MaterialModelProxy.h"

// QT Includes
#include <SharedTools/Qt/QtSharedIncludesBegin.h>
#include <QtCore/QSettings>
#include <QtCore/QTextStream>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QProgressDialog>
#include <QtWidgets/QToolBar>
#include <SharedTools/Qt/QtSharedIncludesEnd.h>
// \ QT Includes

#include <BSCore/BSString.h>
#include <BSCore/BSTScrapSTLContainers.h>
#include <BSMain/BSBind.h>
#include <BSMain/BSResourceReloadManager.h>
#include <BSMain/BSComponentDB2Storage.h>
#include <BSMain/Render/BSRenderUtil.h>
#include <BSMaterial/BSMaterialBinding.h>
#include <BSMaterial/BSMaterialBlender.h>
#include <BSMaterial/BSMaterialDB.h>
#include <BSMaterial/BSMaterialFwd.h>
#include <BSMaterial/BSMaterialLayer.h>
#include <BSMaterial/BSMaterialLayeredMaterial.h>
#include <BSPerforce/BSPerforceFileInfo.h>
#include <BSReflection/BSReflection.h>
#include <BSSystem/BSJobSystem.h>
#include <BSSystem/BSSystemDir.h>
#include <BSSystem/BSJobs.h>
#include <Construction Set/Dialogs/Widgets/FileSelectorWidget.h>
#include <Construction Set/Dialogs/Widgets/PreviewWidget.h>
#include <Construction Set/Misc/BGSRenderWindowUtils.h>
#include <Construction Set/Qt/FormEditing/QtFormComboBox.h>
#include <Perforce/BGSCSPerforce.h>
#include <Qt/Utility/CreationKitUtils.h>
#include <Services/AssetMetaDB/AssetMetaDB.h>
#include <Shared/ExtraData/ExtraDataList.h>
#include <Shared/FormComponents/TESModel.h>
#include <Shared/TESForms/Material/BGSLayeredMaterialSwap.h>
#include <Shared/TESForms/World/TESObjectREFR.h>
#include <SharedTools/Materials/BSMaterialSnapshot.h>
#include <SharedTools/Qt/Dialogs/CreateNewFromHierarchyDialog.h>
#include <SharedTools/Qt/Dialogs/MaterialLayering/MaterialLayeringBakeOptionsDialog.h>
#include <SharedTools/Qt/Dialogs/QtBoundPropertyDialog.h>
#include <SharedTools/Qt/Dialogs/QtGenericListDialog.h>
#include <SharedTools/Qt/Utility/QtSharedToolsFunctions.h>
#include <SharedTools/Qt/Utility/QtPerforceFileInfoCache.h>
#include <SharedTools/Qt/Widgets/MaterialBrowserWidget.h>
#include <SharedTools/Qt/Widgets/PropertyEditor/QtGenericPropertyEditor.h>
#include <SharedTools/Qt/Widgets/PropertyEditor/RuleProcessor.h>
#include <SharedTools/Qt/Widgets/PropertyEditor/CustomUIProcessor.h>
#include <SharedTools/Qt/Widgets/PropertyEditor/RuleProcessor.h>
#include <SharedTools/Qt/Widgets/PropertyEditor/Editor/MaterialLayerButtonsWidget.h>
#include <SharedTools/Qt/Widgets/PropertyEditor/UndoCommand.h>
#include <SharedTools/ShaderModel/ShaderModel.h>
#include <SharedTools/ViewModel/Material/BSMaterialLayerView.h>
#include <SDK/JSONcpp/include/json/json.h>
#include "PropertyEditDialog.h"
#include "RuleTemplateManager.h"
#include "BSMaterial/BSMaterialChangeNotify.h"

extern INISetting bUseVersionControl;
extern INISetting sMaterialDefaultChangeListDesc;
extern INISetting sMaterialIconDepotPath;
extern INISetting sLayeredMaterialDepotPath;


namespace SharedTools
{
	INISetting sMaterialIconRelativeDirectory("sMaterialIconRelativeDirectory:MaterialLayering", "Data/EditorFiles/GeneratedIcons/Materials/");
	INISetting bEnableMaterialMapExport("bEnableMaterialMapExport:MaterialLayering", true);

	INISetting bEnableMaterialSaveAll("bEnableMaterialSaveAll:MaterialLayering", false);
	INISetting bSynchWithoutPrompt("bSynchWithoutPrompt:MaterialLayering", false);

	INIPrefSetting sRecentPreviewMeshFile("sRecentPreviewMeshFile:MaterialLayering", "");
}

namespace
{
	const char* pDialogTitleC = "Material Editor";
	const char* pNewMaterialRootNameC = "Shader Model";
	const char* pMaterialListRootNameC = "Materials";
	const char* pMaterialPrefixC = "Data/";
	const char* pUntitledNameC = "<untitled>";
	const char* pUntitledMaterialDataParentC = "1LayerStandard";
	constexpr int32_t MaterialPreviewRefreshTimerC = 2000;	// Seconds between refreshing, to allow reloaded textures to show up etc
	constexpr int32_t UpdateTickC = 30;

	const QString SplitterPreviewAndBrowserC("splitterPreviewAndBrowser");
	const QString SplitterMainVerticalC("splitterMainVertical");

	const BSString BindablePropertyIconC{ ":/PropertyEditorWidgets/Bindable-Property.png" };
	const BSString BoundPropertyIconC{ ":/PropertyEditorWidgets/Bound-Property.png" };

	/// <summary> Get Material ShaderModel Name Utility function </summary>
	/// <param name="aLayeredMaterialID"> The Material holding a ShaderModel component. </param>
	/// <returns> Name of associated ShaderModel </returns>
	BSFixedString GetShaderModelName(const BSMaterial::LayeredMaterialID aLayeredMaterialID)
	{
		BSMaterial::ShaderModelComponent smComponent = BSMaterial::GetLayeredMaterialShaderModel(aLayeredMaterialID);
		return BSFixedString(smComponent.FileName);
	}

	/// <summary> Searches all forms looking for any TESModel that uses the given layered material. </summary>
	/// <param name="aLayeredMaterialID"> The ID of the layered material </param>
	/// <returns> The collection of models using the layered material </returns>
	BSTArray<BSFixedString> FindFormDependenciesForLayeredMaterial(BSComponentDB2::ID aLayeredMaterialID)
	{
		BSTArray<BSFixedString> modelsUsingMaterial;
		TESDataHandler::QInstance().ForEachFormOfType(LMSW_ID, [&](TESForm *apForm)
		{
			const BGSLayeredMaterialSwap &swap = static_cast<const BGSLayeredMaterialSwap&>(*apForm);
			for (const auto& entry : swap.Entries)
			{
				if (entry.OverrideMaterial == aLayeredMaterialID)
				{
					BSString modelStr;
					modelStr.SPrintF("Material Swap form '%s' %08X", swap.GetFormEditorID(), swap.GetFormID());

					if (!IsInArray(modelsUsingMaterial, modelStr))
					{
						modelsUsingMaterial.Add(modelStr);
					}
				}
			}
			return BSContainer::Continue;
		});
		return modelsUsingMaterial;
	}

	/// <summary> Add Icon type support for the material tree views </summary>
	enum class MaterialType : uint32_t
	{
		Root = 0,
		ShaderModel,
		Template,
		Material,
		Count
	};
	using MaterialIconTypeIntegral = std::underlying_type<MaterialType>::type;
	const uint32_t MaterialCreationIconTypeCountC = static_cast<MaterialIconTypeIntegral>(MaterialType::Count);
	const char* MaterialCreationTypeIconStringA[] = { ":/MainMenu/Icons/rectangle-solid-48.png", ":/MainMenu/Icons/code-block-regular-48.png", ":/MainMenu/Icons/layer-solid-48.png", ":/MainMenu/Icons/medium-logo-48.png" };
	static_assert(COMPILE_TIME_ARRAY_COUNT(MaterialCreationTypeIconStringA) == MaterialCreationIconTypeCountC,
		"The size of MaterialCreationTypeIconStringC is not synchronized with MaterialCreationIconType::Count");
	/// <summary> New Custom data roles for tree view filling to set ID and parent ID. </summary>
	enum CustomRoles
	{
		MaterialParentID = Qt::UserRole,
		MaterialID
	};
	/// <summary> Add the known shader models or materials to a tree widget </summary>
	/// <param name="apTreeWidget">The Tree Widget to fill.</param>
	/// <param name="aRootNodeLabel">The root node label if any.</param>
	/// <param name="aEditedMaterialID">Currently displayed Material.</param>
	/// <param name="aShowAll"> Show all materials rather than just the shader models. </param>
	/// <param name="aRemoveEditedMaterialHierarchy"> If true, will remove the Shader Model root material family for aEditedMaterialID from final hierarchy. </param>
	void FillMaterialHierarchy(QTreeWidget* apTreeWidget, const QString& aRootNodeLabel, BSMaterial::LayeredMaterialID aEditedMaterialID, bool aShowAll, bool aRemoveEditedMaterialHierarchy = false)
	{
		QList<QTreeWidgetItem*> items;
		stl::scrap_unordered_map<BSFixedString, BSFixedString> shaderModelDisplayNameMap;
		stl::scrap_unordered_map<uint32_t, QTreeWidgetItem*> IDtoItemMap; // Map for tracking which ID is associated with which QTreeWidgetItem
		const uint32_t rootLevelID = BSMaterial::Internal::QRootLayeredMaterialsID().QValue();

		//Get the DisplayName Map to use instead of actual ShaderModel data name.
		SharedTools::GetShaderModelDisplayNameMap(shaderModelDisplayNameMap);

		// Do we opt to cut out the edited item family tree from hierarchy (example : to choose a different root material parent).
		BSFixedString shaderModelToRemove;
		if (aRemoveEditedMaterialHierarchy)
		{
			shaderModelToRemove = GetShaderModelName(aEditedMaterialID);
		}

		// Its possible we do not want a root node.
		QTreeWidgetItem* prootNode = nullptr;
		QTreeWidgetItem* pselectedItem = nullptr;
		if (!aRootNodeLabel.isEmpty())
		{
			prootNode = new QTreeWidgetItem(apTreeWidget);
			prootNode->setText(0, aRootNodeLabel);
			prootNode->setIcon(0, QIcon(MaterialCreationTypeIconStringA[static_cast<MaterialIconTypeIntegral>(MaterialType::Root)]));
			prootNode->setData(0, CustomRoles::MaterialParentID, rootLevelID);
			prootNode->setData(0, CustomRoles::MaterialID, rootLevelID);
			// Root is not selectable.
			prootNode->setFlags(prootNode->flags() & ~Qt::ItemIsSelectable);
			items.append(prootNode);
			IDtoItemMap[rootLevelID] = prootNode;
			pselectedItem = prootNode;
		}

		stl::vector<std::string> shaderModels = SharedTools::GetShaderModelTemplateList();

		// Add all DB Materials
		BSMaterial::ForEachLayeredMaterial([&items, apTreeWidget, &IDtoItemMap, &shaderModelDisplayNameMap, rootLevelID, aEditedMaterialID, &pselectedItem, &shaderModels, aShowAll, &shaderModelToRemove](BSMaterial::LayeredMaterialID aParentID, BSMaterial::LayeredMaterialID aLayeredMaterialID)
		{
			// Query the name of the layered material from the DB
			BSFixedString name;
			BSMaterial::GetName(aLayeredMaterialID, name);
			BSWARNING_IF_ONCE_PER_ID(name.QEmpty(), aLayeredMaterialID, WARN_MATERIALS, "Trying to list a Material with empty name for MaterialID:%u, ParentID:%u", aLayeredMaterialID.QID().QValue(), aParentID.QID().QValue());


			// Get the ShaderModel for this material
			BSFixedString shaderModelName = GetShaderModelName(aLayeredMaterialID);

			const bool removeFromHierarchy = !shaderModelToRemove.QEmpty() && (shaderModelToRemove.Compare(shaderModelName) == 0);

			// Some Material can be hidden from user once we move to final production.
			const bool shaderModelAllowed = SharedTools::GetShaderModelAllowed(shaderModelName);

			// Get the RootMaterial for the ShaderModel
			BSFixedString rootMaterialName = SharedTools::GetShaderModelRootMaterial(shaderModelName);

			BSFilePathString file;
			BSMaterial::Internal::QDBStorage().GetObjectFilename(aLayeredMaterialID, file);

			// If we do not show all, then only list RootMaterials
			const bool isRootMaterial = BSstrcmp(name.QString(), rootMaterialName.QString()) == 0;
			bool addMaterial = !removeFromHierarchy && shaderModelAllowed && !name.QEmpty() && name != BSFixedString(pUntitledNameC) && name != BSFixedString(BSMaterial::pTemporaryLayeredInstanceNameC);
			if (!aShowAll && addMaterial)
			{
				// When not showing all material we only want to evaluate root material to show up.
				addMaterial = false;

				// We only add the material if it corresponds to a shader model, and the shader model is not locked down.
				if (isRootMaterial && std::find(shaderModels.begin(), shaderModels.end(), shaderModelName.QString()) != shaderModels.end())
				{
					// Check if this shader model is locked, if so, we prevent making new material from it.
					addMaterial = !SharedTools::GetShaderModelLocked(shaderModelName);
				}
			}

			if (addMaterial)
			{
				// Get the parent item (if any)
				// NOTE: Layered materials that are instances of another layered material will have a parent
				QTreeWidgetItem* pparentItem = nullptr;
				if (aParentID.QValid() == true)
				{
					auto iter = IDtoItemMap.find(aParentID.QID().QValue());
					if (iter != IDtoItemMap.cend())
					{
						pparentItem = IDtoItemMap.at(aParentID.QID().QValue());
					}
				}

				// If we display only the shader models, use this display name instead of root material name
				const BSFixedString& shaderModelDisplayName = shaderModelDisplayNameMap[shaderModelName];

				// Are we showing all the hierarchy or only the shader model names
				QStringList nodeName;
				nodeName << (aShowAll ? name.QString() : shaderModelDisplayName.QString());

				// Create and add a tree item
				QTreeWidgetItem* pitem = nullptr;
				if (pparentItem != nullptr)
				{
					if (aParentID.QID().QValue() == rootLevelID)
					{
						pitem = new QTreeWidgetItem(pparentItem, nodeName);

						// parented directly to root is always a shader model material
						pitem->setIcon(0, QIcon(MaterialCreationTypeIconStringA[static_cast<MaterialIconTypeIntegral>(MaterialType::ShaderModel)]));
					}
					else
					{
						//If we are showing only shader models and end up here, it means we are not showing a root material, delete it and ignore.
						if (aShowAll)
						{
							pitem = new QTreeWidgetItem(pparentItem, nodeName);

							// anything other than parented to root, is treated as a parent material
							pitem->setIcon(0, QIcon(MaterialCreationTypeIconStringA[static_cast<MaterialIconTypeIntegral>(MaterialType::Template)]));
						}
					}
				}
				else
				{
					// un-parented is always a shader model material
					pitem = new QTreeWidgetItem(apTreeWidget, nodeName);
					pitem->setIcon(0, QIcon(MaterialCreationTypeIconStringA[static_cast<MaterialIconTypeIntegral>(MaterialType::ShaderModel)]));
				}

				if (pitem != nullptr)
				{
					pitem->setData(0, CustomRoles::MaterialParentID, aParentID.QID().QValue());
					pitem->setData(0, CustomRoles::MaterialID, aLayeredMaterialID.QID().QValue());

					items.append(pitem);

					// Store the ID->QTreeWidgetItem
					IDtoItemMap[aLayeredMaterialID.QID().QValue()] = pitem;

					if (aLayeredMaterialID == aEditedMaterialID)
					{
						pselectedItem = pitem;
					}
				}
			}

			return BSContainer::Continue;
		});

		// Configure the tree of items
		apTreeWidget->insertTopLevelItems(0, items);
		apTreeWidget->sortItems(0, Qt::SortOrder::AscendingOrder);
		if (pselectedItem != prootNode)
		{
			apTreeWidget->setCurrentItem(pselectedItem);
		}
		apTreeWidget->expandAll();
	}

	/// <summary> Simple Preview Widget class to hide the dialog when closing, potentially using a toggle show/hide QAction </summary>
	class FormPreviewWidgetDialog : public QDialog
	{
	public:
		/// <summary> FormPreviewWidgetDialog - Constructor </summary>
		/// <param name="apParentWidget"> Parent widget </param>
		/// <param name="apToggleShowHideAction"> QAction used to toggle this dialog's visibility </param>
		FormPreviewWidgetDialog(QWidget *apParentWidget, QAction* apToggleShowHideAction) 
			: QDialog(apParentWidget, Qt::WindowSystemMenuHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint) 
			, pToggleShowHideAction{apToggleShowHideAction }
		{
		}
	protected:
		/// <summary> Called when this dialog is closed, ignores the event and uses the toggle show/hide action if present, or just hides the dialog </summary>
		/// <param name="apEvent"> Close event </param>
		void closeEvent(QCloseEvent* apEvent) override
		{
			apEvent->ignore();
			// Let the action hide this dialog
			if ( pToggleShowHideAction )
			{
				pToggleShowHideAction->trigger();
			}
			else
			{
				hide();
			}
		}
	private:
		QAction* pToggleShowHideAction = nullptr;
	};

	/// <summary> Get the absolute directory path for material icons </summary>
	/// <returns> Absolute directory path </returns>
	BSFilePathString GetMaterialIconDirectory()
	{
		BSFilePathString absolutePath;
		FilePathUtilities::AbsPath(SharedTools::sMaterialIconRelativeDirectory, absolutePath);

		return absolutePath;
	}

	/// <summary> Get the absolute directory path for material maps </summary>
	/// <returns> Absolute directory path </returns>
	BSFilePathString GetMaterialMapDirectory()
	{
		BSFilePathString absolutePath;
		FilePathUtilities::AbsPath(sMaterialMapsRelativeDirectory, absolutePath);

		return absolutePath;
	}

	/// <summary> Get the absolute icon path for a material </summary>
	/// <param name="aMaterialName"> Material name </param>
	/// <param name="arIconPath"> our icon path </param>
	/// <returns> Whether or not we have the file </returns>
	bool GetMaterialIconPath(const BSFilePathString& aMaterialName, BSFilePathString& arIconPath)
	{
		arIconPath.SPrintF("%s%s.png", GetMaterialIconDirectory().QString(), aMaterialName.QString());

		return BSFile::Access(arIconPath, NiFile::READ_ONLY) || BSFile::Access(arIconPath, NiFile::READWRITE);
	}

	/// <summary> Get the absolute icon path for a material </summary>
	/// <param name="aMaterialID"> Material ID we want an icon path for </param>
	/// <param name="arIconPath"> our icon path </param>
	/// <returns> Whether or not we have the file </returns>
	bool GetMaterialIconPath(BSMaterial::LayeredMaterialID aMaterialID, BSFilePathString& arIconPath)
	{
		BSFilePathString file;
		BSMaterial::Internal::QDBStorage().GetObjectFilename(aMaterialID, file);

		BSFilePathString name;
		FilePathUtilities::GetFileName(file.QString(), name);

		return GetMaterialIconPath(name, arIconPath);
	}

	/// <summary>
	/// Add all generated material snapshot paths to the given list
	/// </summary>
	/// <param name="arObject"> material object ID</param>
	/// <param name="aBakeDialog"> dialog containing material bake options</param>
	/// <param name="arOutPaths"> List of file paths </param>
	void AddMaterialSnapshotsToFileList(const BSComponentDB2::ID &aObject, const MaterialLayeringBakeOptionsDialog& aBakeDialog, bool aRequireSnapshotsOnDisk, TextureNameArray& arOutPaths)
	{
		BSFilePathString iconPath;
		if (GetMaterialIconPath(BSMaterial::LayeredMaterialID(aObject), iconPath))
		{
			arOutPaths.Add(iconPath);
		}

		stl::scrap_vector<BSFilePathString> mapPaths = aBakeDialog.GetMaterialMapPaths(BSMaterial::LayeredMaterialID(aObject), aRequireSnapshotsOnDisk);
		
		for (BSFilePathString& rpath : mapPaths)
		{
			arOutPaths.Add(rpath);
		}
	}

	/// <summary>
	/// Helper for solo/hide setup as well as exporting the given material
	/// </summary>
	/// <param name="aMaterialBakeSettings">Bake options dialog</param>
	/// <param name="arEditedMaterialID">Current material ID</param>
	void ExportMaterialMapHelper(const MaterialLayeringBakeOptionsDialog& aMaterialBakeSettings, BSMaterial::LayeredMaterialID& arEditedMaterialID)
	{
		auto foreachLayer = [&arEditedMaterialID](std::function<void(uint32_t, BSMaterial::LayerID)> func)
		{
			for (uint16_t layerIndex = 0u; layerIndex < MaterialLayeringBakeOptionsDialog::NumLayersS; layerIndex++)
			{
				const BSMaterial::LayerID layerID = GetLayer(arEditedMaterialID, layerIndex);

				if (layerID != BSComponentDB2::NullIDC)
				{
					func(layerIndex, layerID);
				}
			}
		};

		//Settings for each layer so we can restore it later. layer index, solo, hide
		using HSSetting = std::tuple<BSMaterial::LayerID, bool, bool>;

		stl::scrap_vector<HSSetting> layerSettings;
		layerSettings.reserve(MaterialLayeringBakeOptionsDialog::NumLayersS);

		foreachLayer([&layerSettings](uint32_t /*aLayerIndex*/, BSMaterial::LayerID aLayerID)
		{
			BSMaterial::HideSoloData hsData = BSMaterial::GetHideSoloData(aLayerID);

			layerSettings.emplace_back(HSSetting(aLayerID, hsData.Solo, hsData.Hide));
		});

		auto restoreLayerSettings = [&layerSettings]()
		{
			for (HSSetting& rsetting : layerSettings)
			{
				const BSMaterial::LayerID layerID = std::get<0>(rsetting);
				BSMaterial::HideSoloData hsData = BSMaterial::GetHideSoloData(layerID);

				hsData.Solo = std::get<1>(rsetting);
				hsData.Hide = std::get<2>(rsetting);

				BSMaterial::SetHideSoloData(layerID, hsData);
			}
		};

		//Check if we bake all layers together
		if (aMaterialBakeSettings.ShouldBakeCombinedMap())
		{

			//Disable hide and solo for each layer
			foreachLayer([&layerSettings](uint32_t /*aLayerIndex*/, BSMaterial::LayerID aLayerID)
			{
				BSMaterial::HideSoloData hsData = BSMaterial::GetHideSoloData(aLayerID);

				hsData.Solo = false;
				hsData.Hide = false;

				BSMaterial::SetHideSoloData(aLayerID, hsData);
			});

			BGSRenderWindowUtils::ExportMaterialMaps(arEditedMaterialID, GetMaterialMapDirectory(), BSFilePathString());
		}

		//Export each enabled map, layerNum is 1 indexed because we are querying the UI.
		for (uint32_t layerNum = 0; layerNum < MaterialLayeringBakeOptionsDialog::NumLayersS; layerNum++)
		{
			if (aMaterialBakeSettings.ShouldBakeLayer(layerNum))
			{
				bool hasLayerForExport = false;

				foreachLayer([&layerSettings, layerNum, &hasLayerForExport](uint32_t aLayerIndex, BSMaterial::LayerID aLayerID)
				{
					BSMaterial::HideSoloData hsData = BSMaterial::GetHideSoloData(aLayerID);

					//Solo viewing a layer ignores its hide value so we don't need to worry about it here
					if (aLayerIndex == layerNum)
					{
						hsData.Solo = true;
						hasLayerForExport = true;
					}
					else
					{
						hsData.Solo = false;
					}

					BSMaterial::SetHideSoloData(aLayerID, hsData);
				});

				if (hasLayerForExport)
				{
					BGSRenderWindowUtils::ExportMaterialMaps(arEditedMaterialID, GetMaterialMapDirectory(), aMaterialBakeSettings.GetLayerPostfix(layerNum));
				}
			}
		}

		//Return settings to default
		restoreLayerSettings();
	}

	/// <summary>
	/// This model allows the property select dialog to select material properties from the BSMaterialBinding::Bindings enum and Material instance data.
	/// </summary>
	class MaterialPropertySelectModel : public SharedTools::IPropertySelectModel
	{
	public:
		/// <summary>
		/// Retrieve all of our animatable UV streams
		/// </summary>
		/// <param name="aLayeredMaterialID"> Our layered material ID </param>
		/// <returns>Our unique UV streams</returns>
		static BSScrapArray<BSMaterial::UVStreamID> CollectUVStreams(BSMaterial::LayeredMaterialID aLayeredMaterialID)
		{
			auto comparator = [](BSMaterial::UVStreamID aLhs, BSMaterial::UVStreamID aRhs) { return aLhs.QID() > aRhs.QID(); };

			BSScrapArray<BSMaterial::UVStreamID> uniqueStreamIDs;
			for (uint8_t i = 0; i < BSMaterial::MaxLayerCountC; ++i)
			{
				const BSMaterial::LayerID currentLayerID = BSMaterial::GetLayer(aLayeredMaterialID, i);
				if (currentLayerID.QValid())
				{
					const BSMaterial::UVStreamID currentUVstreamID = BSMaterial::GetUVStream(currentLayerID);
					if (currentUVstreamID.QValid())
					{
						BSFixedString streamName;
						BSMaterial::GetName(BSMaterial::LayeredMaterialID{ currentUVstreamID.QID() }, streamName);

						if (!streamName.QEmpty())
						{
							SortedInsertUnique(uniqueStreamIDs, currentUVstreamID, comparator);
						}
					}
				}
			}

			for (uint8_t i = 0; i < BSMaterial::MaxBlenderCountC; ++i)
			{
				const BSMaterial::BlenderID currentBlenderID = BSMaterial::GetBlender(aLayeredMaterialID, i);
				if (currentBlenderID.QValid())
				{
					const BSMaterial::UVStreamID currentUVstreamID = BSMaterial::GetUVStream(currentBlenderID);
					if (currentUVstreamID.QValid())
					{
						BSFixedString streamName;
						BSMaterial::GetName(BSMaterial::LayeredMaterialID{ currentUVstreamID.QID() }, streamName);

						if (!streamName.QEmpty())
						{
							SortedInsertUnique(uniqueStreamIDs, currentUVstreamID, comparator);
						}
					}
				}
			}

			const BSMaterial::AlphaSettingsComponent materialAlphaSettings = BSMaterial::GetLayeredMaterialAlphaSettings(aLayeredMaterialID);
			const BSMaterial::UVStreamID opacityUVStream = materialAlphaSettings.Blender.OpacityUVStream;
			if (opacityUVStream.QValid())
			{
				BSFixedString streamName;
				BSMaterial::GetName(BSMaterial::LayeredMaterialID{ opacityUVStream.QID() }, streamName);

				if (!streamName.QEmpty())
				{
					SortedInsertUnique(uniqueStreamIDs, opacityUVStream, comparator);
				}
			}

			return uniqueStreamIDs;
		}

		MaterialPropertySelectModel(const BSMaterial::LayeredMaterialID aLayeredMaterialID) :
			LayeredMaterialID(aLayeredMaterialID)
		{

		}

		/// <summary>
		/// Create a node for the index from the model. This will be a material property node.
		/// </summary>
		/// <param name="aName"> The name of our node </param>
		/// <param name="apParent"> Our node's parent in the directory </param>
		/// <param name="aIndex"> Our binding enum value </param>
		/// <returns> The newly created node. </returns>
		BSBind::INode* CreateNodeForIndex(const BSFixedString& aName, BSBind::INode* apParent, uint32_t aIndex) override
		{
			BSBind::INode* pnode = nullptr;

			uint32_t totalIndex = 0;
			for (uint32_t i = 0; i < static_cast<uint32_t>(BSMaterialBinding::Bindings::Count) && pnode == nullptr; i++)
			{
				const BSMaterialBinding::Bindings binding = static_cast<BSMaterialBinding::Bindings>(i);
				for (uint16_t layerIndex = 0; layerIndex < BSMaterialBinding::GetBindingSupportedLayerCount(binding); layerIndex++)
				{
					if (GetLayer(LayeredMaterialID, layerIndex) != BSMaterial::NullIDC)
					{
						if (aIndex == totalIndex)
						{
							pnode = new BSMaterialBinding::MaterialPropertyNode(aName, apParent, static_cast<BSMaterialBinding::Bindings>(i), layerIndex);
							break;
						}
						totalIndex++;
					}
				}
			}

			if (pnode == nullptr)
			{
				const BSScrapArray<BSMaterial::UVStreamID> uvStreams = CollectUVStreams(LayeredMaterialID);
				for (uint32_t streamIndex = 0; streamIndex < uvStreams.QSize() && pnode == nullptr; streamIndex++)
				{
					const BSMaterial::UVStreamID streamID = uvStreams[streamIndex];

					for (uint32_t i = 0; i < static_cast<uint32_t>(BSMaterialBinding::UVStreamBindingType::Count); i++)
					{
						const BSMaterialBinding::UVStreamBindingType binding = static_cast<BSMaterialBinding::UVStreamBindingType>(i);

						if (aIndex == totalIndex)
						{
							pnode = new BSMaterialBinding::MaterialUVStreamPropertyNode(aName, apParent, streamID, binding);
							break;
						}
						totalIndex++;
					}
				}
			}

			return pnode;
		}

		/// <summary>
		/// Iterate through all of the material property that we can bind to.
		/// </summary>
		/// <param name="aForEach"> The functor to call for each element </param>
		/// <returns> The final value of our functor. </returns>
		BSContainer::ForEachResult ForEachProperty(const ForEachFunctor& aForEach) override
		{
			BSContainer::ForEachResult result = BSContainer::Continue;
			for (uint32_t i = 0; i < static_cast<uint32_t>(BSMaterialBinding::Bindings::Count) && result != BSContainer::Stop; i++)
			{
				const BSMaterialBinding::Bindings binding = static_cast<BSMaterialBinding::Bindings>(i);
				const char* pname = BSReflection::EnumToDisplayName(binding);

				const uint16_t maxSupportedLayers = BSMaterialBinding::GetBindingSupportedLayerCount(binding);
				if (maxSupportedLayers > 1)
				{
					for (uint16_t layerIndex = 0; layerIndex < maxSupportedLayers; layerIndex++)
					{
						if (GetLayer(LayeredMaterialID, layerIndex) != BSMaterial::NullIDC)
						{
							BSString name;
							name.SPrintF("%s [Layer %u]", pname, layerIndex + 1);
							result = aForEach(name.QString());
						}
					}
				}
				else
				{
					result = aForEach(pname);
				}
			}

			const BSScrapArray<BSMaterial::UVStreamID> uvStreams = CollectUVStreams(LayeredMaterialID);
			for (BSMaterial::UVStreamID streamID : uvStreams)
			{
				BSFixedString streamName;
				BSMaterial::GetName(BSMaterial::LayeredMaterialID{ streamID.QID() }, streamName);

				for (uint32_t i = 0; i < static_cast<uint32_t>(BSMaterialBinding::UVStreamBindingType::Count) && result != BSContainer::Stop; i++)
				{
					const BSMaterialBinding::UVStreamBindingType binding = static_cast<BSMaterialBinding::UVStreamBindingType>(i);
					const char* pname = BSReflection::EnumToDisplayName(binding);

					BSString propertyName;
					propertyName.SPrintF("%s [%s]", pname, streamName.QString());
					result = aForEach(propertyName.QString());
				}
			}

			return result;
		}

		/// <summary>
		/// Is our index value a valid binding?
		/// </summary>
		/// <param name="aIndex"> Our index value</param>
		/// <returns> If our index is valid </returns>
		bool IsIndexValid(uint32_t aIndex) const override
		{
			uint16_t layerCount = 0;
			for(uint16_t i = 0; i < BSMaterial::MaxLayerCountC; i++)
			{
				if (GetLayer(LayeredMaterialID, i) != BSMaterial::NullIDC)
				{
					layerCount++;
				}
			}

			uint16_t totalCount = 0;
			for (uint16_t i = 0; i < static_cast<uint32_t>(BSMaterialBinding::Bindings::Count); i++)
			{
				totalCount += std::min(BSMaterialBinding::GetBindingSupportedLayerCount(static_cast<BSMaterialBinding::Bindings>(i)), layerCount);
			}

			const BSScrapArray<BSMaterial::UVStreamID> uvStreams = CollectUVStreams(LayeredMaterialID);
			totalCount += static_cast<uint16_t>(uvStreams.QSize() * ToUnderlyingType(BSMaterialBinding::UVStreamBindingType::Count));
			return aIndex < totalCount;
		}

	private:
		const BSMaterial::LayeredMaterialID LayeredMaterialID;

	};


	/// <summary> Find all textures referenced by an object </summary>
	/// <param name="aObject"> Object to scan </param>
	/// <param name="arOutTextures"> OUT: Receives the depot path of textures referenced by aObject and its subobjects </param>
	/// <param name="aResolveSourceWithWildcards"> Resolved path will contain wildcards because it could be either a tga or a tif file </param>
	template<class TSet>
	void FindReferencedTextureFiles(BSComponentDB::ID aObject, TSet& arOutTextures, bool aResolveSourceWithWildcards = false)
	{
		using namespace BSReflection;
		struct TextureVisitor : public ConstVisitor
		{
			TSet& rTextures;
			bool ResolveSourceWithWildcards;
			TextureVisitor(TSet& arTextures, bool aResolveSourceWithWildcards) :
				rTextures(arTextures),
				ResolveSourceWithWildcards(aResolveSourceWithWildcards) {}

			Result Visit(const ObjectPtr& arObject) override
			{
				auto* pmrTexFile = arObject.TryExactCast<const BSMaterial::MRTextureFile*>();

				if (pmrTexFile != nullptr)
				{
					if (!pmrTexFile->FileName.QEmpty())
					{
						constexpr char pbaseFolderC[] = "Data\\Textures";
						const char* pstartOfBaseFolder = BSstristr(pmrTexFile->FileName, pbaseFolderC);

						// Ignore paths that do not start with Data\\Textures
						if (pstartOfBaseFolder != nullptr)
						{
							BSFilePathString sourceFile;
							if (SharedTools::ResolveSourceTextureReference(pmrTexFile->FileName, sourceFile, ResolveSourceWithWildcards))
							{
								rTextures.emplace(sourceFile.QString());
							}
						}
					}
				}
				return Continue;
			}
		};

		// Visit all components of all objects referenced by this material and discover the textures
		TextureVisitor visitor(arOutTextures, aResolveSourceWithWildcards);
		BSMaterial::Internal::QDBStorage().VisitComponents(visitor, aObject, true);
	}

	/// <summary>
	/// Get layer index for a given node.
	/// </summary>
	/// <param name="apModelNode">Model node index to search</param>
	/// <returns>Index of the layer this node lives on, or InvalidLayerIdx if we are on the root or the node given is invalid.</returns>
	uint16_t GetLayerIdxFromNode(const QtPropertyEditor::ModelNode& aModelNode)
	{
		uint16_t idx = BSMaterialBinding::InvalidLayerIdxC;

		//Find the first filter attribute above this node
		const QtPropertyEditor::ModelNode* playerNode = aModelNode.QParent();

		while (playerNode != nullptr && idx == BSMaterialBinding::InvalidLayerIdxC)
		{
			if (playerNode->QMetadata().Has<BSReflection::Metadata::MaterialLayerIndex>())
			{
				idx = playerNode->QMetadata().Find<BSReflection::Metadata::MaterialLayerIndex>()->Index;
			}

			playerNode = playerNode->QParent();
		}

		return idx;
	}

	/// <summary>
	/// Check if a node's path passes the filter for a given binding, empty filter counts as a pass.
	/// </summary>
	/// <param name="aBinding">Binding type to check filter for</param>
	/// <param name="apModelNode">Model Node to check</param>
	/// <returns>Pass or fail</returns>
	bool DoesNodePassBindingViewFilter(BSMaterialBinding::Bindings aBinding, const QtPropertyEditor::ModelNode& aModelNode)
	{
		using MaterialBindingFilter = BSReflection::Metadata::MaterialBindingFilter;

		bool passes = true;

		//Find the first filter attribute above this node
		const QtPropertyEditor::ModelNode* pfilterNode = aModelNode.QParent();
		MaterialBindingFilter filter = MaterialBindingFilter::None;

		while (pfilterNode != nullptr && filter == MaterialBindingFilter::None)
		{
			if (pfilterNode->QMetadata().Has<BSReflection::Metadata::MaterialBindingFilterAttribute>())
			{
				filter = pfilterNode->QMetadata().Find<BSReflection::Metadata::MaterialBindingFilterAttribute>()->Filter;
			}

			pfilterNode = pfilterNode->QParent();
		}

		if (filter != MaterialBindingFilter::None)
		{
			switch (aBinding)
			{
				//Intentional fallthrough
			case BSMaterialBinding::Bindings::UVScale:
			case BSMaterialBinding::Bindings::UVOffset:
				if (filter != MaterialBindingFilter::UVStream)
				{
					passes = false;
				}
				break;
				//Intentional fallthrough
			case BSMaterialBinding::Bindings::BlenderUVScale:
			case BSMaterialBinding::Bindings::BlenderUVOffset:
				if (filter != MaterialBindingFilter::BlendMaskUVStream)
				{
					passes = false;
				}
				break;

				//Intentional fallthrough
			case BSMaterialBinding::Bindings::OpacityBlenderUVScale:
			case BSMaterialBinding::Bindings::OpacityBlenderUVOffset:
				if (filter != MaterialBindingFilter::OpacityUVStream)
				{
					passes = false;
				}
				break;
			default:
				break;
			}
		}

		return passes;
	}
} // Anonymous

namespace SharedTools
{
	// HWND of this dialog
	HWND MaterialLayeringDialog::hwndDialog = 0;

	/// <summary>
	/// Material layering window Ctor
	/// </summary>
	/// <param name="parent"> Parent widget </param>
	/// <param name="arSite"> Site to bind our service to </param>
	MaterialLayeringDialog::MaterialLayeringDialog(QWidget *apParent, BSService::Site& arSite)
		: QDialog(apParent)
		, rSite(arSite)
		, UseVersionControl(bUseVersionControl.Bool())
	{
		if (!QtPropertyEditor::TemplateManager::QInstance().QHasLoaded())
		{
			QtPropertyEditor::TemplateManager::QInstance().LoadTemplates();
		}

		rSite.BindService(this);
		ui.setupUi(this);
		InitializeEditingComponents();
		InitializePreviewWidget();
		InitializeSignalsAndSlots();
		UpdateButtonState();
		CreationKit::Services::AssetHandlerService::QInstance().Register(this, BSMaterial::MatExt);
		pUndoRedoStack = new QUndoStack(this);

		PerforceSyncPath.Format("%s....mat", sLayeredMaterialDepotPath.String());
	}

	MaterialLayeringDialog::~MaterialLayeringDialog()
	{
		CreationKit::Services::AssetHandlerService::QInstance().Unregister(this);
		Close();
		rSite.UnbindService(this);
	}

	///<summary> Saves the state of the dialog's underlying window. </summary>
	void MaterialLayeringDialog::SaveWindowState()
	{
		QSettings settings;

		settings.beginGroup("MaterialLayeringDialog");		
		ui.pMaterialBrowserWidget->SaveGeometry(settings);
		settings.setValue("geometry", saveGeometry());
		settings.setValue(SplitterPreviewAndBrowserC, ui.splitterPreviewAndBrowser->saveState());
		settings.setValue(SplitterMainVerticalC, ui.splitterMiddleVertical->saveState());
		settings.endGroup();

		settings.beginGroup("MaterialLayeringPreviewWindow");
		settings.setValue("geometry", pFormPreviewDialog->saveGeometry());
		settings.endGroup();
	}

	///<summary> Loads the state of the dialog's underlying window. </summary>
	void MaterialLayeringDialog::LoadWindowState()
	{
		QSettings settings;

		settings.beginGroup("MaterialLayeringDialog");		
		ui.pMaterialBrowserWidget->RestoreGeometry(settings);
		CreationKitUtils::RestoreGeometry(settings.value("geometry").toByteArray(), *this);
		ui.splitterPreviewAndBrowser->restoreState(settings.value(SplitterPreviewAndBrowserC).toByteArray());
		ui.splitterMiddleVertical->restoreState(settings.value(SplitterMainVerticalC).toByteArray());
		settings.endGroup();

		settings.beginGroup("MaterialLayeringPreviewWindow");
		CreationKitUtils::RestoreGeometry(settings.value("geometry").toByteArray(), *pFormPreviewDialog);
		settings.endGroup();
	}

	///<summary> OVERRIDE: Handles QShowEvents when the dialog has show() called on it. </summary>
	///<param name="apEvent"> The event data. </param>
	void MaterialLayeringDialog::showEvent(QShowEvent* apEvent)
	{
		QDialog::showEvent(apEvent);
		hwndDialog = reinterpret_cast<HWND>(this->winId());

		LoadWindowState();

		if (!EditedMaterialID.QValid())
		{
			// Wait for the materials to be loaded
			SharedTools::CursorScope cursor(Qt::WaitCursor);
			BSMaterial::AwaitLoad();
			NewUntitledMaterial();
		}

		RefreshTimer.start(MaterialPreviewRefreshTimerC);

		// Offer to sync new files (queue this call so we can show the dialog first)
		QMetaObject::invokeMethod(this, [this]() { CheckForNewerFiles(); }, Qt::QueuedConnection);
	}

	///<summary> OVERRIDE: Handles QCloseEvents when the dialog has close() called on it. </summary>
	///<param name="apEvent"> The event data. </param>
	void MaterialLayeringDialog::closeEvent(QCloseEvent* apEvent)
	{
		if (isVisible() && PromptToSaveChanges())
		{
			hwndDialog = 0;
			RefreshTimer.stop();

			SaveWindowState();

			hide();
			pFormPreviewDialog->hide();
		}

		if (!Application.bPreviewOnly)
		{
			apEvent->ignore();
		}
	}

	///<summary> SLOT OVERRIDE: Handles dialog rejection signal. </summary>
	void MaterialLayeringDialog::reject()
	{
		if (isVisible())
		{
			SaveWindowState();
			pFormPreviewDialog->hide();
		}

		QDialog::reject();
	}

	///<summary> Determines whether the specified file is supported by the Material Layering Dialog. </summary>
	///<param name="apFilepath"> The file to test. </param>
	///<returns> True if the file can be loaded by the Material Layering Dialog. </returns>
	bool MaterialLayeringDialog::GetIsFileSupported(const char* apFilepath)
	{
		const BSResource::ID file(apFilepath);
		return (file.QExt() == BSMaterial::MatExt.QExt());
	}

	/// <summary> Open a layered material for editing </summary>
	/// <param name="aMaterial"> material to edit </param>
	/// <returns> true if the material can now be edited </returns>
	bool MaterialLayeringDialog::Open(BSMaterial::LayeredMaterialID aMaterial)
	{
		bool result = false;

		pUndoRedoStack->clear();

		EditedMaterialID = aMaterial;
		EditedSubMaterial = aMaterial;

		// Inform the Material browser which material is open

		if (EditedMaterialID.QValid())
		{
			SharedTools::CursorScope cursor(Qt::WaitCursor);

			UpdateLODCombo();
			
			// For perf reasons we make sure the Property Editor does not refresh while its populating/expanding
			ui.treeViewPropEditor->setUpdatesEnabled(false);

			ui.pMaterialBrowserWidget->SelectMaterial(EditedMaterialID);
			AdjustSceneForDecalPreview();

			BuildPropertyEditor();

			// When opening a new material file, expand all properties the first time.
			using namespace QtPropertyEditor;
			ui.treeViewPropEditor->ProcessDefaultState(QtGenericPropertyEditor::ItemState::Collapsed);
			ui.treeViewPropEditor->setUpdatesEnabled(true);

			result = true;
		}
		else
		{
			Close();
			BSWARNING(WARN_MATERIALS, "MaterialLayeringDialog::Open: The specified layered material is invalid.");
		}

		return result;
	}

	/// <summary>
	/// Sets up the preview scene for the optimal configuration for previewing a decal material.
	/// state for previewing decal materials.
	/// </summary>
	/// <param name="aForceOperation> When the user toggles between the detached preview and the embedded one, we make sure the new preview widget
	/// has the correct settings for the currently displayed material</param>
	void MaterialLayeringDialog::AdjustSceneForDecalPreview(bool aForceOperation /* = false */)
	{
		const bool isDecal = BSMaterial::GetLayeredMaterialDecalSettings(EditedMaterialID).IsDecal;
		PreviewWidget* ppreviewWidget = ui.pWidget_Preview->isVisible() ? ui.pWidget_Preview : pFormPreviewWidget;
		if (isDecal && (!PreviewingDecal || aForceOperation))
		{
			ppreviewWidget->SetGroundPlaneVisible(true);
			ppreviewWidget->PreviewObject(PreviewWidget::PreviewPrimitive::Quad);
			ppreviewWidget->SetAllowPrimitiveSelection(false);

			NiMatrix3 objRotation = NiMatrix3::IDENTITY;
			objRotation.MakeRotation(90.0f * DEG_TO_RAD, NiPoint3::UNIT_X);
			ppreviewWidget->SetPreviewObjectRotation(objRotation);

			// Point camera down
			ppreviewWidget->SetControlTarget( PreviewWidget::ControlTarget::Camera );
			const NiPoint3 cameraNormal = -NiPoint3::UNIT_Z;
			ppreviewWidget->SetCameraDirection(cameraNormal);
		}
		else if (!isDecal && (PreviewingDecal || aForceOperation))
		{
			ppreviewWidget->SetGroundPlaneVisible(false);
			ppreviewWidget->PreviewObject(PreviewWidget::PreviewPrimitive::Sphere);
			ppreviewWidget->SetAllowPrimitiveSelection(true);
			ppreviewWidget->SetControlTarget(PreviewWidget::ControlTarget::Object);
			ppreviewWidget->SetPreviewObjectRotation(NiMatrix3::IDENTITY);
		}

		PreviewingDecal = isDecal;
	}

	/// <summary> Initialize the contents of the PropertyEditor </summary>
	void MaterialLayeringDialog::BuildPropertyEditor()
	{
		// Clear assigned shared ptr processors.
		ui.treeViewPropEditor->QProcessors().clear();
		ui.treeViewPropEditor->QPostProcessors().clear();

		// Ensure all changes have been committed to the DB
		BSMaterial::Flush();

		// Only apply ShaderModel processors on User Materials. When editing a RootMaterial, we want to
		// have all the properties shown.
		const bool isRootMaterial = BSMaterial::IsShaderModelRootMaterial(EditedSubMaterial);

		// Get the shader model used by the Material if any and apply the rule processor automatically.
		const BSFixedString shaderModel = GetShaderModelName(EditedSubMaterial);
		if(!isRootMaterial && !shaderModel.QEmpty())
		{
			ApplyShaderModel(shaderModel.QString());
		}

		ui.treeViewPropEditor->QPostProcessors().emplace_back(
			std::shared_ptr<QtPropertyEditor::CustomUIProcessor>(
				new QtPropertyEditor::CustomUIProcessor(BSMaterial::LayerID::ReflectedType, std::bind(&MaterialLayeringDialog::UICustomProcessLayerNode, this, std::placeholders::_1))));

		ui.treeViewPropEditor->QPostProcessors().emplace_back(
			std::shared_ptr<QtPropertyEditor::CustomUIProcessor>(
				new QtPropertyEditor::CustomUIProcessor([this](QtPropertyEditor::ModelNode& arNode) { BuildIconsForBoundProperties( ui.treeViewPropEditor, arNode); })));

		// Create model for the property editor.
		BSReflection::AttributeMap attributes(BSReflection::Metadata::DBObjectDocument{ EditedMaterialID.QID().QValue() });
		ui.treeViewPropEditor->BeginAddObjects();
		ui.treeViewPropEditor->AddEditedObject(BSReflection::ObjectPtr(&EditedSubMaterial), nullptr, &attributes);
		ui.treeViewPropEditor->EndAddObjects(UIProcessorsActive);

		UpdateLODCombo();
		UpdateDocumentModified();
		UpdatePreview();
		UpdateMaterialShaderModelState();
		PropagateShaderModelState();
		UpdateButtonState();
	}

	/// <summary>
	/// Callback for the CustomUIProcessor added to the property editor. This specifically handles the UI processing for Layer nodes.
	/// </summary>
	/// <param name="arNode">The PropertyEditor node for LayerID objects.</param>
	void MaterialLayeringDialog::UICustomProcessLayerNode(QtPropertyEditor::ModelNode& arNode)
	{		
		BSMaterial::LayerID layerID;
		if (arNode.QModel() && arNode.GetNativeValue(BSReflection::Ptr(&layerID)))
		{
			if (layerID.QValid())
			{
				const BSMaterial::HideSoloData hsData = BSMaterial::GetHideSoloData(layerID);
				arNode.SetShowWarning(hsData.Hide);

				arNode.ForEach([&hsData](QtPropertyEditor::ModelNode& arChild)
				{
					arChild.SetShowWarning(hsData.Hide);
					return BSContainer::ForEachResult::Continue;
				});
			}
		}	
	}

	/// <summary> Update the contents and selection of the LOD combo box </summary>
	void MaterialLayeringDialog::UpdateLODCombo()
	{
		QSignalBlocker scope(ui.lodCombo);
		ui.lodCombo->clear();
		ui.lodCombo->addItem("High", QVariant(static_cast<int32_t>(BSMaterial::LevelOfDetail::High)));
		ui.lodCombo->setCurrentIndex(0);

		BSMaterial::Flush();
		const bool enabled = EditedMaterialID.QValid() && GetShaderModelUsesLevelOfDetail(GetShaderModelName(EditedMaterialID));
		ui.lodCombo->setEnabled(enabled);
		if (enabled)
		{	
			auto lodSettings = BSMaterial::GetLevelOfDetail(EditedMaterialID);
			for (int32_t i = 0; i < BSMIN(lodSettings.NumLODMaterials, BSMaterial::MaxNumLODMaterialsC); ++i)
			{
				BSMaterial::LevelOfDetail level = static_cast<BSMaterial::LevelOfDetail>(i);
				ui.lodCombo->addItem(BSReflection::EnumToDisplayName(level), QVariant(i));

				if (EditedSubMaterial == BSMaterial::GetLODMaterial(EditedMaterialID, level))
				{
					ui.lodCombo->setCurrentIndex(i+1);
				}
			}
			
			ui.lodCombo->insertSeparator(ui.lodCombo->count());
			ui.lodCombo->addItem("Edit...", QVariant(EditLODsDataC));
		}
	}
	
	/// <summary> SLOT: LOD combobox selection changed </summary>
	/// <param name="aIndex"> Newly selected item index </param>
	void MaterialLayeringDialog::OnLODChanged(int32_t aIndex)
	{
		using namespace QtPropertyEditor;

		auto previousSubMaterial = EditedSubMaterial;

		bool ok = false;
		int32_t value = ui.lodCombo->itemData(aIndex).toInt(&ok);
		if (ok)
		{
			BSMaterial::LevelOfDetailSettings settings = BSMaterial::GetLevelOfDetail(EditedMaterialID);
			if (value == EditLODsDataC)
			{	
				// Make sure every widget that needs the shader model state has that information.
				std::shared_ptr<CustomUIProcessor> spLODPropertyProcessor = std::make_unique<CustomUIProcessor>(
				[this](ModelNode& arNode)
				{	
					QWidget* pwidget = arNode.QPersistentWidget(ModelNode::Column::Value);
					if (pwidget)
					{
						IShaderModelStateConsumer* pconsumer = dynamic_cast<IShaderModelStateConsumer*>(pwidget);
						if (pconsumer != nullptr)
						{
							pconsumer->ProcessShaderModelState(MaterialSMState);
						}
					}			
				});

				UpdateMaterialShaderModelState();
				PropertyEditDialog* pdialog = new PropertyEditDialog(BSReflection::ConstPtr(&settings), this, nullptr, nullptr, spLODPropertyProcessor);
				pdialog->setMaximumSize(QSize(500, 250));
				pdialog->setWindowTitle("Level of Detail");

				connect(pdialog, &QDialog::accepted, this, [this, pdialog]
				{
					BSMaterial::SetLevelOfDetail(EditedMaterialID, pdialog->GetData<BSMaterial::LevelOfDetailSettings>());
					BSMaterial::Flush();
					BSMaterial::UpdateLODMaterials(EditedMaterialID, true);
					UpdateDocumentModified();
					OnRefreshPropertyEditor();
				});
				connect(pdialog, &QDialog::rejected, this, [this]
				{
					// Restore selection
					UpdateLODCombo();
				});
				// NOTE: This better than using exec() because the normal frame/update loop remains intact.
				pdialog->setAttribute(Qt::WA_DeleteOnClose);
				pdialog->setWindowModality(Qt::WindowModal);
				pdialog->show();
			}
			else
			{
				const BSMaterial::LevelOfDetail level = static_cast<BSMaterial::LevelOfDetail>(value);
				EditedSubMaterial = GetLODMaterial(EditedMaterialID, level);
			}

			if (!EditedSubMaterial.QValid())
			{
				QMessageBox::warning(this, pDialogTitleC, "Invalid LOD material found");
				EditedSubMaterial = EditedMaterialID;
			}

			if (previousSubMaterial != EditedSubMaterial)
			{
				OnRefreshPropertyEditor();
			}
		}
		else
		{
			// Not a valid item (separator)
			UpdateLODCombo();
		}
	}


	/// <summary>
	/// Sets icon paths for bound properties.
	/// </summary>
	/// <param name="apEditor">The PropertyEditor.</param>
	/// <param name="arNode">The PropertyEditor node for LayerID objects.</param>
	void MaterialLayeringDialog::BuildIconsForBoundProperties(QtPropertyEditor::QtGenericPropertyEditor* /*apEditor*/, QtPropertyEditor::ModelNode& arNode)
	{
		const BSReflection::Attributes& rattribs = arNode.QMetadata();

		if (rattribs.Has<BSReflection::Metadata::MaterialBinding>() || rattribs.Has<BSReflection::Metadata::UVStreamBinding>())
		{
			//If we don't have icon metadata, generate it
			if (arNode.QDecorationRoleIcon().isNull())
			{
				arNode.SetDecorationRoleIcon(BindablePropertyIconC);

				const uint16_t layerIdx = GetLayerIdxFromNode(arNode);

				BSFilePathString matPath;
				arNode.QModel()->GetFilename(matPath);
				const BSMaterial::LayeredMaterialID matID = BSMaterial::FindLayeredMaterialByFile(matPath.QString());

				if (matID.QValid())
				{
					const BSReflection::Metadata::UVStreamBinding* puvBindingAttrib = rattribs.Find<BSReflection::Metadata::UVStreamBinding>();

					if (puvBindingAttrib != nullptr)
					{
						for (const auto& possibleAttributeBinding : puvBindingAttrib->Bindings)
						{
							if (BSMaterialBinding::FindFirstUVBindableProperty(matID, possibleAttributeBinding, layerIdx) != nullptr)
							{
								arNode.SetDecorationRoleIcon(BoundPropertyIconC);
								break;
							}
						}
					}

					const BSReflection::Metadata::MaterialBinding* pmbattrib = rattribs.Find<BSReflection::Metadata::MaterialBinding>();

					if (pmbattrib != nullptr)
					{
						for (const auto& possibleAttributeBinding : pmbattrib->Bindings)
						{
							if (DoesNodePassBindingViewFilter(possibleAttributeBinding, arNode) && BSMaterialBinding::FindFirstBindableProperty(matID, possibleAttributeBinding, layerIdx) != nullptr)
							{
								arNode.SetDecorationRoleIcon(BoundPropertyIconC);
								break;
							}
						}
					}
				}
			}
		}
	}

	/// <summary> Propagate ShaderModel State for widgets to consume. Some state like the number of visible layers
	/// can only be calculated after applying UI processors, but widgets gets constructed before that.</summary>
	void MaterialLayeringDialog::PropagateShaderModelState()
	{
		LayerNameToNumkeyMap.clear();

		auto pnode = ui.treeViewPropEditor->QTreeNode();
		if (pnode)
		{
			using namespace SharedTools;
			using namespace QtPropertyEditor;

			uint32_t remainingLayersToBind = MaterialSMState.LayerCount;

			// Apply state where there is a consumer widget that wants it.
			pnode->ApplyRecursively(
				[this, &remainingLayersToBind](ModelNode& arNode)
			{
				for (auto columnId = ToUnderlyingType<ModelNode::Column>(ModelNode::Column::Name);
					columnId < ToUnderlyingType<ModelNode::Column>(ModelNode::Column::Count); columnId++)
				{
					QWidget* pwidgetAtColumn = arNode.QPersistentWidget(FromUnderlyingType<ModelNode::Column>(columnId));
					if (pwidgetAtColumn)
					{
						disconnect(pwidgetAtColumn);

						IShaderModelStateConsumer* pconsumer = dynamic_cast<IShaderModelStateConsumer*>(pwidgetAtColumn);
						if (pconsumer != nullptr)
						{
							pconsumer->ProcessShaderModelState(MaterialSMState);
						}
					}
				}

				if (arNode.QModel())
				{
					// Test for Layer
					BSMaterial::LayerID layerID;

					if (arNode.GetNativeValue(BSReflection::Ptr(&layerID)))
					{
						uint32_t numkey = remainingLayersToBind;

						if (numkey == BSMaterial::MaxLayerCountC)
						{
							numkey = 0; // ie Layer10 => zero 0 numkey
						}

						std::string layerName = std::string(arNode.QName());

						auto pair = std::make_pair(layerName, numkey);
						LayerNameToNumkeyMap.insert(pair);

						--remainingLayersToBind;
					}
				}

				InitializeMaterialLayerButtonsCallbacks(arNode);
			});
		}
	}

	/// <summary> Close the current layered material and free the associated model </summary>
	void MaterialLayeringDialog::Close()
	{
		if (EditedMaterialID.QValid())
		{
			if (!EditedFileExists())
			{
				// If the material was newly created(or the default, untitled one) and never saved
				// we can free all its associated objects
				BSMaterial::Internal::QDBStorage().RequestDestroyFileObjects(EditedMaterialID.QID());
			}

			ui.treeViewPropEditor->ClearPropertyEditor();
			EditedMaterialID = EditedSubMaterial = BSMaterial::LayeredMaterialID{};
			UpdateDocumentModified();
			// Make sure to flush the shader model state.
			MaterialSMState = { 0 };
			UpdateButtonState();
		}
	}

	/// <summary> Handler for Opening Material Assets </summary>
	/// <param name="apFileName">File to open</param>
	void MaterialLayeringDialog::OpenAsset(const char* apFileName)
	{
		auto matID = BSMaterial::FindLayeredMaterialByFile(apFileName);
		if (matID.QValid())
		{
			// Sanitize the full asset path as ResourceID compliant without the "Data\\" base folder.
			QString resourceid = QtFileNameToResourceID(apFileName);
			QString relativePath = resourceid.remove("Data\\", Qt::CaseInsensitive);
			ui.pMaterialBrowserWidget->RegisterRecentMaterial(relativePath);
		}
		show();
		raise();
		Open(matID);
	}

	/// ------------------------------------------------------------------------------------------
	/// <summary> Syncs the active state of the material picker button, should be called externally when the object reference picking occurred </summary>
	/// <param name="aActive"> New active state for the material picker button </param>
	/// ------------------------------------------------------------------------------------------
	void MaterialLayeringDialog::SetMaterialPickerActive( bool aActive )
	{
		ui.actionMaterialPicker->setChecked(aActive);
	}

	/// <summary> Connect the Qt signals & slots </summary>
	void MaterialLayeringDialog::InitializeSignalsAndSlots()
	{
		connect(pMaterialModel, &MaterialModelProxy::OnMaterialLayerDrop, this, &MaterialLayeringDialog::OnMaterialLayerDrop);

		connect( ui.actionMaterialPicker, &QAction::triggered, this, [this]( bool aChecked )
		{
			SetMaterialPickerActive( aChecked );
			emit MaterialPickerActivationChanged( aChecked );
		} );
		connect(ui.actionCreateNewMaterial,			&QAction::triggered, this, [this](){ MaterialLayeringDialog::CreateNew(""); });
		connect(ui.actionCreateNewShaderModel,		&QAction::triggered, this, [this]() { MaterialLayeringDialog::CreateNewShaderModel(); });
		connect(ui.actionSave,						&QAction::triggered, this, &MaterialLayeringDialog::Save);
		connect(ui.actionSaveAs,					&QAction::triggered, this, &MaterialLayeringDialog::SaveAs);
		connect(ui.actionReloadAllMaterialFiles,	&QAction::triggered, this, &MaterialLayeringDialog::ReloadAll);
		connect(ui.actionCheckOut,					&QAction::triggered, this, &MaterialLayeringDialog::CheckOut);
		connect(ui.actionCheckIn,					&QAction::triggered, this, &MaterialLayeringDialog::CheckInAll);
		connect(ui.actionRevertAllCheckedOutFiles,	&QAction::triggered, this, &MaterialLayeringDialog::RevertAll);
		connect(ui.actionToggleExperimentalShaders,	&QAction::triggered, this, &MaterialLayeringDialog::ToggleExperimentalModeShaders);
		connect(ui.actionDetachedPreviewWidget,		&QAction::triggered, this, [this]()
		{
			if (pFormPreviewDialog != nullptr)
			{
				const bool detachPreviewWidget = pFormPreviewDialog->isHidden();
				if (detachPreviewWidget)
				{
					ui.pWidget_Preview->hide();
					pFormPreviewDialog->show();
					pFormPreviewDialog->raise();
				}
				else
				{
					pFormPreviewDialog->hide();
					ui.pWidget_Preview->show();
				}

				AdjustSceneForDecalPreview();
				ui.actionDetachedPreviewWidget->setChecked(detachPreviewWidget);
			}
		});
		connect(ui.addLayerButton, &QPushButton::clicked, this, &MaterialLayeringDialog::OnAddLayer, Qt::QueuedConnection);
		connect(ui.removeLayerButton, &QPushButton::clicked, this, &MaterialLayeringDialog::OnRemoveLayer, Qt::QueuedConnection);
		connect(ui.syncTexturesButton, &QPushButton::clicked, this, &MaterialLayeringDialog::OnSyncTextures, Qt::QueuedConnection);
		connect(ui.switchShaderModelButton, &QPushButton::clicked, this, &MaterialLayeringDialog::OnSwitchEditedMaterialShaderModel, Qt::QueuedConnection);
		connect(ui.actionToggleControllers, &QAction::triggered, this, [this]()
		{
			EnableControllerVisualization = !EnableControllerVisualization;
			ui.actionToggleControllers->setChecked(EnableControllerVisualization);
			RefreshTimer.setInterval(EnableControllerVisualization ? UpdateTickC : MaterialPreviewRefreshTimerC);

			//Reset needed so that the edited material will be properly subscribed to the controller updated if it just had controllers added.
			ui.pWidget_Preview->ResetMaterials(EnableControllerVisualization);
			pFormPreviewWidget->ResetMaterials(EnableControllerVisualization);

			//This will re-apply our currently edited material
			UpdatePreview();
		});
		connect(ui.lodCombo, QOverload<int32_t>::of(&QComboBox::currentIndexChanged), this, &MaterialLayeringDialog::OnLODChanged);

		connect(ui.treeViewPropEditor, &QtPropertyEditor::QtGenericPropertyEditor::ForcedRefresh, this, &MaterialLayeringDialog::OnRefreshPropertyEditor);
		connect(ui.treeViewPropEditor, &QtPropertyEditor::QtGenericPropertyEditor::ChildPropertyChanging, this, &MaterialLayeringDialog::OnPropertyChanging);
		connect(ui.treeViewPropEditor, &QtPropertyEditor::QtGenericPropertyEditor::ChildPropertyChanged, this, &MaterialLayeringDialog::OnMaterialPropertyChanged);
		connect(ui.treeViewPropEditor, &QWidget::customContextMenuRequested, this, &MaterialLayeringDialog::OnPropertyContextMenuRequest);

		connect(&RefreshTimer, &QTimer::timeout, this, &MaterialLayeringDialog::RenderPreview);
		connect(this, &MaterialLayeringDialog::SyncTexturesFinished, this, &MaterialLayeringDialog::OnSyncTexturesFinished, Qt::QueuedConnection);
		connect(ui.pWidget_Preview, &PreviewWidget::PreviewObjectChanged, this, &MaterialLayeringDialog::UpdatePreview);

		connect(ui.pMaterialBrowserWidget, &MaterialBrowserWidget::RequestNewMaterial, this, &MaterialLayeringDialog::CreateNew);
		connect(ui.pMaterialBrowserWidget, &MaterialBrowserWidget::RequestNewDerivedMaterial, this, &MaterialLayeringDialog::CreateNewDerivedMaterial);
		connect(ui.pMaterialBrowserWidget, &MaterialBrowserWidget::RequestReparentMaterial, this, &MaterialLayeringDialog::OnReparentMaterial);
		connect(ui.pMaterialBrowserWidget, &MaterialBrowserWidget::RequestBreakInheritance, this, &MaterialLayeringDialog::OnRequestBreakInheritance);
		connect(ui.pMaterialBrowserWidget, &MaterialBrowserWidget::RequestMultipleReparentToMaterial, this, &MaterialLayeringDialog::OnRequestMultipleReparentToMaterial);
		connect(ui.pMaterialBrowserWidget, &MaterialBrowserWidget::RequestMaterialAutomatedSmallInheritance, this, &MaterialLayeringDialog::OnRequestMaterialAutomatedSmallInheritance);
		connect(ui.pMaterialBrowserWidget, &MaterialBrowserWidget::RequestExportBakedMaps, this, &MaterialLayeringDialog::ExportBakedMaps);
		connect(ui.pMaterialBrowserWidget, &MaterialBrowserWidget::MaterialPicked, this, &MaterialLayeringDialog::OnBrowserMaterialPicked);
		connect(ui.pMaterialBrowserWidget, &MaterialBrowserWidget::ShaderModelFileChanged, this, &MaterialLayeringDialog::OnShaderModelFileChanged);

		connect(ui.pMaterialBrowserWidget, &MaterialBrowserWidget::RequestDelete, this, [this](const QString& aFile) { Delete({ BSFixedString(QStringToCStr(aFile)) }); }, Qt::QueuedConnection);
		connect(ui.pMaterialBrowserWidget, &MaterialBrowserWidget::RequestMove, this, [this](const QString& aOldFile, const QString& aNewFile) { Move(BSFixedString(QStringToCStr(aOldFile)), BSFixedString(QStringToCStr(aNewFile))); }, Qt::QueuedConnection);
		connect(ui.pMaterialBrowserWidget, &MaterialBrowserWidget::RequestRename, this, [this](const QString& aFile) { Rename(BSFixedString(QStringToCStr(aFile))); }, Qt::QueuedConnection);
		connect(ui.pMaterialBrowserWidget, &MaterialBrowserWidget::RequestSync, this, [this](const QString& aFile) { Sync(QStringToCStr(aFile)); }, Qt::QueuedConnection);
		connect(ui.pMaterialBrowserWidget, &MaterialBrowserWidget::RequestRevert, this, [this](const QString& aFile) { Revert({ BSFixedString(QStringToCStr(aFile)) }); }, Qt::QueuedConnection);
		connect(ui.pMaterialBrowserWidget, &MaterialBrowserWidget::RequestCheckIn, this, [this](const QString& aFile) { CheckIn({ BSFixedString(QStringToCStr(aFile)) }); }, Qt::QueuedConnection);
		connect(ui.pMaterialBrowserWidget, &MaterialBrowserWidget::RequestCheckOutFile, this, [this](const QString& aFile) { CheckOutFile({ BSFixedString(QStringToCStr(aFile)) }); }, Qt::QueuedConnection);
		connect(ui.pMaterialBrowserWidget, &MaterialBrowserWidget::RequestFileMarkForAdd, this, [this](const QString& aFile) { FileMarkForAdd({ BSFixedString(QStringToCStr(aFile)) }); }, Qt::QueuedConnection);

		const bool enableSaveAll = bEnableMaterialSaveAll.Bool();
		ui.actionSaveAll->setVisible(enableSaveAll);
		if (enableSaveAll)
		{
			connect(ui.actionSaveAll, &QAction::triggered, this, &MaterialLayeringDialog::SaveAll);
		}

		QShortcut *prefreshShortcut = new QShortcut(QKeySequence("CTRL+F5"), this);
		connect(prefreshShortcut, &QShortcut::activated, this, &MaterialLayeringDialog::OnRefreshPropertyEditor);
		connect(prefreshShortcut, &QShortcut::activated, this, &MaterialLayeringDialog::OnRefreshPreviewBiomes);
		connect(prefreshShortcut, &QShortcut::activated, this, &MaterialLayeringDialog::UpdatePreview);

		QShortcut *pundoShortcut = new QShortcut(QKeySequence("CTRL+Z"), this);
		QShortcut *predoShortcut = new QShortcut(QKeySequence("CTRL+Y"), this);
		connect(pundoShortcut, &QShortcut::activated, this, &MaterialLayeringDialog::Undo);
		connect(predoShortcut, &QShortcut::activated, this, &MaterialLayeringDialog::Redo);

		QShortcut *paddNewLayerShortcut = new QShortcut(QKeySequence("CTRL+A"), this);
		connect(paddNewLayerShortcut, &QShortcut::activated, this, &MaterialLayeringDialog::OnAddLayer);
	}

	/// <summary>
	/// Initialize callbacks for material layer buttons to ensure that only one Solo button
	/// is pressed at a time and that the view updates accordingly if a hide/solo button is pressed.
	/// </summary>
	/// <param name="arModelNode"> Model node to process </param>
	void MaterialLayeringDialog::InitializeMaterialLayerButtonsCallbacks(QtPropertyEditor::ModelNode& arModelNode)
	{
		QtPropertyEditor::MaterialLayerButtonsWidget* pWidget = BSBetaDyncast<QtPropertyEditor::MaterialLayerButtonsWidget*>(arModelNode.QPersistentWidget(QtPropertyEditor::ModelNode::Column::Name));

		if (pWidget != nullptr)
		{
			connect(this, &MaterialLayeringDialog::SoloViewLayer, pWidget, &QtPropertyEditor::MaterialLayerButtonsWidget::OnSoloViewLayer);
			connect(pWidget, &QtPropertyEditor::MaterialLayerButtonsWidget::HideClicked,
				[this](bool /*aPressed*/)
				{
					BSMaterial::Flush();
					OnMaterialPropertyChanged();
					OnRefreshPropertyEditor();
				});

			connect(pWidget, &QtPropertyEditor::MaterialLayerButtonsWidget::SoloClicked,
				[this](QWidget* apSender, bool aPressed)
				{
					emit SoloViewLayer(apSender, aPressed);
					BSMaterial::Flush();
					OnMaterialPropertyChanged();
					OnRefreshPropertyEditor();
				});

			BSMaterial::LayerID layerID;
			
			if (arModelNode.GetNativeValue(BSReflection::Ptr(&layerID)))
			{
				auto iter = LayerNameToNumkeyMap.find(arModelNode.QName());

				if (iter != LayerNameToNumkeyMap.end())
				{
					//ALT+Numkey => navigate to layer widget
					const QString altNumkeyString = QString("ALT+%1").arg(iter->second);
					const uint32_t rowIndex = arModelNode.QRow();

					QShortcut* pnavigateToLayerShortcut = new QShortcut(QKeySequence(altNumkeyString), pWidget);

					connect(pnavigateToLayerShortcut, &QShortcut::activated, pWidget,
						[this, rowIndex]()
						{
							const QModelIndex modelIndex = ui.treeViewPropEditor->model()->index(rowIndex, 0);

							QItemSelectionModel* pselection = ui.treeViewPropEditor->selectionModel();
							pselection->select(modelIndex, QItemSelectionModel::Select);

							ui.treeViewPropEditor->scrollTo(modelIndex);
							ui.treeViewPropEditor->expand(modelIndex);
						});
				}
		}
	}
	}


	/// <summary>
	/// Initialize Dynamic components such as property editor.
	/// </summary>
	void MaterialLayeringDialog::InitializeEditingComponents()
	{
		// Toolbar
		QToolBar* ptoolbar = new QToolBar(this);
		ptoolbar->addAction(ui.actionMaterialPicker);
		ptoolbar->addAction(ui.actionDetachedPreviewWidget);
		ptoolbar->addSeparator();
		ptoolbar->addAction(ui.actionCreateNewMaterial);
		ptoolbar->addAction(ui.actionCreateNewShaderModel);
		ptoolbar->addAction(ui.actionSave);
		ptoolbar->addAction(ui.actionReloadAllMaterialFiles);
		ptoolbar->addSeparator();
		ptoolbar->addAction(ui.actionCheckOut);
		ptoolbar->addAction(ui.actionCheckIn);
		ptoolbar->addAction(ui.actionRevertAllCheckedOutFiles);
		ptoolbar->addSeparator();
		ptoolbar->addAction(ui.actionToggleControllers);
		ptoolbar->addAction(ui.actionToggleExperimentalShaders);
		ptoolbar->addSeparator();
		ptoolbar->addAction(ui.actionOpenMaterialBakeOptions);
		layout()->setMenuBar(ptoolbar);

		// Make the toolbar stand out a bit
		ptoolbar->setStyleSheet( "QToolBar { border-bottom: 1px solid #0E0E0E; }" );

		// Setup our save toolbar action as a dropdown button with options for more specific saves (Save as, Save All):
		QToolButton* saveButton = qobject_cast<QToolButton*>(ptoolbar->widgetForAction(ui.actionSave));
		BSASSERT( saveButton != nullptr, "Material Layering Editor dialog's toolbar could not retrive a widget for the save action!" );
		saveButton->setPopupMode( QToolButton::InstantPopup );
		saveButton->addAction(ui.actionSaveAs);
		saveButton->addAction(ui.actionSaveAll);

		ui.actionToggleControllers->setChecked(EnableControllerVisualization);

		// Property editor
		ui.treeViewPropEditor->setUpdatesEnabled(false);
		pMaterialModel = new MaterialModelProxy(this);
		ui.treeViewPropEditor->SetModelProxy(pMaterialModel);
		ui.treeViewPropEditor->setContextMenuPolicy(Qt::CustomContextMenu);
		ui.treeViewPropEditor->setAcceptDrops(true);
		ui.treeViewPropEditor->setDragDropMode(QAbstractItemView::DropOnly);
		ui.treeViewPropEditor->setDropIndicatorShown(true);
		ui.treeViewPropEditor->setUpdatesEnabled(true);

		// Property editor contextual menu
		pPropertyContextMenu = new QMenu(ui.treeViewPropEditor);
		pPropertyContextMenu->addAction(ui.actionSwitchShaderModel);
		pPropertyContextMenu->addAction(ui.actionSet_to_Default);
		pPropertyContextMenu->addAction(ui.actionOpen_Parent_File);
		pPropertyContextMenu->addAction(ui.actionPublish);

		//Setup baking options
		pBakeOptionsDialog = new MaterialLayeringBakeOptionsDialog(this);

		connect(ui.actionOpenMaterialBakeOptions, &QAction::triggered, [this](bool /*aChecked*/)
		{
			if (pBakeOptionsDialog->isHidden())
			{
				pBakeOptionsDialog->show();
			}
			pBakeOptionsDialog->raise();
		});
	}

	/// <summary> Initializes the PreviewWidget. </summary>
	void MaterialLayeringDialog::InitializePreviewWidget()
	{
		// Create our detached preview window
		pFormPreviewDialog = new FormPreviewWidgetDialog(this, ui.actionDetachedPreviewWidget);
		pFormPreviewDialog->setModal(false);
		pFormPreviewDialog->setWindowTitle("Material Preview");

		QBoxLayout* playout = new QBoxLayout(QBoxLayout::Direction::Down, pFormPreviewDialog);
		playout->setSizeConstraint(QLayout::SizeConstraint::SetMinimumSize);

		pFormPreviewWidget = new PreviewWidget(pFormPreviewDialog, "MaterialLayeringDialog");
		pFormPreviewWidget->setObjectName(QString::fromUtf8("pFormPreviewWidget"));
		pFormPreviewWidget->setMinimumSize(QSize(0, 300));
		pFormPreviewWidget->PreviewObject(PreviewWidget::PreviewPrimitive::Sphere);
		pFormPreviewWidget->SetAllowObjectWindowModelDrop(true);
		connect(pFormPreviewWidget, &PreviewWidget::PreviewObjectChanged, this, &MaterialLayeringDialog::UpdatePreview);

		playout->addWidget(pFormPreviewWidget);

		FileSelectorWidget* pfileSelector = new FileSelectorWidget(pFormPreviewDialog);
		pfileSelector->InitForFile("Bethesda Art File (*.nif)", "Data\\Meshes");
		connect(pfileSelector, &FileSelectorWidget::FileChanged, this, &MaterialLayeringDialog::OnPreviewFileChanged);
		playout->addWidget(pfileSelector);

		// If we have saved a previous mesh file, reload it here.
		BSFixedString previousPreviewMeshFile(sRecentPreviewMeshFile.String());
		if (!previousPreviewMeshFile.QEmpty())
		{
			pFormPreviewWidget->PreviewObject(previousPreviewMeshFile);
		}

		pFormPreviewDialog->setLayout(playout);
		pFormPreviewWidget->LoadSettings();

		ui.pWidget_Preview->SetSaveContext("MaterialLayeringDialog");
		ui.pWidget_Preview->PreviewObject(PreviewWidget::PreviewPrimitive::Sphere);
		ui.pWidget_Preview->SetAllowPrimitiveSelection(true);
		ui.pWidget_Preview->SetAllowObjectWindowModelDrop(true);

		pFormPreviewDialog->hide();
	}

	/// <summary>
	/// Apply a shader model to the current property editor.
	/// </summary>
	/// <param name="apShaderModel">The shader model name to apply</param>
	void MaterialLayeringDialog::ApplyShaderModel(const char* apShaderModel)
	{
		using namespace QtPropertyEditor;
		// Try to find the rule processor.
		std::shared_ptr<RuleProcessor> processorToApply = SharedTools::GetShaderModelRuleProcessor(BSFixedString(apShaderModel));
		BSWARNING_IF(processorToApply == nullptr, WARN_DEFAULT, "Cannot assign Invalid shader model (%s) to Property Editor.", apShaderModel);
		if (processorToApply)
		{
			ui.treeViewPropEditor->QProcessors().emplace_back(processorToApply);
		}
	}

	/// <summary> SLOT: Updates the preview object and renders it. </summary>
	void MaterialLayeringDialog::UpdatePreview()
	{		
		// Apply the layered material being edited to the preview sphere
		if(EditedSubMaterial.QValid())
		{
			ui.pWidget_Preview->ApplyLayeredMaterialToGeometry(BSMaterial::LayeredMaterialID(), EditedSubMaterial, EnableControllerVisualization);
			pFormPreviewWidget->ApplyLayeredMaterialToGeometry(BSMaterial::LayeredMaterialID(), EditedSubMaterial, EnableControllerVisualization);
		}

		RenderPreview();
    }

	/// <summary> SLOT: Updates the preview widget </summary>
	void MaterialLayeringDialog::RenderPreview()
	{
		// Start loading new loose texture files from disk
		BSResourceReloadManager::QInstance().Update();

		ui.pWidget_Preview->UpdateImage(UpdateTickC);
		pFormPreviewWidget->UpdateImage(UpdateTickC);
	}

	/// <summary> SLOT: Called when the user drop a base material on a layer in the editor to set the browser next focused item state. </summary>
	/// <param name="aMaterialId"> The Material Id that was dropped on a property from the Material browser </param>
	void MaterialLayeringDialog::OnMaterialLayerDrop(const BSMaterial::LayeredMaterialID aMaterialId)
	{
		FocusedMaterialID = aMaterialId;
	}

	/// <summary> SLOT: Called when the users right clicks in the property grid </summary>
	/// <param name="aPoint"> Point in global space where the user right clicked </param>
	void MaterialLayeringDialog::OnPropertyContextMenuRequest(const QPoint& aPoint)
	{
		QModelIndex index = ui.treeViewPropEditor->indexAt(aPoint);
		auto* ppropertyNode = pMaterialModel->GetModelNode(index);

		if (ppropertyNode != nullptr)
		{
			// Disallow reverting object IDs to default
			BSComponentDB2::ID object;
			const bool isObject = ppropertyNode->Get(object);

			ui.actionSet_to_Default->setEnabled(ppropertyNode->QDifferentFromParent() && !isObject);
			ui.actionPublish->setEnabled(true);

			bool hasParentFile = false;
			QString openParentFileText = "Open Parent File";
			if (ppropertyNode->QHasDataParent())
			{
				BSFilePathString parentFile;
				if (ppropertyNode->QDataParent()->GetFilename(parentFile))
				{
					hasParentFile = true;
					openParentFileText.append(QString("(%1)").arg(parentFile.QString()));
				}
			}
			ui.actionOpen_Parent_File->setText(openParentFileText);
			ui.actionOpen_Parent_File->setEnabled(hasParentFile);

			QAction* paction = pPropertyContextMenu->exec(ui.treeViewPropEditor->mapToGlobal(aPoint));
			if (paction == ui.actionSet_to_Default)
			{
				// At this point the widget has stale data
				if (QWidget* pwidget = ppropertyNode->QVolatile() ? nullptr : ppropertyNode->QPersistentWidget();
					pwidget != nullptr)
				{
					SharedTools::EditorWidgetForceRefresh(pwidget, true);
				}
				pMaterialModel->setData(index, ppropertyNode->GetParentValue());
			}
			else if (paction == ui.actionOpen_Parent_File)
			{
				BSFilePathString file;
				if (ppropertyNode->QDataParent()->GetFilename(file) && PromptToSaveChanges())
				{
					Open(BSMaterial::LayeredMaterialID(BSMaterial::Internal::QDBStorage().GetObjectByFilename(file.QString())));
				}
			}
			else if (paction == ui.actionPublish)
			{
				BSMaterial::Internal::QDBStorage().RequestClaimTransientObjects(EditedMaterialID.QID());
				BSMaterial::Flush();
				// Destroyed on close.
				QtBoundPropertyDialog* pdialog = new QtBoundPropertyDialog(this, EditedMaterialID, stl::make_unique<MaterialPropertySelectModel>(EditedMaterialID), true);
				pdialog->setAttribute(Qt::WA_DeleteOnClose);
				connect(pdialog, &QDialog::accepted, this, &MaterialLayeringDialog::OnMaterialPropertyChanged);
				connect(pdialog, &QDialog::accepted, this, &MaterialLayeringDialog::OnRefreshPropertyEditor);
				connect(pdialog, &QtBoundPropertyDialog::ControllerRefreshed, this, &MaterialLayeringDialog::OnMaterialPropertyControllerRefreshed);
				pdialog->show();
			}
			else if (paction == ui.actionSwitchShaderModel)
			{
				OnSwitchEditedMaterialShaderModel();
			}
		}
	}

	/// <summary> SLOT: Called when the "Add Layer" button is pressed </summary>
	void MaterialLayeringDialog::OnAddLayer()
	{
		if (CanAddLayer())
		{
			// The approach here is to create the new layer immediately, and then make a backup of it. Any
			// subsequent Redo operation will then restore that backup data.
			CursorScope cursor(Qt::WaitCursor);
			if (BSMaterial::AddNewLayer(EditedMaterialID))
			{
				BSMaterial::Flush();

				UndoCallback execute = [this](void* apData) { RestoreMaterialBackup(apData); };
				UndoCallback revert = [this](void* apData) { RemoveLastLayer(apData); };

				// Create a backup with the newly-added layer
				Json::Value* pmaterialBackup = CreateMaterialBackup();
				MakeNewUndoCommand(std::move(revert), std::move(execute), pmaterialBackup);
			}
		}
	}

	/// <summary> SLOT: Called when the "Remove Last Layer" button is pressed </summary>
	void MaterialLayeringDialog::OnRemoveLayer()
	{
		UndoCallback execute = [this](void* apData) { RemoveLastLayer(apData); };
		UndoCallback revert = [this](void* apData) { RestoreMaterialBackup(apData); };

		Json::Value* pmaterialBackup = CreateMaterialBackup();
		MakeNewUndoCommand(std::move(revert), std::move(execute), pmaterialBackup);
	}

	/// <summary> Saves the current state of the current material </summary>
	/// <returns> A Json object suitable for passing to RestoreMaterialBackup() to restore the material's settings at a future time </returns>
	Json::Value* MaterialLayeringDialog::CreateMaterialBackup()
	{
		Json::Value* pmaterialBackup = new Json::Value();
		BSMaterial::Internal::QDB2Instance().RequestExecuteForCreateAndDelete([this, pmaterialBackup](BSComponentDB2::CreateAndDeleteInterface& arInterface)
		{
			BSMaterial::Internal::QDBStorage().SaveJson(arInterface, EditedMaterialID, *pmaterialBackup);
		});

		return pmaterialBackup;
	}

	/// <summary> Remove the last layer on the Material Layer stack </summary>
	void MaterialLayeringDialog::RemoveLastLayer(void* /*apData*/)
	{
		CursorScope cursor(Qt::WaitCursor);
		if (BSMaterial::RemoveLastLayer(EditedMaterialID))
		{
			OnMaterialPropertyChanged();
			OnRefreshPropertyEditor();
		}
	}

	/// <summary> Restore a material's settings to those captured with an earlier call to CreateMaterialBackup() </summary>
	/// <param name="apData"> A Json object containing the settings to apply to the material. This object is typically created by calling CreateMaterialBackup() </param>
	void MaterialLayeringDialog::RestoreMaterialBackup(void* apData)
	{
		const Json::Value *pmaterialData = static_cast<Json::Value*>(apData);
		BSASSERT(pmaterialData != nullptr, "pmaterialData was unexecpectedly null");
		if (pmaterialData != nullptr)
		{
			CursorScope cursor(Qt::WaitCursor);

			BSMaterial::Internal::QDB2Instance().RequestExecuteForCreateAndDelete([this, pmaterialData](BSComponentDB2::CreateAndDeleteInterface& arInterface)
			{
				BSMaterial::Internal::QDBStorage().LoadJson(arInterface, EditedMaterialID, *pmaterialData);
			});

			OnMaterialPropertyChanged();
			OnRefreshPropertyEditor();
		}
	}

	/// <summary> SLOT: Called when the "Sync Textures" button is pressed </summary>
	void MaterialLayeringDialog::OnSyncTextures()
	{
		stl::scrap_set<BSFixedString> referencedTextures;
		FindReferencedTextureFiles(EditedMaterialID.QID(), referencedTextures, true);

		// Indicate that the P4 sync is in progress
		ui.syncTexturesButton->setDisabled(true);

		if (pBakeOptionsDialog->ShouldSyncMapsOnTexSync())
		{
			TextureNameArray bakedMaps;
			//Collect baked maps to sync
			AddMaterialSnapshotsToFileList(EditedMaterialID.QID(), *pBakeOptionsDialog, false, bakedMaps);

			for (const BSFixedString& map : bakedMaps)
			{
				referencedTextures.emplace(map);
			}
		}

		// Launch a job to sync the files in the background
		// Once cooked by the AbyssWatcher, we should automatically load these textures as loose files
		BSJobs::GetBackgroundJobs2ThreadGroup()->Submit([textures = std::move(referencedTextures), this]()
		{
			BSPerforce::ConnectionSmartPtr spperforce;
			CSPerforce::Perforce::QInstance().QPerforce(spperforce);

			for (const BSFixedString& rfile : textures)
			{
				spperforce->SyncFile(rfile.QString());
			}
			// Since this is on another thread we may not interact with UI elements directly
			// Use a signal/slot to safely let the dialog know the sync finished
			emit SyncTexturesFinished();
		});
	}

	/// <summary> SLOT: Called when user wants to Switch EditedMaterial Shader Model. </summary>
	void MaterialLayeringDialog::OnSwitchEditedMaterialShaderModel()
	{
		// Use currently edited LOD material or main material for the switch.
		const auto materialToSwitch = EditedMaterialID != EditedSubMaterial ? EditedSubMaterial : EditedMaterialID;

		// Get the ShaderModel for this material. Check if material shader model migration from one Shader Model to another is permitted.
		BSMaterial::ShaderModelComponent smComponent = BSMaterial::GetLayeredMaterialShaderModel(materialToSwitch);
		const bool switchable = SharedTools::GetShaderModelSwitchable(smComponent.FileName);

		if (switchable)
		{
			SwitchMaterialToShaderModel(materialToSwitch);
		}
		else
		{
			QString warningText = QString("You cannot switch this material to use another shader model.\nShader Model : %1 is explicitly locked out from switching into something else.").arg(smComponent.FileName.QString());
			QMessageBox::warning(this, pDialogTitleC, warningText);
		}
	}

	/// <summary> SLOT: Called when the textures sync job finishes </summary>
	void MaterialLayeringDialog::OnSyncTexturesFinished()
	{
		ui.syncTexturesButton->setDisabled(false);

		// Refresh to let the newly synced textures show up (in the texture widget preview)
		OnRefreshPropertyEditor();
	}

	/// <summary> Checks out the currently edited material and all sub-assets in Perforce </summary>
	/// <param name="aVerbose"> if true, will show popups in harmless edge cases (nothing to checkout etc) </param>
	/// <param name="apOutAllCheckedOut"> OPTIONAL OUT: Set to true if all files were checked out, false if some files could not be checked out </param>
	/// <returns> The set of currently checked out files </returns>
	BSTArray<BSFixedString> MaterialLayeringDialog::CheckoutCurrentFiles(bool aVerbose, bool* apOutAllCheckedOut)
	{
		CursorScope cursor(Qt::WaitCursor);

		if (apOutAllCheckedOut != nullptr)
		{
			apOutAllCheckedOut = false;
		}

		BSTArray<BSFixedString> filesCheckedOut = GetCheckedOutFiles(this, PerforceSyncPath.QString());
		BSScrapArray<BSFilePathString> referencedFiles;
		if (EditedMaterialID.QValid() && BSMaterial::Internal::QDBStorage().GatherReferencedFiles(EditedMaterialID, referencedFiles))
		{
			// Convert referencedFiles array
			BSTArray<BSFixedString> filesToCheckout(referencedFiles.QSize());
			for (BSFilePathString &rfile : referencedFiles)
			{
				auto file = SharedTools::MakePerforcePath(rfile.QString());
				if (Find(filesCheckedOut, file) == FINDRESULT_NOT_FOUND)
				{
					filesToCheckout.Add(std::move(file));
				}
			}

			// Also add any files that were previously modified
			BSScrapArray<BSComponentDB2::ID> modifiedObjects;
			BSMaterial::Internal::QDBStorage().GetAllModifiedFiles(modifiedObjects);
			for (BSComponentDB2::ID dirtyObject : modifiedObjects)
			{
				BSFilePathString relFile;
				if (BSMaterial::Internal::QDBStorage().GetObjectFilename(dirtyObject, relFile))
				{
					auto absFile = SharedTools::MakePerforcePath(relFile.QString());
					if (Find(filesCheckedOut, absFile) == FINDRESULT_NOT_FOUND)
					{
						filesToCheckout.Add(std::move(absFile));
					}
				}
				else
				{
					BSWARNING(WARN_SYSTEM, "Expected an associated file for modified object %u", dirtyObject.QValue());
				}
			}

			if (filesToCheckout.QSize() == 0)
			{
				if (aVerbose)
				{
					QMessageBox::information(this, pDialogTitleC, QString::asprintf("All %u file(s) already checked out", referencedFiles.QSize()));
				}
			}
			else
			{
				const uint32_t changelistNumber = FindOrCreateChangelist(sMaterialDefaultChangeListDesc.String());
				if (SharedTools::CheckoutFiles(this, pDialogTitleC, filesToCheckout, SharedTools::CheckOutFailedOption::TryAdd, SharedTools::VerbosityOption::Quiet, changelistNumber) || !UseVersionControl)
				{
					filesToCheckout.AppendTo(filesCheckedOut);

					if (apOutAllCheckedOut != nullptr)
					{
						*apOutAllCheckedOut = true;
					}
				}
			}
		}
		else
		{
			if (aVerbose)
			{
				QMessageBox::warning(this, pDialogTitleC, "Can't find any files to check out");
			}
		}

		ui.pMaterialBrowserWidget->Refresh();

		return filesCheckedOut;
	}

	/// <summary> SLOT: Creates a new material and switches the active document to it </summary>
	/// <param name="aName"> Name of the new material. Assumed to have been validated already </param>
	/// <param name="aParentMaterial"> Parent material </param>
	void MaterialLayeringDialog::CreateNewMaterial(const BSFixedString& aName, BSMaterial::LayeredMaterialID aParentMaterial)
	{
		BSASSERTFAST(!aName.QEmpty() && aParentMaterial.QValid());

		// Create it and save it so we can add it to Perforce
		auto newMaterial = BSMaterial::CreateLayeredMaterialInstance(aParentMaterial, aName);

		// Rename any inherited sub objects, we must flush to ensure all pending creates are executed
		BSMaterial::Flush();
		BSMaterial::RenameAll(newMaterial, aName);

		Open(newMaterial);
	}

	/// <summary> SLOT: Handles creation of a new Shader Model rule template and its associated root material. </summary>
	void MaterialLayeringDialog::CreateNewShaderModel()
	{
		BSFixedString newShaderModelName;
		BSFixedString newShaderModelFileName;
		BSMaterial::LayeredMaterialID newRootMaterialID(BSMaterial::NullIDC);
		if (SharedTools::CreateNewShaderModel(this, newShaderModelName, newShaderModelFileName, newRootMaterialID))
		{
			Open(newRootMaterialID);

			// Force immediate save dialog for the new root layered material.
			SaveAs();

			// Get the root material name in case user changed it with a save as dialog.
			BSFixedString rootMaterialName;
			BSMaterial::GetName(newRootMaterialID, rootMaterialName);

			SharedTools::SetShaderModelRootMaterial(newShaderModelName, rootMaterialName);

			// Commit possible late changes to file such as rootMaterial name change.
			SharedTools::SaveShaderModelToFile(newShaderModelFileName);
		}
	}

	/// --------------------------------------------------------------------------------
	/// <summary>
	/// SLOT: Migrate over properties that are visible in the destination Shader Model for the requested Material.
	/// This is a destructive process as we will delete/default value non visible properties.
	/// </summary>
	/// <param name="aMaterialToProcess"> Material ID to process </param>
	/// --------------------------------------------------------------------------------
	void MaterialLayeringDialog::SwitchMaterialToShaderModel(BSMaterial::LayeredMaterialID aMaterialToProcess)
	{
		BSTArray<BSMaterial::LayeredMaterialID> affectedMaterials;
		BSString message("Are you sure you want to switch the Material Shader Model ?\nSome settings may not carry over to a different shader model.\nThis change cannot be undone.");
		if (BSMaterial::GetHasDataChildren(aMaterialToProcess))
		{
			// Mark all derived material as dirty
			BSMaterial::Internal::QDB2Instance().ExecuteForRead([&](const auto& aInterface)
			{
				BSComponentDB2::TraverseDataChildren(aInterface, aMaterialToProcess, [&](const auto& /*aInterface*/,
					BSComponentDB2::ID /*aFrom*/,
					BSComponentDB2::ID aObject)
				{
					affectedMaterials.Add(aObject);
					return BSContainer::Continue;
				});
			});
			message += BSFilePathString().Format("\nIMPORTANT: This will affect %u child materials as well, and you must take care to submit these in the same changelist.\n", affectedMaterials.QSize());
		}

		bool ok = QMessageBox::warning(this, pDialogTitleC, message.QString(), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;		
		if (ok)
		{
			// Mark all materials as changed, so they will be checked out
			affectedMaterials.Add(EditedMaterialID);
			for (BSMaterial::ID material : affectedMaterials)
			{
				BSMaterial::Internal::QDBStorage().NotifyObjectModified(material);
			}

			CheckoutCurrentFiles(true, &ok);
			if (!ok)
			{
				QMessageBox::warning(this, pDialogTitleC, "Not all files could be checked out.\nWe can't switch the shader model unless all materials involved are checked out.");
			}
		}

		// Guard against changing shader model for material that have full inheritance active.
		if(ok)
		{
			BSFixedString materialName;
			BSMaterial::GetName(aMaterialToProcess, materialName);
			constexpr bool canEditNameC = false;
			SharedTools::CreateNewFromHierarchyDialog*	pdialog = new SharedTools::CreateNewFromHierarchyDialog(this,
				QString("Switch %1 Shader Model").arg(materialName.QString()), "Select new Shader Model to switch to :", "Material Name", materialName.QString(), canEditNameC);
			pdialog->setAttribute(Qt::WA_DeleteOnClose);
			pdialog->setWindowModality(Qt::WindowModal);
			pdialog->setModal(true);
			// local name validation lambda : we set the name so its always good to go.
			pdialog->SetNameValidationFunctor([](const QString&, QString&)->bool {
				return true;
			});
			// local tree selection validation lambda to prevent Shader Model Root Material node being selected.
			pdialog->SetTreeItemSelectionValidationFunctor([this](QTreeWidgetItem* apCurrent, QTreeWidgetItem*) -> bool
			{
				bool valid = false;
				if (apCurrent)
				{
					valid = apCurrent->data(0, CustomRoles::MaterialID) != BSMaterial::Internal::QRootLayeredMaterialsID().QValue();
				}
				return valid;
			});
			// local tree hierarchy filling lambda to get Shader Model Root Materials.
			pdialog->SetPopulateFunctor([this](QTreeWidget* apTreeWidget)
			{
				FillMaterialHierarchy(apTreeWidget, QString(pNewMaterialRootNameC), EditedMaterialID, false, true);
			});
			// On Accept Button
			connect(pdialog, &QDialog::accepted, this, [this, pdialog, aMaterialToProcess, materials = std::move(affectedMaterials)]()
			{
				SharedTools::CursorScope cursor(Qt::WaitCursor);

				QTreeWidget* pWidget = pdialog->QTreeWidget();
				BSASSERTFAST(pWidget);
				const uint32_t rootMaterialId = pWidget->currentItem()->data(0, CustomRoles::MaterialID).toUInt();
				BSMaterial::LayeredMaterialID shaderModelRootMaterial(BSComponentDB2::NumericIDToID(rootMaterialId));
					
				BSFixedString srcMaterialName;
				BSFixedString destMaterialName;
				BSMaterial::GetName(aMaterialToProcess, srcMaterialName);
				BSMaterial::GetName(shaderModelRootMaterial, destMaterialName);
				BSMaterial::ShaderModelComponent currentSM = BSMaterial::GetLayeredMaterialShaderModel(aMaterialToProcess);
				BSMaterial::ShaderModelComponent destinationSM = BSMaterial::GetLayeredMaterialShaderModel(shaderModelRootMaterial);
				// QA log output
				BSWARNING(WARN_EDITOR, "Switch ShaderModel : Material ([id:%u] %s - %s) to root material ([id:%u] %s - %s)",
					aMaterialToProcess, srcMaterialName.QString(), currentSM.FileName.QString(),
					rootMaterialId, destMaterialName.QString(), destinationSM.FileName.QString());

				BSMaterial::ChangeShaderModel(aMaterialToProcess, shaderModelRootMaterial);
				pUndoRedoStack->clear();
				UpdateDocumentModified();
					
				// Disable all processors including the Shader Model processor while we migrate the properties 
				// (we have to compare model nodes with the older shader model manually applied)
				UIProcessorsActive = false;

				// Make sure to refresh the underlying views with updated new root material parent, this will enable us to revert
				// the properties to data parent with the right parents loaded in the model node views. 
				OnRefreshPropertyEditor();
				// Process individual properties. Properties not found in destination shader models are reverted to data parent.
				SharedTools::MigrateShaderModelProperties(*ui.treeViewPropEditor->QTreeNode(), shaderModelRootMaterial);

				// Enable UI processors being applied, we want the new shader model to be applied on migrated Material.
				UIProcessorsActive = true;

				// Save all affected materials
				bool allMaterialsSaved = BSMaterial::Save(materials);

				OnRefreshPropertyEditor();

				if (!allMaterialsSaved)
				{
					QApplication::restoreOverrideCursor();
					QMessageBox::critical(this, pDialogTitleC, "Some materials failed to save\nCheck the log for details\nYou're recommended to revert all open changes now");
				}
			});
			pdialog->show();
		}
	}

	/// ------------------------------------------------------------------------------------------
	/// <summary> React to when users have used the material picker feature, clicked on an object within the render window to transfer all its associated
	/// materials in the dialog's "Recent Materials/Custom Groups" tree view under a "Material Picker" top-level group </summary>
	/// <param name="aSelectedObjects"> Objects the user just selected in the render window </param>
	/// ------------------------------------------------------------------------------------------
	void MaterialLayeringDialog::OnMaterialsPickedFromRenderWindow(const std::vector<TESObjectREFRPtr>& aSelectedObjects)
	{
		QSet<QString> materialRelativePaths;
		ui.pMaterialBrowserWidget->ClearMaterialPickerQuickAccess();

		for ( const auto& rspobjRefrPtr : aSelectedObjects )
		{
			NiAVObject* p3d = rspobjRefrPtr->Get3D();
			if ( p3d )
			{
				BGSLayeredMaterialSwap::MetadataMap metadata = BGSLayeredMaterialSwap::GetMetadataForObject( *rspobjRefrPtr.getPtr(), *p3d );

				for ( auto& rdata : metadata )
				{
					const BGSLayeredMaterialSwap::MaterialSwapMetadata& matSwapData = rdata.QValue();
					const bool hasSwappedMat = matSwapData.OverrideMaterial.QEmpty() == false;
					const auto matIDOriginal = BSMaterial::FindLayeredMaterialByFile( rdata.QKey().QString() );
					const auto matIDSwapped = BSMaterial::FindLayeredMaterialByFile( matSwapData.OverrideMaterial.QString() );
					const bool success = matIDOriginal.QValid() && (hasSwappedMat == matIDSwapped.QValid());

					if ( success )
					{
						// Sanitize the full asset path as ResourceID compliant, note that the material swap paths are already relative to 
						// the "Data/Materials" folder, so we want to re-add that parent folder so the material browser widget can handle those items correctly.
						QString relativePath = QString("Materials\\%1").arg( QtFileNameToResourceID(rdata.QKey().QString()) );
						materialRelativePaths << relativePath;

						if ( hasSwappedMat )
						{
							relativePath = QString("Materials\\%1").arg( QtFileNameToResourceID(matSwapData.OverrideMaterial.QString()) );
							materialRelativePaths << relativePath;
						}
					}
				}
			}
		}

		ui.pMaterialBrowserWidget->RegisterMaterialPickerPaths(materialRelativePaths);
		SetMaterialPickerActive( false );
	}

	/// --------------------------------------------------------------------------------
	/// <summary>
	/// SLOT: Create a new Material based on a parent Material ID. Uniqueness and valid name is ensured.
	/// </summary>
	/// <param name="aForcedPath"> If non empty, the new file should be created in this directory </param>
	/// --------------------------------------------------------------------------------
	void MaterialLayeringDialog::CreateNew(const QString& aForcedPath)
	{
		if (PromptToSaveChanges())
		{
			pUndoRedoStack->clear();

			SharedTools::CreateNewFromHierarchyDialog*	pdialog = new SharedTools::CreateNewFromHierarchyDialog(this, 
				"Create New Material", "Select Shader Model", "New Material Name");
			// local name validation lambda
			pdialog->SetNameValidationFunctor(&SharedTools::ValidateNewMaterialName);
			// local tree selection validation lambda to prevent Shader Model Root Material node being selected.
			pdialog->SetTreeItemSelectionValidationFunctor([this](QTreeWidgetItem* apCurrent, QTreeWidgetItem*) -> bool
			{
				bool valid = false;
				if (apCurrent)
				{
					valid = apCurrent->data(0, CustomRoles::MaterialID) != BSMaterial::Internal::QRootLayeredMaterialsID().QValue();
				}
				return valid;
			});
			// local tree hierarchy filling lambda
			pdialog->SetPopulateFunctor([this](QTreeWidget* apTreeWidget)
			{
				FillMaterialHierarchy(apTreeWidget, QString(pNewMaterialRootNameC), EditedMaterialID, false);
			});
			// On Accept Button
			connect(pdialog, &QDialog::accepted, this, [this, pdialog, aForcedPath]
			{
				QTreeWidget* pWidget = pdialog->QTreeWidget();
				BSASSERTFAST(pWidget);
				const uint32_t selectedHierarchyItemId = pWidget->currentItem()->data(0, CustomRoles::MaterialID).toUInt();
				CreateNewMaterial(BSFixedString(pdialog->QNameEntry().toLatin1().data()), BSMaterial::LayeredMaterialID(BSComponentDB2::NumericIDToID(selectedHierarchyItemId)));

				//force save as file path if given one
				if (!aForcedPath.isEmpty())
				{
					SaveAsDir = aForcedPath;
				}

				// When creating new material from scratch every layer above the first is hidden to allow the first layer preview to be visible.
				IsolateFirstLayer();

				// Prompt the user to save the new material so it will show up in the MaterialBrowser
				SaveAs();
			});
			pdialog->show();
		}
	}

	/// <summary> SLOT: Export the baked maps for the specified material </summary>
	/// <param name="aMaterial"> The ID of the material to be exported </param>
	void MaterialLayeringDialog::ExportBakedMaps(BSMaterial::LayeredMaterialID aMaterial)
	{
		BSASSERT(pBakeOptionsDialog != nullptr, "pBakeOptionsDialog was unexpectedly null");
		if (pBakeOptionsDialog != nullptr) 
		{
			ExportMaterialMapHelper(*pBakeOptionsDialog, aMaterial);
		}
	}

	/// <summary> SLOT: Asks the user to input a name and then makes a new derived material using full inheritance </summary>
	/// <param name="aParentMaterial"> Parent material ID </param>
	void MaterialLayeringDialog::CreateNewDerivedMaterial(BSMaterial::LayeredMaterialID aParentMaterial)
	{
		constexpr char dialogTitle[] = { "Create new derived material" };
		if (SharedTools::GetShaderModelLocked(GetShaderModelName(aParentMaterial)))
		{
			QMessageBox::information(this, dialogTitle, "Selected material is Locked out of full inheritance", QMessageBox::Ok);
		}
		else if (PromptToSaveChanges())
		{
			bool loop = true;
			while (loop)
			{
				bool ok = false;
				QString name = QInputDialog::getText(this, dialogTitle, "Material name", QLineEdit::Normal, "", &ok);
				if (ok)
				{
					QString message;
					if (SharedTools::ValidateNewMaterialName(name, message))
					{
						CreateNewMaterial(BSFixedString(name.toLatin1().data()), aParentMaterial);
						Save();
						loop = false;
					}
					else
					{
						QMessageBox::warning(this, pDialogTitleC, message);
					}
				}
				else
				{
					// User canceled
					loop = false;
				}
			}
		}
	}

	/// <summary> SLOT: Called when user wants to BreakInheritance. This will reparent the material to its root shader model material. </summary>
	/// <param name="aMaterial"> Material to reparent to root shader model material </param>
	void MaterialLayeringDialog::OnRequestBreakInheritance(BSMaterial::LayeredMaterialID aMaterial)
	{
		BSFixedString materialName;
		BSFixedString parentName;

		const BSMaterial::LayeredMaterialID dataParentID(BSMaterial::GetDataParent(aMaterial));
		const BSMaterial::LayeredMaterialID rootShaderModelID(BSMaterial::GetShaderModelRootMaterial(aMaterial));

		BSMaterial::GetName(aMaterial, materialName);
		BSMaterial::GetName(dataParentID, parentName);

		if (dataParentID == rootShaderModelID)
		{
			QMessageBox::information(this, pDialogTitleC, QString("Cannot break inheritance : %1 is already parented to %2").arg(materialName.QString(), parentName.QString()));
		}
		else
		{
			QString message = QString("Are you sure you want to break inheritance from %1 to %2 ?").arg(materialName.QString(), parentName.QString());
			if (QMessageBox::question(this, pDialogTitleC, message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
			{
				if (ReparentMaterial(this, aMaterial, rootShaderModelID, false) == false)
				{
					QMessageBox::warning(this, pDialogTitleC, "Breaking inheritance operation failed.");
				}
			}
		}
	}

	/// <summary> SLOT: Reparent the currently edited material </summary>
	/// <param name="aParentMaterial"> Material to reparent to </param>
	void MaterialLayeringDialog::OnReparentMaterial(BSMaterial::LayeredMaterialID aParentMaterial)
	{
		ReparentMaterial(this, EditedMaterialID, aParentMaterial);
		OnRefreshPropertyEditor();
	}

	/// <summary> SLOT: Called when OnRequestMultipleReparentToMaterial happens from the Material Browser context menu. </summary>
	/// <param name="aTargetIDList"> List of materials to reparent </param>
	/// <param name="aParentMaterial"> New parent for the materials </param>
	void MaterialLayeringDialog::OnRequestMultipleReparentToMaterial(QList<BSMaterial::LayeredMaterialID> aTargetIDList, BSMaterial::LayeredMaterialID aParentMaterial)
	{
		QProgressDialog progress("Re-parenting Materials ...", "Cancel", 0, aTargetIDList.count(), this);
		progress.setWindowModality(Qt::ApplicationModal);

		const int32_t numMaterials = aTargetIDList.count();
		QStringList list;
		for (int32_t i = 0; i < numMaterials; i++)
		{
			progress.setValue(i);
			if (progress.wasCanceled())
			{
				break;
			}
			else
			{
				if (ReparentMaterial(&progress, aTargetIDList[i], aParentMaterial, false) == false)
				{
					BSFixedString materialName;
					BSFixedString parentName;
					BSMaterial::GetName(aTargetIDList[i], materialName);
					BSMaterial::GetName(aParentMaterial, parentName);
					QMessageBox::warning(this, pDialogTitleC, QString("Reparenting %1 to %2 failed, aborting process.").arg(materialName.QString(), parentName.QString()));
					break;
				}
			}
		}
		progress.setValue(numMaterials);
	}

	/// <summary> Reparent Material to a new target material </summary>
	/// <param name="apParent"> Parent widget (for modality z order if using a progress dialog) </param>
	/// <param name="aTargetMaterial"> Material we are reparenting </param>
	/// <param name="aParentMaterial"> New parent to use </param>
	/// <param name="aUserConfirmationPrompt"> True if we want user interaction during single reparenting operation (used for single operations). False during list processing. </param>
	/// <returns> True if reparenting is a success </returns>
	bool MaterialLayeringDialog::ReparentMaterial(QWidget* apParent, BSMaterial::LayeredMaterialID aTargetMaterial, BSMaterial::LayeredMaterialID aParentMaterial, bool aUserConfirmationPrompt /*=true*/)
	{
		bool success = false;

		BSFixedString targetMaterialName;
		BSFixedString parentMaterialName;

		BSMaterial::GetName(aTargetMaterial, targetMaterialName);
		BSMaterial::GetName(aParentMaterial, parentMaterialName);

		const QString cannotReparentMsg = QString("Can't reparent %1 to %2 ").arg(targetMaterialName.QString(), parentMaterialName.QString());

		const BSMaterial::LayeredMaterialID targetMaterialShaderModelRoot = BSMaterial::GetShaderModelRootMaterial(aTargetMaterial);

		// Are we reparenting to a Shader Model Root Material, breaking inheritance in the process ?
		const bool isBreakingInheritance = targetMaterialShaderModelRoot == aParentMaterial;

		// If we are breaking inheritance, skip root material check, otherwise make sure both child and new parent share the same shader model.
		if (!isBreakingInheritance && targetMaterialShaderModelRoot != BSMaterial::GetShaderModelRootMaterial(aParentMaterial))
		{
			QMessageBox::warning(apParent, pDialogTitleC, QString("%1 because its using a different shader model.\nTo remedy this you can right click -> Switch Shader Model in the bottom left panel").arg(cannotReparentMsg));
		}
		else
		{
			uint32_t numChildMaterials = 0;

			// Iterate all data children and
			// 1) Validate aParentMaterial is NOT a derived material (if so, we abort)
			// 2) Count each derived material (and emit warnings to the log)
			const auto parentIsDataChild = BSMaterial::Internal::QDB2Instance().ExecuteForRead([&](const auto& aInterface)
				{
					return BSComponentDB2::TraverseDataChildren(aInterface, aParentMaterial, [&](const auto& /*aInterface*/,
						BSComponentDB2::ID /*aFrom*/,
						BSComponentDB2::ID aChildObject)
						{
							BSFilePathString file;
							if (BSMaterial::GetFilename(aChildObject, file))
							{
								BSWARNING(WARN_MATERIALS, "Reparenting would affect %s", file.QString());
							}
							++numChildMaterials;

							return aChildObject == aParentMaterial
								? BSContainer::Stop
								: BSContainer::Continue;
						});
				});

			if (parentIsDataChild != BSContainer::Continue)
			{
				QMessageBox::critical(apParent, pDialogTitleC, QString("%1 because it would create a circular inheritance link").arg(cannotReparentMsg));
			}
			else
			{
				bool proceedWithOperation = true;
				if (aUserConfirmationPrompt)
				{
					QString message;
					QTextStream stream(&message);
					if (numChildMaterials > 0)
					{
						stream << "Reparenting this material affects " << numChildMaterials << " derived materials\n";
						stream << "Check the log for the list of files affected.\n";
						stream << "\n";
					}
					stream << "Are you sure you want to reparent your material?";
					proceedWithOperation = QMessageBox::question(apParent, pDialogTitleC, message, QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
				}

				if (proceedWithOperation)
				{
					BSMaterial::Internal::QDBStorage().RequestReparentObject(aTargetMaterial, aParentMaterial, true);

					// If the operation does not require user confirmation, then proceed right away with saving the material.
					if (!aUserConfirmationPrompt)
					{
						success = BSMaterial::Save(aTargetMaterial);
					}
				}
			}
		}

		// Success, check to refresh Edited Material if its was the target material
		if (success)
		{
			if (aTargetMaterial == EditedMaterialID)
			{
				OnRefreshPropertyEditor();
			}
		}

		return success;
	}

	/// <summary> SLOT: Automated Small Inheritance operation that include creating a new material of a specified Shader Model parent and automating the drag and drop of supplied base material. </summary>
	/// <param name="aSrcMaterialToSwitch"> Source material to copy and switch </param>
	void MaterialLayeringDialog::OnRequestMaterialAutomatedSmallInheritance(const QString& aForcedPath, BSMaterial::LayeredMaterialID aBaseMaterial, BSMaterial::LayeredMaterialID aNewShaderModelToUse)
	{
		// Make sure to save outstanding changes.
		if (PromptToSaveChanges())
		{
			// Create the new material of selected shader model
			CreateNewMaterial(BSFixedString(pUntitledNameC), aNewShaderModelToUse);

			//force save as file path if given one
			if (!aForcedPath.isEmpty())
			{
				SaveAsDir = aForcedPath;
			}

			// When creating new material from scratch every layer above the first is hidden to allow the first layer preview to be visible.
			IsolateFirstLayer();

			// Prompt the user to save the new material so it will show up in the MaterialBrowser (This will rename objects prior to Small Inheritance)
			if (SaveAs())
			{
				// Automate the Small Inheritance operation with base layer as target.
				BSMaterial::LayerID baseLayerID = BSMaterial::GetLayer(EditedMaterialID, 0);
				if (BSMaterial::CreateSmallInheritance(aBaseMaterial, EditedMaterialID, baseLayerID))
				{
					// Save small inheritance operation with the base layer name changed
					Save();
				}
			}
		}
	}

	/// <summary> Check if the currently edited material has a corresponding local file </summary>
	/// <returns> True if the material already exists on disk </returns>
	bool MaterialLayeringDialog::EditedFileExists() const
	{
		BSFilePathString filename;
		return EditedMaterialID.QValid()
			&& BSMaterial::Internal::QDBStorage().GetObjectFilename(EditedMaterialID, filename)
			&& BSaccess(filename.QString(), 0) != -1;
	}

	/// <summary> SLOT: Save the material that's currently being edited </summary>
	/// <returns> True if the material was saved, false if the user canceled the save </returns>
	bool MaterialLayeringDialog::Save()
	{
		CursorScope cursor(Qt::WaitCursor);

		bool result = false;
		// If there is no file on disk, ask the user to choose a location for it
		if (EditedFileExists())
		{
			// Always check out the files first
			auto filesCheckedOut = CheckoutCurrentFiles(false);
			if (filesCheckedOut.QSize() != 0)
			{
				// Save the active material
				bool saved = BSMaterial::Save(EditedMaterialID);
		
				if(saved)
				{
#if 0 //https://bgs.atlassian.net/browse/GEN-320052
						BGSRenderWindowUtils::ExportMaterialIcon(EditedMaterialID, GetMaterialIconDirectory());

						if (bEnableMaterialMapExport && pBakeOptionsDialog->AreMapsEnabled())
						{
							ExportMaterialMapHelper(*pBakeOptionsDialog, EditedMaterialID);
						}
#endif //https://bgs.atlassian.net/browse/GEN-320052
				}			
				else
				{
					BSFilePathString filename;
					BSMaterial::Internal::QDBStorage().GetObjectFilename(EditedMaterialID, filename);
					QMessageBox::critical(this, pDialogTitleC, QString::asprintf("Failed to save %s", filename.QString()));
				}

				// Since we may purge unused assets during the Save() we should refresh the UI so those assets don't show up in the DBObjectWidgets
				OnRefreshPropertyEditor();
				result = saved;
			}
		}
		else
		{
			// Our material has never been saved (its either the default <untitled> one, or a new one)
			// Prompt the user to choose a location to save it
			result = SaveAs();
		}

		return result;
	}

	/// <summary> SLOT: Save the currently edited material in a new location </summary>
	/// <returns> True if the material was saved, false if the user canceled the save </returns>
	bool MaterialLayeringDialog::SaveAs()
	{
		bool result = false;

		// Get the ShaderModel for this material, prevent save as operations on locked materials.
		BSMaterial::ShaderModelComponent smComponent = BSMaterial::GetLayeredMaterialShaderModel(EditedMaterialID);
		const bool materialLocked = SharedTools::GetShaderModelLocked(smComponent.FileName);

		if (materialLocked)
		{
			QString warningText = QString("You cannot save a copy of this material.\nShader Model : %1 is locked.").arg(smComponent.FileName.QString());
			QMessageBox::warning(this, pDialogTitleC, warningText);
		}
		else
		{
			if (SaveAsDir.isEmpty())
			{
				SaveAsDir = ui.pMaterialBrowserWidget->QMaterialBrowserRoot();
			}
			BSFilePathString oldFilename;
			bool existent = false;
			if (BSMaterial::Internal::QDBStorage().GetObjectFilename(EditedMaterialID, oldFilename))
			{
				existent = BSaccess(oldFilename.QString(), 0) != -1;
			}
			BSFixedString oldName;
			BSMaterial::GetName(EditedMaterialID, oldName);

			// Even new layered materials have a (temp) filename assigned, but we should use the last
			// folder the user saved to.
			QString startPath = existent
				? oldFilename.QString()
				: QString("%1/%2.mat").arg(SaveAsDir, oldName.QString());

			QString absoluteFilename;
			if (SharedTools::ShowMaterialSaveAsDialog(this, EditedMaterialID, startPath, existent, absoluteFilename))
			{
				CursorScope cursor(Qt::WaitCursor);

				// Convert the filenames from Qt's format (UNIX like) to Windows
				QFileInfo fileInfo(absoluteFilename);
				QString filename = QDir::current().relativeFilePath(absoluteFilename);
				filename = QDir::toNativeSeparators(filename);
				absoluteFilename = QDir::toNativeSeparators(absoluteFilename);

				BSFixedString name(fileInfo.baseName().toLatin1().data());
				BSFixedString filenameFixed(filename.toLatin1().data());

				// Remember the directory where we last saved
				SaveAsDir = fileInfo.absolutePath();

				auto& rstorage = BSMaterial::Internal::QDBStorage();
				BSMaterial::LayeredMaterialID savedObject = EditedMaterialID;
				bool fileWasMoved = existent && fileInfo != QFileInfo(startPath);
				if (fileWasMoved)
				{
					// An material file was saved to a new location. This effectively clones the material
					// If we would just save out the material elsewhere we will get collisions on internal GUIDs
					// between the 2 material's sub-objects.
					// This function will clone the material and its nested objects and assign new IDs
					BSMaterial::Internal::QDB2Instance().RequestExecuteForCreateAndDelete(
						[&rstorage, this, &filenameFixed, &savedObject](BSComponentDB2::CreateAndDeleteInterface& arInterface)
						{
							savedObject = BSMaterial::LayeredMaterialID(rstorage.CloneFileObjects(arInterface, EditedMaterialID, filenameFixed.QString()));
						});

					// We flush in order to execute the request above immediately
					BSMaterial::Flush();
				}

				if (name != oldName)
				{
					// Rename the material and all sub objects
					BSMaterial::RenameAll(savedObject, name);
				}

				result = BSMaterial::SaveAs(savedObject, filenameFixed.QString());

				const uint32_t changelistNumber = FindOrCreateChangelist(sMaterialDefaultChangeListDesc.String());

				//Capture a preview image
#if 0 //https://bgs.atlassian.net/browse/GEN-320052
				BGSRenderWindowUtils::ExportMaterialIcon(EditedMaterialID, GetMaterialIconDirectory());

				if (bEnableMaterialMapExport && pBakeOptionsDialog->AreMapsEnabled())
				{
					ExportMaterialMapHelper(*pBakeOptionsDialog, EditedMaterialID);
				}
#endif //https://bgs.atlassian.net/browse/GEN-320052

				// Try to add the file to Perforce
				// NOTE: we have to use either the depot path or an absolute path
				SharedTools::CheckoutFiles(this, "Save As - Perforce", { BSFixedString(absoluteFilename.toLatin1().data()) }, SharedTools::CheckOutFailedOption::TryAdd, SharedTools::VerbosityOption::Quiet, changelistNumber);

				if (fileWasMoved)
				{
					// If the file was saved to a different location we have to reopen the document since its ID changed
					Open(savedObject);
				}
				else
				{
					OnRefreshPropertyEditor();
				}
			}
		}

		return result;
	}

	/// <summary>
	/// SLOT: Save all the layered materials in the project.
	/// </summary>
	/// <returns>True if all materials were successfully saved.</returns>
	bool MaterialLayeringDialog::SaveAll()
	{
		bool result = false;

		if(QMessageBox::information(this, "Save All", "You are about to save all the materials in the project. This could take a while. Proceed?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
		{
			CursorScope cursor(Qt::WaitCursor);

			// Get currently checked out files.
			stl::scrap_set<BSFixedString> filesCheckedOut;
			
			{
				const BSTArray<BSFixedString> checkedOutFiles = GetCheckedOutFiles(this, PerforceSyncPath.QString());
				filesCheckedOut = stl::scrap_set<BSFixedString>(checkedOutFiles.begin(), checkedOutFiles.end());
			}

			BSTArray<BSMaterial::LayeredMaterialID> allMaterials;
			BSTArray<BSFixedString> pathsToCheckout;

			BSMaterial::ForEachLayeredMaterial([&filesCheckedOut, &pathsToCheckout, &allMaterials](BSMaterial::LayeredMaterialID /*aParentID*/, BSMaterial::LayeredMaterialID aLayeredMaterialID)
			{
				BSFilePathString relativeFile;
				// Only process file-object materials
				if (BSMaterial::Internal::QDBStorage().GetObjectFilename(aLayeredMaterialID, relativeFile))
				{
					const BSFixedString absolutePath = SharedTools::MakePerforcePath(relativeFile.QString());

					// If the file is not checked out, add it to be checked it out.
					if (filesCheckedOut.find(absolutePath) == filesCheckedOut.end())
					{
						pathsToCheckout.Add(std::move(absolutePath));
					}
					
					// Track material for saving.
					allMaterials.Add(aLayeredMaterialID);
				}

				return BSContainer::ForEachResult::Continue;
			});

			// check out any files that need to be checked out.
			const uint32_t changelistNumber = FindOrCreateChangelist(sMaterialDefaultChangeListDesc.String());
			SharedTools::CheckoutFiles(this, pDialogTitleC, pathsToCheckout, SharedTools::CheckOutFailedOption::TryAdd, SharedTools::VerbosityOption::Verbose, changelistNumber);
			
			// Save whole material list.
			if (allMaterials.QSize() > 0)
			{
				result = BSMaterial::Save(allMaterials);

				if (!result)
				{
					QMessageBox::critical(this, pDialogTitleC, "Failed to save all materials");
				}
			}		
		}

		OnRefreshPropertyEditor();

		return result;
	}

	//////////////////////////////////////////////////////////////////////////
	// Perforce operations
	/// <summary> Sync file(s) from Perforce </summary>
	/// <param name="apDepotPath"> Path to the files, may contain wildcards </param>
	void MaterialLayeringDialog::Sync(const char* apDepotPath)
	{
		BSPerforce::ConnectionSmartPtr spperforce;
		CSPerforce::Perforce::QInstance().QPerforce(spperforce);

		BSFilePathString syncSummary;
		BSTArray<BSFixedString> updatedFiles;
		if (spperforce)
		{
			CursorScope cursor(Qt::WaitCursor);
			spperforce->SyncAndGetUpdatedFiles(apDepotPath, updatedFiles);

			// Make sure to update cache for newly modified file
			BSScrapArray<BSFixedString> modifedKeys;
			QtPerforceFileInfoCache::QInstance().UpdateCache(updatedFiles, modifedKeys);

			for (const BSFixedString& file : modifedKeys)
			{
				QtPerforceFileInfoCache::CacheIterator fileInfoIt;
				if(QtPerforceFileInfoCache::QInstance().GetFileInfo(file.QString(), fileInfoIt))
				{
					const BSPerforce::FileInfo& p4FileInfo = fileInfoIt->second;
					bool fileWasDeleted = p4FileInfo.QHeadAction() == BSPerforce::FileInfo::ACTION_DELETE || p4FileInfo.QHeadAction() == BSPerforce::FileInfo::ACTION_MOVE_DELETE;
					if (fileWasDeleted)
					{
						BSFilePathString relativePath, absolutePath;
						FilePathUtilities::Join(pMaterialPrefixC, MakeLocalPath(file.QString()), relativePath);
						FilePathUtilities::AbsPath(relativePath, absolutePath);
						QFileInfo fileInfo(absolutePath.QString());
						if (fileInfo.exists())
						{
							// The local file being writeable is the only known reason for the sync to fail.
							// If a user ever hits the second error message we will need to investigate further.
							if (fileInfo.isWritable())
							{
								QMessageBox::warning(this, pDialogTitleC, QString::asprintf("Could not sync %s.\n\nYour local copy is writeable and this file was moved or deleted.", file.QString()));
							}
							else
							{
								QMessageBox::warning(this, pDialogTitleC, QString::asprintf("Could not sync %s.\n\nYou will need to manually resolve the problem.", file.QString()));
							}
						}
					}
				}
			}

			syncSummary.SPrintF("Synced %u file(s) from Perforce\n", updatedFiles.QSize());

			if (pBakeOptionsDialog->ShouldSyncAllMapsOnLoad())
			{
				pBakeOptionsDialog->SyncAllMaps();
			}
		}

		constexpr char successfulLoadC[] = "All materials were loaded successfully";
		constexpr char failedLoadC[] = "Failed to load all the materials, see warning output";
		QString loadResultMessage = failedLoadC;
		bool loadSuccess = false;
		{
			CursorScope cursor(Qt::WaitCursor);
			loadSuccess = BSMaterial::LoadAll();
		}
		if (loadSuccess)
		{
			loadResultMessage = successfulLoadC;
		}

		// If we have synchronized files, show the list of updated files, else warn in a smaller message box.
		if (updatedFiles.QSize())
		{
			QStringList listedFiles;
			for (auto& file : updatedFiles)
			{
				listedFiles.append(file.QString());
			}

			QtGenericListDialog*	plistDialog = new QtGenericListDialog(this, pDialogTitleC, loadResultMessage, QString("Modified/new files: %1").arg(syncSummary.QString()), listedFiles);
			plistDialog->setAttribute(Qt::WA_DeleteOnClose);
			plistDialog->setWindowModality(Qt::WindowModal);
			plistDialog->setModal(true);
			plistDialog->show();
		}
		else
		{
			// Will show a smaller version of the load message with syncSummary showing no new synchronized files.
			QMessageBox::information(this, pDialogTitleC, QString::asprintf("%s%s", syncSummary.QString(), loadResultMessage.toLatin1().data()));
		}

		OnRefreshPropertyEditor();
	}

	/// <summary> SLOT: Sync all materials and reload them </summary>
	void MaterialLayeringDialog::ReloadAll()
	{
		Sync(PerforceSyncPath.QString());
	}

	/// <summary> Check in a collection of files </summary>
	/// <param name="aFiles"> Files to check in </param>
	void MaterialLayeringDialog::CheckIn(const BSTArray<BSFixedString>& aFiles)
	{
		if (aFiles.QSize() != 0 && PromptToSaveChanges() && SourceTextureDepotPathValid())
		{
			BSTArray<BSFixedString> filesToCheckIn(aFiles);
			bool aborted = false;

			// Referenced textures & parent materials
			stl::scrap_set<BSFixedString> dependencies;	// NOTE: Must be local file paths (not p4 depot path)

			// Discover parent materials (both full & small inheritance)
			BSComponentDB2::StorageService& rstorage = BSMaterial::Internal::QDBStorage();
			for(uint32_t i = 0; i < filesToCheckIn.QSize(); ++i)
			{
				BSFixedString &rfile = filesToCheckIn[i];

				// Convert to relative path
				BSComponentDB2::ID object = rstorage.GetObjectByFilename(SharedTools::MakeLocalPath(rfile.QString()).QString());
				if (object != BSComponentDB2::NullIDC)
				{
					FindReferencedTextureFiles(object, dependencies);

					stl::scrap_set<BSMaterial::ID> dataParents;
					BSMaterial::FindDataParents(BSMaterial::LayeredMaterialID(object), dataParents);
					
					// Convert data parent IDs to filenames
					for (BSMaterial::ID parent : dataParents)
					{
						BSFilePathString relativePath;
						if (rstorage.GetObjectFilename(parent, relativePath))
						{
							BSFilePathString absolutePath;
							FilePathUtilities::AbsPath(relativePath.QString(), absolutePath);
							dependencies.emplace(absolutePath.QString());
						}
					}

					// Check in relevant icons and 3DS Maps
					// NOTE: this appends items to filesToCheckIn
					AddMaterialSnapshotsToFileList(object, *pBakeOptionsDialog, true, filesToCheckIn);
				}
			}

			// Transfer all files during check out to the material default CL, making sure to mark for add missing icons and textures.
			const uint32_t changelistNumber = FindOrCreateChangelist(sMaterialDefaultChangeListDesc.String());
			SharedTools::CheckoutFiles(this, "Check out of referenced Material Assets", filesToCheckIn, SharedTools::CheckOutFailedOption::TryAdd, SharedTools::VerbosityOption::Quiet, changelistNumber);

			BSPerforce::ConnectionSmartPtr spperforce;
			CSPerforce::Perforce::QInstance().QPerforce(spperforce);
			if (spperforce)
			{
				QProgressDialog progress("Checking Perforce state of texture/material dependencies...", "Cancel", 0, static_cast<int32_t>(dependencies.size()), this);
				progress.setWindowModality(Qt::WindowModal);
				progress.setMinimumDuration(0);

				// Add referenced textures to filesToCheckIn, as needed				
				for (const BSFixedString& rdep : dependencies)
				{
					aborted = progress.wasCanceled();

					// Check if we have the dependent texture/material checked out, or need to add it to the Perforce depot
					BSPerforce::FileInfo info;
					if (!aborted &&
						BSaccess(rdep.QString(), 0) != -1 &&						// Check if we have this file locally
						spperforce->GetFileInfo(rdep, info) &&						// Query Perforce state
						info.QAction() != BSPerforce::FileInfo::ACTION_INVALID ||	// Do we have it checked out already?
						(info.QHeadRevision() == 0 &&								// Or if Perforce doesn't have this file
							spperforce->AddFile(rdep.QString())))					// ... and we can add it to the depot
					{
						// Check in this dependency along with the material(s)
						filesToCheckIn.Add(std::move(rdep));
					}
				}
			}

			if(!aborted && SharedTools::CheckinFiles(this, pDialogTitleC, filesToCheckIn))
			{
				ui.pMaterialBrowserWidget->Refresh();
			}
		}
	}

	/// <summary> SLOT : Check Out a Single File in the Material default ChangeList </summary>
	/// <param name="aFile">Filename to check out</param>
	void MaterialLayeringDialog::CheckOutFile(const BSFixedString& aFile)
	{
		BSPerforce::ConnectionSmartPtr spperforce;
		CSPerforce::Perforce::QInstance().QPerforce(spperforce);
		if (spperforce)
		{
			const uint32_t changelistNumber = FindOrCreateChangelist(sMaterialDefaultChangeListDesc.String());
			spperforce->CheckoutFile(aFile, changelistNumber);
			QtPerforceFileInfoCache::QInstance().UpdateCacheAsync(aFile.QString());
		}
	}

	/// <summary> SLOT : Mark a Single File for add in the Material default ChangeList </summary>
	/// <param name="aFile">Filename to add in P4</param>
	void MaterialLayeringDialog::FileMarkForAdd(const BSFixedString& aFile)
	{
		BSPerforce::ConnectionSmartPtr spperforce;
		CSPerforce::Perforce::QInstance().QPerforce(spperforce);
		if (spperforce)
		{
			const uint32_t changelistNumber = FindOrCreateChangelist(sMaterialDefaultChangeListDesc.String());
			spperforce->AddFile(aFile, changelistNumber);
			QtPerforceFileInfoCache::QInstance().UpdateCacheAsync(aFile.QString());

			ui.pMaterialBrowserWidget->Refresh();
		}
	}

	/// <summary> SLOT: Check in all opened material files </summary>
	void MaterialLayeringDialog::CheckInAll()
	{
		// Make sure the user saves his changes first, this can change the set of checked out files
		if (PromptToSaveChanges())
		{
			CheckIn(GetCheckedOutFiles(this, PerforceSyncPath.QString()));
		}
	}

	/// <summary> SLOT: Check out the current document's files </summary>
	void MaterialLayeringDialog::CheckOut()
	{
		CheckForNewerFiles();
		CheckoutCurrentFiles(true);
	}

	/// <summary> Delete a set of files </summary>
	/// <param name="aFiles"> Files to delete </param>
	void MaterialLayeringDialog::Delete(const BSFixedString& aFile)
	{
		BSPerforce::ConnectionSmartPtr spperforce;
		CSPerforce::Perforce::QInstance().QPerforce(spperforce);
		BSPerforce::FileInfo fileInfo;

		if (spperforce || !UseVersionControl)
		{
			QString deleteConfirmMessage = "";

			if (bUseVersionControl)
			{
				const bool perforceFile = spperforce->GetFileInfo(aFile, fileInfo) && fileInfo.QAction() != BSPerforce::FileInfo::ACTION_ADD;
				deleteConfirmMessage = QString::asprintf("Are you sure you would like to delete this %s\n\n%s", perforceFile ? "file from Perforce?" : "local file?", perforceFile ? SharedTools::MakePerforcePath(aFile.QString()).QString() : aFile.QString());
			}
			else
			{
				deleteConfirmMessage = QString::asprintf("Are you sure you would like to delete %s ?", aFile.QString());
			}

			if (QMessageBox::warning(this, pDialogTitleC, deleteConfirmMessage, QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes)
			{
				// Convert to relative path
				BSComponentDB2::StorageService& rstorage = BSMaterial::Internal::QDBStorage();
				BSComponentDB2::ID object = rstorage.GetObjectByFilename(SharedTools::MakeLocalPath(aFile.QString()).QString());
				if (object != BSComponentDB2::NullIDC)
				{
					bool deleteHappened = false;
					const bool deletingCurrentDocument = object.QValue() == EditedMaterialID.QID().QValue();

					// Check for data children.
					bool deletionRestricted = BSMaterial::Internal::QDB2Instance().ExecuteForRead([](const BSComponentDB2::ReadInterface& arInterface, BSComponentDB2::ID aObjectID)
					{
						return BSComponentDB2::HasDataChildren(arInterface, aObjectID);
					}, object);

					if (!deletionRestricted)
					{
						// Check for TESModel dependencies.
						const BSTArray<BSFixedString> dependencies = FindFormDependenciesForLayeredMaterial(object);
						if (!dependencies.QEmpty())
						{
							deletionRestricted = true;
							int32_t modelIndex = 0;
							QString restrictedMessage = QString::asprintf("Cannot delete the layered material because it is being used by:\n\n");
							for (const BSFixedString& rmodel : dependencies)
							{
								if (modelIndex++ >= 10)
								{
									restrictedMessage += "...\n";
									break;
								}

								restrictedMessage += QString::asprintf("%s\n", rmodel.QString());
							}

							QMessageBox::information(this, pDialogTitleC, restrictedMessage);
						}
					}
					else
					{
						QMessageBox::information(this, pDialogTitleC, "Cannot delete the layered material because it has data children.");
					}

					if (!deletionRestricted)
					{
						// Collect the files for this layered material to be deleted.
						BSTArray<BSFixedString> filesToDelete;

						// Find referenced textures
						stl::scatter_table_set<BSFixedString> textures;
						FindReferencedTextureFiles(object, textures);

						if (!textures.empty())
						{
							QProgressDialog progress("Finding textures...", "Cancel", 0, static_cast<int32_t>(textures.size()), this);
							progress.setWindowModality(Qt::WindowModal);
							progress.setMinimumDuration(0);

							// Collect the referenced textures that exist in Perforce
							int32_t index = 0;
							for (BSFixedString& texture : textures)
							{
								progress.setValue(index++);

								if (!progress.wasCanceled())
								{
									filesToDelete.Add(std::move(texture));
								}
							}
						}

						// Add the files for its sub objects.
						BSScrapArray<BSFilePathString> subObjectFiles;
						if (rstorage.GatherReferencedFiles(object, subObjectFiles))
						{
							for (BSFilePathString& subObjFile : subObjectFiles)
							{
								auto perforceSubObjectFileName = SharedTools::MakePerforcePath(subObjFile.QString());
								if (!Find(filesToDelete, perforceSubObjectFileName))
								{
									filesToDelete.Add(std::move(perforceSubObjectFileName));
								}
							}
						}

						// Iterate each layered material and see if any of the files are referenced by it.
						BSMaterial::ForEachLayeredMaterial([this, object, &filesToDelete](BSMaterial::LayeredMaterialID /*aParentID*/, BSMaterial::LayeredMaterialID aIteratedMaterialID)
							{
								if (aIteratedMaterialID != object)
								{
									// Check its referenced textures and remove any found from the delete list.
									stl::scatter_table_set<BSFixedString> iterObjectTextures;
									FindReferencedTextureFiles(aIteratedMaterialID, iterObjectTextures);

									if (iterObjectTextures.size())
									{
										for (BSFixedString& objTexture : iterObjectTextures)
										{
											if (IsInArray(filesToDelete, objTexture))
											{
												RemoveAllEqual(filesToDelete, objTexture);
											}
										}
									}

									// Check its sub objects and remove any found from the delete list.
									BSScrapArray<BSFilePathString> iterSubObjectFiles;
									if (BSMaterial::Internal::QDBStorage().GatherReferencedFiles(aIteratedMaterialID, iterSubObjectFiles))
									{
										for (BSFilePathString& iterSubObjFile : iterSubObjectFiles)
										{
											auto perforceSubObjFileName = SharedTools::MakePerforcePath(iterSubObjFile.QString());
											if (Find(filesToDelete, perforceSubObjFileName))
											{
												RemoveAllEqual(filesToDelete, perforceSubObjFileName);
											}
										}
									}
								}

								return BSContainer::Continue;
							});

						//Delete the associated icon should one exist
						AddMaterialSnapshotsToFileList(object, *pBakeOptionsDialog, true, filesToDelete);

						// Handle deleting the layered material root file and sub files we collected.
						BSTArray<BSFixedString> localFilesToDelete;
						BSTArray<BSFixedString> p4FilesToDelete;
						if (!filesToDelete.QEmpty())
						{
							uint32_t fileIndex = 0;
							QString message = QString::asprintf("Would you like to delete its sub files too?\n\n");
							for (auto& subFile : filesToDelete)
							{
								if (fileIndex++ >= 10)
								{
									message += "...\n";
									break;
								}

								message += QString::asprintf("%s\n\n", subFile.QString());
							}

							if (QMessageBox::warning(this, pDialogTitleC, message, QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes)
							{
								if (UseVersionControl)
								{
									// Batch update state in cache.
									QtPerforceFileInfoCache::QInstance().UpdateCacheAsync(filesToDelete);

									for (const auto& delFile : filesToDelete)
									{
										QtPerforceFileInfoCache::CacheIterator fileInfoIt;
										if (QtPerforceFileInfoCache::QInstance().GetFileInfo(delFile.QString(), fileInfoIt))
										{
											const BSPerforce::FileInfo& p4FileInfo = fileInfoIt->second;
											if (p4FileInfo.QAction() == BSPerforce::FileInfo::ACTION_ADD)
											{
												// Revert sub file marked for add.
												spperforce->RevertFile(delFile);
												localFilesToDelete.Add(delFile);
											}
											else
											{
												// Mark sub file for delete.
												spperforce->MarkForDelete(delFile);
												p4FilesToDelete.Add(delFile);
											}
										}
										else
										{
											localFilesToDelete.Add(delFile);
										}
									}
								}
								else
								{
									for (const auto& delFile : filesToDelete)
									{
										localFilesToDelete.Add(delFile);
									}
								}
							}
						}

						if (!deletionRestricted)
						{
							bool checkinCanceled = false;
							if (UseVersionControl && spperforce->GetFileInfo(aFile, fileInfo))
							{
								if (fileInfo.QAction() == BSPerforce::FileInfo::ACTION_ADD)
								{
									// Revert layered material root marked for add.
									spperforce->RevertFile(aFile);
									localFilesToDelete.Add(aFile);
								}
								else if (fileInfo.QHasOtherCheckouts())
								{
									deletionRestricted = true;
									checkinCanceled = true;
									QMessageBox::warning(nullptr, pDialogTitleC, "Cannot delete this material.  It is checked out by someone else.");
								}
								else
								{
									// Mark layered material root for delete.
									spperforce->MarkForDelete(aFile);
									p4FilesToDelete.Add(aFile);
								}
							}
							else
							{
								localFilesToDelete.Add(aFile);
							}

							if (!checkinCanceled && p4FilesToDelete.QSize() > 0)
							{
								checkinCanceled = !SharedTools::CheckinFiles(this, pDialogTitleC, p4FilesToDelete);
							}

							if (checkinCanceled)
							{
								for (auto& p4File : p4FilesToDelete)
								{
									if (spperforce->GetFileInfo(p4File, fileInfo))
									{
										// The file was marked for delete but the checkin was cancelled.
										// Need to revert so it is no longer marked for delete.
										spperforce->RevertFile(p4File);
									}
									else
									{
										spperforce->AddFile(p4File);
									}
								}
							}
							else
							{
								BSScrapArray<BSFixedString> failedToDelete;

								for (auto& localFile : localFilesToDelete)
								{
									if (!BSDeleteFile(localFile))
									{
										failedToDelete.Add(localFile);
									}
								}

								if (!failedToDelete.QEmpty())
								{
									QString message = "Failed to delete local files: ";
									for (const BSFixedString& file : failedToDelete)
									{
										message += QString::asprintf("%s\\n", file.QString());
									}

									message += "; Please ensure that the files are not read only or used by another process.";
									QMessageBox::warning(nullptr, pDialogTitleC, message);
								}

								rstorage.RequestDestroyFileObjects(object);
								BSMaterial::Flush();
								ui.pMaterialBrowserWidget->Refresh();
								deleteHappened = true;
							}
						}
					}

					// If we delete the currently edited document, create a new one like on Material Editor open.
					if (deleteHappened && deletingCurrentDocument)
					{
						NewUntitledMaterial();
					}
				}
				else
				{
					QMessageBox::information(this, pDialogTitleC, "Cannot delete because object for material layer could not be found.");
				}
			}
		}
	}

	/// <summary> Moves a file </summary>
	/// <param name="aOldPathFilename"> Relative filename of file to be moved before the move. </param>
	/// <param name="aNewPathFilename"> Relative filename of file to be moved after the move. </param>
	void MaterialLayeringDialog::Move(const BSFixedString& aOldFilename, const BSFixedString& aNewFilename)
	{
		if (QMessageBox::warning(this, pDialogTitleC, QString::asprintf("Are you sure you would like to move %s to %s?", SharedTools::MakePerforcePath(aOldFilename.QString()).QString(), SharedTools::MakePerforcePath(aNewFilename.QString()).QString()), QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes)
		{
			// If a file with the same name already exists in the destination directory, we must abort moving the file.
			if (BSaccess(aNewFilename, 0) == -1)
			{
				BSPerforce::ConnectionSmartPtr spperforce;
				CSPerforce::Perforce::QInstance().QPerforce(spperforce);
				BSPerforce::FileInfo fileInfo;
				if ((spperforce || !UseVersionControl) && PromptToSaveChanges())
				{
					BSComponentDB2::StorageService& rstorage = BSMaterial::Internal::QDBStorage();
					BSComponentDB2::ID object = rstorage.GetObjectByFilename(aOldFilename);
					if (object != BSComponentDB2::NullIDC)
					{
						bool moveHappened = false;
						const bool movingCurrentDocument = object.QValue() == EditedMaterialID.QID().QValue();

						if (UseVersionControl && spperforce->GetFileInfo(aOldFilename, fileInfo))
						{
							if (fileInfo.IsCheckedOut() && !fileInfo.QHasOtherCheckouts())
							{
								if (spperforce->RenameFile(aOldFilename, aNewFilename))
								{
									moveHappened = true;

									if (fileInfo.QAction() != BSPerforce::FileInfo::ACTION_ADD)
									{
										BSTArray<BSFixedString> p4FilesToSubmit;
										BSFixedString oldP4FilePath(SharedTools::MakePerforcePath(aOldFilename.QString()).QString());
										BSFixedString newP4FilePath(SharedTools::MakePerforcePath(aNewFilename.QString()).QString());
										p4FilesToSubmit.Add(oldP4FilePath);
										p4FilesToSubmit.Add(newP4FilePath);
										moveHappened = SharedTools::CheckinFiles(this, pDialogTitleC, p4FilesToSubmit);
									}

									if (!moveHappened)
									{
										// User canceled or we failed to submit the move, revert the rename/move change.
										spperforce->RenameFile(aNewFilename, aOldFilename);
									}
								}
							}
							else
							{
								QMessageBox::warning(nullptr, pDialogTitleC, "Cannot move this material.  You do not have it checked out or it is also checked out by someone else.");
							}
						}
						else
						{
							// We are moving a local file.
							auto status = BSSystemFile::RenameFile(aOldFilename, aNewFilename);
							moveHappened = status == BSSystemFile::EC_NONE;

							if (!moveHappened)
							{
								QString errorMessage;
								errorMessage = QString::asprintf("Failed to move file.  Error code: %d", status);
								QMessageBox::warning(this, pDialogTitleC, errorMessage);
							}
						}

						if (moveHappened)
						{
							// Requests to save the new filename for the move.
							BSMaterial::SaveAs(BSMaterial::LayeredMaterialID(object), aNewFilename);

							// Update the UI.
							
							// Make sure to update cache for newly moved file
							QtPerforceFileInfoCache::QInstance().UpdateCacheAsync(aNewFilename.QString());
							
							ui.pMaterialBrowserWidget->Refresh();
							if (movingCurrentDocument)
							{
								// Reopen the file.
								Open(BSMaterial::LayeredMaterialID(object));
							}
							RefreshTimer.start(MaterialPreviewRefreshTimerC);
						}
					}
					else
					{
						QMessageBox::information(this, pDialogTitleC, "Cannot move file.  Object for file being moved could not be found.");
					}
				}
				else
				{
					QMessageBox::information(this, pDialogTitleC, "Cannot move file.  Unable to connect to Perforce and retrieve file info.  Please check Perforce settings.");
				}
			}
			else
			{
				QMessageBox::information(this, pDialogTitleC, "Cannot move file.  A file with the same name already exists in the destination directory.");
			}
		}
	}

	/// <summary> Create a new Untitled Material based on the BaseMaterial shader model </summary>
	void MaterialLayeringDialog::NewUntitledMaterial()
	{
		// Make an empty layered material, derived from BaseMaterial
		BSMaterial::LayeredMaterialID base = BSMaterial::GetLayeredMaterial(BSFixedString(pUntitledMaterialDataParentC));
		if (base.QValid())
		{
			Open(BSMaterial::CreateLayeredMaterialInstance(base, BSFixedString(pUntitledNameC)));
		}
		RefreshTimer.start(MaterialPreviewRefreshTimerC);
	}

	/// <summary> Renames a file </summary>
	/// <param name="aFile"> File to rename </param>
	void MaterialLayeringDialog::Rename(const BSFixedString& aFile)
	{
		BSPerforce::ConnectionSmartPtr spperforce;
		CSPerforce::Perforce::QInstance().QPerforce(spperforce);
		if ((spperforce || !UseVersionControl) && PromptToSaveChanges())
		{
			BSFixedString oldLocalFilePath(SharedTools::MakeLocalPath(aFile.QString()).QString());
			BSComponentDB2::StorageService& rstorage = BSMaterial::Internal::QDBStorage();
			BSComponentDB2::ID object = rstorage.GetObjectByFilename(oldLocalFilePath);
			BSFixedString prevName;
			BSMaterial::GetName(BSMaterial::LayeredMaterialID(object), prevName);
			const QString oldNameQString(prevName.QString());
			if (object != BSComponentDB2::NullIDC)
			{
				bool loop = true;
				while (loop)
				{
					bool ok = false;
					QString newName = QInputDialog::getText(this, pDialogTitleC, "New Name", QLineEdit::Normal, oldNameQString, &ok);

					// Skip doing anything if the name is unchanged.
					if (ok && oldNameQString.compare(newName, Qt::CaseInsensitive) != 0)
					{
						QString message;
						if (SharedTools::ValidateNewMaterialName(newName, message))
						{
							// Construct the new file path.
							QString newLocalFilePath = "\\" + newName;
							BSFilePathString root, oldName, ext;
							FilePathUtilities::SplitPath(oldLocalFilePath, root, oldName);
							newLocalFilePath = root + newLocalFilePath;
							FilePathUtilities::SplitExt(oldLocalFilePath, root, ext);
							newLocalFilePath += ext;

							// Rename the layered material and all of its sub-objects.
							BSMaterial::LayeredMaterialID renamedMaterial = BSMaterial::LayeredMaterialID(object);
							BSMaterial::RenameAll(renamedMaterial, BSFixedString(newName.toLatin1().data()));

							// Requests to save the changes.
							BSMaterial::SaveAs(BSMaterial::LayeredMaterialID(object), newLocalFilePath.toLatin1().data());

							bool cancelRename = false;

							if (UseVersionControl)
							{
								BSFixedString oldP4FilePath(SharedTools::MakePerforcePath(aFile.QString()).QString());
								BSPerforce::FileInfo fileInfo;

								if (spperforce->GetFileInfo(oldP4FilePath, fileInfo) && fileInfo.QAction() != BSPerforce::FileInfo::ACTION_ADD)
								{
									spperforce->RevertFile(oldLocalFilePath);
									spperforce->MarkForDelete(oldLocalFilePath);
									spperforce->AddFile(newLocalFilePath.toLatin1().data());

									BSTArray<BSFixedString> p4FilesToSubmit;
									p4FilesToSubmit.Add(oldP4FilePath);
									p4FilesToSubmit.Add(newLocalFilePath.toLatin1().data());
									cancelRename = !SharedTools::CheckinFiles(this, pDialogTitleC, p4FilesToSubmit);
								}
								else
								{
									const uint32_t changelistNumber = SharedTools::FindOrCreateChangelist(sMaterialDefaultChangeListDesc.String());
									if (spperforce->AddFile(QStringToCStr(newLocalFilePath), changelistNumber))
									{
										spperforce->RevertFile(oldLocalFilePath);
										BSDeleteFile(oldLocalFilePath);
									}
								}
							}
							else if (!BSDeleteFile(oldLocalFilePath))
							{		
								QMessageBox::warning(this, pDialogTitleC, "Unable to rename file. Make sure that it is not read only");
								cancelRename = true;					
							}

							if (cancelRename)
							{
								if (UseVersionControl)
								{
									// Restore old name.
									spperforce->RevertFile(oldLocalFilePath);
								}

								BSMaterial::RenameAll(renamedMaterial, BSFixedString(oldName));
								BSMaterial::SaveAs(BSMaterial::LayeredMaterialID(object), oldLocalFilePath);

								// Delete the renamed version.
								if (UseVersionControl)
								{
									spperforce->RevertFile(newLocalFilePath.toLatin1().data());
								}

								BSDeleteFile(newLocalFilePath.toLatin1().data());
							}
							else
							{
								if (EditedMaterialID == renamedMaterial)
								{
									// Reopen the file.
									Close();
									Open(renamedMaterial);
								}
							}

							BSMaterial::Flush();
							ui.pMaterialBrowserWidget->Refresh();

							loop = false;
						}
						else
						{
							QMessageBox::warning(this, pDialogTitleC, message);
						}
					}
					else
					{
						// User cancelled
						loop = false;
					}
				}
			}
			else
			{
				QMessageBox::information(this, pDialogTitleC, "Cannot rename because object for material layer could not be found.");
			}
		}
		else
		{
			QMessageBox::information(this, pDialogTitleC, "Cannot rename.  Please check Perforce settings.");
		}
	}

	/// <summary> Revert a set of files </summary>
	/// <param name="aFiles"> Files to revert </param>
	void MaterialLayeringDialog::Revert(const BSTArray<BSFixedString>& aFiles)
	{
		BSTArray<BSFixedString> filesToRevert;

		//Check if any of the files to revert have icons or maps 
		for (const BSFixedString &rFile : aFiles)
		{
			BSFilePathString materialName;
			FilePathUtilities::GetFileName(rFile, materialName);

			BSFilePathString iconPath;
			if (GetMaterialIconPath(materialName, iconPath))
			{
				filesToRevert.Add(iconPath);
			}

			stl::scrap_vector<BSFilePathString> mapPaths = pBakeOptionsDialog->GetMaterialMapPaths(materialName, true);
			for (BSFilePathString& rpath : mapPaths)
			{
				filesToRevert.Add(rpath);
			}
		}

		aFiles.AppendTo(filesToRevert);

		// Revert all open material assets
		// NOTE: This may cause the current material to be deleted (if it was a newly added one)
		if (SharedTools::RevertFiles(this, pDialogTitleC, filesToRevert))
		{
			SharedTools::CursorScope cursor(Qt::WaitCursor);

			// If our edited material was newly added, it will have been deleted by the revert operation
			if (!EditedFileExists())
			{
				Close();
			}

			// Reload all assets
			BSMaterial::LoadAll();

			// Update the UI
			OnRefreshPropertyEditor();
		}
	}

	/// <summary> SLOT: Revert all opened material files </summary>
	void MaterialLayeringDialog::RevertAll()
	{
		Revert(GetCheckedOutFiles(this, PerforceSyncPath.QString()));
	}

	/// <summary> SLOT: Toggles experimental mode shaders on/off </summary>
	void MaterialLayeringDialog::ToggleExperimentalModeShaders()
	{
		const bool bNewExperimentalModeEnabled = !CreationRenderer::Material::GetExperimentalModeEnabled();
		CreationRenderer::Material::SetExperimentalModeEnable(bNewExperimentalModeEnabled);
	}

	/// <summary> SLOT: Check Perforce if there are new files in the depot or checked out </summary>
	void MaterialLayeringDialog::CheckForNewerFiles()
	{
		BSPerforce::ConnectionSmartPtr spperforce;
		CSPerforce::Perforce::QInstance().QPerforce(spperforce);
		if (spperforce)
		{
			// Prompt the user to sync if new(er) files are available
			if (SyncLatestOnOpening && spperforce->NewerFilesAvailable(PerforceSyncPath.QString()))
			{
				if (bSynchWithoutPrompt)
				{
					ReloadAll();
				}
				else
				{
					// Set up an asynchronous always-on-top but non-modal messagebox
					// NOTE: A regular QMessageBox exec() caused issues here when the user quickly changed focus while the Material Editor was opening.
					QMessageBox* ppopup = new QMessageBox(QMessageBox::Information, pDialogTitleC, "Newer material files are available in Perforce.\nWould you like to sync?", QMessageBox::Yes | QMessageBox::No, this);
					ppopup->setWindowModality(Qt::NonModal);
					ppopup->setAttribute(Qt::WA_DeleteOnClose);
					ppopup->setWindowFlags(ppopup->windowFlags() | Qt::WindowStaysOnTopHint);
					ppopup->setDefaultButton(QMessageBox::Yes);

					connect(ppopup, &QMessageBox::finished,
						[this](int32_t aResult)
						{
							if (aResult == QMessageBox::Yes)
							{
								ReloadAll();
							}
						});

					ppopup->show();
					ppopup->raise();
					ppopup->activateWindow();
				}

				SyncLatestOnOpening = false;
			}
		}
		else if (UseVersionControl)
		{
			QMessageBox::information(this, pDialogTitleC, "The Material editor expects a Perforce connection to the Data depot.\nYou can set it up in File > Preferences > Perforce");
			UseVersionControl = false;
		}
	}

	//////////////////////////////////////////////////////////////////////////

	/// <summary> SLOT: Called whenever any property was modified by the user </summary>
	void MaterialLayeringDialog::OnMaterialPropertyChanged()
	{
		BSMaterial::Internal::QDBStorage().NotifyObjectModified(EditedMaterialID);
		BSMaterial::MaterialChangeNotifyService::QInstance().Flush();

		// Update the preview widget
		AdjustSceneForDecalPreview();
		UpdatePreview();
		UpdateDocumentModified();

		// Finally, if there is a change in the ShaderModel (rule processor) then reload the current material in the property editor
		// to update the visible properties.
		UpdateShaderModel();
	}

	/// <summary> SLOT: Called whenever the file is changed. </summary>
	/// <param name="aFilePath"> The new file path. </param>
	void MaterialLayeringDialog::OnPreviewFileChanged(const QString& aFilePath)
	{
		pFormPreviewWidget->PreviewObject(BSFixedString(QStringToCStr(aFilePath)));
		sRecentPreviewMeshFile = QStringToCStr(aFilePath);
	}

	/// <summary>
	/// SLOT : Indicate that we need to forcefully regenerate the Property Editor Model and Adapter and trigger a refresh of the property editor.
	/// </summary>
	void MaterialLayeringDialog::OnRefreshPropertyEditor()
	{
		ui.treeViewPropEditor->BeginRefresh();
		BuildPropertyEditor();
		ui.treeViewPropEditor->EndRefresh();

		// If we specified a post drop material to focus on (the dropped material), focus it, else focus current document on save/refresh.
		const BSMaterial::LayeredMaterialID invalidMaterialIDC(BSMaterial::NullIDC);
		BSMaterial::LayeredMaterialID browserMaterialToFocus = EditedMaterialID;
		ui.pMaterialBrowserWidget->SelectMaterial(FocusedMaterialID == invalidMaterialIDC ? EditedMaterialID : FocusedMaterialID);
		// Clear focus drop target state for next refresh.
		FocusedMaterialID = invalidMaterialIDC;

		// Refresh Asset and Tags Checkpoint in memory
		CreationKit::Services::AssetMetaDB::RefreshCheckpoint([this](bool aSuccess) {
			if (aSuccess)
			{
				SharedTools::CursorScope cursor(Qt::WaitCursor);
				ui.pMaterialBrowserWidget->Refresh();
			}
		});
	}

	/// <summary> SLOT: On Refresh we update the the biome combobox in the Preview Widget </summary>
	void MaterialLayeringDialog::OnRefreshPreviewBiomes()
	{
		if (pFormPreviewWidget)
		{
			pFormPreviewWidget->RefreshBiomeComboBox();
		}
	}

	/// <summary> Check if there are modified files that have not yet been saved, and if so, prompt the user to save them </summary>
	/// <returns> True if the user chose to save/revert, false if the user chose to cancel (meaning they wish to keep editing the current document) </returns>
	bool MaterialLayeringDialog::PromptToSaveChanges()
	{
		bool result = true;
		if (EditedMaterialIsModified)
		{
			// Prompt the user to save
			BSFixedString name;
			BSMaterial::GetName(EditedMaterialID, name);
			switch (QMessageBox::information(this, "Save", QString::asprintf("Save unsaved changes to %s?", name.QString()), QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel))
			{
			case QMessageBox::Yes:
				result = Save();
				break;
			case QMessageBox::No:
				// Reload the material and derived object
				BSMaterial::ReloadMaterial(EditedMaterialID);
				OnRefreshPropertyEditor();
				break;
			case QMessageBox::Cancel:
				result = false;
				break;
			}
		}
		return result;
	}

	/// <summary> Updates EditedMaterialIsModified and the material browser </summary>
	void MaterialLayeringDialog::UpdateDocumentModified()
	{
		// See if our current document has unsaved changes
		if (EditedMaterialID.QValid())
		{
			// currently edited LOD information
			const bool editingLOD = EditedMaterialID != EditedSubMaterial;
			QString windowTitle;
			QTextStream windowTitleStream(&windowTitle);

			EditedMaterialIsModified = BSMaterial::Internal::QDBStorage().IsFileModified(EditedMaterialID);
			// Always refresh Dialog title bar ( Material and Shader Model )
			BSFixedString name;
			BSMaterial::GetName(EditedMaterialID, name);
			// Use Shader Model Display Name if any exist.
			BSFixedString shaderModelName = GetShaderModelName(EditedMaterialID);
			BSFixedString smDisplayName = GetShaderModelDisplayName(shaderModelName);
			windowTitleStream << name.QString() << (EditedMaterialIsModified ? "*" : "") << "(" << smDisplayName.QString() << ")";
			if (editingLOD)
			{
				bool ok = false;
				int32_t lodLevelEnumValue = ui.lodCombo->itemData(ui.lodCombo->currentIndex()).toInt(&ok);
				if (ok)
				{
					BSMaterial::LevelOfDetail level = static_cast<BSMaterial::LevelOfDetail>(lodLevelEnumValue);
					BSFixedString lodLvlShaderModelName = GetShaderModelName(EditedSubMaterial);
					BSFixedString lodLvlSMDisplayName = GetShaderModelDisplayName(lodLvlShaderModelName);
					windowTitleStream << " LOD: " << BSReflection::EnumToDisplayName(level) << "(" << lodLvlSMDisplayName.QString() << ")";
				}
			}

			windowTitleStream << " - " << pDialogTitleC;

			setWindowTitle(windowTitle);
			if (ui.pMaterialBrowserWidget->SetActiveDocument(EditedMaterialID.QID(), EditedMaterialIsModified))
			{
				ui.pMaterialBrowserWidget->SelectMaterial(EditedMaterialID);
			}
		}
		else
		{
			EditedMaterialIsModified = false;
			if (ui.pMaterialBrowserWidget->SetActiveDocument(BSComponentDB2::NullIDC, false))
			{
				setWindowTitle(pDialogTitleC);
			}
		}
	}

	/// <summary> Updates document related buttons according to whether or not we have an active document </summary>
	void MaterialLayeringDialog::UpdateButtonState()
	{
		bool sourceDepotValid = SourceTextureDepotPathValid();
		if (!sourceDepotValid && UseVersionControl)
		{
			// Make sure to warn when dialog only when dialog is visible the first time.
			if (isVisible())
			{
				QMessageBox::warning(this, pDialogTitleC, QString::asprintf("Source texture depot folder not mapped in source control workspace : \n%s, some features may be disabled such as saving.", sPerforceSourceTextureDepotPath.String()));
				UseVersionControl = false;
			}
		}
		bool enabled = EditedMaterialID.QValid();
		bool hasPerforce = CSPerforce::Perforce::QInstance().QPerforceAvailable() && sourceDepotValid;
		ui.actionCreateNewMaterial->setEnabled(enabled);
		ui.syncTexturesButton->setEnabled(hasPerforce && enabled);
		ui.actionSave->setEnabled(enabled);
		ui.actionSaveAs->setEnabled(enabled);
		ui.actionCheckIn->setEnabled(hasPerforce);
		ui.actionCheckOut->setEnabled(enabled && hasPerforce);
		ui.actionRevertAllCheckedOutFiles->setEnabled(hasPerforce);

		ui.addLayerButton->setEnabled(CanAddLayer());
		ui.removeLayerButton->setEnabled(CanRemoveLayer());
	}

	/// <summary> Check if the user can add a layer to the material. </summary>
	/// <returns> True if number of layers in use is less than the total number of layers in a shader model. </returns>
	bool MaterialLayeringDialog::CanAddLayer() const
	{
		return MaterialSMState.LayersInUse < MaterialSMState.LayerCount;
	}

	/// <summary> Check if the user can remove a layer to the material. </summary>
	/// <returns> True if the number of layers in use is greater than zero. </returns>
	bool MaterialLayeringDialog::CanRemoveLayer() const
	{
		return MaterialSMState.LayersInUse > 0;
	}

	/// <summary> Updates the property editor should the shader model has changed. </summary>
	void MaterialLayeringDialog::UpdateShaderModel()
	{
		using namespace QtPropertyEditor;
		bool shouldReload = false;

		// Only apply ShaderModel processors on User Materials. When editing a RootMaterial, we want to
		// have all the properties shown.
		const bool isRootMaterial = BSMaterial::IsShaderModelRootMaterial(EditedMaterialID);

		if (!isRootMaterial)
		{
			// Get the shader model used by the Material if any and apply the rule processor automatically.
			const BSFixedString shaderModelName = GetShaderModelName(EditedMaterialID);
			auto& processors = ui.treeViewPropEditor->QProcessors();

			if (!shaderModelName.QEmpty())
			{
				// Compare the Material shader model with the one we have currently loaded as processor.
				// If we do not find the rule processor as being the same, it means it has changed and thus need to cause a
				// UI property refresh.
				std::shared_ptr<RuleProcessor> processorToApply = SharedTools::GetShaderModelRuleProcessor(shaderModelName);
				if (processorToApply)
				{
					shouldReload = std::find(processors.begin(), processors.end(), processorToApply) == processors.end();
				}
			}
			else if (processors.size() > 0)
			{
				// In the event that the user has set it back to "None" (empty smComponent filename), make sure no Shader Model is applied.
				processors.clear();
				shouldReload = true;
			}
		}

		if (shouldReload)
		{
			// Processor is outdated and needs to be updated.
			OnRefreshPropertyEditor();
		}
	}

	/// <summary> Calculate available(visible) Material properties after Shader Model has processed the hierarchy. </summary>
	void MaterialLayeringDialog::UpdateMaterialShaderModelState()
	{
		SharedTools::CalculateShaderModelState(*ui.treeViewPropEditor->QTreeNode(), MaterialSMState);
	}

	/// <summary>
	/// Isolates the first layer in the material by hiding all the layers above it.
	/// </summary>
	void MaterialLayeringDialog::IsolateFirstLayer()
	{
		for (uint16_t layerIdx = 1; layerIdx < BSMaterial::MaxLayerCountC; layerIdx++)
		{
			const BSMaterial::LayerID layerID = BSMaterial::GetLayer(EditedMaterialID, layerIdx);
			if (layerID.QValid())
			{
				const BSMaterial::HideSoloData hsData{ true, false };
				BSMaterial::SetHideSoloData(layerID, hsData);
			}
		}
	}

	/// <summary> SLOT: Called when a property node is about to change, but before the actual change occurs.</summary>
	/// <param name="aIndex"> Index of the property that changed </param>
	/// <param name="aPreviousValue"> The previous value of the model node. </param>
	/// <param name="aNewValue"> The target new value of the model node.</param>
	void MaterialLayeringDialog::OnPropertyChanging(const QModelIndex& aIndex, const QVariant &aPreviousValue, const QVariant &aNewValue)
	{
		QtPropertyEditor::ModelNode* pchangedNode = ui.treeViewPropEditor->GetNode(aIndex);
		BSASSERT(pchangedNode != nullptr, "ui.treeViewPropEditor->GetNode() returned nullptr");

		QtPropertyEditor::ModelNode* pparent = pchangedNode->QParent();
		while (pparent != nullptr)
		{
			if (pparent->QModel())
			{
				BSMaterial::LayerID layerID;
				if (pparent->GetNativeValue(BSReflection::Ptr(&layerID)))
				{
					if (layerID.QValid())
					{
						const BSMaterial::HideSoloData hsData = BSMaterial::GetHideSoloData(layerID);
						
						if (hsData.Hide)
						{
							QMessageBox::warning(this, pDialogTitleC, "You are editing a layer that is hidden");
							break;
						}
					}
				}
			}

			pparent = pparent->QParent();
		}


		QtPropertyEditor::UndoCommand* pcommand = new QtPropertyEditor::UndoCommand(pchangedNode->QModel(), pchangedNode->QDataPath(), aPreviousValue, aNewValue, ui.treeViewPropEditor);
		if (pcommand != nullptr)
		{
			QtPropertyEditor::UndoSignalBlocker block(pcommand);
			pUndoRedoStack->push(pcommand);
		}
	}

	/// <summary>
	/// SLOT : Called when the controllers are refreshed on the bound property dialog. Used to set default values for controllers.
	/// </summary>
	/// <param name="aspController">The controller.</param>
	/// <param name="apNode">The current selected node.</param>
	void MaterialLayeringDialog::OnMaterialPropertyControllerRefreshed(BSBind::ControllerPtr aspController, BSBind::NodePtr apNode)
	{
		const BSBind::DataBindingHandle spbindingHandle = BSMaterialBinding::CreateBindingHandleForProperty(EditedMaterialID, apNode);
		if (spbindingHandle != nullptr)
		{
			aspController->SetDefaultValue(*spbindingHandle);
		}
	}

	/// <summary>
	/// SLOT: Called when a material is selected in the material browser widget.
	/// </summary>
	/// <param name="aMaterialId">The ID of the selected material.</param>
	void MaterialLayeringDialog::OnBrowserMaterialPicked(const BSMaterial::LayeredMaterialID& aMaterialId)
	{
		if (PromptToSaveChanges())
		{
			Open(aMaterialId);
		}
	}

	/// <summary>
	/// SLOT: Called when any shader source file change has been detected by the browser widget.
	/// </summary>
	/// <param name="aPath">Path of the shader model template that has changed.</param>
	void MaterialLayeringDialog::OnShaderModelFileChanged(const QString& /*aPath*/)
	{
		// Clear assigned shared ptr processors.
		ui.treeViewPropEditor->QProcessors().clear();

		// Force a refresh of the property editor with current Material & Shader Model edited if any.
		OnRefreshPropertyEditor();
	}

	/// <summary> SLOT: Called when the user triggers an Undo command </summary>
	void MaterialLayeringDialog::Undo()
	{
		if (pUndoRedoStack->canUndo())
		{
			pUndoRedoStack->undo();
		}
	}

	/// <summary> SLOT: Called when the user triggers a Redo command </summary>
	void MaterialLayeringDialog::Redo()
	{
		if (pUndoRedoStack->canRedo())
		{
			pUndoRedoStack->redo();
		}
	}

	/// <summary>Creates a new undo command </summary>
	/// <param name="aUndoAction">The undo action</param>
	/// <param name="aRedoAction">The redo action</param>
	/// <param name="apData">The undoredo data context</param>
	/// <returns> A pointer to a new undo command </returns>
	QUndoCommand* MaterialLayeringDialog::MakeNewUndoCommand(MaterialLayeringDialog::UndoCallback&& aUndoAction, MaterialLayeringDialog::UndoCallback&& aRedoAction, void* apData)
	{
		QUndoCommand* pcommand = new MaterialLayeringUndoCommand(std::move(aUndoAction), std::move(aRedoAction), apData);
		pUndoRedoStack->push(pcommand);
		return pcommand;
	}


	/// <summary>Constructor for undo commands within the Material Layering dialog </summary> 
	/// <param name="aUndoAction">The action to execute on undo </param>
	/// <param name="aRedoAction">The action to execute on redo </param>
	/// <param name="apData">A pointer to the undoredo context data. </param>
	/// <param name="apParent"> The parent command </param>
	MaterialLayeringUndoCommand::MaterialLayeringUndoCommand(
		MaterialLayeringDialog::UndoCallback && aUndoAction,
		MaterialLayeringDialog::UndoCallback && aRedoAction,
		void* apData,
		QUndoCommand* apParent) : QUndoCommand(apParent)
	{
		UndoAction = MaterialLayeringDialog::UndoCallback(std::move(aUndoAction));
		RedoAction = MaterialLayeringDialog::UndoCallback(std::move(aRedoAction));
		pData = apData;
	}

	/// <summary>Executes the undo action if possible </summary> 
	void MaterialLayeringUndoCommand::undo()
	{
		UndoAction(pData);
	}

	/// <summary>Executes the redo action if possible </summary> 
	void MaterialLayeringUndoCommand::redo()
	{
		RedoAction(pData);
	}

} // MaterialLayering namespace
