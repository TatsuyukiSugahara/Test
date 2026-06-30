#include "aq.h"
#include "VideoPlayer.h"
#include "Sound/Decoder/MFDecoder.h"
#include "Sound/SoundEngine.h"
#include "Sound/SoundStream.h"


namespace aq
{
	namespace video
	{
		namespace
		{
			constexpr uint32_t CHUNK_FRAMES = 4096u;
			constexpr int      MAX_PUSH_PER_FRAME = 8;   // 1 フレームあたりの供給回数上限
		}


		VideoPlayer::VideoPlayer() = default;


		VideoPlayer::~VideoPlayer()
		{
			Stop();
		}


		bool VideoPlayer::Open(const char* path, sound::SoundBusId bus)
		{
			Stop();
			if (!sound::SoundEngine::IsAvailable()) {
				return false;
			}

			audio_ = std::make_unique<sound::MFDecoder>();
			if (!audio_->Open(path)) {
				audio_.reset();
				EnginePrintf("[Video] 開けませんでした: %s\n", path ? path : "(null)");
				return false;
			}
			format_ = audio_->GetFormat();

			stream_ = sound::SoundEngine::Get().OpenPushStream(format_, bus);
			if (!stream_) {
				audio_.reset();
				return false;
			}

			scratch_.resize(static_cast<size_t>(CHUNK_FRAMES) * format_.BytesPerFrame());
			pending_.clear();
			stream_->Play();
			playing_ = true;
			return true;
		}


		void VideoPlayer::Update(float /*deltaTime*/)
		{
			if (!playing_ || !stream_ || !audio_) {
				return;
			}
			const uint32_t bpf = format_.BytesPerFrame();
			if (bpf == 0u) {
				return;
			}

			// 背圧で押し戻された分を先に流す。
			if (!pending_.empty()) {
				if (stream_->PushPCM(pending_.data(), static_cast<uint32_t>(pending_.size()))) {
					pending_.clear();
				}
				else {
					return;   // まだ詰まっている
				}
			}

			// デコードして供給する。満杯になったら残りを pending_ に退避。
			for (int i = 0; i < MAX_PUSH_PER_FRAME; ++i) {
				const uint32_t frames = audio_->ReadFrames(scratch_.data(), CHUNK_FRAMES);
				if (frames == 0u) {
					playing_ = false;   // 終端（A/V 同期するなら映像側もここで終了）
					break;
				}
				const uint32_t bytes = frames * bpf;
				if (!stream_->PushPCM(scratch_.data(), bytes)) {
					pending_.assign(scratch_.begin(), scratch_.begin() + bytes);
					break;
				}
			}
		}


		void VideoPlayer::Stop()
		{
			stream_.reset();   // SoundStream の dtor が voice を停止・登録解除
			audio_.reset();
			pending_.clear();
			playing_ = false;
		}


		sound::MediaClock VideoPlayer::GetClock() const
		{
			return stream_ ? stream_->GetPlaybackClock() : sound::MediaClock{};
		}
	}
}
