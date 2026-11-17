#pragma once
// エディタ構成(AQ_EDITOR=1)専用。ランタイムのビルドでは中身ごと消える。
#if AQ_EDITOR
#include "Rendering/RenderTargetHandle.h"


namespace aq
{
	/**
	 * AppHost がフレームの決まった位置で呼ぶエディタ側の処理。
	 * Editor プロジェクトが実装し、起動時に AppHost::SetEditorLayer で登録する(AppHost は所有しない)。
	 * どちらのフックも、AppHost がエディタ用の ImGui コンテキストを current にした状態で呼ぶ。
	 */
	class IEditorLayer
	{
	public:
		virtual ~IEditorLayer() = default;

		/** エディタ用コンテキストでパネルを構築する(NewFrame の後、Render の前)。gameViewRT はこのフレームの displayRT */
		virtual void OnBuildUI(const aq::rendering::RenderTargetHandle gameViewRT) = 0;
		/** エディタ用コンテキストの描画データを確定する(内部で ImGui::Render() を呼ぶ) */
		virtual void OnEndFrame() = 0;
	};
}
#endif // AQ_EDITOR
