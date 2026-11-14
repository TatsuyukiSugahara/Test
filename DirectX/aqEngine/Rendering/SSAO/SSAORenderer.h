#pragma once
#include <cstdint>
#include <memory>
#include "Rendering/RenderCommandList.h"
#include "Rendering/RenderTargetHandle.h"
#include "Graphics/IShader.h"


namespace aq
{
	namespace rendering
	{
		class DeferredRenderer;
		class IBLEnvironment;


		/**
		 * SSAO (Screen Space Ambient Occlusion) のレンダラー (設計書/SSAO設計.md §1)。
		 *
		 * 合成の VS/PS/PSIBL (SSAOComposite.fx)・AO の RT 2 枚 (生の AO・ぼかした AO。R32_Float、全解像度) を所有し、
		 * パラメータを保持する。RT のリサイズは PrepareResize / CommitResize / AbortResize (SSRRenderer と同じ作法)。
		 * P1 では AO の CS (SSAO.fx / SSAOBlur.fx) を持たない (P2 で足す)。
		 *
		 * 生成に失敗しても落とさない (VS / PS が作れなければ Create() が false を返し、PipelinePresets は SSAOPass を足さない。
		 * PSIBL だけが作れないときは IBL 経路だけ合成しない。RT が作れなかったときは何も積まない。
		 * どれも絵は SSAO なしと同じになる)。
		 *
		 * スレッド: Create / パラメータの読み書き / Build* / リサイズはメインスレッド。
		 * パラメータはコマンド生成時に値で写す (レンダースレッドはこのクラスを読まない。§0.2)。
		 */
		class SSAORenderer
		{
		public:
			/** testMode の値 (デバッグ用。合成シェーダへ固定の ao を与える) */
			static constexpr uint32_t TEST_MODE_NONE    = 0;  // 通常 (ぼかした AO を使う)
			static constexpr uint32_t TEST_MODE_AO_ONE  = 1;  // ao を 1 に固定 (差分 0)
			static constexpr uint32_t TEST_MODE_AO_ZERO = 2;  // ao を 0 に固定 (拡散の環境光を全部差し引く)


		private:
			/** 合成の VS/PS/PSIBL (SSAOComposite.fx) */
			std::unique_ptr<graphics::IShader> compositeVS_;
			std::unique_ptr<graphics::IShader> compositePS_;
			std::unique_ptr<graphics::IShader> compositePSIBL_;

			/** 生の AO・ぼかした AO (R32_Float。r = 遮蔽されない割合) */
			RenderTargetHandle rawAORTHandle_;
			RenderTargetHandle blurredAORTHandle_;

			/** リサイズ用の一時領域 (PrepareResize で作り、CommitResize で本体へ入れ替える) */
			RenderTargetHandle pendingRawAORTHandle_;
			RenderTargetHandle pendingBlurredAORTHandle_;

			/** SSAO の ON/OFF (既定 true。false なら何も積まない = SSAO なしの絵) */
			bool enabled_ = true;

			/** AO のパラメータ (設計書 §0.3 / §0.4) */
			float    radius_          = 0.5f;    // サンプルの半径 (ワールド単位)
			float    intensity_       = 1.0f;
			float    power_           = 1.0f;
			float    biasScale_       = 0.002f;
			uint32_t sampleCount_     = 12;
			float    maxScreenRadius_ = 64.0f;   // 画面上の半径の上限 (画素)
			float    planeTolerance_  = 0.05f;   // ぼかしの平面距離の許容

			/** デバッグ表示 (ao をグレーで上書きする) */
			bool debugView_ = false;

			/** デバッグ用の切り替え (TEST_MODE_*) */
			uint32_t testMode_ = TEST_MODE_NONE;


		private:
			/** AO の RT の生成 (CreateTargets / PrepareResize 共用) */
			static RenderTargetHandle CreateAORTInternal(const uint32_t width, const uint32_t height);


		public:
			/**
			 * 合成の VS/PS/PSIBL を生成する (同期)。
			 * @return VS と PS が作れたら true。false なら SSAO を使わない。PSIBL だけ作れないときは true (IBL 経路だけ合成しない)
			 */
			bool Create();

