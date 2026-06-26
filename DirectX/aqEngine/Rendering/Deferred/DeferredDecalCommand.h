#pragma once
#include "Rendering/IRenderCommand.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderTargetHandle.h"
#include "Graphics/IShader.h"
#include "Graphics/ISamplerState.h"

namespace aq
{
	namespace rendering
	{
		/**
		 * 投影デカール 1 個分のドローコール。
		 *
		 * デカール毎にフルスクリーントライアングルを描画し、PS 内で GBuffer2 の
		 * worldPos をデカールローカル空間 ([-0.5,0.5]^3) へ逆変換、箱の外を clip し、
		 * デカールテクスチャを GBuffer0 (albedo) へ DecalColor ブレンドで書き戻す。
		 *
		 * 前提: 呼び出し前に GBuffer0 が RT としてバインド済みであること。
		 * GBuffer1/2/3 SRV は t9-t11 に、デカールテクスチャは t0 にバインドする。
		 */
		class DeferredDecalCommand final : public IRenderCommand
		{
		public:
			DeferredDecalCommand(graphics::IShader&       decalVS,
			                     graphics::IShader&       decalPS,
			                     graphics::ISamplerState& sampler,
			                     const DecalRenderItem&   item,
			                     RenderTargetHandle       gbuffer1Handle,
			                     RenderTargetHandle       gbuffer2Handle,
			                     RenderTargetHandle       gbuffer3Handle);

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;

		private:
			graphics::IShader*       decalVS_;
			graphics::IShader*       decalPS_;
			graphics::ISamplerState* sampler_;
			DecalRenderItem          item_;
			RenderTargetHandle       gbuffer1Handle_;
			RenderTargetHandle       gbuffer2Handle_;
			RenderTargetHandle       gbuffer3Handle_;
		};
	}
}
