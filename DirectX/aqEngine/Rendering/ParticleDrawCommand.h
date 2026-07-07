#pragma once
#include "IRenderCommand.h"
#include "RenderFrame.h"

namespace aq
{
	namespace rendering
	{
		// ============================================================
		// ParticleDrawCommand — 半透明パーティクルビルボードを 1 ドロー描画する。
		//
		// フォワードパスは Opaque 固定のため、このコマンドが自前でブレンド
		// (AlphaBlend / Additive) を設定する。b0 に view/proj (world=Identity、
		// 頂点は CPU でワールド展開済み) を積み、テクスチャは使わない。
		// ============================================================
		class ParticleDrawCommand final : public IRenderCommand
		{
		public:
			ParticleDrawCommand(const ParticleRenderItem& item, const CameraData& camera);
			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;

		private:
			ParticleRenderItem item_;
			CameraData         camera_;
		};
	}
}
