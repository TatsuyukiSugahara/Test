#pragma once
#include <memory>
#include "Rendering/RenderTargetHandle.h"
#include "Rendering/Shadow/ShadowData.h"
#include "Math/Vector.h"


namespace aq
{
	namespace rendering
	{
		struct RenderFrame;
		class RenderCommandList;
		class DeferredRenderer;


		/**
		 * 独立したオフスクリーンシーンパス。
		 *
		 * カラー RT (深度なし) と縮小 GBuffer 一式 (DeferredRenderer) を自前で所有し、
		 * G-Buffer → ディファードライティング → フォワード/インスタンスの最小パス列だけを記録する。
		 * シャドウ / Hi-Z / ポストプロセス / UI / パーティクル / デカール / 海は積まない。
		 *
		 * Renderer を流用しないのは、あちらの所有物 (シャドウ・Hi-Z・ポスト・UI コールバック) が
		 * 1 組しか無く、2 パス目に使うと UI の焼き込みやシャドウの二重描画を踏むため。
		 *
		 * 使い方:
		 *   pass.Create(512, 512);
		 *   RenderSystem::Get().BuildRenderFrame(frame, camera, false, false, false, true);
		 *   pass.BuildCommandList(frame, *list);
		 *   renderThread.Submit(std::move(list), RenderTargetHandle{}, lighting,
		 *                       OffscreenScenePass::MakeNeutralShadowCBData());
		 */
		class OffscreenScenePass
		{
		private:
			/** 描画先 (深度は縮小 GBuffer0 が内包するので hasDepth=false) */
			RenderTargetHandle sceneRTHandle_;
			uint32_t           width_  = 0;
			uint32_t           height_ = 0;

			/** このパス専用の縮小 GBuffer 一式 */
			std::unique_ptr<DeferredRenderer> deferredRenderer_;

			/** 背景色 (ライティングは背景ピクセルを clip するのでここの色がそのまま残る) */
			float clearColor_[4] = { 0.0f, 0.0f, 0.0f, 1.0f };


		public:
			OffscreenScenePass();
			~OffscreenScenePass();

			OffscreenScenePass(const OffscreenScenePass&) = delete;
			OffscreenScenePass& operator=(const OffscreenScenePass&) = delete;


		public:
			/** カラー RT と縮小 GBuffer を生成する。失敗時は false */
			bool Create(const uint32_t width, const uint32_t height);

			/** 最小パス列 (GBuffer → ライティング → フォワード/インスタンス) を記録する */
			void BuildCommandList(const RenderFrame& frame, RenderCommandList& outList) const;


		public:
			/** 描画結果のカラー RT */
			inline RenderTargetHandle GetSceneRT() const { return sceneRTHandle_; }
			/** Create 済みか */
			inline bool IsReady() const { return deferredRenderer_ != nullptr && sceneRTHandle_.IsValid(); }
			/** 背景色 */
			void SetClearColor(const math::Vector4& color);


		public:
			/**
			 * 影を無効化する b3 シャドウ CB を返す。
			 * lightViewProj を「どのワールド座標も ndc.z = -1 へ落とす」行列にすることで、
			 * ShadowSampling.fx の範囲外判定が常に「影なし (1.0)」を返すようにする。
			 */
			static ShadowCBData MakeNeutralShadowCBData();
		};
	}
}
