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
// FILE 	MaterialLayeringDialog.h
// OWNER 	Christian Roy
// DATE 	2019-02-25
//----------------------------------------------------------------------

#pragma once

#ifndef SHARED_TOOLS_MATERIAL_LAYERING_DIALOG_H
#define SHARED_TOOLS_MATERIAL_LAYERING_DIALOG_H

#include <BSMain/BSBind_Controller.h>
#include <BSMaterial/BSMaterialFwd.h>
#include <BSReflection/EnumerationType.h>
#include <BSSystem/BSService.h>
#include <Construction Set/Services/AssetHandlerService.h>
#include <SharedTools/ShaderModel/ShaderModel.h>

// QT Includes
#include <SharedTools/Qt/QtSharedIncludesBegin.h>
#include "ui_MaterialLayeringDialog.h"
#include <QtCore/QFutureWatcher>
#include <QtCore/QTimer>
#include <QtWidgets/QShortcut>
#include <QtWidgets/QDialog>
#include <QtWidgets/QUndoStack>
#include <SharedTools/Qt/QtSharedIncludesEnd.h>
// \ QT Includes

class PreviewWidget;
class MaterialLayeringBakeOptionsDialog;
class QUndoStack;

namespace QtPropertyEditor
{
	class QtGenericPropertyEditor;
	class ModelNode;
}
namespace SharedTools
{
	class QtMaterialBrowserProxyModel;
	class MaterialModelProxy;

