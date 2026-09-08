#include "aq.h"
#include "XAudio2SoundBackend.h"
#include "XAudio2SoundVoice.h"
#include <objbase.h>

#pragma comment(lib, "xaudio2.lib")


namespace aq
{
	namespace sound
	{
		XAudio2SoundBackend::~XAudio2SoundBackend()
		{
			Finalize();
		}


		bool XAudio2SoundBackend::Initialize()
		{
			// XAudio2 は COM を要求する。本関数は Engine のサウンド初期化スレッドから呼ばれるため、
			// COM の初期化/解放はこのスレッド内で対にする(スコープ脱出時に解放)。プロセス寿命の MTA は
			// Engine::Initialize がメインスレッドで保持しており、XAudio2 オブジェクトはそこに属し続ける。
			struct ComScope
			{
				bool ok;
				ComScope()  : ok(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
				~ComScope() { if (ok) { CoUninitialize(); } }
			} comScope;

			HRESULT hr = XAudio2Create(&xaudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
			if (FAILED(hr)) {
				return false;
			}

			hr = xaudio2_->CreateMasteringVoice(&masteringVoice_);
			if (FAILED(hr) || masteringVoice_ == nullptr) {
				return false;
			}

			XAUDIO2_VOICE_DETAILS details = {};
			masteringVoice_->GetVoiceDetails(&details);
			outputChannels_   = details.InputChannels;
			outputSampleRate_ = details.InputSampleRate;

			// バス submix を生成（Master 以外）。すべて mastering へ送る。
			for (size_t i = 0; i < static_cast<size_t>(SoundBusId::Count); ++i) {
				if (i == static_cast<size_t>(SoundBusId::Master)) {
					continue;
				}
				HRESULT hrBus = xaudio2_->CreateSubmixVoice(
					&busSubmix_[i], outputChannels_, outputSampleRate_, 0, 0, nullptr, nullptr);
				if (FAILED(hrBus)) {
					return false;
				}
			}

			return true;
		}


		void XAudio2SoundBackend::Finalize()
		{
			for (size_t i = 0; i < static_cast<size_t>(SoundBusId::Count); ++i) {
				if (busSubmix_[i]) {
					busSubmix_[i]->DestroyVoice();
					busSubmix_[i] = nullptr;
				}
			}
			if (masteringVoice_) {
				masteringVoice_->DestroyVoice();
				masteringVoice_ = nullptr;
			}
			if (xaudio2_) {
				xaudio2_->Release();
				xaudio2_ = nullptr;
			}
		}


		std::unique_ptr<ISoundVoice> XAudio2SoundBackend::CreateVoice(const SoundFormat& format, SoundBusId bus)
		{
			if (xaudio2_ == nullptr) {
				return nullptr;
			}

			auto voice = std::make_unique<XAudio2SoundVoice>(xaudio2_, GetDestForBus(bus));
			if (!voice->Initialize(format)) {
				return nullptr;
			}
			return voice;
		}


		void XAudio2SoundBackend::SetBusVolume(SoundBusId bus, float volume)
		{
			if (bus == SoundBusId::Master) {
				SetMasterVolume(volume);
				return;
			}
			IXAudio2SubmixVoice* submix = busSubmix_[static_cast<size_t>(bus)];
			if (submix) {
				submix->SetVolume(volume);
			}
		}


		void XAudio2SoundBackend::SetMasterVolume(float volume)
		{
			if (masteringVoice_) {
				masteringVoice_->SetVolume(volume);
			}
		}


		SoundClock XAudio2SoundBackend::GetOutputClock() const
		{
			// §3.4: mastering voice の SamplesPlayed は device clock として使わない。
			// latency / underrun を best-effort で埋め、権威ある A/V 時刻は stream 側（§3.5）。
			SoundClock clock;
			clock.sampleRate = outputSampleRate_;

			if (xaudio2_) {
				XAUDIO2_PERFORMANCE_DATA perf = {};
				xaudio2_->GetPerformanceData(&perf);
				clock.underrunCount = perf.GlitchesSinceEngineStarted;
				if (outputSampleRate_ != 0u) {
					clock.latencySeconds =
						static_cast<double>(perf.CurrentLatencyInSamples) / outputSampleRate_;
				}
			}
			return clock;
		}


		IXAudio2Voice* XAudio2SoundBackend::GetDestForBus(SoundBusId bus) const
		{
			if (bus == SoundBusId::Master) {
				return masteringVoice_;
			}
			return busSubmix_[static_cast<size_t>(bus)];
		}
	}
}
