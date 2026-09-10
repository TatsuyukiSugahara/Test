#include "aq.h"
#include "NullSoundBackend.h"
#include "SoundClip.h"


namespace aq
{
	namespace sound
	{
		namespace
		{
			/** 仮想デバイスの出力フォーマット(CoreAudio の既定に合わせた想定値) */
			static constexpr uint32_t OUTPUT_SAMPLE_RATE     = 48000u;
			static constexpr double   OUTPUT_LATENCY_SECONDS = 0.02;
		}


		/**
		 * 無音ボイス
		 */
		NullSoundVoice::NullSoundVoice()
			: format_()
			, clip_()
			, frequencyRatio_(1.0f)
			, queue_()
			, state_(PlayState::Stopped)
			, consumedFrames_(0u)
			, fractionFrames_(0.0)
			, lastTick_(Clock::now())
			, finished_(false)
			, submittedEndOfStream_(false)
			, loopFrameCount_(0u)
		{
		}


		bool NullSoundVoice::Initialize(const SoundFormat& format)
		{
			if (!format.IsValid()) {
				return false;
			}
			format_   = format;
			lastTick_ = Clock::now();
			return true;
		}


		SubmitResult NullSoundVoice::SubmitBuffer(const void* data, uint32_t byteSize, bool endOfStream)
		{
			if (!format_.IsValid()) {
				return SubmitResult::Closed;
			}
			if (submittedEndOfStream_) {
				return SubmitResult::Closed;
			}
			Advance();

			const uint32_t bpf = format_.BytesPerFrame();
			if (bpf == 0u) {
				return SubmitResult::InvalidFormat;
			}

			// 長さ 0 の投入は「終端通知だけ」なのでキューを消費しない(SoundStream::Pump)。
			const uint64_t frames = static_cast<uint64_t>(byteSize) / bpf;
			if (frames > 0u) {
				if (queue_.size() >= MAX_QUEUED_BUFFER_COUNT) {
					return SubmitResult::WouldBlock;
				}
				queue_.push_back(frames);
			}
			if (endOfStream) {
				submittedEndOfStream_ = true;
			}

			// data は無音なので参照しない(呼び出し側は即解放してよい。§3.3a)。
			static_cast<void>(data);
			return SubmitResult::Accepted;
		}


		SubmitResult NullSoundVoice::SubmitClipRegion(RefSoundClip clip, uint64_t startFrame, uint64_t frameCount,
		                                              const LoopRegion& loop, bool endOfStream)
		{
			if (!format_.IsValid()) {
				return SubmitResult::Closed;
			}
			if (submittedEndOfStream_) {
				return SubmitResult::Closed;
			}
			if (!clip || clip->GetFormat() != format_) {
				return SubmitResult::InvalidFormat;
			}
			Advance();

			// clip をボイスが保持して生存保証(§3.3a)
			clip_ = std::move(clip);

			// ループは submit 時に確定する(§3.3c)。有限回はフレーム数に畳み込み、
			// 無限ループは loopFrameCount_ に覚えて「尽きない」扱いにする。
			uint64_t totalFrames = frameCount;
			if (loop.IsLooping()) {
				if (loop.loopCount == 0u) {
					loopFrameCount_ = loop.frameCount;
				} else {
					totalFrames += loop.frameCount * static_cast<uint64_t>(loop.loopCount);
				}
			}
			if (totalFrames > 0u) {
				queue_.push_back(totalFrames);
			}
			if (endOfStream && !loop.IsLooping()) {
				submittedEndOfStream_ = true;
			}

			static_cast<void>(startFrame);
			return SubmitResult::Accepted;
		}


		void NullSoundVoice::Start()
		{
			finished_ = false;
			state_    = PlayState::Playing;
			lastTick_ = Clock::now();
		}


		void NullSoundVoice::Pause()
		{
			// 位置・キューを保持して停止(§3.3b)
			Advance();
			state_ = PlayState::Paused;
		}


		void NullSoundVoice::Resume()
		{
			state_    = PlayState::Playing;
			lastTick_ = Clock::now();
		}


		void NullSoundVoice::Stop()
		{
			// 停止 + キュー破棄 + 巻き戻し(§3.3b)
			queue_.clear();
			state_                = PlayState::Stopped;
			consumedFrames_       = 0u;
			fractionFrames_       = 0.0;
			lastTick_             = Clock::now();
			finished_             = false;
			submittedEndOfStream_ = false;
			loopFrameCount_       = 0u;
			clip_.reset();
		}


		void NullSoundVoice::SetVolume(float volume)
		{
			// 無音なので反映先が無い。
			static_cast<void>(volume);
		}


