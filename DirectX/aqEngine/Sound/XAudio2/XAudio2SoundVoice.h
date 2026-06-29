#pragma once
#include <xaudio2.h>
#include <atomic>
#include <array>
#include <vector>
#include "Sound/ISoundVoice.h"
#include "Sound/SoundClip.h"


namespace aq
{
	namespace sound
	{
		// IXAudio2SourceVoice をラップした 1 音源インスタンス（§7）。
		// 自身が IXAudio2VoiceCallback を実装し、OnBufferEnd/OnStreamEnd で
		// ストリーミングブロック回収と自然終了判定を行う。
		class XAudio2SoundVoice : public ISoundVoice, public IXAudio2VoiceCallback
		{
		// ── メンバ変数 ──
		private:
			IXAudio2*               xaudio2_   = nullptr;   // 所有しない
			IXAudio2Voice*          destVoice_ = nullptr;   // 送り先バス submix（所有しない）
			IXAudio2SourceVoice*    sourceVoice_ = nullptr; // 所有する

			SoundFormat  format_;
			RefSoundClip clip_;   // SubmitClipRegion のゼロコピー対象を生存保証（§3.3a）

			std::atomic<bool> finished_{ false };

		// ── 定数 ──
		private:
			static constexpr uint32_t STREAM_BLOCK_COUNT = 16u;

		// ── メンバ変数（続き）──
		private:
			// ストリーミング用ブロックプール（SubmitBuffer のコピー先）。
			// std::atomic を含むため非ムーブ。固定長 std::array で resize を避ける。
			struct Block
			{
				std::vector<uint8_t> data;
				std::atomic<bool>    inUse{ false };
			};
			std::array<Block, STREAM_BLOCK_COUNT> blocks_;
			bool                                  submittedEndOfStream_ = false;

		// ── メンバ関数 ──
		public:
			XAudio2SoundVoice(IXAudio2* xaudio2, IXAudio2Voice* destVoice);
			~XAudio2SoundVoice() override;

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

			// IXAudio2VoiceCallback（オーディオスレッドから呼ばれる）
			void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
			void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
			void STDMETHODCALLTYPE OnStreamEnd() override;
			void STDMETHODCALLTYPE OnBufferStart(void*) override {}
			void STDMETHODCALLTYPE OnBufferEnd(void* context) override;
			void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
			void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}

		private:
			int32_t AcquireBlock(uint32_t byteSize);
			void    DestroySourceVoice();
		};
	}
}
