#include "stdafx.h"
#include "NodeCommands.h"
#include "ECS/PrefabEditNodeOps.h"


namespace editor
{
	CreateNodeCommand::CreateNodeCommand(const NodeParent& parent, const size_t index, const std::string& name)
		: parent_(parent)
		, index_(index)
		, name_(name)
	{
	}


	bool CreateNodeCommand::Execute(aq::level::LevelEditModel& model)
	{
		// ノードは実行の時点で作る(取り消しで戻ってきたノードがあればそれを使い、ID を保つ)
		if (node_ == nullptr) {
			node_ = std::make_unique<aq::ecs::PrefabEditNode>();
			node_->name = name_;
			// Level の実体ノードは Transform を必ず持つ
			aq::ecs::PrefabNodeEnsureTransform(*node_);
		}

		// 失敗したとき InsertNode はノードを捨てる。失敗したコマンドは捨てるので持ち直さない
		createdId_ = model.InsertNode(parent_, index_, std::move(node_));
		return createdId_ != 0;
	}


	void CreateNodeCommand::Undo(aq::level::LevelEditModel& model)
	{
		// Undo / Redo を作るときに実装する(createdId_ のノードを DetachNode して node_ へ戻す)
		(void)model;
	}


	/************************************/




	DeleteNodeCommand::DeleteNodeCommand(const uint32_t nodeId)
		: nodeId_(nodeId)
	{
	}


	bool DeleteNodeCommand::Execute(aq::level::LevelEditModel& model)
	{
		// 取り外したサブツリーは捨てずに持つ(取り消しで元の位置へ戻すと、子孫の ID も元どおりになる)
		detached_ = model.DetachNode(nodeId_, &oldParent_, &oldIndex_);
		return detached_ != nullptr;
	}


	void DeleteNodeCommand::Undo(aq::level::LevelEditModel& model)
	{
		// Undo / Redo を作るときに実装する(detached_ を oldParent_ / oldIndex_ へ InsertNode で戻す)
		(void)model;
	}


	/************************************/




	MoveNodeCommand::MoveNodeCommand(const uint32_t nodeId, const NodeParent& newParent, const size_t newIndex)
		: nodeId_(nodeId)
		, newParent_(newParent)
		, newIndex_(newIndex)
	{
	}


	bool MoveNodeCommand::Execute(aq::level::LevelEditModel& model)
	{
		// 取り消しで戻す先を、動かす前に控える
		if (!model.FindParent(nodeId_, oldParent_, oldIndex_)) return false;
		return model.MoveNode(nodeId_, newParent_, newIndex_);
	}


	void MoveNodeCommand::Undo(aq::level::LevelEditModel& model)
	{
		// Undo / Redo を作るときに実装する(nodeId_ を oldParent_ / oldIndex_ へ MoveNode で戻す)
		(void)model;
	}
}
