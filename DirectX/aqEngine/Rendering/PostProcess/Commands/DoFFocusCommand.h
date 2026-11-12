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
		 * DoFFocus.fx の cbuffer レイアウトと一致させること(設計書/被写界深度設計.md §1)
		 */
		struct DoFFocusCBData
		{
			math::Matrix4x4 view;             // ワールド → ビュー (z はカメラから前方へ正。列ベクトル規約)
			float           screenWidth;
			float           screenHeight;
			float           focusDistance;    // 手動のピント位置 (ビュー空間の z)。reset で目標が無いときに書く
			float           speed;            // 目標へ寄せる割合 (0〜1)
			uint32_t        reset;            // 1 なら前の値を使わずに書き直す
			uint32_t        pad[3];
		};
		static_assert(sizeof(DoFFocusCBData) == 96, "DoFFocusCBData must be 96 bytes (16B aligned, DoFFocus.fx と一致)");




		/**
		 * 被写界深度のオートフォーカスの CS パス(DoFPassCommand の直前)。
		 * 画面中央 5×5 画素の worldPos から目標のピント位置を求め、1×1 のフォーカス RT(u0)を更新する。
		 * フォーカス RT は DoFPassCommand が SRV(t2)で読む。SRV と UAV を同時に使わないよう別コマンドにしている
		 * (設計書 §0.4 の 5)。
		 * RT はハンドルで持ち Execute で解決する。CB の値はゲームスレッドのコマンド構築時に値で確定させる
		 * (DoFPassCommand と同じ)。
		 */
		class DoFFocusCommand final : public IRenderCommand
		{
		private:
			graphics::IShader*         shader_;
			graphics::IConstantBuffer* constantBuffer_;

			RenderTargetHandle worldPosRTHandle_;
			RenderTargetHandle focusRTHandle_;

			DoFFocusCBData cbData_;


		public:
			DoFFocusCommand(
				graphics::IShader*         shader,
				graphics::IConstantBuffer* constantBuffer,
				RenderTargetHandle         worldPosRT,
				RenderTargetHandle         focusRT,
				const DoFFocusCBData&      cbData);

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;
		};
	}
}
