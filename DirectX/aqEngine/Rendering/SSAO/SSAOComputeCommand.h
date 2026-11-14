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
		 * SSAO の compute 用 定数バッファ (b0。AO の計算とぼかしで共用)。設計書/SSAO設計.md §1 の SSAOCBData。
		 * perDrawCBPool (= sizeof(VSConstantBuffer) = 192B) から Allocate するため 192B に固定する。
		 * SSAO.fx / SSAOBlur.fx の cbuffer SSAOCB と並びを一致させること。
		 *
		 * 行列は MotionBlur.fx / SSRTrace.fx と同じ規約 (C++ 側の値をそのまま渡し、HLSL 側は mul(M, v))。
		 * 射影行列は投影の種別を持たず、_22 / _34 / _44 の 3 要素だけを写す (§0.3)。
		 */
		struct SSAOCBData
		{
			math::Matrix4x4 view;                       // ワールド → ビュー (z はカメラから前方へ正)
			math::Vector4   viewRect;                   // ビュー矩形 (x, y, w, h。ピクセル)
			float           projScaleY      = 1.0f;     // 射影行列の _22 (y の拡大率)
			float           projW34         = 1.0f;     // 射影行列の _34 (クリップ空間の w = _34 × z + _44)
			float           projW44         = 0.0f;     // 射影行列の _44
			float           radius          = 0.5f;     // サンプルの半径 (ワールド単位)
			float           intensity       = 1.0f;     // 強度
			float           power           = 1.0f;     // 結果のべき乗
			float           biasScale       = 0.002f;   // 自己遮蔽を防ぐ bias の係数 (ビュー空間の深度に掛ける)
			uint32_t        sampleCount     = 12;       // 1 画素あたりのサンプル数
			float           maxScreenRadius = 64.0f;    // 画面上の半径の上限 (画素)
			float           planeTolerance  = 0.05f;    // ぼかしの平面距離の許容
			float           _pad0           = 0.0f;
			float           _pad1           = 0.0f;
			math::Vector4   pad[4];                     // 192B へのパディング
		};
		static_assert(sizeof(SSAOCBData) == 192,
		              "SSAOCBData must match perDrawCBPool slot size (192B)");




		/**
		 * SSAO の AO の計算とぼかし (設計書/SSAO設計.md §0.3・§0.4)。
		 * compute 2 本 (SSAO.fx → SSAOBlur.fx。どちらも 8×8) を順に回す。
		 *   ① AO: t0 = GBuffer1、t1 = GBuffer2、t2 = GBuffer3 → u0 = 生の AO
		 *   ② ぼかし: t0 = 生の AO、t1 = GBuffer1、t2 = GBuffer2 → u0 = ぼかした AO
		 * 間で生の AO の UAV を外してから SRV にする。各 compute の SRV は t0〜t2 の 3 枚まで (D3D12 の制約。§0.1)。
		 * dispatch はビュー矩形の範囲だけ (シェーダが矩形の原点を足す)。
		 *
		 * G-Buffer と AO の RT だけを読み書きし、どれもこの時点で RT としてバインドされていないので、
		 * シーン RT の RTV は外さない (§0.5-7)。合成の前に SSAOPass がシーン RT をバインドし直す。
		 *
		 * RT はハンドルで持ち Execute() で解決する。パラメータ (行列・矩形を含む) はコマンド生成時に値で写す (§0.2)。
		 * CB は Execute 内で fc.perDrawCBPool から取る (ビューごとに別の値。分割画面で上書きしない)。
		 */
		class SSAOComputeCommand final : public IRenderCommand
		{
		private:
			graphics::IShader* aoCS_;
			graphics::IShader* blurCS_;

			/** 入力 (GBuffer1 = 法線 + roughness・GBuffer2 = worldPos・GBuffer3 = pixelTag) */
			RenderTargetHandle gbuffer1Handle_;
			RenderTargetHandle gbuffer2Handle_;
			RenderTargetHandle gbuffer3Handle_;

			/** 出力 (生の AO・ぼかした AO) */
			RenderTargetHandle rawAOHandle_;
			RenderTargetHandle blurredAOHandle_;

			SSAOCBData cbData_;


		public:
			SSAOComputeCommand(
				graphics::IShader& aoCS,
				graphics::IShader& blurCS,
				RenderTargetHandle gbuffer1Handle,
				RenderTargetHandle gbuffer2Handle,
				RenderTargetHandle gbuffer3Handle,
				RenderTargetHandle rawAOHandle,
				RenderTargetHandle blurredAOHandle,
				const SSAOCBData&  cbData);

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;
		};
	}
}
