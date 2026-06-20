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
#endif
	};
}
