#pragma once
#include "aqEngine/Core/Application.h"
#include "Sound/SoundFwd.h"
#include "Sound/SoundHandle.h"
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

	// ── サウンドテスト（P:SE / B:BGM / 3:3D 周回）──
	private:
		aq::sound::RefSoundClip                 testClip_;
		std::unique_ptr<aq::sound::SoundStream>  bgmStream_;
		aq::sound::SoundSourceHandle             orbitSource_;
		bool  bgmPlaying_     = false;
		bool  eventBgmOn_     = false;
		bool  orbitActive_    = false;
		bool  soundPaused_    = false;
		bool  event3DOn_      = false;
		bool  engineOn_       = false;
		float orbitAngle_     = 0.0f;
		float event3DAngle_   = 0.0f;
#ifdef AQ_DEBUG_IMGUI
		std::unique_ptr<aq::audio::AudioAuthoringPanel> audioPanel_;
#endif

		void UpdateSoundTest();

	protected:
		bool OnInitialize() override;
		void OnFinalize() override;
		void OnUpdate() override;
		void OnRegister() override;
		void OnPreRender() override;
	};
}
