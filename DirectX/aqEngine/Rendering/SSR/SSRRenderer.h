#pragma once
#include <cstdint>
#include <memory>
#include "Rendering/RenderCommandList.h"
#include "Graphics/IShader.h"


namespace aq
{
	namespace rendering
	{
		class DeferredRenderer;
		class IBLEnvironment;


		/**
		 * SSR (Screen Space Reflection) のレンダラー (設計書/SSR設計.md §1)。
		 *
		 * P1 (差分合成の土台) では合成の VS/PS だけを所有し、SSRCompositeCommand を 1 本積む。
		 * トレースの CS・サンプラ・出力 RT とリサイズ、トレース用のパラメータは P2 で足す。
		 *
		 * 生成に失敗しても落とさない (Create() が false を返し、PipelinePresets は SSRPass を足さない。
		 * 絵は IBL だけのときと同じになる)。
		 *
		 * スレッド: Create / パラメータの読み書き / BuildCommandList はメインスレッド。
		 * パラメータはコマンド生成時に値で写す (レンダースレッドはこのクラスを読まない。§0.2)。
		 */
		class SSRRenderer
		{
		public:
			/** testMode の値 (P1 の評価用。トレースの代わりに合成シェーダへ固定の ssr を与える) */
			static constexpr uint32_t TEST_MODE_NONE       = 0;  // ssr = (0,0,0,0)。P1 ではトレースが無いのでこれが通常
			static constexpr uint32_t TEST_MODE_FULL_BLACK = 1;  // ssr = (0,0,0,1)。鏡面キューブの項を全部差し引く


		private:
			/** 合成の VS/PS (SSRComposite.fx) */
			std::unique_ptr<graphics::IShader> compositeVS_;
			std::unique_ptr<graphics::IShader> compositePS_;

			/** SSR の ON/OFF (既定 true。false なら何も積まない = IBL だけの絵) */
			bool enabled_ = true;

			/**
			 * P1 の評価用の切り替え (TEST_MODE_*)。P2 以降に残すかは P2 で判断する (設計書 §4)。
			 * P2 でトレースの出力 RT に置き換わるまでは、合成シェーダの ssr はこの値だけで決まる。
			 */
			uint32_t testMode_ = TEST_MODE_NONE;


		public:
			/**
			 * 合成の VS/PS を生成する (同期)。
			 * @return 生成できたら true。false なら SSR を使わない
			 */
			bool Create();

			/**
			 * 合成のコマンドを 1 本積む。RT は設定しない (呼び出し側でシーン RT をバインド済みの前提)。
			 * 呼び出し側 (SSRPass) が、そのビューのライティングが IBL で描いたこと (DeferredRenderer::WasIBLUsed) を
			 * 確かめてから呼ぶ。
			 * @param deferred G-Buffer のハンドルを取る
			 * @param ibl      ライティングが使った IBL の環境 (コマンドが shared_ptr で保持する)
			 */
			void BuildCommandList(const DeferredRenderer& deferred,
			                      const std::shared_ptr<const IBLEnvironment>& ibl,
			                      RenderCommandList& outList) const;


		public:
			/** SSR の ON/OFF (メインスレッド) */
			inline void SetEnabled(const bool enabled) { enabled_ = enabled; }
			inline bool IsEnabled() const { return enabled_; }

			/** P1 の評価用の切り替え (TEST_MODE_*。メインスレッド) */
			inline void SetTestMode(const uint32_t testMode) { testMode_ = testMode; }
			inline uint32_t GetTestMode() const { return testMode_; }
		};
	}
}
