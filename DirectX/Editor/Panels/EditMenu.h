#pragma once


namespace editor
{
	class EditorContext;




	/**
	 * 取り消しとやり直し(メインメニューバーの Edit メニュー)。
	 * Undo / Redo と Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z。
	 * 操作は EditorContext へ要求として積むだけで、モデルへの適用は次のフレームの先頭で行われる
	 */
	class EditMenu
	{
	public:
		/** メインメニューバーの中で Edit メニューを描く(BeginMainMenuBar の後、File メニューの次に呼ぶ) */
		void DrawMenu(EditorContext& context);

		/** ショートカット。メインメニューバーの外(EndMainMenuBar の後)で毎フレーム呼ぶ */
		void HandleShortcuts(EditorContext& context);


	private:
		/** 取り消し・やり直しを受け付けるか(Edit のとき、編集モデルがあり、ファイル操作の要求が残っていないときだけ) */
		static bool IsEnabled(const EditorContext& context);
	};
}
