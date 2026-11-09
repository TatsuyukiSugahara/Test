#pragma once
#include <memory>
#include "Rendering/PostProcess/Effects/IPostProcessEffect.h"
#include "Rendering/PostProcess/Commands/BloomPassCommand.h"
#include "Rendering/RenderTargetHandle.h"
#include "Graphics/IShader.h"
#include "Graphics/IBuffer.h"
#include "Graphics/ISamplerState.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * CS ベースのブルーム生成エフェクト(Dual Blur アルゴリズム)。
		 * 旧 BloomPass。設計書/レンダーパイプライン設計.md P2 で改名。
		 * 輝度抽出 → Down × N → Up × N を行い、結果を bright RT に残す。
		 * 合成は TonemapEffect が担当する。
		 */
		class BloomEffect final : public IPostProcessEffect
		{
		public:
			/** ブルームピラミッドの最大段数 */
			static constexpr uint32_t MAX_LEVELS = BloomPassCommand::MAX_LEVELS;


		private:
			std::unique_ptr<graphics::IShader>         extractShader_;
			std::unique_ptr<graphics::IShader>         dualBlurDownShader_;
			std::unique_ptr<graphics::IShader>         dualBlurUpShader_;       // 最終段（brightRT への純粋上書き）
			std::unique_ptr<graphics::IShader>         dualBlurUpAccumShader_;  // 中間段（pyramid への累積加算）
			std::unique_ptr<graphics::IConstantBuffer> bloomCB_;
			std::unique_ptr<graphics::ISamplerState>   sampler_;

			RenderTargetHandle brightRTHandle_;
			RenderTargetHandle pyramidRTHandles_[MAX_LEVELS]; // pyramid[i]: W/2^(i+1) × H/2^(i+1)

			/** リサイズ用の一時領域(PrepareResize で作り、CommitResize で本体へ入れ替える) */
			RenderTargetHandle pendingBrightRTHandle_;
			RenderTargetHandle pendingPyramidRTHandles_[MAX_LEVELS];

			float    threshold_  = 1.0f;   // HDR: 1.0 超の輝度のみブルーム
			float    intensity_  = 0.45f;
			uint32_t blurPasses_ = MAX_LEVELS;


		public:
			BloomEffect() = default;
			~BloomEffect() override = default;


		public:
			bool Initialize(const uint32_t width, const uint32_t height) override;
			bool IsEnabled(const PostProcessContext& context) const override;

			/**
			 * リサイズ(設計書/エンジン課題解消設計.md P4)。ゲームスレッドで WaitIdle 済みの区間にだけ呼ぶ。
			 * PrepareResize は新寸法の RT を一時領域へ作る(失敗したら自分で片付けて false)。
			 * CommitResize は旧 RT を解放して一時領域を入れ替える。AbortResize は一時領域を解放する(何度呼んでも安全)。
			 */
			bool PrepareResize(const uint32_t width, const uint32_t height);
			void CommitResize();
			void AbortResize();

			RenderTargetHandle Build(
				RenderCommandList&        outList,
				const PostProcessContext& context,
				const RenderTargetHandle  input) override;

			/** 輝度抽出後テクスチャのハンドル（デバッグ表示用） */
			inline RenderTargetHandle GetBrightRTHandle() const { return brightRTHandle_; }


			/**
			 * パラメータ
			 */
		public:
			inline float    GetThreshold()  const { return threshold_; }
			inline float    GetIntensity()  const { return intensity_; }
			inline uint32_t GetBlurPasses() const { return blurPasses_; }
			inline void     SetThreshold(const float v)     { threshold_  = v; }
			inline void     SetIntensity(const float v)     { intensity_  = v; }
			inline void     SetBlurPasses(const uint32_t v) { blurPasses_ = (v < 1) ? 1 : (v > MAX_LEVELS) ? MAX_LEVELS : v; }


		private:
			/** 輝度抽出 RT とピラミッド RT を作る(Initialize / PrepareResize 共用)。失敗時は作れた分が out に残る */
			static bool CreateTargets(const uint32_t width, const uint32_t height,
			                          RenderTargetHandle& outBright, RenderTargetHandle* outPyramid);

			/** RT 群を解放して無効ハンドルへ戻す */
			static void ReleaseTargets(RenderTargetHandle& bright, RenderTargetHandle* pyramid);
		};
	}
}
