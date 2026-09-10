#pragma once
// macOS 専用。他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_MAC)
#include "Sound/ISoundVoice.h"
#include "Sound/Mixer/SoftwareMixer.h"

namespace aq
{
	namespace sound
	{
		/**
		 * `SoftwareMixer` の論理ボイス 1 本を `ISoundVoice` として見せる薄いアダプタ
		 * (設計書/Mac移植設計.md §5)。
		 *
		 * `ISoundVoice` の各メソッドは `SoftwareMixer` の同名操作と 1 対 1 に対応するので、
		 * ここには**委譲以外のロジックを置かない**。再生状態・キュー・リサンプル・
		 * 出力行列の意味論はすべてミキサ側が持つ。
		 *
		 * 寿命: 生成は `CoreAudioSoundBackend::CreateVoice`、破棄時にスロットをミキサへ返す。
		 * スレッド: サウンドスレッドから呼ばれる(`SoftwareMixer` が SPSC 前提のため
		 * プロデューサは 1 本に限る。architecture.md §5)。
		 */
		class CoreAudioSoundVoice : public ISoundVoice
		{
		private:
			/** 生成元のミキサ。寿命は CoreAudioSoundBackend が保証する */
			SoftwareMixer* mixer_;

			/** ミキサ上のスロット。無効なら INVALID_VOICE_ID */
			SoftwareMixer::VoiceId voiceId_;


		public:
			CoreAudioSoundVoice(SoftwareMixer* mixer, SoftwareMixer::VoiceId voiceId);
			~CoreAudioSoundVoice() override;


		public:
			bool Initialize(const SoundFormat& format) override;

			SubmitResult SubmitBuffer(const void* data, uint32_t byteSize, bool endOfStream) override;
			SubmitResult SubmitClipRegion(RefSoundClip clip, uint64_t startFrame, uint64_t frameCount,
			                              const LoopRegion& loop, bool endOfStream) override;

			void Start()  override;
			void Pause()  override;
			void Resume() override;
			void Stop()   override;

			void SetVolume(float volume) override;
			void SetFrequencyRatio(float ratio) override;
			void SetOutputMatrix(uint32_t srcChannels, uint32_t dstChannels, const float* matrix) override;

			uint64_t GetConsumedFrames()    const override;
			uint32_t GetQueuedBufferCount() const override;
			bool     IsFinished()           const override;
		};
	}
}
#endif // AQ_PLATFORM_MAC
