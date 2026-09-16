#pragma once
#include <cstdint>
#include "Rendering/RenderTargetHandle.h"
#include "Rendering/PostProcess/PostProcessContext.h"


namespace aq
{
	namespace rendering
	{
		class RenderCommandList;


		/**
		 * ポストプロセス 1 エフェクトの抽象(旧 IPostProcessPass。設計書/レンダーパイプライン設計.md P2 で改名)。
		 * 中間 RT は各エフェクトが Initialize で生成・所有する。
		 * Rendering/Pipeline/Passes/ の MotionBlurPass / BloomPass / TonemapPass が
		 * 対応するエフェクトを 1 つずつ所有して薄く包む。
		 */
		class IPostProcessEffect
		{
		public:
			virtual ~IPostProcessEffect() = default;

			/** シェーダーと RT を生成する */
			virtual bool Initialize(const uint32_t width, const uint32_t height) = 0;

			/** このフレームでエフェクトを積むか (false なら入力を素通しする) */
			virtual bool IsEnabled(const PostProcessContext& context) const = 0;

			/**
			 * コマンドを積む
			 * @param input このエフェクトの入力 RT
			 * @return このエフェクトの出力 RT
			 */
			virtual RenderTargetHandle Build(
				RenderCommandList&         outList,
				const PostProcessContext&  context,
				const RenderTargetHandle   input) = 0;
		};
	}
}
