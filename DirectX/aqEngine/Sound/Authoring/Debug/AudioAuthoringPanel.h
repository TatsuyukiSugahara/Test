#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"


namespace aq
{
	namespace audio
	{
		// データ駆動オーディオ層のデバッグ/オーサリング ImGui パネル（§14）。
		// 鳴っているイベント/ボイス、Switch 状態を可視化し、イベントの手動発火・
		// Switch 切り替えができる。将来の専用エディタへの布石。
		class AudioAuthoringPanel : public IDebugRenderable
		{
		public:
			void DebugRenderMenu() override;
			void DebugRender()     override;
			void RenderContent()   override;
			const char* GetDebugLabel() const override { return "Audio"; }

		private:
			bool show_ = false;
		};
	}
}
#endif
