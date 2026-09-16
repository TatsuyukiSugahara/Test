#include "aq.h"
#include "PipelinePresets.h"
#include "Passes/ShadowPass.h"
#include "Passes/ClusterCullPass.h"
#include "Passes/GBufferPass.h"
#include "Passes/HiZPass.h"
#include "Passes/DecalPass.h"
#include "Passes/DeferredLightingPass.h"
#include "Passes/SkyPass.h"
#include "Passes/ForwardPass.h"
#include "Passes/OceanPass.h"
#include "Passes/ParticlePass.h"
#include "Passes/PostProcessPass.h"
#include "Passes/UIPass.h"
// HardShadowRenderer / DeferredRenderer / PostProcessChain は aq.h が供給済み。
// HiZRenderer と SkyRenderer は供給されないのでここで足す。
#include "Rendering/Occlusion/HiZRenderer.h"
#include "Rendering/Sky/SkyRenderer.h"


namespace aq
{
	namespace rendering
	{
		namespace PipelinePresets
		{
			PipelineBuilder Standard(const RendererPreset& preset, std::function<void(RenderCommandList&)> uiCallback)
			{
				PipelineBuilder builder;

				const uint32_t renderW = Engine::Get().GetRenderWidth();
				const uint32_t renderH = Engine::Get().GetRenderHeight();

				// 影。生成に失敗したら影なしで続行する(従来の Application::SetupStandardRenderers と同じ作法)。
				if (preset.enableShadow)
				{
					auto shadow = std::make_unique<HardShadowRenderer>();
					if (shadow->Create(preset.shadow, preset.shadowVSPath)) {
						builder.Add<ShadowPass>(std::move(shadow));
					}
				}

				// GPU 駆動クラスタ(トライアングル)カリング。非対応環境は IsSupported() で自動除外される。
				builder.Add<ClusterCullPass>();

				// ディファード。生成に失敗したらフォワードのみで続行する。
				if (preset.enableDeferred)
				{
					auto deferred = std::make_shared<DeferredRenderer>();
					if (deferred->Create(renderW, renderH))
					{
						builder.Add<GBufferPass>(deferred);

						// Hi-Z(オクリュージョン基盤)。worldPos (GBuffer2) 確定後に構築する。
						if (preset.enableHiZ) {
							builder.Add<HiZPass>(std::make_shared<HiZRenderer>());
						}

						// 投影デカール(GBuffer0 albedo へ書き戻す。ライティング前)。
						builder.Add<DecalPass>(deferred);
						// ディファードライティング(シーン RT に書き込む)。
						builder.Add<DeferredLightingPass>(deferred);
					}
				}

				// 空。ロードに失敗しても続行する(背景はクリア色のまま)。
				if (preset.enableSky)
				{
					auto sky = std::make_unique<SkyRenderer>();
					if (sky->Create(preset.skyCubemapPath)) {
						builder.Add<SkyPass>(std::move(sky));
					}
				}

				// フォワード(透明・特殊マテリアル。Depth が無ければ全アイテムをフォワードで描く)。
				builder.Add<ForwardPass>();
				// 海(FFT コンピュートに依存するため compute 必須。非対応環境は IsSupported() で除外)。
				builder.Add<OceanPass>();
				// パーティクル(半透明ビルボード。先頭ビューのみ)。
				builder.Add<ParticlePass>();

				// ポストプロセス(MotionBlur → Bloom → Tonemap)。compute 必須。
				// worldPos (GBuffer2) の配線は PostProcessPass::Setup が掲示板から行う。
				if (preset.enablePostProcess)
				{
					auto postProcess = std::make_unique<PostProcessChain>();
					if (postProcess->Initialize(renderW, renderH,
					                            preset.bloomThreshold,
					                            preset.bloomIntensity,
					                            preset.bloomBlurPasses)) {
						builder.Add<PostProcessPass>(std::move(postProcess));
					}
				}

				// UI(ポストプロセス後・ImGui 前に描画)。
				builder.Add<UIPass>(std::move(uiCallback));

				return builder;
			}
		}
	}
}
