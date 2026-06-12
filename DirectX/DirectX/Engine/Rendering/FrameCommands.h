#pragma once
#include <cstdint>
#include <cstring>
#include "IRenderCommand.h"
#include "RenderTargetHandle.h"

namespace engine
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

			/**
			 * MRT用コンストラクタ。numViews は [1, MAX_MRT] の範囲であること。
			 * DX11 は現在 handles_[0] のみ使用。フル MRT 対応は
			 * IRenderContextImpl::OMSetRenderTargets が IRenderTarget* 配列を
			 * 受け取れるようになってから行う。
			 */
			SetRenderTargetCommand(uint32_t numViews, const RenderTargetHandle* handles);

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;

		private:
			uint32_t           numViews_ = 0;
			RenderTargetHandle handles_[MAX_MRT];
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
