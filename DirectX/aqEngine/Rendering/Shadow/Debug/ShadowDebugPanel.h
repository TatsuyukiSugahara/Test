#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"
#include "Rendering/Shadow/ShadowData.h"

namespace aq
{
	namespace rendering
	{
		class IShadowRenderer;

		/**
		 * シャドウ設定のデバッグ UI パネル。
		 * IShadowRenderer とは独立したクラスで、settings への参照を通じて値を編集する。
		 * renderer が渡された場合、各ディレクショナルライトのシャドウマップも表示する。
		 */
		class ShadowDebugPanel : public IDebugRenderable
		{
		public:
			explicit ShadowDebugPanel(ShadowSettings& settings,
			                          IShadowRenderer* renderer = nullptr);

			void        DebugRenderMenu()     override;
			void        DebugRender()         override;
			void        RenderContent()       override;
			const char* GetDebugLabel() const override { return "Shadow"; }

		private:
			ShadowSettings&  settings_;
			IShadowRenderer* renderer_;
			bool             show_        = false;
			float            previewSize_ = 180.0f;
		};
	}
}
#endif
