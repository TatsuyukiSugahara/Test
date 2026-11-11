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
#include "Passes/UIPass.h"
// HardShadowRenderer / DeferredRenderer / MotionBlurPass / BloomPass / TonemapPass は
// aq.h が供給済み。HiZRenderer と SkyRenderer は供給されないのでここで足す。
#include "Rendering/Occlusion/HiZRenderer.h"
#include "Rendering/Sky/SkyRenderer.h"
#include "Rendering/IBL/IBLEnvironment.h"


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
					} else {
						// 生成に失敗すると ShadowPass は列へ入らない(パス側の Setup は走らない)ので、ここで残す。
						aq::StartupMark("[pipeline] 影の生成に失敗したので影なしで続行");
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
						// IBL。生成に失敗したら従来の環境光(PSMain)で続行する。
						// ロード完了は DeferredRenderer が毎フレーム IsReady() で見る(完了までは PSMain)。
						if (preset.enableIBL)
						{
							auto ibl = std::make_shared<IBLEnvironment>();
							if (ibl->Create(preset.iblIrradiancePath, preset.iblSpecularPath, preset.iblBrdfLutPath)) {
								deferred->SetIBLEnvironment(std::move(ibl));
							} else {
								aq::StartupMark("[pipeline] IBL の生成に失敗したので IBL なしで続行");
							}
						}

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
					} else {
						// 生成に失敗すると SkyPass は列へ入らない(パス側の Setup は走らない)ので、ここで残す。
						aq::StartupMark("[pipeline] 空の生成に失敗したので空なしで続行");
					}
				}

				// フォワード(透明・特殊マテリアル。Depth が無ければ全アイテムをフォワードで描く)。
				builder.Add<ForwardPass>();
				// 海(FFT コンピュートに依存するため compute 必須。非対応環境は IsSupported() で除外)。
				builder.Add<OceanPass>();
				// パーティクル(半透明ビルボード。先頭ビューのみ)。
				builder.Add<ParticlePass>();

				// ポストプロセス(MotionBlur → Bloom → Tonemap)。compute 必須(各パスの IsSupported() で判定)。
				// worldPos (GBuffer2) の配線は MotionBlurPass::Build が掲示板(WorldPos キー)から行う。
				if (preset.enablePostProcess)
				{
					builder.Add<MotionBlurPass>();

					auto bloom = std::make_unique<BloomPass>();
					bloom->GetEffect()->SetThreshold(preset.bloomThreshold);
					bloom->GetEffect()->SetIntensity(preset.bloomIntensity);
					bloom->GetEffect()->SetBlurPasses(preset.bloomBlurPasses);
					BloomPass* bloomPtr = bloom.get();
					builder.Add(std::move(bloom));

					auto tonemap = std::make_unique<TonemapPass>();
					tonemap->SetBloomIntensitySource(bloomPtr);
					builder.Add(std::move(tonemap));
				}

				// UI(ポストプロセス後・ImGui 前に描画)。
				builder.Add<UIPass>(std::move(uiCallback));

				return builder;
			}


			PipelineBuilder Mobile(const RendererPreset& preset, std::function<void(RenderCommandList&)> uiCallback)
			{
				PipelineBuilder builder;

				// 影。生成に失敗したら影なしで続行する(Standard() と同じ作法)。
				if (preset.enableShadow)
				{
					auto shadow = std::make_unique<HardShadowRenderer>();
					if (shadow->Create(preset.shadow, preset.shadowVSPath)) {
						builder.Add<ShadowPass>(std::move(shadow));
					} else {
						// 生成に失敗すると ShadowPass は列へ入らない(パス側の Setup は走らない)ので、ここで残す。
						aq::StartupMark("[pipeline] 影の生成に失敗したので影なしで続行");
					}
				}

				// 空。ロードに失敗しても続行する(背景はクリア色のまま)。
				if (preset.enableSky)
				{
					auto sky = std::make_unique<SkyRenderer>();
					if (sky->Create(preset.skyCubemapPath)) {
						builder.Add<SkyPass>(std::move(sky));
					} else {
						// 生成に失敗すると SkyPass は列へ入らない(パス側の Setup は走らない)ので、ここで残す。
						aq::StartupMark("[pipeline] 空の生成に失敗したので空なしで続行");
					}
				}

				// GBuffer を持たない(フォワードのみ)。Depth が無いので全アイテムをここで描く。
				// 空より後ろに置く: 半透明が空と合成されるため(Standard() と同じ順。Skybox設計 §4 課題 5)。
				builder.Add<ForwardPass>();

				// パーティクル(半透明ビルボード。先頭ビューのみ)。
				builder.Add<ParticlePass>();

				// トーンマップのみ(Bloom / MotionBlur は無し。compute 必須)。
				if (preset.enablePostProcess) {
					builder.Add<TonemapPass>();
				}

				// UI(ポストプロセス後・ImGui 前に描画)。
				builder.Add<UIPass>(std::move(uiCallback));

				return builder;
			}


			PipelineBuilder ForCurrentPlatform(const RendererPreset& preset, std::function<void(RenderCommandList&)> uiCallback)
			{
				PipelineKind kind = preset.pipeline;
				if (kind == PipelineKind::Auto)
				{
#if defined(AQ_PLATFORM_ANDROID) || defined(AQ_PLATFORM_IOS)
					kind = PipelineKind::Mobile;
#else
					kind = PipelineKind::Standard;
#endif
				}

				return (kind == PipelineKind::Mobile)
					? Mobile(preset, std::move(uiCallback))
					: Standard(preset, std::move(uiCallback));
			}
		}
	}
}
