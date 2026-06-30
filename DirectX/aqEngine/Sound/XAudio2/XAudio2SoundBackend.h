#pragma once
#include <xaudio2.h>
#include "Sound/ISoundBackend.h"


namespace aq
{
	namespace sound
	{
		// XAudio2 デバイス / マスタリング / バス submix を所有する Windows バックエンド（§7）。
		class XAudio2SoundBackend : public ISoundBackend
		{
		// ── メンバ変数 ──
		private:
			IXAudio2*                xaudio2_        = nullptr;
			IXAudio2MasteringVoice*  masteringVoice_ = nullptr;
			IXAudio2SubmixVoice*     busSubmix_[static_cast<size_t>(SoundBusId::Count)] = {};

			uint32_t outputChannels_   = 0;
			uint32_t outputSampleRate_ = 0;
			bool     comInitialized_   = false;

		// ── メンバ関数 ──
		public:
			XAudio2SoundBackend() = default;
			~XAudio2SoundBackend() override;

			bool Initialize() override;
			void Finalize()   override;

			std::unique_ptr<ISoundVoice> CreateVoice(const SoundFormat& format, SoundBusId bus) override;

			void SetBusVolume(SoundBusId bus, float volume) override;
			void SetMasterVolume(float volume) override;

			SoundClock GetOutputClock() const override;

		private:
			// バスの送り先 IXAudio2Voice を返す。Master は mastering、その他は submix。
			IXAudio2Voice* GetDestForBus(SoundBusId bus) const;
		};
	}
}
