#pragma once

namespace aq
{
	/**
	 * デバッグ UI を持つオブジェクトの共通インターフェース。
	 * ECS System とは独立して使用できる。
	 * DebugUI::Get().Register(this) で登録すると engine が自動で呼び出す。
	 */
	class IDebugRenderable
	{
	public:
		virtual ~IDebugRenderable() = default;

#ifdef AQ_DEBUG_IMGUI
		/** メインメニューバーのメニュー項目を追加する（BeginMainMenuBar 済み前提）。 */
		virtual void DebugRenderMenu() {}
		/** デバッグウィンドウを描画する。 */
		virtual void DebugRender() {}
		/** Begin/End なしで中身だけ描画する（タブに埋め込む場合に使用）。 */
		virtual void RenderContent() {}
		/** タブに表示するラベル文字列を返す。 */
		virtual const char* GetDebugLabel() const { return ""; }
		/**
		 * メインメニューバーで属するカテゴリ名を返す（"Rendering" / "UI" / "Tools" / "Profiling" など）。
		 * DebugUI が同名カテゴリの項目を 1 つのドロップダウンメニューへまとめる。
		 */
		virtual const char* GetDebugCategory() const { return "Misc"; }
#endif
	};
}
