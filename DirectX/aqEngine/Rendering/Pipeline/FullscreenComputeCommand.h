#pragma once
#include <array>
#include <cstdint>
#include "Rendering/IRenderCommand.h"
#include "Rendering/RenderTargetHandle.h"


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
		 * 汎用フルスクリーン compute コマンド(設計書/レンダーパイプライン設計.md P2)。
		 * CS + 入力 SRV 最大 MAX_INPUT_COUNT 枚(t0..)+ 出力 UAV 1 枚(u0)+ 定数バッファ(b0、
		 * 最大 MAX_CB_SIZE_BYTES バイト)を受け取り、8x8 の Dispatch で実行する。
		 * ゲームパスの基本部品(TonemapPassCommand と同じ手順で実行する)。
		 */
		class FullscreenComputeCommand final : public IRenderCommand
		{
		public:
			static constexpr uint32_t MAX_INPUT_COUNT   = 4;
			static constexpr uint32_t MAX_CB_SIZE_BYTES = 256;


		private:
			graphics::IShader* computeShader_;

			RenderTargetHandle inputs_[MAX_INPUT_COUNT];
			uint32_t           inputCount_;
			RenderTargetHandle output_;

			graphics::IConstantBuffer*             constantBuffer_;
			std::array<uint8_t, MAX_CB_SIZE_BYTES> cbData_;

			uint32_t width_;
			uint32_t height_;


		public:
			/**
			 * @param computeShader  実行する CS
			 * @param inputs         t0.. に順にバインドする入力 RT(先頭 inputCount 個。最大 MAX_INPUT_COUNT)
			 * @param inputCount     inputs の要素数
			 * @param output         u0 にバインドする出力 RT
			 * @param constantBuffer b0 にバインドする CB。nullptr なら CB を使わない
			 * @param cbData         constantBuffer へコピーする内容。constantBuffer が nullptr なら無視
			 * @param cbSizeBytes    cbData のバイト数(MAX_CB_SIZE_BYTES 以下)
			 * @param width          Dispatch のスレッドグループ数算出に使う幅(ピクセル)
			 * @param height         同高さ(ピクセル)
			 */
			FullscreenComputeCommand(graphics::IShader*         computeShader,
			                         const RenderTargetHandle*  inputs, const uint32_t inputCount,
			                         const RenderTargetHandle   output,
			                         graphics::IConstantBuffer* constantBuffer, const void* cbData, const uint32_t cbSizeBytes,
			                         const uint32_t width, const uint32_t height);

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;
		};
	}
}
