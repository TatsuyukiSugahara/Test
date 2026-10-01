#pragma once


namespace aq
{
	class AppHost;


	/**
	 * AppHost に載せて動かす差し替え単位(ゲーム本体・将来のエディタ用モードなど)。
	 * フックの呼ばれる位置は旧 aq::Application の On* と同じ。
	 */
	class IAppModule
	{
	public:
		virtual ~IAppModule() = default;


	public:
		/** ログやレジストリで使う名前(起動引数 -app= と照合する) */
		virtual const char* GetName() const = 0;

		/** エンジンサブシステム初期化後に 1 回 */
		virtual bool OnInitialize(AppHost& /*host*/) { return true; }
		/** エンジンサブシステム終了前に 1 回 */
		virtual void OnFinalize() {}
		/** エンジン標準 System の登録後、FinalizeRegistration 前に 1 回 */
		virtual void OnRegister() {}
		/** Input/ECS/Level/Resource 更新後、UI 更新前に毎フレーム */
		virtual void OnUpdate() {}
		/** メインパス Submit 前に毎フレーム(オフスクリーンパス等を Submit する) */
		virtual void OnPreRender() {}
#ifdef AQ_IMGUI
		/** ImGui::NewFrame() 直後に毎フレーム。モジュール固有の ImGui ウィンドウをここで構築する */
		virtual void OnImGuiRender() {}
#endif
#ifdef AQ_DEBUG_IMGUI
		/** メインメニューバー内にモジュール固有のメニュー項目を追加する */
		virtual void OnDebugRenderMenu() {}
		/** ImGui ウィンドウ構築(EntityContext::DebugRender() の後に呼ばれる) */
		virtual void OnDebugRender() {}
#endif
	};
}
