#pragma once
#include "IRenderCommand.h"
#include "RenderFrame.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * 1 メッシュを instanceCount 個まとめて描く1ドローぶんのコマンド。
		 * Execute() で b0(view/projection)を確定し、slot0=共有ジオメトリ・slot1=per-instance
		 * ワールド行列(動的VB)をバインドして DrawIndexedInstanced する。
		 * world は per-instance ストリームから取るため b0 の world は使わない。
		 */
		class InstancedDrawItemCommand final : public IRenderCommand
		{
		public:
			InstancedDrawItemCommand(const InstancedRenderItem& item, const CameraData& camera);
			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;

		private:
			InstancedRenderItem item_;
			CameraData          camera_;
		};
	}
}
