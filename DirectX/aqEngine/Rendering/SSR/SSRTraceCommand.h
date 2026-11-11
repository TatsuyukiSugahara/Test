#pragma once
#include <cstdint>
#include "Rendering/IRenderCommand.h"
#include "Rendering/RenderTargetHandle.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Graphics/IShader.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * SSR トレース用 定数バッファ (b0)。設計書/SSR設計.md §1 の SSRCBData。
		 * perDrawCBPool (= 192B) から Allocate するため 192B に固定する。
		 * SSRTrace.fx の cbuffer SSRCB と並びを一致させること。
		 *
		 * 行列は MotionBlur.fx と同じ規約 (C++ 側の値をそのまま渡し、HLSL 側は mul(M, v)。§0.6-3)。
		 * view は剛体 (回転 + 平行移動) の前提で、HLSL 側は左上 3×3 の転置を逆回転として使う。
		 */
		struct SSRCBData
		{
			math::Matrix4x4 viewProj;                         // view × projection (ワールド → クリップ)
			math::Matrix4x4 view;                             // ワールド → ビュー (z はカメラから前方へ正)
			math::Vector4   viewRect;                         // ビュー矩形 (x, y, w, h。ピクセル)
			float           screenWidth        = 0.0f;        // 画面全体の寸法 (ピクセル)
			float           screenHeight       = 0.0f;
			float           maxDistance        = 50.0f;       // レイの最大長 (ワールド単位)
			float           thicknessScale     = 0.02f;       // 厚み係数 (厚み = max(係数 × zScene, 4 × e_hit)。§0.5)
			uint32_t        maxSteps           = 64;          // 線形マーチの最大ステップ数
			uint32_t        binarySteps        = 6;           // 二分探索の回数
			float           edgeFade           = 0.1f;        // 画面端で確信度を下げる幅 (ビュー矩形に対する割合)
			float           nearZ              = 0.1f;        // near 面 (レイの終点をここで切る)
			float           roughnessFadeStart = 0.3f;        // この roughness から確信度を下げ始める
			float           roughnessFadeEnd   = 0.5f;        // この roughness 以上は SSR を使わない
			float           _pad0              = 0.0f;
			float           _pad1              = 0.0f;
		};
		static_assert(sizeof(SSRCBData) == 192,
		              "SSRCBData must match perDrawCBPool slot size (192B)");




		/**
		 * SSR のトレース (設計書/SSR設計.md §0.3〜§0.5)。compute (SSRTrace.fx。8×8) で反射レイを追い、
		 * 出力 RT に rgb = 当たった画素の Scene の色、a = 確信度 を書く (外れは (0,0,0,0))。
		 *
		 * Scene を SRV で読むので、RTV を外してから compute を回す (MotionBlurPassCommand と同じ。§0.6-2)。
		 * dispatch はビュー矩形の範囲だけ (§0.6-4)。
		 *
		 * RT はハンドルで持ち Execute() で解決する。パラメータ (行列・矩形を含む) はコマンド生成時に値で写す (§0.2)。
		 * CB は Execute 内で fc.perDrawCBPool から取る (GPU 実行中の CB を上書きしない)。
		 */
		class SSRTraceCommand final : public IRenderCommand
		{
		private:
			graphics::IShader* traceCS_;

			/** 入力 (Scene・GBuffer1 = 法線 + roughness・GBuffer2 = worldPos・GBuffer3 = pixelTag) と出力 */
			RenderTargetHandle sceneHandle_;
			RenderTargetHandle gbuffer1Handle_;
			RenderTargetHandle gbuffer2Handle_;
			RenderTargetHandle gbuffer3Handle_;
			RenderTargetHandle outputHandle_;

			SSRCBData cbData_;


		public:
			SSRTraceCommand(
				graphics::IShader& traceCS,
				RenderTargetHandle sceneHandle,
				RenderTargetHandle gbuffer1Handle,
				RenderTargetHandle gbuffer2Handle,
				RenderTargetHandle gbuffer3Handle,
				RenderTargetHandle outputHandle,
				const SSRCBData&   cbData);

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;
		};
	}
}
