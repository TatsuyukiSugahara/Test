#pragma once
#include "Rendering/IRenderCommand.h"
#include "Rendering/RenderFrame.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Graphics/IShader.h"
#include "Graphics/ISamplerState.h"
#include "Graphics/IShaderResourceView.h"

namespace aq
{
	namespace rendering
	{
		/**
		 * スカイボックス用 定数バッファ (b0)。
		 * perDrawCBPool (= sizeof(VSConstantBuffer) = 192B) から Allocate するため
		 * サイズを 192B に固定する。Skybox.fx の cbuffer SkyCB と一致させること。
		 */
		struct SkyCBData
		{
			math::Matrix4x4 invViewProjection;  // 平行移動を抜いた view * projection の逆行列
			math::Vector4   tint;               // rgb=色調, a=強度
			math::Vector4   pad[7];             // 192B へのパディング
		};
		static_assert(sizeof(SkyCBData) == 192,
		              "SkyCBData must match perDrawCBPool slot size (192B)");




		/**
		 * スカイパス: フルスクリーン三角形 1 枚でキューブマップ (t5) を描く。
		 *
		 * 前提: 呼び出し前にシーン RT + GBuffer0 の深度が
		 *       SetRenderTargetWithDepthCommand でバインド済みであること。
		 *       このコマンドは RT を一切設定しない。
		 *
		 * 深度は ReadOnly (テスト有効・書き込み無し)。Skybox.fx の VS が 1.0 未満の z を
		 * 出すので、深度クリア値 1.0 のまま残っている背景画素だけを通す。
		 * 描画後は DepthMode を既定 (ReadWrite) へ戻す。
		 */
		class SkyCommand final : public IRenderCommand
		{
		public:
			SkyCommand(graphics::IShader&             skyVS,
			           graphics::IShader&             skyPS,
			           graphics::ISamplerState&       sampler,
			           graphics::IShaderResourceView& cubeMap,
			           const CameraData&              camera,
			           const math::Vector4&           tint);

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;

		private:
			graphics::IShader*             skyVS_;
			graphics::IShader*             skyPS_;
			graphics::ISamplerState*       sampler_;
			graphics::IShaderResourceView* cubeMap_;
			CameraData                     camera_;
			math::Vector4                  tint_;
		};
	}
}
