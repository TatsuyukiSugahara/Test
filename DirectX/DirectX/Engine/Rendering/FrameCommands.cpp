#include "FrameCommands.h"
#include "../Graphics/RenderContext.h"
#include "../Graphics/GraphicsDevice.h"
#include <cassert>


namespace engine
{
	namespace rendering
	{
		SetRenderTargetCommand::SetRenderTargetCommand(uint32_t numViews, const RenderTargetHandle* handles)
			: numViews_(numViews)
		{
			assert(numViews >= 1 && numViews <= MAX_MRT);
			uint32_t count = (numViews <= MAX_MRT) ? numViews : MAX_MRT;
			for (uint32_t i = 0; i < count; ++i)
				handles_[i] = handles[i];
		}


		void SetRenderTargetCommand::Execute(graphics::RenderContext& ctx, FrameContext&) const
		{
			// MRT 未実装: numViews_ > 1 の場合、handles_[0] だけ解決した状態で
			// OMSetRenderTargets に numViews_ を渡すと、D3D11 側が隣接メモリを RTV として
			// 読み込み範囲外アクセスになる。IRenderContextImpl が IRenderTarget* 配列を
			// 受け取れるようになるまで 1 に制限する。
			assert(numViews_ == 1);
			if (numViews_ != 1) return;

			// 無効ハンドルまたは範囲外インデックスは呼び出し元のバグ。
			// Release ビルドでも GetMainRenderTargetCount() で境界を確認し、
			// 配列外アクセスを防ぐ。
			const uint32_t rtCount = graphics::GraphicsDevice::Get().GetMainRenderTargetCount();
			assert(handles_[0].IsValid() && handles_[0].index < rtCount);
			if (!handles_[0].IsValid() || handles_[0].index >= rtCount) return;

			// Execute 時にハンドルを解決して IRenderTarget を取得する。
			// GraphicsDevice がすべての RT オブジェクトを所有する。
			// Submit が in-flight の間は RT のリサイズ・再生成をしないこと（呼び出し元の責任）。
			// DX12: RTV デスクリプタ・ヒープから D3D12_CPU_DESCRIPTOR_HANDLE を取得して渡す。
			auto& rt = graphics::GraphicsDevice::Get().GetMainRenderTarget(handles_[0].index);
			ctx.OMSetRenderTargets(numViews_, &rt);
		}


		void ClearRenderTargetCommand::Execute(graphics::RenderContext& ctx, FrameContext&) const
		{
			ctx.ClearRenderTargetView(index_, const_cast<float*>(color_));
		}


		void SetViewportCommand::Execute(graphics::RenderContext& ctx, FrameContext&) const
		{
			ctx.RSSetViewport(x_, y_, w_, h_);
		}
	}
}
