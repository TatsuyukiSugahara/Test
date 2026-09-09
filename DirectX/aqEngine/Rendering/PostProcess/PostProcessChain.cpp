#include "aq.h"
#include "PostProcessChain.h"
#ifdef AQ_DEBUG_IMGUI
#include "Rendering/PostProcess/Debug/PostProcessDebugPanel.h"
#endif


namespace aq
{
	namespace rendering
	{
		bool PostProcessChain::Initialize(uint32_t width, uint32_t height, float threshold, float intensity, uint32_t blurPasses)
		{
			bloomPass_.SetThreshold(threshold);
			bloomPass_.SetIntensity(intensity);
			bloomPass_.SetBlurPasses(blurPasses);

			if (!bloomPass_.Initialize(width, height))   { return false; }
			if (!tonemapPass_.Initialize(width, height)) { return false; }

			// モーションブラーは任意機能。生成に失敗してもチェーンは動かす。
			motionBlurPass_.Initialize(width, height);

			return true;
		}


		void PostProcessChain::BuildPostProcessCommandList(
			RenderCommandList& outList,
			RenderTargetHandle sceneRT,
			uint32_t           width,
			uint32_t           height)
		{
			PostProcessContext context;
			context.sceneRT    = sceneRT;
			context.width      = width;
			context.height     = height;
			context.hasCamera  = frameCameraSet_;
			context.worldPosRT = worldPosRTHandle_;

			// カメラは毎フレーム受け取り直す (SetFrameCamera を呼ばない分割画面経路はブラー無効)。
			frameCameraSet_ = false;

			// モーションブラー: 無効なら入力をそのまま次段へ流す
			RenderTargetHandle sceneInput = sceneRT;
			if (motionBlurPass_.IsEnabled(context)) {
				sceneInput = motionBlurPass_.Build(outList, context, sceneInput);
			}

			// ブルーム: 結果は合成の第 2 入力になるので本流の RT は差し替えない
			RenderTargetHandle bloomRT        = sceneInput;
			float              bloomIntensity = 0.0f;
			if (bloomPass_.IsEnabled(context)) {
				bloomRT        = bloomPass_.Build(outList, context, sceneInput);
				bloomIntensity = bloomPass_.GetIntensity();
			}
			tonemapPass_.SetBloomInput(bloomRT, bloomIntensity);

			// トーンマップ: 常時 ON
			RenderTargetHandle output = sceneInput;
			if (tonemapPass_.IsEnabled(context)) {
				output = tonemapPass_.Build(outList, context, sceneInput);
			}

			// ImGui 等その後のパスが最終 RT に描画できるよう RTV として復元する
			outList.Enqueue<SetRenderTargetCommand>(output);
		}


		void PostProcessChain::SetFrameCamera(const CameraData& camera)
		{
			motionBlurPass_.SetFrameCamera(camera);
			frameCameraSet_ = true;
		}


#ifdef AQ_DEBUG_IMGUI
		std::unique_ptr<IDebugRenderable> PostProcessChain::CreateDebugPanel()
		{
			return std::make_unique<PostProcessDebugPanel>(*this);
		}
#endif
	}
}
