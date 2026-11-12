#pragma once
#include <cstdint>
#include "Rendering/IRenderCommand.h"
#include "Rendering/RenderTargetHandle.h"
#include "Math/Matrix.h"


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
		 * DoF.fx の cbuffer レイアウトと一致させること(設計書/被写界深度設計.md §1)
		 */
		struct DoFCBData
		{
			math::Matrix4x4 view;             // ワールド → ビュー (z はカメラから前方へ正。列ベクトル規約)
			float           screenWidth;
			float           screenHeight;
			float           focusDistance;    // 手動のピント位置 (ビュー空間の z。useAutoFocus == 0 のとき使う)
			float           focusRange;       // ピント位置の前後でぼけない幅 (片側)
			float           nearTransition;   // 手前側で CoC が 0 → -1 になる距離
			float           farTransition;    // 奥側で CoC が 0 → +1 になる距離
			float           maxRadius;        // 最大のぼけ半径 (画素)
			uint32_t        debugView;        // 1 なら CoC を色で出す
			uint32_t        useAutoFocus;     // 1 ならピント位置をフォーカス RT (t2) から読む
			uint32_t        pad[3];
		};
		static_assert(sizeof(DoFCBData) == 112, "DoFCBData must be 112 bytes (16B aligned, DoF.fx と一致)");




		/**
		 * 被写界深度の CS パス(MotionBlur の後段、Bloom の前段)。
		 * 入力(HDR)と worldPos を読み、CoC に応じて出力 RT へ書く。
		 * あわせて符号付きぼけ半径(画素)を CoC RT(u1)へ書く(OutlinePass が読む。設計書 §0.5)。
		 * オートフォーカスを使うフレームは、ピント位置をフォーカス RT(t2。DoFFocusCommand が更新)から読む。
		 * RT はハンドルで持ち Execute で解決する。CB の値はゲームスレッドのコマンド構築時に値で確定させる
		 * (MotionBlurPassCommand と同じ)。
		 */
		class DoFPassCommand final : public IRenderCommand
		{
		private:
			graphics::IShader*         shader_;
			graphics::IConstantBuffer* constantBuffer_;

			RenderTargetHandle inputRTHandle_;
			RenderTargetHandle worldPosRTHandle_;
			RenderTargetHandle outputRTHandle_;
			RenderTargetHandle cocRTHandle_;
			/** フォーカス RT(1×1)。オートフォーカスを使わないフレームは INVALID(t2 にはダミーを入れる) */
			RenderTargetHandle focusRTHandle_;

			DoFCBData cbData_;


		public:
			DoFPassCommand(
				graphics::IShader*         shader,
				graphics::IConstantBuffer* constantBuffer,
				RenderTargetHandle         inputRT,
				RenderTargetHandle         worldPosRT,
				RenderTargetHandle         outputRT,
				RenderTargetHandle         cocRT,
				RenderTargetHandle         focusRT,
				const DoFCBData&           cbData);

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;
		};
	}
}
