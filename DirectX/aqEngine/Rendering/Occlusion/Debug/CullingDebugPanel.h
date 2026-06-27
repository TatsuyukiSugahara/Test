#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"

namespace aq
{
	namespace rendering
	{
		/**
		 * カリング系のトグルと統計をまとめたデバッグ UI パネル (Rendering タブに表示)。
		 *   - フラスタムカリング (可視/総数)
		 *   - オクリュージョンカリング (Hi-Z)
		 *   - トライアングル(クラスタ)カリング GPU 駆動 + min clusters + CPU 統計
		 */
		class CullingDebugPanel : public IDebugRenderable
		{
		public:
			void        RenderContent()       override;
			const char* GetDebugLabel() const override { return "Culling"; }
		};
	}
}
#endif
