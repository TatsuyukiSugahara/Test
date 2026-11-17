#pragma once
#include "Editor/IEditorLayer.h"


namespace editor
{
	/**
	 * エディタの UI 層。現状はゲームビューだけを持つ。
	 * AppHost がエディタ用の ImGui コンテキストを current にして呼ぶ。寿命は EditorMain が持つ
	 */
	class EditorLayer final : public aq::IEditorLayer
	{
	public:
		void OnBuildUI(const aq::rendering::RenderTargetHandle gameViewRT) override;
		void OnEndFrame() override;


	private:
		/** ゲームビュー。displayRT をアスペクト比を保って縮小表示する */
		void DrawGameView(const aq::rendering::RenderTargetHandle gameViewRT);
	};
}
