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
		 * 標準的な描画構成の設定値(設計書/使いやすさ改善設計.md P2-B)。
		 *
		 * 元は Core/Application.h の RendererPreset。`Rendering/` から `Core/` への依存を避けるため
		 * こちらへ移した。ゲームが Shadow / Deferred / Hi-Z / PostProcess / Sky を 1 つずつ生成して
		 * 配線していた定型を `PipelinePresets::Standard()` 1 呼び出しへ畳むための引数。
		 * **既定値のまま渡せば従来と同じ絵になる。**
		 *
		 * `Core/Application.h` は `using RendererPreset = rendering::RendererPreset;` で互換を保つ
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

			/** ポストプロセス(MotionBlur / Bloom / Tonemap)。false で素通し */
			bool     enablePostProcess = true;
			float    bloomThreshold    = 1.0f;
			float    bloomIntensity    = 0.45f;
			uint32_t bloomBlurPasses   = 4;

			/** 空。false で背景はクリア色のまま */
			bool        enableSky = true;
			/** キューブマップ。既定はエンジン所有 */
			const char* skyCubemapPath = "aqEngine/Assets/Sky/DefaultSkyCube.dds";
		};




		/**
		 * プラットフォーム別の標準パイプライン構成(設計書/レンダーパイプライン設計.md §1.4)。
		 * P1 では Standard() のみを提供する。Mobile() / ForCurrentPlatform() は P2 で追加する。
		 */
		namespace PipelinePresets
		{
			/**
			 * 今の Application::SetupStandardRenderers と同じ構成
			 * (Shadow / ClusterCull / GBuffer / Hi-Z / Decal / Lighting / Sky / Forward / Ocean /
			 *  Particle / PostProcess / UI)。ClusterCull は GBuffer の前に置く。
			 * 動かない機能(compute 非対応など)は PipelineBuilder::Build() の IsSupported() 検証で
			 * 自動的に除外される。
			 * @param uiCallback UIPass にそのまま渡す UI 描画コールバック
			 */
			PipelineBuilder Standard(const RendererPreset& preset, std::function<void(RenderCommandList&)> uiCallback);
		}
	}
}
