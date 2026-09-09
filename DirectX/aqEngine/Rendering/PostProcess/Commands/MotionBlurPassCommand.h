#pragma once
#include "Rendering/IRenderCommand.h"
#include "Rendering/RenderTargetHandle.h"
#include "PostProcessCB.h"


namespace aq
{
	namespace graphics
	{
		class IShader;
		class IConstantBuffer;
	}


	namespace rendering
	{
		/**
		 * カメラモーションブラーの CS パス (Bloom の前段)。
		 * GBuffer2 (worldPos) と前フレームの viewProj からスクリーン空間速度を再構成し、
		 * シーン RT を速度方向にぼかして出力 RT へ書く。
		 * 前フレーム行列はゲームスレッドのコマンド構築時に値で確定させる (パイプライン時の競合防止)。
		 */
		class MotionBlurPassCommand final : public IRenderCommand
		{
		private:
			graphics::IShader*         shader_;
			graphics::IConstantBuffer* constantBuffer_;

			RenderTargetHandle sceneRTHandle_;
			RenderTargetHandle worldPosRTHandle_;
			RenderTargetHandle outputRTHandle_;

			MotionBlurCBData cbData_;


		public:
			MotionBlurPassCommand(
				graphics::IShader*         shader,
				graphics::IConstantBuffer* constantBuffer,
				RenderTargetHandle         sceneRT,
				RenderTargetHandle         worldPosRT,
				RenderTargetHandle         outputRT,
				const MotionBlurCBData&    cbData);

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;
		};
	}
}
