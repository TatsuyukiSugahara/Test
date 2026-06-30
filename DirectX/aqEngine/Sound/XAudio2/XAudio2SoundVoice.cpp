#include "aq.h"
#include "XAudio2SoundVoice.h"


#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#endif


namespace aq
{
	namespace sound
	{
		namespace
		{
			// OnBufferEnd の context は「ブロック番号 + 1」。0 はクリップ（ゼロコピー）で回収不要。
			void* EncodeBlockContext(int32_t blockIndex)
			{
				return reinterpret_cast<void*>(static_cast<uintptr_t>(blockIndex + 1));
			}
			int32_t DecodeBlockContext(void* context)
			{
				const uintptr_t v = reinterpret_cast<uintptr_t>(context);
				return v == 0u ? -1 : static_cast<int32_t>(v - 1u);
			}

			WAVEFORMATEX MakeWaveFormat(const SoundFormat& f)
			{
				WAVEFORMATEX wfx = {};
				wfx.wFormatTag      = f.isFloat ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
				wfx.nChannels       = f.channels;
				wfx.nSamplesPerSec  = f.sampleRate;
				wfx.wBitsPerSample  = f.bitsPerSample;
				wfx.nBlockAlign     = static_cast<WORD>(f.channels * (f.bitsPerSample / 8u));
				wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
				wfx.cbSize          = 0;
				return wfx;
			}
		}


		XAudio2SoundVoice::XAudio2SoundVoice(IXAudio2* xaudio2, IXAudio2Voice* destVoice)
			: xaudio2_(xaudio2)
			, destVoice_(destVoice)
		{
		}


		XAudio2SoundVoice::~XAudio2SoundVoice()
		{
			DestroySourceVoice();
		}


		bool XAudio2SoundVoice::Initialize(const SoundFormat& format)
		{
			if (xaudio2_ == nullptr || !format.IsValid()) {
				return false;
			}
			format_ = format;

			const WAVEFORMATEX wfx = MakeWaveFormat(format);

			XAUDIO2_SEND_DESCRIPTOR sendDesc = { 0, destVoice_ };
			XAUDIO2_VOICE_SENDS     sends    = { 1, &sendDesc };

			HRESULT hr = xaudio2_->CreateSourceVoice(
				&sourceVoice_, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO,
				this, destVoice_ ? &sends : nullptr, nullptr);
			if (FAILED(hr)) {
				return false;
			}
			return true;
		}


		SubmitResult XAudio2SoundVoice::SubmitBuffer(const void* data, uint32_t byteSize, bool endOfStream)
		{
			if (sourceVoice_ == nullptr) {
				return SubmitResult::Closed;
			}
			if (submittedEndOfStream_) {
				return SubmitResult::Closed;
			}

			// 内部ブロックへコピー（呼び出し側バッファは即解放可。§3.3a）
			const int32_t blockIndex = AcquireBlock(byteSize);
			if (blockIndex < 0) {
				return SubmitResult::WouldBlock;
			}
			Block& block = blocks_[blockIndex];
			if (byteSize > 0u && data != nullptr) {
				std::memcpy(block.data.data(), data, byteSize);
			}

			XAUDIO2_BUFFER buffer = {};
			buffer.AudioBytes = byteSize;
			buffer.pAudioData = block.data.data();
			buffer.pContext   = EncodeBlockContext(blockIndex);
			if (endOfStream) {
				buffer.Flags = XAUDIO2_END_OF_STREAM;
				submittedEndOfStream_ = true;
			}

			HRESULT hr = sourceVoice_->SubmitSourceBuffer(&buffer);
			if (FAILED(hr)) {
				block.inUse.store(false, std::memory_order_release);
				return SubmitResult::WouldBlock;
			}
			return SubmitResult::Accepted;
		}


