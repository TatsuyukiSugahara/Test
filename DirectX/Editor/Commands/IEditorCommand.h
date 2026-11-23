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
	 * 成功したものは CommandHistory が持ち、Undo で取り消し、Execute をもう一度呼んでやり直す。
	 * Execute / Undo が受け取るのは編集モデルだけで、選択や UI の状態には触れない
	 */
	class IEditorCommand
	{
	public:
		virtual ~IEditorCommand() = default;

		/**
		 * 編集モデルへ適用する。対象が無いなどで適用できなければ false(モデルは変えない)。
		 * 取り消しの直後にもう一度呼ばれる(やり直し)ので、取り消しに要るものは呼ばれるたびに控え直す
		 */
		virtual bool Execute(aq::level::LevelEditModel& model) = 0;

		/** 直前の Execute を取り消す。モデルとコマンドが食い違って取り消せなければ false(モデルは変えない) */
		virtual bool Undo(aq::level::LevelEditModel& model) = 0;

		/** 履歴とログに出す名前 */
		virtual const char* GetName() const = 0;
	};
}
