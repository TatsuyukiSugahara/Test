#include "aq.h"
#include "CompositeCommand.h"
#include "Level/LevelEditModel.h"


namespace editor
{
	namespace
	{
		/** 立て直しに失敗したことをログに出す(モデルとコマンドが食い違っている) */
		void LogRecoveryFailure(const std::string& compositeName, const char* phase, const char* childName)
		{
			const std::string message = std::string("[editor] composite command '") + compositeName + "': failed to recover during "
			                          + phase + " (child: " + childName + "). the edit model may not match the history";
			aq::StartupLog(message.c_str());
		}
	}


	CompositeCommand::CompositeCommand(const std::string& name)
		: name_(name)
	{
	}


	void CompositeCommand::Add(std::unique_ptr<IEditorCommand> child)
	{
		if (child == nullptr) return;
		children_.push_back(std::move(child));
	}


	bool CompositeCommand::Execute(aq::level::LevelEditModel& model)
	{
		for (size_t i = 0; i < children_.size(); ++i) {
			if (children_[i]->Execute(model)) continue;

			// 途中の子が失敗した。それまでに成功した子を逆順に取り消し、モデルを実行前に戻してから false を返す
			for (size_t j = i; j > 0; --j) {
				IEditorCommand& done = *children_[j - 1];
				if (!done.Undo(model)) {
					LogRecoveryFailure(name_, "execute rollback", done.GetName());
					return false;
				}
			}
			return false;
		}
		return true;
	}


	bool CompositeCommand::Undo(aq::level::LevelEditModel& model)
	{
		for (size_t i = children_.size(); i > 0; --i) {
			if (children_[i - 1]->Undo(model)) continue;

			// 途中の子の取り消しが失敗した。取り消した子(i 番目から後ろ)を順に実行し直し、モデルを取り消し前に戻してから false を返す
			for (size_t j = i; j < children_.size(); ++j) {
				IEditorCommand& undone = *children_[j];
				if (!undone.Execute(model)) {
					LogRecoveryFailure(name_, "undo rollback", undone.GetName());
					return false;
				}
			}
			return false;
		}
		return true;
	}
}
