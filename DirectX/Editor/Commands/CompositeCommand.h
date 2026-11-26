#pragma once
#include "IEditorCommand.h"
#include <memory>
#include <string>
#include <vector>


namespace editor
{
	/**
	 * 複数のコマンドを 1 つにまとめるコマンド(ギズモの確定で 2 つ以上のキーが変わったとき。エディタ E11 7.)。
	 * IEditorCommand の「false ならモデルは変えない」を保つ:
	 * Execute は子を順に実行し、途中の子が false なら、それまでに成功した子を逆順に取り消してから false を返す。
	 * Undo は子を逆順に取り消し、途中の子が false なら、取り消した子を順に実行し直してから false を返す。
	 * 立て直しの途中でも失敗したら(モデルとコマンドが食い違っている)、ログに出して false を返す(それ以上は直せない)
	 */
	class CompositeCommand final : public IEditorCommand
	{
	private:
		/** 履歴とログに出す名前 */
		std::string name_;

		/** まとめるコマンド(実行する順) */
		std::vector<std::unique_ptr<IEditorCommand>> children_;


	public:
		explicit CompositeCommand(const std::string& name);

		/** 子を末尾に足す(積む前に足し終える) */
		void Add(std::unique_ptr<IEditorCommand> child);

		inline size_t GetChildCount() const { return children_.size(); }

		bool        Execute(aq::level::LevelEditModel& model) override;
		bool        Undo(aq::level::LevelEditModel& model) override;
		const char* GetName() const override { return name_.c_str(); }
	};
}
