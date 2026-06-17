#pragma once
#include "ShadowData.h"
#include "Graphics/Lighting.h"
#include "Graphics/IDepthMap.h"
#include "Rendering/RenderTargetHandle.h"


namespace aq
{
	namespace rendering
	{
		struct RenderFrame;
		class RenderCommandList;

		/**
		 * シャドウ手法の抽象インターフェース。
		 * HardShadowRenderer (今回) / CascadedShadowRenderer / VSMRenderer などが実装する。
		 */
		class IShadowRenderer
		{
		public:
			virtual ~IShadowRenderer() = default;

			/**
			 * シャドウパスのコマンドを outList に記録する。
			 * prevHandle / prevW / prevH はシャドウパス終了後に直前の RT を復元するために使う。
			 * ハンドルの解決は Execute 時に行う。
			 */
			virtual void BuildShadowCommandList(
				const RenderFrame& frame,
				RenderCommandList& outList,
				RenderTargetHandle prevHandle,
				float              prevViewportW,
				float              prevViewportH) = 0;

			/**
			 * フレームの DirectionalLight 設定から ShadowCBData を計算する。
			 * Renderer::BuildCommandList の先頭で呼ばれ RenderFrame::shadow を埋める。
			 */
			virtual void FillShadowCBData(
				const graphics::DirectionalLight& light,
				ShadowCBData&                     outData) const = 0;

			/** メインパスで SRV (t4) にバインドするデプスマップ */
			virtual graphics::IDepthMap* GetDepthMap() const = 0;
		};
	}
}
