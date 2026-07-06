#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "SoundTypes.h"
#include "ISoundVoice.h"
#include "VolumeEnvelope.h"
#include "Decoder/ISoundDecoder.h"


namespace aq
{
	namespace sound
	{
		class SoundEngine;

		// BGM / 動画音声などの逐次供給ストリーム（§5）。
		// decoder + voice を所有し、SoundEngine::Update() から Pump() で背圧制御供給される。
		// キャッシュ共有しない実体。SoundEngine::OpenStream() が生成する。
		class SoundStream
		{
		// ── メンバ変数 ──
		private:
			SoundEngine&                   engine_;
			std::unique_ptr<ISoundVoice>   voice_;
			std::unique_ptr<ISoundDecoder> decoder_;
			SoundFormat                    format_;
			SoundBusId                     bus_;
			LoopRegion                     loop_;
			std::vector<uint8_t>           scratch_;   // デコード一時バッファ
			VolumeEnvelope                 volumeEnv_;
			bool                           playing_      = false;
			bool                           endSubmitted_ = false;

		// ── 定数 ──
		private:
			// 維持する投入バッファ数。バッファ補充(Pump)はメインスレッドで行うため、
			// 非同期ロード等でフレームが伸びるとアンダーラン(音切れ)する。先読みを深くして
			// 許容ヒッチ幅を稼ぐ: 8 × CHUNK_FRAMES(4096) ≒ 48kHz で約 680ms。
			// 上限は XAudio2SoundVoice の STREAM_BLOCK_COUNT(16) 未満に収めること。
			static constexpr uint32_t TARGET_QUEUED = 8u;     // 維持する投入バッファ数
			static constexpr uint32_t CHUNK_FRAMES  = 4096u;  // 1 回の供給フレーム数

		// ── メンバ関数 ──
		public:
			// デコーダ駆動（BGM/ファイル）。
			SoundStream(SoundEngine& engine,
			            std::unique_ptr<ISoundVoice> voice,
			            std::unique_ptr<ISoundDecoder> decoder,
			            SoundBusId bus);
			// プッシュ駆動（動画音声など外部供給）。decoder を持たず PushPCM で供給する（§11）。
			SoundStream(SoundEngine& engine,
			            std::unique_ptr<ISoundVoice> voice,
			            const SoundFormat& format,
			            SoundBusId bus);
			~SoundStream();

			SoundStream(const SoundStream&) = delete;
			SoundStream& operator=(const SoundStream&) = delete;

			SoundBusId GetBus() const { return bus_; }
			bool IsPushMode() const { return decoder_ == nullptr; }

			// プッシュ駆動: 外部（VideoPlayer 等）が PCM を供給する。
			// 受理で true、内部キュー満杯（背圧）で false。
			bool PushPCM(const void* data, uint32_t byteSize);

			// 再生開始。loop.frameCount!=0 なら EOF で loop.startFrame へ戻ってループ（§3.3c）。
			void Play(const LoopRegion& loop = LoopRegion{});
			void Pause();
			void Resume();
			void Stop();

			void SetVolume(float volume);
			void SetPitch(float ratio);   // 再生速度/ピッチ（1.0=等倍）

			// フェード（§ フェード対応）。FadeOut は完了時に Stop する。
			void FadeIn(float seconds, float targetVolume = 1.0f);
			void FadeOut(float seconds);
			void FadeTo(float targetVolume, float seconds);

			bool IsPlaying() const { return playing_; }

			// §3.5: いま発音中の音声の media 時刻。動画 A/V 同期の master。
			MediaClock GetPlaybackClock() const;

			// SoundEngine::Update から呼ばれる供給ポンプ（背圧制御）。
			void Pump();
			// SoundEngine::Update から呼ばれるフェード更新。
			void UpdateFade(float deltaTime);
		};
	}
}
