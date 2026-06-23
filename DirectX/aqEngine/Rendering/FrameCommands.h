#pragma once
#include <cstdint>
#include <cstring>
#include "IRenderCommand.h"
#include "RenderTargetHandle.h"

namespace aq
{
	namespace rendering
	{
		/**
		 * フレーム単位のセットアップコマンド群（レンダーターゲット、クリア、ビューポート）。
		 * DX12 では、これらはリソースバリア・OMSetRenderTargets・ClearRTV・RSSetViewports として
		 * 描画コマンドと同じコマンドリストに記録される。
		 */

		class SetRenderTargetCommand final : public IRenderCommand
		{
		public:
			/** 最大同時レンダーターゲット数（D3D11/D3D12 の上限に合わせる）。 */
			static constexpr uint32_t MAX_MRT = 8;

			/** 単一RT用コンストラクタ（最多パス）。 */
			explicit SetRenderTargetCommand(RenderTargetHandle handle)
				: numViews_(1) { handles_[0] = handle; }

			/** MRT用コンストラクタ。numViews は [1, MAX_MRT] の範囲であること。 */
			SetRenderTargetCommand(uint32_t numViews, const RenderTargetHandle* handles);

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;

		private:
			uint32_t           numViews_ = 0;
			RenderTargetHandle handles_[MAX_MRT];
		};


		/** colorRT のカラーに depthSourceRT の深度を組み合わせてバインドする。
		 *  Forward pass で GBuffer0 の深度を scene RT に差し込む際に使用する。 */
		class SetRenderTargetWithDepthCommand final : public IRenderCommand
		{
		public:
			SetRenderTargetWithDepthCommand(RenderTargetHandle colorHandle,
			                                RenderTargetHandle depthHandle)
				: colorHandle_(colorHandle), depthHandle_(depthHandle) {}

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;

		private:
			RenderTargetHandle colorHandle_;
			RenderTargetHandle depthHandle_;
		};


		class ClearRenderTargetCommand final : public IRenderCommand
		{
		public:
			ClearRenderTargetCommand(uint32_t index, const float color[4])
				: index_(index)
			{
				std::memcpy(color_, color, sizeof(color_));
			}
			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;

		private:
			uint32_t index_;
			float    color_[4];
		};


		/** バインド中の深度バッファを 1.0 でクリアする。
		 *  ClearRenderTargetCommand (カラーのみ) と対で使う。 */
		class ClearDepthCommand final : public IRenderCommand
		{
		public:
			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;
		};


		class SetViewportCommand final : public IRenderCommand
		{
		public:
			SetViewportCommand(float x, float y, float w, float h)
				: x_(x), y_(y), w_(w), h_(h) {}
			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;

		private:
			float x_, y_, w_, h_;
		};
	}
}
