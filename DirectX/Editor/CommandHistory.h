#pragma once
#include "Commands/IEditorCommand.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>


namespace editor
{
	/**
	 * 編集コマンドの履歴(Undo / Redo の列と、保存した状態の印)。
	 * 各項目に 1 から通し番号を振り、「今の状態の番号」(Undo の列の先頭の項目の番号。空なら基底の番号)と
	 * 保存した時点の番号を比べて、保存済みの状態かを判定する。ImGui に依らない
	 */
	class CommandHistory
	{
	private:
		/** 履歴の 1 項目 */
		struct Entry
		{
			std::unique_ptr<IEditorCommand> command;
			uint64_t                        serial = 0;   // Push のときに振る通し番号(1 から)
		};


	public:
		/** 履歴に残す項目の上限(超えたら一番古いものを捨てる) */
		static constexpr size_t MAX_ENTRIES = 256;

		/** 保存の印が、どの状態とも一致しないことを表す値 */
		static constexpr uint64_t NO_SAVED_STATE = UINT64_MAX;


	private:
		/** Undo の列(末尾が次に取り消すもの)と Redo の列(末尾が次にやり直すもの) */
		std::deque<Entry> undoEntries_;
		std::deque<Entry> redoEntries_;

		/** 次に振る通し番号 */
		uint64_t nextSerial_ = 1;

		/** Undo の列が空のときの今の状態の番号(上限で捨てた項目のうち最後のものの番号。初めは 0) */
		uint64_t baseSerial_ = 0;

		/** 保存した時点の今の状態の番号 */
		uint64_t savedSerial_ = 0;


	public:
		/** 実行に成功したコマンドを積む。Redo の列を捨てる。上限を超えたら一番古いものを捨てる */
		void Push(std::unique_ptr<IEditorCommand> command);

		/** 取り消す。失敗したら履歴を空にして false(保存の印は一致しなくなる) */
		bool Undo(aq::level::LevelEditModel& model);

		/** やり直す(コマンドの Execute をもう一度呼ぶ)。失敗したら Undo と同じ */
		bool Redo(aq::level::LevelEditModel& model);

		/** 空にする。保存の印は「どの状態とも一致しない」にする */
		void Clear();

		/** 今の状態を保存済みとする */
		void MarkSaved();

		/** 空にして、空の状態を保存済みとする(New / Open の後) */
		void MarkCleanEmpty();


	public:
		inline bool   CanUndo() const       { return !undoEntries_.empty(); }
		inline bool   CanRedo() const       { return !redoEntries_.empty(); }
		inline size_t GetUndoCount() const  { return undoEntries_.size(); }
		inline size_t GetRedoCount() const  { return redoEntries_.size(); }

		/** 次に取り消すコマンドの名前(無ければ nullptr) */
		const char* GetUndoName() const;
		/** 次にやり直すコマンドの名前(無ければ nullptr) */
		const char* GetRedoName() const;

		/** 今の状態が保存した時点の状態か */
		inline bool IsAtSavedState() const { return GetCurrentSerial() == savedSerial_; }


	private:
		/** 今の状態の番号 */
		uint64_t GetCurrentSerial() const;
	};
}
