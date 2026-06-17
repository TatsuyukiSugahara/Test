#pragma once
#include "IShaderResourceView.h"
#include "IUnorderedAccessView.h"


namespace aq
{
	namespace graphics
	{
		/**
		 * RenderTarget abstraction (Bridge Pattern)
		 *
		 * 抽象層が扱うレンダリングターゲットのインターフェース。
		 * D3D11 具象型 (ID3D11RenderTargetView* 等) はこのインターフェースに一切登場しない。
		 */
		class IRenderTarget
		{
		public:
			virtual ~IRenderTarget() = default;

			virtual IShaderResourceView&  GetRenderTargetSRV() = 0;
			virtual IUnorderedAccessView& GetRenderTargetUAV() = 0;
		};
	}
}