		SubmitResult XAudio2SoundVoice::SubmitClipRegion(RefSoundClip clip, uint64_t startFrame, uint64_t frameCount,
		                                                 const LoopRegion& loop, bool endOfStream)
		{
			if (sourceVoice_ == nullptr) {
				return SubmitResult::Closed;
			}
			if (!clip || clip->GetFormat() != format_) {
				return SubmitResult::InvalidFormat;
			}

			const uint32_t bpf = format_.BytesPerFrame();
			if (bpf == 0u) {
				return SubmitResult::InvalidFormat;
			}

			// clip をボイスが保持して生存保証（§3.3a / High）
			clip_ = std::move(clip);

			XAUDIO2_BUFFER buffer = {};
			buffer.pAudioData = clip_->GetPcm() + static_cast<size_t>(startFrame) * bpf;
			buffer.AudioBytes = static_cast<UINT32>(frameCount * bpf);
			buffer.pContext   = nullptr;   // ブロック回収不要
			buffer.PlayBegin  = 0;
			buffer.PlayLength = static_cast<UINT32>(frameCount);

			if (loop.IsLooping()) {
				buffer.LoopBegin  = static_cast<UINT32>(loop.startFrame);
				buffer.LoopLength = static_cast<UINT32>(loop.frameCount);
				buffer.LoopCount  = loop.loopCount == 0u ? XAUDIO2_LOOP_INFINITE
				                                         : static_cast<UINT32>(loop.loopCount);
			}
			if (endOfStream && !loop.IsLooping()) {
				buffer.Flags = XAUDIO2_END_OF_STREAM;
				submittedEndOfStream_ = true;
			}

			HRESULT hr = sourceVoice_->SubmitSourceBuffer(&buffer);
			return SUCCEEDED(hr) ? SubmitResult::Accepted : SubmitResult::WouldBlock;
		}


		void XAudio2SoundVoice::Start()
		{
			if (sourceVoice_) {
				finished_.store(false, std::memory_order_release);
				sourceVoice_->Start(0);
			}
		}


		void XAudio2SoundVoice::Pause()
		{
			// 位置・キューを保持して停止（§3.3b）
			if (sourceVoice_) {
				sourceVoice_->Stop(0);
			}
		}


		void XAudio2SoundVoice::Resume()
		{
			if (sourceVoice_) {
				sourceVoice_->Start(0);
			}
		}


		void XAudio2SoundVoice::Stop()
		{
			// 停止 + キュー破棄 + 巻き戻し（§3.3b）
			if (sourceVoice_) {
				sourceVoice_->Stop(0);
				sourceVoice_->FlushSourceBuffers();
			}
			for (Block& block : blocks_) {
				block.inUse.store(false, std::memory_order_release);
			}
			submittedEndOfStream_ = false;
			finished_.store(false, std::memory_order_release);
			clip_.reset();
		}


		void XAudio2SoundVoice::SetVolume(float volume)
		{
			if (sourceVoice_) {
				sourceVoice_->SetVolume(volume);
			}
		}


		void XAudio2SoundVoice::SetFrequencyRatio(float ratio)
		{
			if (sourceVoice_) {
				sourceVoice_->SetFrequencyRatio(ratio);
			}
		}


		void XAudio2SoundVoice::SetOutputMatrix(uint32_t srcChannels, uint32_t dstChannels, const float* matrix)
		{
			if (sourceVoice_ && matrix) {
				sourceVoice_->SetOutputMatrix(destVoice_, srcChannels, dstChannels, matrix);
			}
		}


		uint64_t XAudio2SoundVoice::GetConsumedFrames() const
		{
			if (sourceVoice_ == nullptr) {
				return 0u;
			}
			XAUDIO2_VOICE_STATE state = {};
			sourceVoice_->GetState(&state);
			return state.SamplesPlayed;
		}


		uint32_t XAudio2SoundVoice::GetQueuedBufferCount() const
		{
			if (sourceVoice_ == nullptr) {
				return 0u;
			}
			XAUDIO2_VOICE_STATE state = {};
			sourceVoice_->GetState(&state);
			return state.BuffersQueued;
		}


		bool XAudio2SoundVoice::IsFinished() const
		{
			return finished_.load(std::memory_order_acquire);
		}


		void STDMETHODCALLTYPE XAudio2SoundVoice::OnStreamEnd()
		{
			finished_.store(true, std::memory_order_release);
		}


		void STDMETHODCALLTYPE XAudio2SoundVoice::OnBufferEnd(void* context)
		{
			const int32_t blockIndex = DecodeBlockContext(context);
			if (blockIndex >= 0 && blockIndex < static_cast<int32_t>(blocks_.size())) {
				blocks_[blockIndex].inUse.store(false, std::memory_order_release);
			}
		}


		int32_t XAudio2SoundVoice::AcquireBlock(uint32_t byteSize)
		{
			for (int32_t i = 0; i < static_cast<int32_t>(blocks_.size()); ++i) {
				bool expected = false;
				if (blocks_[i].inUse.compare_exchange_strong(expected, true,
				                                             std::memory_order_acq_rel)) {
					if (blocks_[i].data.size() < byteSize) {
						blocks_[i].data.resize(byteSize);
					}
					return i;
				}
			}
			return -1;
		}


		void XAudio2SoundVoice::DestroySourceVoice()
		{
			if (sourceVoice_) {
				sourceVoice_->Stop(0);
				sourceVoice_->FlushSourceBuffers();
				sourceVoice_->DestroyVoice();
				sourceVoice_ = nullptr;
			}
			clip_.reset();
		}
	}
}
