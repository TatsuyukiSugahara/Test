#pragma once
#include <cstdint>
#include <memory>
#include "Rendering/RenderCommandList.h"
#include "Rendering/RenderTargetHandle.h"
#include "Graphics/IShader.h"
#include "Core/IDebugRenderable.h"


namespace aq
{
	namespace rendering
	{
		class DeferredRenderer;
		class IBLEnvironment;
		struct CameraData;
		struct PassViewInfo;


		/**
		 * SSR (Screen Space Reflection) のレンダラー (設計書/SSR設計.md §1)。
		 *
		 * トレースの CS (SSRTrace.fx)・合成の VS/PS (SSRComposite.fx)・トレースの出力 RT
		 * (R16G16B16A16_Float、全解像度) を所有し、パラメータを保持する。
		 * 出力 RT のリサイズは PrepareResize / CommitResize / AbortResize (MotionBlurEffect と同じ作法)。
		 *
		 * 生成に失敗しても落とさない (Create() が false を返し、PipelinePresets は SSRPass を足さない。
		 * 出力 RT が作れなかったときは何も積まない。どちらも絵は IBL だけのときと同じになる)。
		 *
		 * スレッド: Create / パラメータの読み書き / Build* / リサイズはメインスレッド。
		 * パラメータはコマンド生成時に値で写す (レンダースレッドはこのクラスを読まない。§0.2)。
		 */
		class SSRRenderer
		{
		public:
			/** testMode の値 (デバッグ用。トレースの出力の代わりに合成シェーダへ固定の ssr を与える) */
			static constexpr uint32_t TEST_MODE_NONE       = 0;  // 通常 (トレースの出力を使う)
			static constexpr uint32_t TEST_MODE_FULL_BLACK = 1;  // 当たった色を黒・確信度 1 として扱う (鏡面キューブの項を全部差し引く)


		private:
			/** トレースの CS (SSRTrace.fx) */
			std::unique_ptr<graphics::IShader> traceCS_;

			/** 合成の VS/PS (SSRComposite.fx) */
			std::unique_ptr<graphics::IShader> compositeVS_;
			std::unique_ptr<graphics::IShader> compositePS_;

			/** トレースの出力 RT (rgb = 当たった画素の Scene の色、a = 確信度) */
			RenderTargetHandle outputRTHandle_;

			/** リサイズ用の一時領域 (PrepareResize で作り、CommitResize で本体へ入れ替える) */
			RenderTargetHandle pendingOutputRTHandle_;

			/** SSR の ON/OFF (既定 true。false なら何も積まない = IBL だけの絵) */
			bool enabled_ = true;

			/** トレースのパラメータ (設計書 §0.3〜§0.5) */
			float    maxDistance_        = 50.0f;   // レイの最大長 (ワールド単位)
			float    thicknessScale_     = 0.02f;   // 厚み係数
			uint32_t maxSteps_           = 64;      // 線形マーチの最大ステップ数
			uint32_t binarySteps_        = 6;       // 二分探索の回数
			float    edgeFade_           = 0.1f;    // 画面端で確信度を下げる幅 (ビュー矩形に対する割合)
			float    roughnessFadeStart_ = 0.3f;    // この roughness から確信度を下げ始める
			float    roughnessFadeEnd_   = 0.5f;    // この roughness 以上は SSR を使わない

			/** デバッグ表示 (確信度をグレーで上書きする) */
			bool debugView_ = false;

			/**
			 * デバッグ用の切り替え (TEST_MODE_*)。TEST_MODE_FULL_BLACK は P1 の評価
			 * (負の加算がクランプされないこと) に使った固定入力で、確かめ直すために残している。
			 */
			uint32_t testMode_ = TEST_MODE_NONE;


		private:
			/** 出力 RT の生成 (CreateOutputRT / PrepareResize 共用) */
			static RenderTargetHandle CreateOutputRTInternal(const uint32_t width, const uint32_t height);


		public:
			/**
			 * トレースの CS と合成の VS/PS を生成する (同期)。
			 * @return 生成できたら true。false なら SSR を使わない
			 */
			bool Create();

			/**
			 * トレースの出力 RT を作る (SSRPass::Setup から呼ぶ)。
			 * @return 作れたら true。false なら SSR は何も積まない (起動は続ける)
			 */
			bool CreateOutputRT(const uint32_t width, const uint32_t height);

