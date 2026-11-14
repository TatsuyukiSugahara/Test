#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"

namespace aq
{
	namespace rendering
	{
		class SSAORenderer;

		/**
		 * SSAO のパラメータを触るデバッグパネル (RenderingDebugPanel のタブ "SSAO"。設計書/SSAO設計.md §1)。
		 * ぼかした AO の RT も縮小表示する (SSR の映り込みが重ならない AO そのものを見るため)。
		 * SSAORenderer の寿命は SSAOPass が持つので、パネルはそれより先に破棄されること (AppHost が保証)。
		 */
		class SSAODebugPanel : public IDebugRenderable
		{
		private:
			SSAORenderer& ssao_;

			/** AO の RT の縮小表示の幅 (高さは RT の縦横比に合わせる) */
			float previewWidth_ = 320.0f;


		public:
			explicit SSAODebugPanel(SSAORenderer& ssao);

			void        DebugRenderMenu()     override {}
			void        DebugRender()         override {}
			void        RenderContent()       override;
			const char* GetDebugLabel() const override { return "SSAO"; }
		};
	}
}
#endif
