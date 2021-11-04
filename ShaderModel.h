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
// FILE ShaderModel.h
// AUTHOR Christian Roy
// OWNER Christian Roy
// DATE 5/13/2020
//----------------------------------------------------------------------

#pragma once

#ifndef SHAREDTOOLS_SHADERMODEL_H_
#define SHAREDTOOLS_SHADERMODEL_H_

#include <BSMaterial/BSMaterialFwd.h>
#include <BSSystem/BSFixedString.h>
#include <SharedTools/Qt/Widgets/PropertyEditor/GenericEditorBuilder.h>
#include <SharedTools/Qt/Widgets/PropertyEditor/RuleProcessor.h>

class QWidget;

/// --------------------------------------------------------------------------------
/// <summary>
/// ShaderModel utility functions and constants.
/// </summary>
/// --------------------------------------------------------------------------------
namespace SharedTools
{
	/// <summary> Material Shader Model state </summary>
	struct ShaderModelState
	{
		uint16_t LayersInUse = 0;
		uint16_t LayerCount = 0;
		uint16_t BlenderCount = 0;
	};

	// ShaderModel Utilities
	bool IsBaseMaterial(BSMaterial::LayeredMaterialID aMaterialID);
	bool IsExperimental(const BSFixedString& aShaderModelName);
	bool GetShaderModelAllowed(bool aShaderModelIsExperimental);
	BSFilePathString GetShaderModelWatchFolder();
	bool CreateNewShaderModel(QWidget* apParent, BSFixedString& aOutShaderModelName, BSFixedString& aOutShaderModelFileName, BSMaterial::LayeredMaterialID& aOutCreatedRootMaterial);
	void CalculateShaderModelState(QtPropertyEditor::ModelNode& arMaterialPropertyEditorRootNode, ShaderModelState& arState);
	void MigrateShaderModelProperties(QtPropertyEditor::ModelNode& arMaterialPropertyEditorRootNode, BSMaterial::LayeredMaterialID aShaderModelRootMaterial);
	void SaveShaderModelToFile(const BSFixedString& aShaderModelAbsoluteFilePath);
	stl::vector<std::string> GetShaderModelTemplateList();
	std::shared_ptr<QtPropertyEditor::RuleProcessor> GetShaderModelRuleProcessor(const BSFixedString& aShaderModelName);

	// ShaderModel metadata utilities
	BSFixedString GetShaderModelRootMaterial(const BSFixedString& aShaderModelName);
	void SetShaderModelRootMaterial(const BSFixedString& aShaderModelName, const BSFixedString& aRootMaterialName);
	bool GetShaderModelLocked(const BSFixedString& aShaderModelName);
	bool GetShaderModelSwitchable(const BSFixedString& aShaderModelName);
	BSFixedString GetShaderModelDisplayName(const BSFixedString& aShaderModelName);
	void GetShaderModelDisplayNameMap(stl::scrap_unordered_map<BSFixedString, BSFixedString>& arDisplayNames);
	const BSFixedString ResolveShaderModelDisplayName(const BSFixedString& aDisplayName, const stl::scrap_unordered_map<BSFixedString, BSFixedString>& aDisplayNameMap);
	bool GetShaderModelUsesLevelOfDetail(const BSFixedString& aShaderModelName);

	void CopyAndSwitchMaterial(BSMaterial::LayeredMaterialID aSrcMaterialId, BSMaterial::LayeredMaterialID aShaderModelRootMaterialId, const BSFixedString& aDestMaterialFilePath);

	/// <summary> Widgets that want to consume the Material Shader Model state </summary>
	class IShaderModelStateConsumer
	{
	public:
		virtual ~IShaderModelStateConsumer(  ) = default;
		virtual void ProcessShaderModelState(const SharedTools::ShaderModelState& aShaderModelState) = 0;
	};

}

#endif // SHAREDTOOLS_SHADERMODEL_H_