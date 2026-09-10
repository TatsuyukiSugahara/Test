#include "aq.h"
// macOS 以外では空 TU。
#if defined(AQ_PLATFORM_MAC)
#include "Sound/CoreAudio/CoreAudioSoundVoice.h"


namespace aq
{
	namespace sound
	{
		CoreAudioSoundVoice::CoreAudioSoundVoice(SoftwareMixer* mixer, SoftwareMixer::VoiceId voiceId)
			: mixer_(mixer)
			, voiceId_(voiceId)
		{
		}


		CoreAudioSoundVoice::~CoreAudioSoundVoice()
		{
			if (mixer_ != nullptr && voiceId_ != SoftwareMixer::INVALID_VOICE_ID)
			{
				mixer_->ReleaseVoice(voiceId_);
				voiceId_ = SoftwareMixer::INVALID_VOICE_ID;
			}
		}


		bool CoreAudioSoundVoice::Initialize(const SoundFormat& /*format*/)
		{
			// スロットの確保とフォーマット設定は CreateVoice の時点で
			// SoftwareMixer::AcquireVoice が済ませている。ここでは成否だけ返す。
			return mixer_ != nullptr && voiceId_ != SoftwareMixer::INVALID_VOICE_ID;
		}


		SubmitResult CoreAudioSoundVoice::SubmitBuffer(const void* data, uint32_t byteSize, bool endOfStream)
		{
			if (mixer_ == nullptr) { return SubmitResult::Closed; }
			return mixer_->SubmitBuffer(voiceId_, data, byteSize, endOfStream);
		}


		SubmitResult CoreAudioSoundVoice::SubmitClipRegion(RefSoundClip clip, uint64_t startFrame, uint64_t frameCount,
		                                                   const LoopRegion& loop, bool endOfStream)
		{
			if (mixer_ == nullptr) { return SubmitResult::Closed; }
			return mixer_->SubmitClipRegion(voiceId_, std::move(clip), startFrame, frameCount, loop, endOfStream);
		}


		void CoreAudioSoundVoice::Start()
		{
			if (mixer_ != nullptr) { mixer_->Start(voiceId_); }
		}


		void CoreAudioSoundVoice::Pause()
		{
			if (mixer_ != nullptr) { mixer_->Pause(voiceId_); }
		}


		void CoreAudioSoundVoice::Resume()
		{
			if (mixer_ != nullptr) { mixer_->Resume(voiceId_); }
		}


		void CoreAudioSoundVoice::Stop()
		{
			if (mixer_ != nullptr) { mixer_->Stop(voiceId_); }
		}


		void CoreAudioSoundVoice::SetVolume(float volume)
		{
			if (mixer_ != nullptr) { mixer_->SetVoiceVolume(voiceId_, volume); }
		}


		void CoreAudioSoundVoice::SetFrequencyRatio(float ratio)
		{
			if (mixer_ != nullptr) { mixer_->SetFrequencyRatio(voiceId_, ratio); }
		}


		void CoreAudioSoundVoice::SetOutputMatrix(uint32_t srcChannels, uint32_t dstChannels, const float* matrix)
		{
			if (mixer_ != nullptr) { mixer_->SetOutputMatrix(voiceId_, srcChannels, dstChannels, matrix); }
		}


		uint64_t CoreAudioSoundVoice::GetConsumedFrames() const
		{
			return mixer_ != nullptr ? mixer_->GetConsumedFrames(voiceId_) : 0u;
		}


		uint32_t CoreAudioSoundVoice::GetQueuedBufferCount() const
		{
			return mixer_ != nullptr ? mixer_->GetQueuedBufferCount(voiceId_) : 0u;
		}


		bool CoreAudioSoundVoice::IsFinished() const
		{
			return mixer_ != nullptr ? mixer_->IsFinished(voiceId_) : true;
		}
	}
}
#endif // AQ_PLATFORM_MAC