			/**
			 * AO の RT 2 枚を作る (SSAOPass::Setup から呼ぶ)。
			 * @return 作れたら true。false なら SSAO は何も積まない (起動は続ける)
			 */
			bool CreateTargets(const uint32_t width, const uint32_t height);

			/**
			 * リサイズ (設計書/エンジン課題解消設計.md P4)。ゲームスレッドで WaitIdle 済みの区間にだけ呼ぶ。
			 * PrepareResize は新寸法の RT を一時領域へ作る (失敗したら片付けて false)。
			 * CommitResize は旧 RT を解放して入れ替える。AbortResize は一時領域を解放する (何度呼んでも安全)。
			 */
			bool PrepareResize(const uint32_t width, const uint32_t height);
			void CommitResize();
			void AbortResize();

			/** 合成を積める状態か (ON で、VS / PS と RT 2 枚がそろっている) */
			bool IsActive() const;

			/**
			 * 合成のコマンドを 1 本積む。RT は設定しない (呼び出し側でシーン RT をバインド済みの前提)。
			 * 呼び出し側 (SSAOPass) が、そのビューのライティングが IBL で描いたか (DeferredRenderer::WasIBLUsed) を useIBL で渡す。
			 * P1 では AO を計算しないので、testMode が TEST_MODE_NONE のときは何も積まない (P2 で外す)。
			 * @param useIBL true なら PSMainIBL (ibl が null か PSIBL が無ければ積まない)、false なら PSMain
			 * @param ibl    useIBL のとき、ライティングが使った IBL の環境 (コマンドが shared_ptr で保持する)
			 */
			void BuildCompositeCommandList(const DeferredRenderer& deferred,
			                               const bool useIBL,
			                               const std::shared_ptr<const IBLEnvironment>& ibl,
			                               RenderCommandList& outList) const;


			/**
			 * パラメータ (メインスレッド)
			 */
		public:
			/** SSAO の ON/OFF */
			inline void SetEnabled(const bool enabled) { enabled_ = enabled; }
			inline bool IsEnabled() const { return enabled_; }

			/** サンプルの半径 (ワールド単位) */
			inline void SetRadius(const float radius) { radius_ = radius; }
			inline float GetRadius() const { return radius_; }

			/** 強度 */
			inline void SetIntensity(const float intensity) { intensity_ = intensity; }
			inline float GetIntensity() const { return intensity_; }

			/** 結果のべき乗 */
			inline void SetPower(const float power) { power_ = power; }
			inline float GetPower() const { return power_; }

			/** 自己遮蔽を防ぐ bias の係数 (ビュー空間の深度に掛ける) */
			inline void SetBiasScale(const float biasScale) { biasScale_ = biasScale; }
			inline float GetBiasScale() const { return biasScale_; }

			/** 1 画素あたりのサンプル数 */
			inline void SetSampleCount(const uint32_t sampleCount) { sampleCount_ = sampleCount; }
			inline uint32_t GetSampleCount() const { return sampleCount_; }

			/** 画面上の半径の上限 (画素) */
			inline void SetMaxScreenRadius(const float maxScreenRadius) { maxScreenRadius_ = maxScreenRadius; }
			inline float GetMaxScreenRadius() const { return maxScreenRadius_; }

			/** ぼかしの平面距離の許容 */
			inline void SetPlaneTolerance(const float planeTolerance) { planeTolerance_ = planeTolerance; }
			inline float GetPlaneTolerance() const { return planeTolerance_; }

			/** デバッグ表示 (ao をグレーで上書きする) */
			inline void SetDebugView(const bool debugView) { debugView_ = debugView; }
			inline bool IsDebugView() const { return debugView_; }

			/** デバッグ用の切り替え (TEST_MODE_*) */
			inline void SetTestMode(const uint32_t testMode) { testMode_ = testMode; }
			inline uint32_t GetTestMode() const { return testMode_; }
		};
	}
}
