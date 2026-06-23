#pragma once
#include <memory>
#include "Rendering/IRenderCommand.h"
#include "Rendering/RenderFrame.h"

namespace aq
{
	namespace rendering
	{
		/**
		 * G-Buffer パスの 1 ドローコール。
		 * item.vs + item.gbufferPS を使い MRT (GBuffer0-3) に書き込む。
		 * item.gbufferPS が nullptr のアイテムは呼び出し元がスキップすること。
		 */
		class GBufferItemCommand final : public IRenderCommand
		{
		public:
			GBufferItemCommand(const RenderItem& item, const CameraData& camera);
			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;

		private:
			RenderItem item_;
			CameraData camera_;
		};
	}
}
