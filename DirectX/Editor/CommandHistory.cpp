#include "aq.h"
#include "CommandHistory.h"


namespace editor
{
	void CommandHistory::Push(std::unique_ptr<IEditorCommand> command)
	{
		if (command == nullptr) return;

		// 新しい操作をしたら、やり直しの列は使えなくなる(保存した時点の項目がここにあれば、以後は二度と一致しない)
		redoEntries_.clear();

		Entry entry;
		entry.command = std::move(command);
		entry.serial  = nextSerial_++;
		undoEntries_.push_back(std::move(entry));

		// 上限を超えたら一番古いものを捨て、空のときの状態をその項目の後の状態にする
		while (undoEntries_.size() > MAX_ENTRIES) {
			baseSerial_ = undoEntries_.front().serial;
			undoEntries_.pop_front();
		}
	}


	bool CommandHistory::Undo(aq::level::LevelEditModel& model)
	{
		if (undoEntries_.empty()) return false;

		Entry entry = std::move(undoEntries_.back());
		undoEntries_.pop_back();

		// 取り消せないのはモデルとコマンドが食い違っているとき。続けると以後の取り消しがずれるので、履歴ごと捨てる
		if (!entry.command->Undo(model)) {
			Clear();
			return false;
		}
		redoEntries_.push_back(std::move(entry));
		return true;
	}


	bool CommandHistory::Redo(aq::level::LevelEditModel& model)
	{
		if (redoEntries_.empty()) return false;

		Entry entry = std::move(redoEntries_.back());
		redoEntries_.pop_back();

		// 取り消しの直後の状態は最初の実行の前と同じなので、同じ Execute で同じ結果になる
		if (!entry.command->Execute(model)) {
			Clear();
			return false;
		}
		// 通し番号は最初に積んだときのものを保つ(保存した時点へ戻ったかを番号で判定するため)
		undoEntries_.push_back(std::move(entry));
		return true;
	}


	void CommandHistory::Clear()
	{
		undoEntries_.clear();
		redoEntries_.clear();
		savedSerial_ = NO_SAVED_STATE;
	}


	void CommandHistory::MarkSaved()
	{
		savedSerial_ = GetCurrentSerial();
	}


	void CommandHistory::MarkCleanEmpty()
	{
		undoEntries_.clear();
		redoEntries_.clear();
		savedSerial_ = GetCurrentSerial();
	}


	const char* CommandHistory::GetUndoName() const
	{
		return undoEntries_.empty() ? nullptr : undoEntries_.back().command->GetName();
	}


	const char* CommandHistory::GetRedoName() const
	{
		return redoEntries_.empty() ? nullptr : redoEntries_.back().command->GetName();
	}


	uint64_t CommandHistory::GetCurrentSerial() const
	{
		return undoEntries_.empty() ? baseSerial_ : undoEntries_.back().serial;
	}
}
