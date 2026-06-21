#pragma once
#include "Rendering/RenderTargetHandle.h"
#ifdef AQ_DEBUG_IMGUI
#include <memory>
#include "Core/IDebugRenderable.h"
#endif

namespace aq
{
	namespace rendering
	{
		class RenderCommandList;

		class IPostProcessRenderer
		{
		public:
			virtual ~IPostProcessRenderer() = default;

			virtual void BuildPostProcessCommandList(
				RenderCommandList& outList,
				RenderTargetHandle sceneRT,
				uint32_t           width,
				uint32_t           height) const = 0;

			/** ポストプロセス後の最終出力 RT ハンドル。displayRT に渡す。 */
			virtual RenderTargetHandle GetFinalRT() const = 0;

#ifdef AQ_DEBUG_IMGUI
			/** デバッグパネルを生成して返す。非対応の実装は nullptr を返す。 */
			virtual std::unique_ptr<IDebugRenderable> CreateDebugPanel() { return nullptr; }
#endif
		};
	}
}
