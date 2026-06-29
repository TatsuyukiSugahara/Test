#include "aq.h"
#include "SoundStream.h"
#include "SoundEngine.h"


namespace aq
{
	namespace sound
	{
		SoundStream::SoundStream(SoundEngine& engine,
		                         std::unique_ptr<ISoundVoice> voice,
		                         std::unique_ptr<ISoundDecoder> decoder,
		                         SoundBusId bus)
			: engine_(engine)
			, voice_(std::move(voice))
			, decoder_(std::move(decoder))
			, bus_(bus)
		{
			if (decoder_) {
				format_ = decoder_->GetFormat();
				scratch_.resize(static_cast<size_t>(CHUNK_FRAMES) * format_.BytesPerFrame());
			}
			engine_.RegisterStream(this);
		}


		SoundStream::~SoundStream()
		{
			engine_.UnregisterStream(this);
			if (voice_) {
				voice_->Stop();
			}
		}


		void SoundStream::Play(const LoopRegion& loop)
		{
			if (!voice_ || !decoder_) {
				return;
			}
			loop_         = loop;
			endSubmitted_ = false;
			decoder_->Seek(0);

			Pump();            // 開始前にバッファをプライムしてアンダーランを避ける
			playing_ = true;
			voice_->Start();
		}


		void SoundStream::Pause()
		{
			if (voice_) {
				voice_->Pause();
			}
			playing_ = false;
		}


		void SoundStream::Resume()
		{
			if (voice_) {
				voice_->Resume();
			}
			playing_ = true;
		}


		void SoundStream::Stop()
		{
			if (voice_) {
				voice_->Stop();
			}
			if (decoder_) {
				decoder_->Seek(0);
			}
			playing_      = false;
			endSubmitted_ = false;
		}


		void SoundStream::SetVolume(float volume)
		{
			volumeEnv_.SetImmediate(volume);
			if (voice_) {
				voice_->SetVolume(volume);
			}
		}


		void SoundStream::SetPitch(float ratio)
		{
			if (voice_) {
				voice_->SetFrequencyRatio(ratio);
			}
		}


		void SoundStream::FadeIn(float seconds, float targetVolume)
		{
			volumeEnv_.current = 0.0f;
			if (voice_) {
				voice_->SetVolume(0.0f);
			}
			volumeEnv_.FadeTo(targetVolume, seconds, false);
		}


		void SoundStream::FadeOut(float seconds)
		{
			volumeEnv_.FadeTo(0.0f, seconds, /*stopWhenDone*/ true);
		}


		void SoundStream::FadeTo(float targetVolume, float seconds)
		{
			volumeEnv_.FadeTo(targetVolume, seconds, false);
		}


		void SoundStream::UpdateFade(float deltaTime)
		{
			if (!volumeEnv_.active) {
				return;
			}
			const bool done = volumeEnv_.Update(deltaTime);
			if (voice_) {
				voice_->SetVolume(volumeEnv_.current);
			}
			if (done && volumeEnv_.stopAtEnd) {
				Stop();
			}
		}


		MediaClock SoundStream::GetPlaybackClock() const
		{
			MediaClock clock;
			if (!voice_ || format_.sampleRate == 0u) {
				return clock;
			}

			const uint64_t consumed = voice_->GetConsumedFrames();
			const double   latency  = engine_.GetOutputClock().latencySeconds;
			double seconds = static_cast<double>(consumed) / format_.sampleRate - latency;
			if (seconds < 0.0) {
				seconds = 0.0;
			}
			clock.presentedMediaSeconds = seconds;
			clock.valid                 = playing_;
			return clock;
		}


		void SoundStream::Pump()
		{
			if (!playing_ || endSubmitted_ || !voice_ || !decoder_) {
				return;
			}
			const uint32_t bpf = format_.BytesPerFrame();
			if (bpf == 0u) {
				return;
			}

			// 投入バッファが目標数を下回る間だけ供給する（背圧制御）。
			while (voice_->GetQueuedBufferCount() < TARGET_QUEUED)
			{
				const uint32_t frames = decoder_->ReadFrames(scratch_.data(), CHUNK_FRAMES);
				if (frames == 0u)
				{
					// 終端。ループなら戻り、そうでなければ末尾を通知する。
					if (loop_.IsLooping()) {
						decoder_->Seek(loop_.startFrame);
						continue;
					}
					voice_->SubmitBuffer(nullptr, 0, /*endOfStream*/ true);
					endSubmitted_ = true;
					break;
				}

				const SubmitResult result = voice_->SubmitBuffer(scratch_.data(), frames * bpf, false);
				if (result != SubmitResult::Accepted) {
					break;   // WouldBlock などは次フレームで再供給
				}
			}
		}
	}
}
