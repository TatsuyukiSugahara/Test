#include "aq.h"
#include "SoundSource.h"
#include "SoundClip.h"
#include "SoundListener.h"
#include "Mixer3D.h"


namespace aq
{
	namespace sound
	{
		SoundSource::SoundSource(std::unique_ptr<ISoundVoice> voice, RefSoundClip clip, SoundBusId bus)
			: voice_(std::move(voice))
			, clip_(std::move(clip))
			, bus_(bus)
		{
			if (clip_) {
				format_ = clip_->GetFormat();
				isMono_ = (format_.channels == 1u);
				if (!isMono_) {
					EnginePrintf("[Sound] SoundSource: 3D ソースは mono 推奨です（距離減衰のみ適用）。\n");
				}
			}
		}


		SoundSource::~SoundSource()
		{
			if (voice_) {
				voice_->Stop();
			}
		}


		void SoundSource::Play(const LoopRegion& loop)
		{
			if (!voice_ || !clip_) {
				return;
			}
			const uint64_t frameCount = clip_->GetFrameCount();
			const SubmitResult result =
				voice_->SubmitClipRegion(clip_, 0, frameCount, loop, /*endOfStream*/ !loop.IsLooping());
			if (result != SubmitResult::Accepted) {
				return;
			}
			voice_->SetVolume(volume_);
			voice_->Start();
		}


		void SoundSource::Pause()
		{
			if (voice_) {
				voice_->Pause();
			}
		}


		void SoundSource::Resume()
		{
			if (voice_) {
				voice_->Resume();
			}
		}


		void SoundSource::Stop()
		{
			if (voice_) {
				voice_->Stop();
			}
		}


		bool SoundSource::IsPlaying() const
		{
			return voice_ && !voice_->IsFinished();
		}


		void SoundSource::FadeIn(float seconds, float targetVolume)
		{
			volume_ = 0.0f;
			volumeEnv_.current = 0.0f;
			volumeEnv_.FadeTo(targetVolume, seconds, false);
		}


		void SoundSource::FadeOut(float seconds)
		{
			volumeEnv_.FadeTo(0.0f, seconds, /*stopWhenDone*/ true);
		}


		void SoundSource::FadeTo(float targetVolume, float seconds)
		{
			volumeEnv_.FadeTo(targetVolume, seconds, false);
		}


		void SoundSource::UpdateFade(float deltaTime)
		{
			if (!volumeEnv_.active) {
				return;
			}
			const bool done = volumeEnv_.Update(deltaTime);
			volume_ = volumeEnv_.current;   // UpdateSpatialization がこれをボイスへ反映する
			if (done && volumeEnv_.stopAtEnd) {
				Stop();
			}
		}


		void SoundSource::UpdateSpatialization(const SoundListener& listener)
		{
			if (!voice_) {
				return;
			}

			SpatializationInput input;
			input.listenerPos     = listener.GetPosition();
			input.listenerForward = listener.GetForward();
			input.listenerUp      = listener.GetUp();
			input.listenerVel     = listener.GetVelocity();
			input.sourcePos       = position_;
			input.sourceVel       = velocity_;
			input.model           = attenuation_;
			input.minDistance     = minDistance_;
			input.maxDistance     = maxDistance_;
			input.dopplerFactor   = dopplerFactor_;

			const SpatializationResult result = Mixer3D::Compute(input);

			voice_->SetFrequencyRatio(pitch_ * result.frequencyRatio);

			if (isMono_) {
				// mono 入力 → ステレオ出力の 1×2 行列（距離減衰込み）。
				const float matrix[2] = { result.leftGain, result.rightGain };
				voice_->SetOutputMatrix(1u, 2u, matrix);
				voice_->SetVolume(volume_);
			}
			else {
				// 非 mono は距離減衰を全体音量へ畳み込む（パンは行わない）。
				voice_->SetVolume(volume_ * result.distanceGain);
			}
		}
	}
}
