#pragma once
#include <cstdint>
#include "Rendering/RenderTargetHandle.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * ポストプロセスチェーン 1 フレームぶんの入力
		 */
		struct PostProcessContext
		{
			/** 入力シーン RT (HDR) */
			RenderTargetHandle sceneRT;

			/** 処理解像度 */
			uint32_t width  = 0;
			uint32_t height = 0;

			/** このフレームのカメラを受け取っているか (分割画面経路は false) */
			bool hasCamera = false;

			/** worldPos (GBuffer2) の RT。速度再構成に使う */
			RenderTargetHandle worldPosRT;
		};
	}
}
