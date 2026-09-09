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

	// ── ミニマップ用オフスクリーンパス（俯瞰スナップショット）──
	protected:
		aq::rendering::OffscreenScenePass offscreenPass_;

	private:
		/** 次の描画で俯瞰ベイクを行うか（ステージ確定時に立てて 1 回だけ描く） */
		bool minimapBakeRequested_ = false;

		static constexpr uint32_t OFFSCREEN_RT_WIDTH  = 512;
		static constexpr uint32_t OFFSCREEN_RT_HEIGHT = 512;

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
		inline aq::rendering::RenderTargetHandle GetMinimapRT() const { return offscreenPass_.GetSceneRT(); }

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
