#include "aq.h"
#include "Sound/Mixer/MixerSoundVoice.h"


namespace aq
{
	namespace sound
	{
		MixerSoundVoice::MixerSoundVoice(SoftwareMixer* mixer, SoftwareMixer::VoiceId voiceId)
			: mixer_(mixer)
			, voiceId_(voiceId)
		{
		}


		MixerSoundVoice::~MixerSoundVoice()
		{
			if (mixer_ != nullptr && voiceId_ != SoftwareMixer::INVALID_VOICE_ID)
			{
				mixer_->ReleaseVoice(voiceId_);
				voiceId_ = SoftwareMixer::INVALID_VOICE_ID;
			}
		}


		bool MixerSoundVoice::Initialize(const SoundFormat& /*format*/)
		{
			// スロットの確保とフォーマット設定は CreateVoice の時点で
			// SoftwareMixer::AcquireVoice が済ませている。ここでは成否だけ返す。
			return mixer_ != nullptr && voiceId_ != SoftwareMixer::INVALID_VOICE_ID;
		}


		SubmitResult MixerSoundVoice::SubmitBuffer(const void* data, uint32_t byteSize, bool endOfStream)
		{
			if (mixer_ == nullptr) { return SubmitResult::Closed; }
			return mixer_->SubmitBuffer(voiceId_, data, byteSize, endOfStream);
		}


		SubmitResult MixerSoundVoice::SubmitClipRegion(RefSoundClip clip, uint64_t startFrame, uint64_t frameCount,
		                                               const LoopRegion& loop, bool endOfStream)
		{
			if (mixer_ == nullptr) { return SubmitResult::Closed; }
			return mixer_->SubmitClipRegion(voiceId_, std::move(clip), startFrame, frameCount, loop, endOfStream);
		}


		void MixerSoundVoice::Start()
		{
			if (mixer_ != nullptr) { mixer_->Start(voiceId_); }
		}


		void MixerSoundVoice::Pause()
		{
			if (mixer_ != nullptr) { mixer_->Pause(voiceId_); }
		}


		void MixerSoundVoice::Resume()
		{
			if (mixer_ != nullptr) { mixer_->Resume(voiceId_); }
		}


		void MixerSoundVoice::Stop()
		{
			if (mixer_ != nullptr) { mixer_->Stop(voiceId_); }
		}


		void MixerSoundVoice::SetVolume(float volume)
		{
			if (mixer_ != nullptr) { mixer_->SetVoiceVolume(voiceId_, volume); }
		}


		void MixerSoundVoice::SetFrequencyRatio(float ratio)
		{
			if (mixer_ != nullptr) { mixer_->SetFrequencyRatio(voiceId_, ratio); }
		}


		void MixerSoundVoice::SetOutputMatrix(uint32_t srcChannels, uint32_t dstChannels, const float* matrix)
		{
			if (mixer_ != nullptr) { mixer_->SetOutputMatrix(voiceId_, srcChannels, dstChannels, matrix); }
		}


		uint64_t MixerSoundVoice::GetConsumedFrames() const
		{
			return mixer_ != nullptr ? mixer_->GetConsumedFrames(voiceId_) : 0u;
		}


		uint32_t MixerSoundVoice::GetQueuedBufferCount() const
		{
			return mixer_ != nullptr ? mixer_->GetQueuedBufferCount(voiceId_) : 0u;
		}


		bool MixerSoundVoice::IsFinished() const
		{
			return mixer_ != nullptr ? mixer_->IsFinished(voiceId_) : true;
		}
	}
}
