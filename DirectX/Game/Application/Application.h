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

	protected:
		aq::rendering::RenderTargetHandle offscreenRTHandle_;

	private:
		static constexpr float kOffscreenRTWidth  = 512.0f;
		static constexpr float kOffscreenRTHeight = 512.0f;

	// ── BGM（起動時から常時ループ再生）──
	private:
		std::unique_ptr<aq::sound::SoundStream> bgmStream_;
#ifdef AQ_DEBUG_IMGUI
		std::unique_ptr<aq::audio::AudioAuthoringPanel> audioPanel_;
#endif

	protected:
		bool OnInitialize() override;
		void OnFinalize() override;
		void OnUpdate() override;
		void OnRegister() override;
		void OnPreRender() override;
	};
}
