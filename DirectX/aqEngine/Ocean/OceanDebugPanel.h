#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"

namespace aq
{
	namespace ocean
	{
		// ============================================================
		// OceanDebugPanel — ImGui による海パラメータのリアルタイム調整
		//
		// DebugUI::Get().Register(panel.get()) で登録すると
		// ECS から最初の OceanComponent を毎フレーム検索し、
		// 波・Fresnel・色などのパラメータをスライダーで編集できる。
		// ============================================================
		class OceanDebugPanel : public IDebugRenderable
		{
		public:
			void DebugRenderMenu() override;
			void DebugRender()     override;
			void RenderContent();

		private:
			bool show_ = false;
		};
	}
}
#endif