		void NullSoundVoice::SetFrequencyRatio(float ratio)
		{
			// 消費速度に効くので、ここまでの消費ぶんを確定してから差し替える。
			Advance();
			frequencyRatio_ = (ratio > 0.0f) ? ratio : 0.0f;
		}


		void NullSoundVoice::SetOutputMatrix(uint32_t srcChannels, uint32_t dstChannels, const float* matrix)
		{
			// 無音なのでパン結果を保持する意味が無い。
			static_cast<void>(srcChannels);
			static_cast<void>(dstChannels);
			static_cast<void>(matrix);
		}


		uint64_t NullSoundVoice::GetConsumedFrames() const
		{
			Advance();
			return consumedFrames_;
		}


		uint32_t NullSoundVoice::GetQueuedBufferCount() const
		{
			Advance();
			return static_cast<uint32_t>(queue_.size());
		}


		bool NullSoundVoice::IsFinished() const
		{
			Advance();
			return finished_;
		}


		void NullSoundVoice::Advance() const
		{
			const Clock::time_point now = Clock::now();
			if (state_ != PlayState::Playing) {
				lastTick_ = now;
				return;
			}
			const double elapsed = std::chrono::duration<double>(now - lastTick_).count();
			lastTick_ = now;

			if (format_.IsValid()) {
				// 入力フレームの消費速度 = サンプルレート × ピッチ比。
				fractionFrames_ += elapsed * static_cast<double>(format_.sampleRate)
				                           * static_cast<double>(frequencyRatio_);

				uint64_t remain = static_cast<uint64_t>(fractionFrames_);
				fractionFrames_ -= static_cast<double>(remain);

				while (remain > 0u) {
					if (queue_.empty()) {
						// 無限ループ中はキューが尽きても回り続ける(§3.3c)。
						if (loopFrameCount_ > 0u) {
							consumedFrames_ += remain;
						}
						break;   // ループ中 / 終端 / 供給待ち(アンダーラン)
					}
					uint64_t&      head = queue_.front();
					const uint64_t step = (head < remain) ? head : remain;
					head            -= step;
					remain          -= step;
					consumedFrames_ += step;
					if (head == 0u) {
						queue_.pop_front();
					}
				}
			}

			// 終端投入済みでキューが尽きたら自然終了(§3.3b)。
			if (submittedEndOfStream_ && loopFrameCount_ == 0u && queue_.empty()) {
				finished_ = true;
				state_    = PlayState::Stopped;
			}
		}


		/************************************/




		/**
		 * 無音バックエンド
		 */
		NullSoundBackend::NullSoundBackend()
			: startTime_(Clock::now())
			, initialized_(false)
			, masterVolume_(1.0f)
			, busVolume_{}
		{
			for (size_t i = 0; i < static_cast<size_t>(SoundBusId::Count); ++i) {
				busVolume_[i] = 1.0f;
			}
		}


		NullSoundBackend::~NullSoundBackend()
		{
			Finalize();
		}


		bool NullSoundBackend::Initialize()
		{
			startTime_   = Clock::now();
			initialized_ = true;
			EnginePrintf("[Sound] NullSoundBackend で初期化しました(無音。Mac移植設計 §9 P2)。\n");
			return true;
		}


		void NullSoundBackend::Finalize()
		{
			initialized_ = false;
		}


		std::unique_ptr<ISoundVoice> NullSoundBackend::CreateVoice(const SoundFormat& format, SoundBusId bus)
		{
			if (!initialized_) {
				return nullptr;
			}
			auto voice = std::make_unique<NullSoundVoice>();
			if (!voice->Initialize(format)) {
				return nullptr;
			}
			static_cast<void>(bus);
			return voice;
		}


		void NullSoundBackend::SetBusVolume(SoundBusId bus, float volume)
		{
			const size_t index = static_cast<size_t>(bus);
			if (index >= static_cast<size_t>(SoundBusId::Count)) {
				return;
			}
			busVolume_[index] = volume;
		}


		void NullSoundBackend::SetMasterVolume(float volume)
		{
			masterVolume_ = volume;
		}


		SoundClock NullSoundBackend::GetOutputClock() const
		{
			SoundClock clock;
			clock.sampleRate     = OUTPUT_SAMPLE_RATE;
			clock.latencySeconds = OUTPUT_LATENCY_SECONDS;
			clock.underrunCount  = 0u;
			if (initialized_) {
				const double elapsed = std::chrono::duration<double>(Clock::now() - startTime_).count();
				clock.outputFrames = static_cast<uint64_t>(elapsed * static_cast<double>(OUTPUT_SAMPLE_RATE));
			}
			return clock;
		}
	}
}
