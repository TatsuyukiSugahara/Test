#include "aq.h"
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
		if (createdId_ == 0) return false;

		// 位置が範囲外なら末尾に置かれるので、実際に置かれた場所を控える(取り消しの前に食い違いを確かめる)
		return model.FindParent(createdId_, placedParent_, placedIndex_);
	}


	bool CreateNodeCommand::Undo(aq::level::LevelEditModel& model)
	{
		if (createdId_ == 0 || node_ != nullptr) return false;

		// 置いた場所に無ければ、モデルとコマンドが食い違っている(取り外さずに断る)
		NodeParent parent;
		size_t     index = 0;
		if (!model.FindParent(createdId_, parent, index)) return false;
		if (parent.nodeId != placedParent_.nodeId || parent.subLevelIndex != placedParent_.subLevelIndex || index != placedIndex_) return false;

		// 取り外したノードは持っておき、やり直しで同じノード(同じ ID)を置き直す
		node_ = model.DetachNode(createdId_);
		return node_ != nullptr;
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


	bool DeleteNodeCommand::Undo(aq::level::LevelEditModel& model)
	{
		if (detached_ == nullptr) return false;

		// InsertNode は採番済みの ID を変えないので、削除の前に子孫を対象にしていた履歴もそのまま使える
		return model.InsertNode(oldParent_, oldIndex_, std::move(detached_)) == nodeId_;
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


	bool MoveNodeCommand::Undo(aq::level::LevelEditModel& model)
	{
		// 控えた位置は取り外す前の添字。同じ列の中でも、MoveNode は取り外した後の添字として扱うので元の並びに戻る
		return model.MoveNode(nodeId_, oldParent_, oldIndex_);
	}
}
