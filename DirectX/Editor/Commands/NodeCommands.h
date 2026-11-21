#pragma once
#include "IEditorCommand.h"
#include "Level/LevelEditModel.h"
#include <cstdint>
#include <memory>
#include <string>


namespace editor
{
	/** ノードの置き場所(親のノード、または Level / インライン subLevel のルート列) */
	using NodeParent = aq::level::LevelEditModel::NodeParent;

	/** 置き場所の末尾を表す位置 */
	static constexpr size_t NODE_INDEX_APPEND = aq::level::LevelEditModel::APPEND;




	/**
	 * 実体ノードを 1 つ作るコマンド。
	 * 作るノードは名前と Transform だけを持つ。実行後に振られた ID を覚える
	 */
	class CreateNodeCommand final : public IEditorCommand
	{
	private:
		/** 置き場所 */
		NodeParent parent_;
		size_t     index_;

		/** 作るノード。Execute でモデルへ移す(取り消しで戻ってきたときは同じノードをまた移す) */
		std::unique_ptr<aq::ecs::PrefabEditNode> node_;
		std::string                              name_;

		/** 実行後に振られた ID(0 = まだ実行していない・失敗した) */
		uint32_t createdId_ = 0;


	public:
		CreateNodeCommand(const NodeParent& parent, const size_t index, const std::string& name);

		bool        Execute(aq::level::LevelEditModel& model) override;
		void        Undo(aq::level::LevelEditModel& model) override;
		const char* GetName() const override { return "Create Node"; }

		inline uint32_t GetCreatedId() const { return createdId_; }
	};




	/**
	 * ノードをサブツリーごと削除するコマンド。
	 * 取り外したサブツリー(子孫の ID を含む)と元の置き場所を持ち、取り消しでそのまま戻せるようにする
	 */
	class DeleteNodeCommand final : public IEditorCommand
	{
	private:
		/** 削除するノード */
		uint32_t nodeId_;

		/** 実行後に取り外したサブツリーと、元の置き場所 */
		std::unique_ptr<aq::ecs::PrefabEditNode> detached_;
		NodeParent                               oldParent_;
		size_t                                   oldIndex_ = 0;


	public:
		explicit DeleteNodeCommand(const uint32_t nodeId);

		bool        Execute(aq::level::LevelEditModel& model) override;
		void        Undo(aq::level::LevelEditModel& model) override;
		const char* GetName() const override { return "Delete Node"; }
	};




	/**
	 * ノードを子孫ごと別の置き場所へ移すコマンド(親の付け替え)。
	 * 実行前の置き場所を覚える
	 */
	class MoveNodeCommand final : public IEditorCommand
	{
	private:
		/** 移すノードと移動先 */
		uint32_t   nodeId_;
		NodeParent newParent_;
		size_t     newIndex_;

		/** 実行前の置き場所 */
		NodeParent oldParent_;
		size_t     oldIndex_ = 0;


	public:
		MoveNodeCommand(const uint32_t nodeId, const NodeParent& newParent, const size_t newIndex);

		bool        Execute(aq::level::LevelEditModel& model) override;
		void        Undo(aq::level::LevelEditModel& model) override;
		const char* GetName() const override { return "Move Node"; }
	};
}
