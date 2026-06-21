#pragma once
#include "IRenderCommand.h"
#include "RenderFrame.h"

namespace aq
{
	namespace rendering
	{
		// ============================================================
		// OceanDrawCommand — DrawItemCommand に b5 (OceanCB) のバインドを追加したコマンド
		//
		// OceanCBData を FrameContext::oceanCBPool からアロケートし、
		// VS/PS の register(b5) にセットする。それ以外は DrawItemCommand と同じ。
		// ============================================================
		class OceanDrawCommand final : public IRenderCommand
		{
		public:
			OceanDrawCommand(const OceanRenderItem& item, const CameraData& camera);
			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;

		private:
			OceanRenderItem item_;
			CameraData      camera_;
		};
	}
}
