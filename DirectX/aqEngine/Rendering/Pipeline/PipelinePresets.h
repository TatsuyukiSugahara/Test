#pragma once
#include <cstdint>
#include <functional>
#include "PipelineBuilder.h"
#include "Rendering/RenderCommandList.h"
#include "Rendering/Shadow/ShadowData.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * ゲームが選べるパイプライン構成(設計書/レンダーパイプライン設計.md §1.4、P2)。
		 * Auto は ForCurrentPlatform() がプラットフォームで Standard / Mobile を選ぶ。
		 */
		enum class PipelineKind : uint8_t
		{
			Standard,
			Mobile,
			Auto,
		};




		/**
		 * 標準的な描画構成の設定値(設計書/使いやすさ改善設計.md P2-B)。
		 *
		 * 元は Core/AppHost.h の RendererPreset。`Rendering/` から `Core/` への依存を避けるため
		 * こちらへ移した。ゲームが Shadow / Deferred / Hi-Z / PostProcess / Sky を 1 つずつ生成して
		 * 配線していた定型を `PipelinePresets::Standard()` 1 呼び出しへ畳むための引数。
		 * **既定値のまま渡せば従来と同じ絵になる。**
		 *
		 * `Core/AppHost.h` は `using RendererPreset = rendering::RendererPreset;` で互換を保つ
		 * (ゲームは `aq::RendererPreset` と書いている)。
		 *
		 * 解像度はここで持たない。`Engine::GetRenderWidth/Height()` から取るため
		 * (ゲームが Engine に渡した値を、もう一度ゲームが書き写す理由がない)。
		 */
		struct RendererPreset
		{
			/** 影。false でシャドウなし */
			bool enableShadow = true;
			/** 影の設定。既定値は ShadowSettings 側が持つ */
			ShadowSettings shadow = {};
			/** ShadowDepth のシェーダ。既定はエンジン所有 */
			const char* shadowVSPath = "aqEngine/Assets/Shader/ShadowDepth.fx";

			/** ディファード。false でフォワードのみ */
			bool enableDeferred = true;

			/** Hi-Z(オクリュージョン基盤)。enableDeferred が false のときは意味を持たない */
			bool enableHiZ = true;

			/**
			 * IBL(設計書/IBL設計.md)。enableDeferred が false のときは意味を持たない。
			 * 既定 false(true にするとディファードの環境光が変わり、従来の絵と変わる)。
			 * 3 本は Tools/IBLGen/gen_ibl.py の生成物。既定はエンジン所有
			 */
			bool        enableIBL         = false;
			/** IBL の強度(IBLEnvironment::SetIntensity。デバッグパネルで実行時調整できる) */
			float       iblIntensity      = 0.5f;
			const char* iblIrradiancePath = "aqEngine/Assets/Sky/DefaultSkyCube_Irradiance.dds";
			const char* iblSpecularPath   = "aqEngine/Assets/Sky/DefaultSkyCube_Specular.dds";
			const char* iblBrdfLutPath    = "aqEngine/Assets/Sky/BRDF_LUT.dds";

			/**
			 * SSR(設計書/SSR設計.md)。enableDeferred && enableIBL が前提(どちらかが false なら意味を持たない)。
			 * 既定 false(true にすると IBL の鏡面反射が画面上の映り込みに置き換わり、従来の絵と変わる)
			 */
			bool enableSSR = false;

			/**
			 * SSAO(設計書/SSAO設計.md)。enableDeferred が前提(IBL は不要)。Mobile 構成は対象外。
			 * 既定 false(true にすると拡散の環境光が遮蔽で暗くなり、従来の絵と変わる)。
			 * パラメータは SSAOPass の setter(ゲームは Find<SSAOPass>())
			 */
			bool enableSSAO = false;

			/** ポストプロセス(MotionBlur / Bloom / Tonemap)。false で素通し */
			bool     enablePostProcess = true;
			float    bloomThreshold    = 1.0f;
			float    bloomIntensity    = 0.45f;
			uint32_t bloomBlurPasses   = 4;

			/**
			 * 被写界深度(設計書/被写界深度設計.md)。enablePostProcess が false のときは意味を持たない。
			 * 既定 false(既定値なら従来と同じ絵)。パラメータは DoFPass の setter(ゲームは Find<DoFPass>())
			 */
			bool enableDoF = false;

			/** 空。false で背景はクリア色のまま */
			bool        enableSky = true;
			/** キューブマップ。既定はエンジン所有 */
			const char* skyCubemapPath = "aqEngine/Assets/Sky/DefaultSkyCube.dds";

			/** 使うパイプライン構成。既定は Standard(従来どおり)。Auto はプラットフォームで選ぶ */
			PipelineKind pipeline = PipelineKind::Standard;
		};




		/**
		 * プラットフォーム別の標準パイプライン構成(設計書/レンダーパイプライン設計.md §1.4)。
		 */
		namespace PipelinePresets
		{
			/**
			 * 今の Application::SetupStandardRenderers と同じ構成
			 * (Shadow / ClusterCull / GBuffer / Hi-Z / Decal / Lighting / SSAO(preset.enableSSAO)/ SSR(preset.enableSSR)/ Sky / Forward / Ocean /
			 *  Particle / MotionBlur / DoF(preset.enableDoF)/ Bloom / Tonemap / UI)。ClusterCull は GBuffer の前に置く。
			 * 動かない機能(compute 非対応など)は PipelineBuilder::Build() の IsSupported() 検証で
			 * 自動的に除外される。
			 * @param uiCallback UIPass にそのまま渡す UI 描画コールバック
			 */
			PipelineBuilder Standard(const RendererPreset& preset, std::function<void(RenderCommandList&)> uiCallback);

			/**
			 * フォワードのみの軽量構成(GBuffer を持たない)。
			 * Shadow(preset.enableShadow)→ Forward → Sky(preset.enableSky)→ Particle →
			 * Tonemap(preset.enablePostProcess。Bloom / MotionBlur は無し)→ UI。
			 * @param uiCallback UIPass にそのまま渡す UI 描画コールバック
			 */
			PipelineBuilder Mobile(const RendererPreset& preset, std::function<void(RenderCommandList&)> uiCallback);

			/**
			 * プラットフォームに応じて Standard() / Mobile() を選ぶ。
			 * preset.pipeline が Standard / Mobile ならそれを優先し、Auto のときだけ
			 * プラットフォーム(AQ_PLATFORM_ANDROID / AQ_PLATFORM_IOS なら Mobile、他は Standard)で選ぶ。
			 * `#if AQ_PLATFORM_*` を持つのはこの関数だけ(ゲームコードに書かせない、architecture.md §2)。
			 * @param uiCallback UIPass にそのまま渡す UI 描画コールバック
			 */
			PipelineBuilder ForCurrentPlatform(const RendererPreset& preset, std::function<void(RenderCommandList&)> uiCallback);
		}
	}
}
