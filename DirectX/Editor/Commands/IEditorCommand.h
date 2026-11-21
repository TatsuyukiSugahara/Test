#pragma once


namespace aq
{
	namespace level
	{
		class LevelEditModel;
	}
}


namespace editor
{
	/**
	 * エディタの編集操作(コマンド)。
	 * パネルが EditorContext::Submit で積み、次のフレームの先頭で EditorContext が編集モデルへ実行する。
	 * Execute / Undo が受け取るのは編集モデルだけで、選択や UI の状態には触れない
	 */
	class IEditorCommand
	{
	public:
		virtual ~IEditorCommand() = default;

		/** 編集モデルへ適用する。対象が無いなどで適用できなければ false(モデルは変えない) */
		virtual bool Execute(aq::level::LevelEditModel& model) = 0;

		/** Execute を取り消す(Undo / Redo を作るときに実装する。今は呼ばない) */
		virtual void Undo(aq::level::LevelEditModel& model) = 0;

		/** 履歴とログに出す名前 */
		virtual const char* GetName() const = 0;
	};
}
