#include "aq.h"
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
		// 取り消しで戻す名前は、積んだ時点ではなく実行の時点のモデルから取る(同じフレームの先の操作を反映した名前にする)
		const aq::ecs::PrefabEditNode* node = model.FindNode(nodeId_);
		if (node == nullptr) return false;
		oldName_ = node->name;
		return model.SetNodeName(nodeId_, newName_);
	}


	bool RenameNodeCommand::Undo(aq::level::LevelEditModel& model)
	{
		return model.SetNodeName(nodeId_, oldName_);
	}


	/************************************/




	SetFieldCommand::SetFieldCommand(const uint32_t nodeId, const std::string& typeName, const std::string& key,
	                                 const aq::util::JsonValue& newValue)
		: nodeId_(nodeId)
		, typeName_(typeName)
		, key_(key)
		, newValue_(newValue)
	{
	}


	bool SetFieldCommand::Execute(aq::level::LevelEditModel& model)
	{
		// 変更前は書き換える直前のモデルから取る。参照ノードで未付け替えなら、付け替えで基準になる値(解決済みの値)が返る
		oldValue_ = aq::util::JsonValue();
		if (!model.GetComponentField(nodeId_, typeName_, key_, oldValue_)) return false;

		// 参照ノードの基準の付け替えと、そのキーだけの書き換えはモデルが行う
		return model.SetComponentField(nodeId_, typeName_, key_, newValue_);
	}


	bool SetFieldCommand::Undo(aq::level::LevelEditModel& model)
	{
		// 参照ノードの付け替えは取り消さない。変更前の値に戻せば保存の差分から消えるので、override に無かったキーは書かれない
		return model.SetComponentField(nodeId_, typeName_, key_, oldValue_);
	}


	/************************************/




	AddComponentCommand::AddComponentCommand(const uint32_t nodeId, const std::string& typeName)
		: nodeId_(nodeId)
		, typeName_(typeName)
	{
	}


	AddComponentCommand::AddComponentCommand(const uint32_t nodeId, const std::string& typeName, const aq::util::JsonValue& json)
		: nodeId_(nodeId)
		, typeName_(typeName)
		, json_(json)
	{
	}


	bool AddComponentCommand::Execute(aq::level::LevelEditModel& model)
	{
		// 既定値(JSON があればその値)で末尾に足す。参照ノードや、すでに持っている型ならモデルが断る
		const aq::util::JsonValue* json = json_.IsNull() ? nullptr : &json_;
		return model.AddComponent(nodeId_, typeName_, json);
	}


	bool AddComponentCommand::Undo(aq::level::LevelEditModel& model)
	{
		// 足したものを外すのは足す操作のちょうど逆なので、addable == false の型(ギズモが足す Transform)も外す。
		// インスペクターの Remove Component(RemoveComponentCommand)は今までどおり addable を見る
		return model.RemoveComponent(nodeId_, typeName_, nullptr, nullptr, true);
	}


	/************************************/




	RemoveComponentCommand::RemoveComponentCommand(const uint32_t nodeId, const std::string& typeName)
		: nodeId_(nodeId)
		, typeName_(typeName)
	{
	}


	bool RemoveComponentCommand::Execute(aq::level::LevelEditModel& model)
	{
		// 取り外した値と位置は捨てずに持つ(取り消しで同じ値・同じ並びに戻せるように)
		removedJson_  = aq::util::JsonValue();
		removedIndex_ = 0;
		return model.RemoveComponent(nodeId_, typeName_, &removedJson_, &removedIndex_);
	}


	bool RemoveComponentCommand::Undo(aq::level::LevelEditModel& model)
	{
		// serializePtr の無い型は値を控えられないので、既定値で戻す
		const aq::util::JsonValue* json = removedJson_.IsNull() ? nullptr : &removedJson_;
		return model.AddComponent(nodeId_, typeName_, json, removedIndex_);
	}
}
