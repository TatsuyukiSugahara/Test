#pragma once
#include "aqEngine/Core/Application.h"
#include "Sound/SoundFwd.h"
#include <memory>

namespace aq { namespace audio { class AudioAuthoringPanel; } }

namespace app
{
	class Application : public aq::Application
	{
	public:
		// unique_ptr<SoundStream>（不完全型）のため、ctor/dtor は .cpp 側で定義する。
		Application();
		~Application();

	// ── ミニマップ用オフスクリーンパイプライン（俯瞰スナップショット）──
	//    メインとは別の 2 本目の RenderPipeline（GBuffer → ライティング → フォワードの 3 パス）。
	//    掲示板もパスもこちらが単独で持つので、メインの列とは混ざらない
	//    （設計書/レンダーパイプライン設計.md P4）。
	protected:
		std::unique_ptr<aq::rendering::RenderPipeline> minimapPipeline_;
		aq::rendering::RenderTargetHandle              minimapRT_;

	private:
		/** 次の描画で俯瞰ベイクを行うか（ステージ確定時に立てて 1 回だけ描く） */
		bool minimapBakeRequested_ = false;

		static constexpr uint32_t OFFSCREEN_RT_WIDTH  = 512;
		static constexpr uint32_t OFFSCREEN_RT_HEIGHT = 512;

		/** 俯瞰ベイクの背景色（ライティングは背景ピクセルを clip するのでこの色がそのまま残る） */
		static constexpr float OFFSCREEN_CLEAR_COLOR[4] = { 0.02f, 0.08f, 0.16f, 1.0f };

	// ── 輪郭線（エンジンの任意パス OutlinePass に渡す値）──
	private:
		/** 線の色。海と空に馴染む濃紺 */
		static const aq::math::Vector3 OUTLINE_COLOR;
		static constexpr float   OUTLINE_INTENSITY = 0.55f;  // 線の濃さ
		static constexpr float   OUTLINE_THRESHOLD = 0.03f;  // エッジとみなす相対深度差
		static constexpr int32_t OUTLINE_THICKNESS = 1;      // 線の太さ (px)

	// ── BGM（起動時から常時ループ再生）──
	private:
		std::unique_ptr<aq::sound::SoundStream> bgmStream_;
#ifdef AQ_DEBUG_IMGUI
		std::unique_ptr<aq::audio::AudioAuthoringPanel> audioPanel_;
#endif

	public:
		/** ミニマップの俯瞰ベイクを次の描画で 1 回だけ要求する（構図はオフスクリーンカメラ側で設定） */
		inline void RequestMinimapBake() { minimapBakeRequested_ = true; }
		/** 俯瞰ベイク先の RT。UI へ SRV を渡すのに使う */
		inline aq::rendering::RenderTargetHandle GetMinimapRT() const { return minimapRT_; }

	protected:
		bool OnInitialize() override;
		void OnFinalize() override;
		void OnUpdate() override;
		void OnRegister() override;
		void OnPreRender() override;


	private:
		static Application* instance_;

	public:
		static Application& Get()   { return *instance_; }
		static bool IsAvailable()   { return instance_ != nullptr; }
	};
}
