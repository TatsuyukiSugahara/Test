#include "stdafx.h"
#include "ComponentCommands.h"
#include "Level/LevelEditModel.h"


namespace editor
{
	RenameNodeCommand::RenameNodeCommand(const uint32_t nodeId, const std::string& oldName, const std::string& newName)
		: nodeId_(nodeId)
		, oldName_(oldName)
		, newName_(newName)
	{
	}


	bool RenameNodeCommand::Execute(aq::level::LevelEditModel& model)
	{
		return model.SetNodeName(nodeId_, newName_);
	}


	void RenameNodeCommand::Undo(aq::level::LevelEditModel& model)
	{
		// Undo / Redo を作るときに実装する(nodeId_ の名前を SetNodeName で oldName_ へ戻す)
		(void)model;
	}


	/************************************/




	SetFieldCommand::SetFieldCommand(const uint32_t nodeId, const std::string& typeName, const std::string& key,
	                                 const aq::util::JsonValue& oldValue, const aq::util::JsonValue& newValue)
		: nodeId_(nodeId)
		, typeName_(typeName)
		, key_(key)
		, oldValue_(oldValue)
		, newValue_(newValue)
	{
	}


	bool SetFieldCommand::Execute(aq::level::LevelEditModel& model)
	{
		// 参照ノードの基準の付け替えと、そのキーだけの書き換えはモデルが行う
		return model.SetComponentField(nodeId_, typeName_, key_, newValue_);
	}


	void SetFieldCommand::Undo(aq::level::LevelEditModel& model)
	{
		// Undo / Redo を作るときに実装する(SetComponentField で key_ を oldValue_ へ戻す)
		(void)model;
	}


	/************************************/




	AddComponentCommand::AddComponentCommand(const uint32_t nodeId, const std::string& typeName)
		: nodeId_(nodeId)
		, typeName_(typeName)
	{
	}


	bool AddComponentCommand::Execute(aq::level::LevelEditModel& model)
	{
		// 既定値で足す。参照ノードや、すでに持っている型ならモデルが断る
		return model.AddComponent(nodeId_, typeName_);
	}


	void AddComponentCommand::Undo(aq::level::LevelEditModel& model)
	{
		// Undo / Redo を作るときに実装する(RemoveComponent で typeName_ を取り外す)
		(void)model;
	}


	/************************************/




	RemoveComponentCommand::RemoveComponentCommand(const uint32_t nodeId, const std::string& typeName)
		: nodeId_(nodeId)
		, typeName_(typeName)
	{
	}


	bool RemoveComponentCommand::Execute(aq::level::LevelEditModel& model)
	{
		// 取り外した値は捨てずに持つ(取り消しで同じ値のまま戻せるように)
		removedJson_ = aq::util::JsonValue();
		return model.RemoveComponent(nodeId_, typeName_, &removedJson_);
	}


	void RemoveComponentCommand::Undo(aq::level::LevelEditModel& model)
	{
		// Undo / Redo を作るときに実装する(AddComponent で typeName_ を removedJson_ の値で戻す)
		(void)model;
	}
}
