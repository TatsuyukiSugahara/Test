#pragma once
#include <memory>
#include "IPostProcessRenderer.h"
#include "PostProcessContext.h"
#include "Passes/MotionBlurPass.h"
#include "Passes/BloomPass.h"
#include "Passes/TonemapPass.h"
#include "Rendering/RenderTargetHandle.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * ポストプロセスのパスチェーン。
		 * MotionBlur → Bloom → Tonemap の順に実行し、パス間の入出力 RT を受け渡す。
		 *
		 * 使い方:
		 *   auto chain = std::make_unique<PostProcessChain>();
		 *   chain->Initialize(width, height);
		 *   renderer.SetPostProcessRenderer(std::move(chain));
		 */
		class PostProcessChain : public IPostProcessRenderer
		{
		private:
			/** パス (この順で実行する) */
			MotionBlurPass motionBlurPass_;
			BloomPass      bloomPass_;
			TonemapPass    tonemapPass_;

			/** フレーム入力 */
			RenderTargetHandle worldPosRTHandle_;   // GBuffer2 (SetWorldPosRT で注入)
			bool               frameCameraSet_ = false;  // SetFrameCamera 済みフレームのみ true


		public:
			PostProcessChain() = default;
			~PostProcessChain() override = default;

			bool Initialize(uint32_t width, uint32_t height,
			                float    threshold  = 1.0f,   // HDR: 1.0 超の輝度のみブルーム
			                float    intensity  = 0.45f,
			                uint32_t blurPasses = 4);


		public:
			void BuildPostProcessCommandList(
				RenderCommandList& outList,
				RenderTargetHandle sceneRT,
				uint32_t           width,
				uint32_t           height) override;

			RenderTargetHandle GetFinalRT() const override { return tonemapPass_.GetFinalRT(); }

			void SetFrameCamera(const CameraData& camera) override;
			void SetMotionBlurStrength(const float strength) override { motionBlurPass_.SetStrength(strength); }
			void SetWorldPosRT(RenderTargetHandle handle) override { worldPosRTHandle_ = handle; }

#ifdef AQ_DEBUG_IMGUI
			std::unique_ptr<IDebugRenderable> CreateDebugPanel() override;
#endif


			/**
			 * 各パスへのアクセス (デバッグパネル用)
			 */
		public:
			inline MotionBlurPass& GetMotionBlurPass() { return motionBlurPass_; }
			inline BloomPass&      GetBloomPass()      { return bloomPass_; }
			inline TonemapPass&    GetTonemapPass()    { return tonemapPass_; }
		};
	}
}
