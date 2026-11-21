#pragma once
#include "IEditorCommand.h"
#include "Util/SimpleJson.h"
#include <cstdint>
#include <string>


namespace editor
{
	/**
	 * ノードの名前を変えるコマンド。
	 * 取り消しのために変更前の名前を持つ
	 */
	class RenameNodeCommand final : public IEditorCommand
	{
	private:
		/** 対象のノード */
		uint32_t nodeId_;

		/** 変更前と変更後の名前 */
		std::string oldName_;
		std::string newName_;


	public:
		RenameNodeCommand(const uint32_t nodeId, const std::string& oldName, const std::string& newName);

		bool        Execute(aq::level::LevelEditModel& model) override;
		void        Undo(aq::level::LevelEditModel& model) override;
		const char* GetName() const override { return "Rename Node"; }
	};




	/**
	 * コンポーネントの 1 つのフィールドの値を変えるコマンド。
	 * 値は Reflect の永続キーと、JsonWriteVisitor と同じ JSON 表現で持つ(取り消しのために変更前の値も持つ)
	 */
	class SetFieldCommand final : public IEditorCommand
	{
	private:
		/** 対象のノードとコンポーネント、フィールド */
		uint32_t    nodeId_;
		std::string typeName_;
		std::string key_;

		/** 変更前と変更後の値(変更前が分からなければ Null) */
		aq::util::JsonValue oldValue_;
		aq::util::JsonValue newValue_;


	public:
		SetFieldCommand(const uint32_t nodeId, const std::string& typeName, const std::string& key,
		                const aq::util::JsonValue& oldValue, const aq::util::JsonValue& newValue);

		bool        Execute(aq::level::LevelEditModel& model) override;
		void        Undo(aq::level::LevelEditModel& model) override;
		const char* GetName() const override { return "Set Field"; }
	};




	/**
	 * 実体ノードにコンポーネントを既定値で足すコマンド
	 */
	class AddComponentCommand final : public IEditorCommand
	{
	private:
		/** 対象のノードと、足す型 */
		uint32_t    nodeId_;
		std::string typeName_;


	public:
		AddComponentCommand(const uint32_t nodeId, const std::string& typeName);

		bool        Execute(aq::level::LevelEditModel& model) override;
		void        Undo(aq::level::LevelEditModel& model) override;
		const char* GetName() const override { return "Add Component"; }
	};




	/**
	 * 実体ノードからコンポーネントを取り外すコマンド。
	 * 取り消しで同じ値に戻せるように、取り外したコンポーネントの JSON を Execute で控える
	 */
	class RemoveComponentCommand final : public IEditorCommand
	{
	private:
		/** 対象のノードと、取り外す型 */
		uint32_t    nodeId_;
		std::string typeName_;

		/** 実行後に取り外したコンポーネントの JSON */
		aq::util::JsonValue removedJson_;


	public:
		RemoveComponentCommand(const uint32_t nodeId, const std::string& typeName);

		bool        Execute(aq::level::LevelEditModel& model) override;
		void        Undo(aq::level::LevelEditModel& model) override;
		const char* GetName() const override { return "Remove Component"; }
	};
}
