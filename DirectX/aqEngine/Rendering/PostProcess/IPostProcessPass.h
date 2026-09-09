#pragma once
#include <cstdint>
#include "Rendering/RenderTargetHandle.h"
#include "PostProcessContext.h"


namespace aq
{
	namespace rendering
	{
		class RenderCommandList;


		/**
		 * ポストプロセス 1 パスの抽象。
		 * 中間 RT は各パスが Initialize で生成・所有する。
		 */
		class IPostProcessPass
		{
		public:
			virtual ~IPostProcessPass() = default;

			/** シェーダーと RT を生成する */
			virtual bool Initialize(const uint32_t width, const uint32_t height) = 0;

			/** このフレームでパスを積むか (false なら入力を素通しする) */
			virtual bool IsEnabled(const PostProcessContext& context) const = 0;

			/**
			 * コマンドを積む
			 * @param input このパスの入力 RT
			 * @return このパスの出力 RT
			 */
			virtual RenderTargetHandle Build(
				RenderCommandList&         outList,
				const PostProcessContext&  context,
				const RenderTargetHandle   input) = 0;
		};
	}
}