	/// <summary> The Material editor's main window </summary>
	class MaterialLayeringDialog :
		public QDialog,
		public BSService::ServiceBaldPointer<MaterialLayeringDialog>,
		public CreationKit::Services::IAssetHandler
	{
		Q_OBJECT

	public:

		/// <summary> the signature for an undo redo callback </summary>
		using UndoCallback = stl::unique_function<void(void*)>;

		MaterialLayeringDialog(QWidget *apParent, BSService::Site& arSite);
		~MaterialLayeringDialog();

		static bool GetIsFileSupported(const char* apFilepath);
		bool Open(BSMaterial::LayeredMaterialID aMaterial);
		void Close();

		static HWND QWindowHandle() { return hwndDialog; }

		void OpenAsset(const char* apFileName) override;
		void SetMaterialPickerActive( bool aActive );

	signals:
		void Hidden();
		void SyncTexturesFinished();
		void SoloViewLayer(QWidget* apWidget, bool aIsSolo);
		void MaterialPickerActivationChanged(bool aNewActiveState);

	public slots:

		void OnMaterialsPickedFromRenderWindow(const std::vector<TESObjectREFRPtr>& aSelectedObjects);

	private slots:
		void CreateNew(const QString& aForcedPath);
		void CreateNewDerivedMaterial(BSMaterial::LayeredMaterialID aParentMaterial);
		void CreateNewMaterial(const BSFixedString& aName, BSMaterial::LayeredMaterialID aParentMaterial);
		void CreateNewShaderModel();
		void ExportBakedMaps(BSMaterial::LayeredMaterialID aMaterial);
		void SwitchMaterialToShaderModel(BSMaterial::LayeredMaterialID aMaterialToProcess);
		void ReloadAll();
		bool Save();
		bool SaveAs();
		bool SaveAll();

		void CheckInAll();
		void CheckOut();
		void RevertAll();
		void ToggleExperimentalModeShaders();
		void OnReparentMaterial(BSMaterial::LayeredMaterialID aParentMaterial);
		void OnMaterialPropertyChanged();
		void OnPreviewFileChanged(const QString& arFilePath);
		void OnRefreshPropertyEditor();
		void OnRefreshPreviewBiomes();
		void CheckForNewerFiles();
		void OnMaterialLayerDrop(const BSMaterial::LayeredMaterialID aMaterialId);
		void OnPropertyContextMenuRequest(const QPoint& aPoint);
		void OnAddLayer();
		void OnRemoveLayer();
		void OnSyncTextures();
		void OnSwitchEditedMaterialShaderModel();
		void OnRequestBreakInheritance(BSMaterial::LayeredMaterialID aMaterial);
		void OnRequestMultipleReparentToMaterial(QList<BSMaterial::LayeredMaterialID> aTargetIDList, BSMaterial::LayeredMaterialID aParentMaterial);
		void OnRequestMaterialAutomatedSmallInheritance(const QString& aForcedPath, BSMaterial::LayeredMaterialID aBaseMaterial, BSMaterial::LayeredMaterialID aNewShaderModelToUse);
		void OnSyncTexturesFinished();
		void UpdatePreview();
		void RenderPreview();
		void OnPropertyChanging(const QModelIndex& aIndex, const QVariant& aPreviousValue, const QVariant& aNewValue);
		void Undo();
		void Redo();

		void OnBrowserMaterialPicked(const BSMaterial::LayeredMaterialID& aMaterialId);
		void OnShaderModelFileChanged(const QString& aPath);

		void OnMaterialPropertyControllerRefreshed(BSBind::ControllerPtr aspController, BSBind::NodePtr apNode);
		void OnLODChanged(int32_t aIndex);

	private:
		void InitializeSignalsAndSlots();
		void InitializeEditingComponents();
		void InitializePreviewWidget();

		bool ReparentMaterial(QWidget* apParent, BSMaterial::LayeredMaterialID aTargetMaterial, BSMaterial::LayeredMaterialID aParentMaterial, bool aUserConfirmationPrompt =true);
		void AdjustSceneForDecalPreview(bool aForceOperation = false);
		void InitializeMaterialLayerButtonsCallbacks(QtPropertyEditor::ModelNode& arModelNode);
		void BuildPropertyEditor();
		void PropagateShaderModelState();
		void ApplyShaderModel(const char* apShaderModel);
		BSTArray<BSFixedString> CheckoutCurrentFiles(bool aVerbose, bool* apOutAllCheckedOut = nullptr);
		bool PromptToSaveChanges();
		void UpdateDocumentModified();
		void UpdateButtonState();
		void UpdateShaderModel();
		void UpdateMaterialShaderModelState();
		bool EditedFileExists() const;
		void RestoreMaterialBackup(void* apData);
		void RemoveLastLayer(void*);
		Json::Value* CreateMaterialBackup();
		QUndoCommand* MakeNewUndoCommand(UndoCallback&& aUndoAction, UndoCallback&& aRedoAction, void* apData);

		void Delete(const BSFixedString& aFile);
		void Move(const BSFixedString& aOldFilename, const BSFixedString& aNewFilename);
		void NewUntitledMaterial();
		void Rename(const BSFixedString& arFile);
		void Revert(const BSTArray<BSFixedString>& aFiles);
		void CheckIn(const BSTArray<BSFixedString>& ailes);
		void CheckOutFile(const BSFixedString& aFile);
		void FileMarkForAdd(const BSFixedString& aFile);
		void Sync(const char* apDepotPath);

		void SaveWindowState();
		void LoadWindowState();

		bool CanAddLayer() const;
		bool CanRemoveLayer() const;

		void IsolateFirstLayer();
		void UICustomProcessLayerNode(QtPropertyEditor::ModelNode& arNode);
		void UpdateLODCombo();
		void BuildIconsForBoundProperties(QtPropertyEditor::QtGenericPropertyEditor* apEditor, QtPropertyEditor::ModelNode& arNode);

		// from QDialog
		void closeEvent(QCloseEvent* apEvent) override;
		void showEvent(QShowEvent* apEvent) override;

		void reject() override;

		static HWND	hwndDialog;							// Our window handle
		static constexpr int32_t EditLODsDataC = -1;

		// Qt UI
		Ui::MaterialLayeringDialog ui;
		std::map<std::string, uint32_t> LayerNameToNumkeyMap;
		MaterialModelProxy* pMaterialModel = nullptr;
		QMenu *pPropertyContextMenu = nullptr;
		QTimer RefreshTimer;
		QDialog* pFormPreviewDialog = nullptr;
		PreviewWidget* pFormPreviewWidget = nullptr;
		MaterialLayeringBakeOptionsDialog* pBakeOptionsDialog = nullptr;

		BSService::Site& rSite;							// Site we're registered to
		QUndoStack*	pUndoRedoStack = nullptr;			// Stack of QUndoCommands
		BSString PerforceSyncPath;						// Path to sync material files from in Perforce
		QString SaveAsDir;								// The last folder the user saved to
		BSMaterial::LayeredMaterialID EditedMaterialID; // Current top level material that's being edited
		BSMaterial::LayeredMaterialID EditedSubMaterial;// Current LOD material that's being edited
		BSMaterial::LayeredMaterialID FocusedMaterialID;// Next Material to focus in the Material browser on refresh, if a Drag&Drop occurred.
		SharedTools::ShaderModelState MaterialSMState;	// Current Shader Model properties calculated dynamically.
		bool UIProcessorsActive = true;					// Set if we should apply any UI Processors when loading model nodes.
		bool EditedMaterialIsModified = false;			// If true there are unsaved changes
		bool EnableControllerVisualization = true;		// Determine if we want to visualize the controllers on a material
		bool SyncLatestOnOpening = true;				// Ask the user if they wish to sync to head
		bool UseVersionControl = true;					// Whether to allow Perforce operations
		bool PreviewingDecal = false;					// If the editor is currently previewing a decal.
	};

	/// <summary> Custom undo/redo commands for the Material Layering dialog </summary>
	class MaterialLayeringUndoCommand : public QUndoCommand
	{
	public:
		MaterialLayeringUndoCommand(
			MaterialLayeringDialog::UndoCallback&& aUndoAction,
			MaterialLayeringDialog::UndoCallback&& aRedoAction,
			void* apContext,
			QUndoCommand* apParent = nullptr);

		void undo() override;
		void redo() override;

	private:
		MaterialLayeringDialog::UndoCallback UndoAction;
		MaterialLayeringDialog::UndoCallback RedoAction;
		void* pData = nullptr;
	};

} // SharedTools namespace

#endif // SHARED_TOOLS_MATERIAL_LAYERING_DIALOG_H