			/**
			 * リサイズ (設計書/エンジン課題解消設計.md P4)。ゲームスレッドで WaitIdle 済みの区間にだけ呼ぶ。
			 * PrepareResize は新寸法の出力 RT を一時領域へ作る (失敗したら片付けて false)。
			 * CommitResize は旧 RT を解放して入れ替える。AbortResize は一時領域を解放する (何度呼んでも安全)。
			 */
			bool PrepareResize(const uint32_t width, const uint32_t height);
			void CommitResize();
			void AbortResize();

			/** トレースと合成を積める状態か (ON で、シェーダと出力 RT がそろっている) */
			bool IsActive() const;

			/**
			 * トレースのコマンドを 1 本積む (compute。RTV を外して回すので、呼び出し側は合成の前に
			 * シーン RT をバインドし直すこと)。
			 * @param camera   このビューのカメラ (行列と near 面を値で写す)
			 * @param view     このビューの矩形と画面全体の寸法
			 * @param sceneRT  ライティング済みのシーン RT (SRV で読む)
			 */
			void BuildTraceCommandList(const DeferredRenderer&  deferred,
			                           const CameraData&        camera,
			                           const PassViewInfo&      view,
			                           const RenderTargetHandle sceneRT,
			                           RenderCommandList&       outList) const;

			/**
			 * 合成のコマンドを 1 本積む。RT は設定しない (呼び出し側でシーン RT をバインド済みの前提)。
			 * 呼び出し側 (SSRPass) が、そのビューのライティングが IBL で描いたこと (DeferredRenderer::WasIBLUsed) を
			 * 確かめてから呼ぶ。
			 * @param deferred G-Buffer のハンドルを取る
			 * @param ibl      ライティングが使った IBL の環境 (コマンドが shared_ptr で保持する)
			 */
			void BuildCompositeCommandList(const DeferredRenderer& deferred,
			                               const std::shared_ptr<const IBLEnvironment>& ibl,
			                               RenderCommandList& outList) const;

#ifdef AQ_DEBUG_IMGUI
			/** デバッグパネル ("SSR" タブ) を作る。パネルはこのレンダラーを参照するので、先に破棄すること */
			std::unique_ptr<IDebugRenderable> CreateDebugPanel();
#endif


			/**
			 * パラメータ (メインスレッド)
			 */
		public:
			/** SSR の ON/OFF */
			inline void SetEnabled(const bool enabled) { enabled_ = enabled; }
			inline bool IsEnabled() const { return enabled_; }

			/** レイの最大長 (ワールド単位) */
			inline void SetMaxDistance(const float maxDistance) { maxDistance_ = maxDistance; }
			inline float GetMaxDistance() const { return maxDistance_; }

			/** 厚み係数 */
			inline void SetThicknessScale(const float thicknessScale) { thicknessScale_ = thicknessScale; }
			inline float GetThicknessScale() const { return thicknessScale_; }

			/** 線形マーチの最大ステップ数 */
			inline void SetMaxSteps(const uint32_t maxSteps) { maxSteps_ = maxSteps; }
			inline uint32_t GetMaxSteps() const { return maxSteps_; }

			/** 二分探索の回数 */
			inline void SetBinarySteps(const uint32_t binarySteps) { binarySteps_ = binarySteps; }
			inline uint32_t GetBinarySteps() const { return binarySteps_; }

			/** 画面端で確信度を下げる幅 (ビュー矩形に対する割合) */
			inline void SetEdgeFade(const float edgeFade) { edgeFade_ = edgeFade; }
			inline float GetEdgeFade() const { return edgeFade_; }

			/** roughness のしきい値 (start から確信度を下げ始め、end 以上は使わない) */
			inline void SetRoughnessFade(const float start, const float end) { roughnessFadeStart_ = start; roughnessFadeEnd_ = end; }
			inline float GetRoughnessFadeStart() const { return roughnessFadeStart_; }
			inline float GetRoughnessFadeEnd() const { return roughnessFadeEnd_; }

			/** デバッグ表示 (確信度をグレーで上書きする) */
			inline void SetDebugView(const bool debugView) { debugView_ = debugView; }
			inline bool IsDebugView() const { return debugView_; }

			/** デバッグ用の切り替え (TEST_MODE_*) */
			inline void SetTestMode(const uint32_t testMode) { testMode_ = testMode; }
			inline uint32_t GetTestMode() const { return testMode_; }
		};
	}
}