#pragma once
#include "Editor/IEditorLayer.h"
#include "GameViewInput.h"
#include "PlayModeController.h"
#include "PlayStopSelfTest.h"


namespace editor
{
	/**
	 * エディタの UI 層。ゲームビューと、再生・停止のツールバーを持つ。
	 * AppHost がエディタ用の ImGui コンテキストを current にして呼ぶ。寿命は EditorMain が持つ
	 */
	class EditorLayer final : public aq::IEditorLayer
	{
	private:
		/** ゲームビューへの入力の転送 */
		GameViewInput gameViewInput_;

		/** 再生・一時停止・停止の遷移 */
		PlayModeController playModeController_;

		/** Play/Stop 自己テスト */
		PlayStopSelfTest playStopSelfTest_;

		/** 起動直後の最初のフレームか(ゲームビューにフォーカスを与える) */
		bool isFirstFrame_ = true;

		/** 起動引数で自己テストを指示された(最初のフレームで始める) */
		bool startSelfTestOnLaunch_ = false;


	public:
		void OnBeginFrame(ImGuiContext* gameContext) override;
		void OnBuildUI(const aq::rendering::RenderTargetHandle gameViewRT) override;
		void OnEndFrame() override;

		/** 起動引数 -editor-selftest を受けて、最初のフレームから自己テストを始める(終わったら終了コードを返して終了する) */
		inline void RequestSelfTestOnLaunch() { startSelfTestOnLaunch_ = true; }


	private:
		/** ゲームビュー。displayRT をアスペクト比を保って縮小表示し、このフレームの状態を返す */
		GameViewState DrawGameView(const aq::rendering::RenderTargetHandle gameViewRT);

		/** メインメニューバーに再生・一時停止・再開・停止のボタン、現在のモード、自己テストを置く */
		void DrawPlayToolbar();
	};
}
