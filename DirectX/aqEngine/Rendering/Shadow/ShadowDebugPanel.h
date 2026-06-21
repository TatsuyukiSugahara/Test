#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"
#include "ShadowData.h"

namespace aq
{
	namespace rendering
	{
		/**
		 * シャドウ設定のデバッグ UI パネル。
		 * IShadowRenderer とは独立したクラスで、settings への参照を通じて値を編集する。
		 */
		class ShadowDebugPanel : public IDebugRenderable
		{
		public:
			explicit ShadowDebugPanel(ShadowSettings& settings);

			void        DebugRenderMenu()     override;
			void        DebugRender()         override;
			void        RenderContent()       override;
			const char* GetDebugLabel() const override { return "Shadow"; }

		private:
			ShadowSettings& settings_;
			bool            show_ = false;
		};
	}
}
#endif
