#pragma once
#include <memory>
#include "IDeferredRenderer.h"
#include "Graphics/IShader.h"
#include "Graphics/ISamplerState.h"
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"
namespace aq { namespace rendering { class IShadowRenderer; } }
#endif

namespace aq
{
	namespace rendering
	{
		class IBLEnvironment;


		/**
		 * ディファードレンダラー実装。
		 *
		 * 所有するリソース:
		 *   - GBuffer0: RGBA8  (albedo RGB + specMask)      ← depth バッファあり
		 *   - GBuffer1: RGBA16F (normal XY + gloss)
		 *   - GBuffer2: RGBA16F (worldPos XYZ + specularIntensity)
		 *   - GBuffer3: RGBA16F (emissive RGB + pixelTag)
		 *   - PBRLighting の VS/PS シェーダー(と、デカール用の Decal VS/PS)
		 *   - PBRLighting の PSMainIBL と IBL 環境(任意。設計書/IBL設計.md)
		 */
		class DeferredRenderer final : public IDeferredRenderer
		{
		public:
			bool Create(uint32_t width, uint32_t height) override;

			/**
			 * リサイズ(設計書/エンジン課題解消設計.md P4)。インスタンスは作り直さず、中身の
			 * G-Buffer(G0〜G3 と G0 の深度)だけを入れ替える(GBuffer / Decal / Lighting の 3 パスが共有するため)。
			 * ゲームスレッドで、レンダースレッド停止中(WaitIdle 済み)にだけ呼ぶ。
			 *   PrepareResize: 新寸法の G-Buffer を一時領域へ作る(旧はそのまま)。失敗時は自己清掃して false
			 *   CommitResize : 旧 G-Buffer を解放して一時領域と入れ替える
			 *   AbortResize  : 一時領域を解放する(何度呼んでも安全)
			 */
			bool PrepareResize(uint32_t width, uint32_t height);
			void CommitResize();
			void AbortResize();

			void BuildGBufferCommandList(const RenderFrame& frame,
			                             RenderCommandList& outList,
			                             const bool clearTargets = true) const override;

			void BuildDecalCommandList(const RenderFrame& frame,
			                           RenderCommandList& outList) const override;

			void BuildLightingCommandList(const RenderFrame& frame,
			                              RenderCommandList& outList,
			                              RenderTargetHandle sceneRT) const override;

			RenderTargetHandle GetGBuffer0Handle() const override { return gbuffer0Handle_; }
			RenderTargetHandle GetGBuffer1Handle() const { return gbuffer1Handle_; }
			RenderTargetHandle GetGBuffer2Handle() const { return gbuffer2Handle_; }
			RenderTargetHandle GetGBuffer3Handle() const { return gbuffer3Handle_; }

			/**
			 * IBL 環境を設定する(設計書/IBL設計.md §1.1)。パイプライン構築時に 1 回だけ呼ぶ前提
			 * (実行時の差し替えは範囲外)。null で IBL を使わない。
			 * 環境が IsReady() で、PSMainIBL の生成に成功しているときだけライティングが IBL を使う。
			 */
			void SetIBLEnvironment(std::shared_ptr<IBLEnvironment> ibl) { ibl_ = std::move(ibl); }
			const std::shared_ptr<IBLEnvironment>& GetIBLEnvironment() const { return ibl_; }

#ifdef AQ_DEBUG_IMGUI
			std::unique_ptr<IDebugRenderable> CreateDebugPanel(IShadowRenderer* shadow = nullptr);
#endif

		private:
			/** G-Buffer の RT 一式 */
			struct Targets
			{
				RenderTargetHandle g0;
				RenderTargetHandle g1;
				RenderTargetHandle g2;
				RenderTargetHandle g3;
			};

			/** 新寸法の G-Buffer を out へ作る。失敗したら作った分を解放して false */
			static bool CreateTargets(uint32_t width, uint32_t height, Targets& out);
			/** out の RT を解放して無効化する(何度呼んでも安全) */
			static void ReleaseTargets(Targets& targets);


		private:
			RenderTargetHandle gbuffer0Handle_;
			RenderTargetHandle gbuffer1Handle_;
			RenderTargetHandle gbuffer2Handle_;
			RenderTargetHandle gbuffer3Handle_;

			/** PrepareResize が作った新 G-Buffer(CommitResize で本体へ入れ替わる) */
			Targets pending_;

			std::shared_ptr<graphics::IShader> lightingVS_;
			std::shared_ptr<graphics::IShader> lightingPS_;

			// IBL 用 (任意: 生成・ロード失敗時は従来の PSMain で続行)
			std::shared_ptr<graphics::IShader> lightingIBLPS_;
			std::shared_ptr<IBLEnvironment>    ibl_;

			// 投影デカールパス用 (任意: ロード失敗時はデカールパスをスキップ)
			std::shared_ptr<graphics::IShader>       decalVS_;
			std::shared_ptr<graphics::IShader>       decalPS_;
			std::unique_ptr<graphics::ISamplerState> decalSampler_;
		};
	}
}
