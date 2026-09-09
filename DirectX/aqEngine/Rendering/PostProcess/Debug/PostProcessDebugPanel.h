#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"


namespace aq
{
	namespace rendering
	{
		class PostProcessChain;


		/** ポストエフェクト(Bloom + トーンマップ)のデバッグ UI パネル。 */
		class PostProcessDebugPanel : public IDebugRenderable
		{
		private:
			PostProcessChain& chain_;
			bool              show_ = false;


		public:
			explicit PostProcessDebugPanel(PostProcessChain& chain);


		public:
			void        DebugRenderMenu()          override;
			void        DebugRender()              override;
			void        RenderContent()            override;
			const char* GetDebugLabel() const      override { return "ポストエフェクト"; }
		};
	}
}
#endif
