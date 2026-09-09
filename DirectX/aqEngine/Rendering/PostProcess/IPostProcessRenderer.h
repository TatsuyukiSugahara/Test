#pragma once
#include "Rendering/RenderTargetHandle.h"
#ifdef AQ_DEBUG_IMGUI
#include <memory>
#include "Core/IDebugRenderable.h"
#endif

namespace aq
{
	namespace rendering
	{
		class RenderCommandList;
		struct CameraData;

		class IPostProcessRenderer
		{
		public:
			virtual ~IPostProcessRenderer() = default;

			virtual void BuildPostProcessCommandList(
				RenderCommandList& outList,
				RenderTargetHandle sceneRT,
				uint32_t           width,
				uint32_t           height) = 0;

			/** ポストプロセス後の最終出力 RT ハンドル。displayRT に渡す。 */
			virtual RenderTargetHandle GetFinalRT() const = 0;

			// ---- カメラモーションブラー用フック (対応しない実装は無視してよい) ----

			/** メインパスのカメラ。BuildPostProcessCommandList の前に毎フレーム呼ばれる */
			virtual void SetFrameCamera(const CameraData& /*camera*/) {}

			/** ブラー強度スケール (0 でパス無効) */
			virtual void SetMotionBlurStrength(const float /*strength*/) {}

			/** worldPos (GBuffer2) の RT。速度再構成に使う */
			virtual void SetWorldPosRT(RenderTargetHandle /*handle*/) {}

#ifdef AQ_DEBUG_IMGUI
			/** デバッグパネルを生成して返す。非対応の実装は nullptr を返す。 */
			virtual std::unique_ptr<IDebugRenderable> CreateDebugPanel() { return nullptr; }
#endif
		};
	}
}
