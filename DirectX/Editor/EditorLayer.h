#pragma once
#include "Editor/IEditorLayer.h"
#include "GameViewInput.h"


namespace editor
{
	/**
	 * エディタの UI 層。現状はゲームビューだけを持つ。
	 * AppHost がエディタ用の ImGui コンテキストを current にして呼ぶ。寿命は EditorMain が持つ
	 */
	class EditorLayer final : public aq::IEditorLayer
	{
	private:
		/** ゲームビューへの入力の転送 */
		GameViewInput gameViewInput_;

		/** 起動直後の最初のフレームか(ゲームビューにフォーカスを与える) */
		bool isFirstFrame_ = true;


	public:
		void OnBeginFrame(ImGuiContext* gameContext) override;
		void OnBuildUI(const aq::rendering::RenderTargetHandle gameViewRT) override;
		void OnEndFrame() override;


	private:
		/** ゲームビュー。displayRT をアスペクト比を保って縮小表示し、このフレームの状態を返す */
		GameViewState DrawGameView(const aq::rendering::RenderTargetHandle gameViewRT);
	};
}
