#pragma once
#include <cstdint>
#include <chrono>
#include <deque>
#include <memory>
#include "ISoundBackend.h"
#include "ISoundVoice.h"


namespace aq
{
	namespace sound
	{
		/**
		 * 無音の 1 音源インスタンス(Mac の P2〜P3 用)
		 *
		 * PCM の出力は行わないが、再生位置(GetConsumedFrames)と自然終了(IsFinished)は
		 * 実時間で進める。SoundEngine のボイス回収(§2.1)や SoundStream の背圧制御
		 * (§3.3)が Windows と同じ流れで回るようにするため。
		 */
		class NullSoundVoice : public ISoundVoice
		{
		private:
			using Clock = std::chrono::steady_clock;

			/** 再生状態(§3.3b の状態機械) */
			enum class PlayState : uint8_t
			{
				Stopped,
				Playing,
				Paused,
			};

		// ── メンバ変数 ──
		private:
			SoundFormat  format_;
			RefSoundClip clip_;             // SubmitClipRegion のゼロコピー対象を生存保証(§3.3a)
			float        frequencyRatio_;   // リサンプル比。入力フレームの消費速度に反映する

			/** 仮想再生カーソル(const な問い合わせから時間を進めるため mutable) */
			mutable std::deque<uint64_t> queue_;                 // 投入バッファごとの残り入力フレーム数
			mutable PlayState            state_;
			mutable uint64_t             consumedFrames_;        // 消費済みの入力フレーム総数
			mutable double               fractionFrames_;        // 端数フレームの持ち越し
			mutable Clock::time_point    lastTick_;
			mutable bool                 finished_;
			mutable bool                 submittedEndOfStream_;
			mutable uint64_t             loopFrameCount_;        // 無限ループ 1 周ぶん(0 = ループなし)

		// ── メンバ関数 ──
		public:
			NullSoundVoice();
			~NullSoundVoice() override = default;

			// ISoundVoice
			bool         Initialize(const SoundFormat& format) override;
			SubmitResult SubmitBuffer(const void* data, uint32_t byteSize, bool endOfStream) override;
			SubmitResult SubmitClipRegion(RefSoundClip clip, uint64_t startFrame, uint64_t frameCount,
			                              const LoopRegion& loop, bool endOfStream) override;
			void     Start()  override;
			void     Pause()  override;
			void     Resume() override;
			void     Stop()   override;
			void     SetVolume(float volume) override;
			void     SetFrequencyRatio(float ratio) override;
			void     SetOutputMatrix(uint32_t srcChannels, uint32_t dstChannels, const float* matrix) override;
			uint64_t GetConsumedFrames() const override;
			uint32_t GetQueuedBufferCount() const override;
			bool     IsFinished() const override;

		private:
			// 前回呼び出しからの経過時間ぶん、仮想再生カーソルを進める。
			void Advance() const;

		// ── static ──
		private:
			// 投入キューの上限。XAudio2 側のブロックプール数と揃える。
			static constexpr uint32_t MAX_QUEUED_BUFFER_COUNT = 16u;
		};




		/**
		 * 無音サウンドバックエンド(Mac の P2〜P3 用)
		 *
		 * Initialize は成功し、ボイスの生成も通るが音は鳴らない。
		 * P2 の到達点「Mac でビルド・リンクが通る」を満たすための骨格
		 * (Mac移植設計 §9 P2)。
		 * TODO(P4): Sound/CoreAudio/CoreAudioSoundBackend.{h,mm} へ差し替え、
		 *           SoftwareMixer に接続する。
		 */
		class NullSoundBackend : public ISoundBackend
		{
		private:
			using Clock = std::chrono::steady_clock;

		// ── メンバ変数 ──
		private:
			Clock::time_point startTime_;
			bool              initialized_;

			/** 音量(保持のみ。出力には影響しない) */
			float masterVolume_;
			float busVolume_[static_cast<size_t>(SoundBusId::Count)];

		// ── メンバ関数 ──
		public:
			NullSoundBackend();
			~NullSoundBackend() override;

			bool Initialize() override;
			void Finalize()   override;

			std::unique_ptr<ISoundVoice> CreateVoice(const SoundFormat& format, SoundBusId bus) override;

			void SetBusVolume(SoundBusId bus, float volume) override;
			void SetMasterVolume(float volume) override;

			SoundClock GetOutputClock() const override;
		};
	}
}
