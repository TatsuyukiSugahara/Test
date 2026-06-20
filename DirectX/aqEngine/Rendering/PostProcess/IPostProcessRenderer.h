#pragma once
#include "Rendering/RenderTargetHandle.h"

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
		};
	}
}
