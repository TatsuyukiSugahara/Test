#pragma once
#include <cstdint>
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderCommandList.h"
#include "Rendering/RenderTargetHandle.h"

namespace aq
{
	namespace rendering
	{
		/**
		 * ディファードレンダラーインターフェース。
		 * G-Buffer パスとライティングパスのコマンドリストを構築する責務を持つ。
		 */
		class IDeferredRenderer
		{
		public:
			virtual ~IDeferredRenderer() = default;

			/** G-Buffer RT を幅 width × 高さ height で生成する。失敗時は false を返す。 */
			virtual bool Create(uint32_t width, uint32_t height) = 0;

			/** G-Buffer パスのコマンドを outList に記録する。
			 *  clearTargets=false で GBuffer/深度のクリアを省略する
			 *  (分割画面で 2 ビュー目以降の描画結果を消さないため。クリアはビューポートを無視して全面に効く)。 */
			virtual void BuildGBufferCommandList(const RenderFrame& frame,
			                                     RenderCommandList& outList,
			                                     const bool clearTargets = true) const = 0;

			/** 投影デカールパスのコマンドを outList に記録する (G-Buffer パスとライティングパスの間)。
			 *  GBuffer0 (albedo) へデカールを書き戻す。decalItems が空なら何もしない。 */
			virtual void BuildDecalCommandList(const RenderFrame& /*frame*/,
			                                   RenderCommandList& /*outList*/) const {}

			/** ディファードライティングパスのコマンドを outList に記録する。
			 *  sceneRT は lightingPass の書き込み先 RT ハンドル。深度は GBuffer0 を使用する。 */
			virtual void BuildLightingCommandList(const RenderFrame& frame,
			                                      RenderCommandList& outList,
			                                      RenderTargetHandle sceneRT) const = 0;

			/** GBuffer0 の RT ハンドルを返す（Forward pass の深度源として使う）。 */
			virtual RenderTargetHandle GetGBuffer0Handle() const = 0;
		};
	}
}
