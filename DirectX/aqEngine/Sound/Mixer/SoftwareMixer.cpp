#include "aq.h"
#include "SoftwareMixer.h"
#include "Sound/SoundClip.h"


namespace aq
{
	namespace sound
	{
		namespace
		{
			/** 線形補間 */
			inline float Lerp(const float a, const float b, const float t)
			{
				return a + (b - a) * t;
			}


			/**
			 * PCM 1 サンプルを float(-1.0〜1.0)へ変換する
			 * リトルエンディアン前提(x64 / ARM64 のみを対象とする)。
			 */
			float ReadSample(const uint8_t* frameBase, const uint32_t channel, const SoundFormat& format)
			{
				const uint32_t byteWidth = format.bitsPerSample / 8u;
				const uint8_t* p         = frameBase + static_cast<size_t>(channel) * byteWidth;

				if (format.isFloat) {
					if (byteWidth == 4u) {
						float v = 0.0f;
						std::memcpy(&v, p, sizeof(v));
						return v;
					}
					if (byteWidth == 8u) {
						double v = 0.0;
						std::memcpy(&v, p, sizeof(v));
						return static_cast<float>(v);
					}
					return 0.0f;
				}

				switch (byteWidth) {
				case 1u:
					// WAV の 8bit は符号なし(128 が無音)
					return (static_cast<float>(p[0]) - 128.0f) / 128.0f;
				case 2u:
				{
					int16_t v = 0;
					std::memcpy(&v, p, sizeof(v));
					return static_cast<float>(v) / 32768.0f;
				}
				case 3u:
				{
					const int32_t v = static_cast<int32_t>(p[0])
					                | (static_cast<int32_t>(p[1]) << 8)
					                | (static_cast<int32_t>(static_cast<int8_t>(p[2])) << 16);
					return static_cast<float>(v) / 8388608.0f;
				}
				case 4u:
				{
					int32_t v = 0;
					std::memcpy(&v, p, sizeof(v));
					return static_cast<float>(static_cast<double>(v) / 2147483648.0);
				}
				default:
					return 0.0f;
				}
			}


			/**
			 * 出力行列が未設定のときの既定ルーティング
			 * mono は全出力へ等倍(XAudio2 の既定と同じ)。それ以外は同 index を素通し。
			 */
			inline float DefaultMatrixGain(const uint32_t srcChannels, const uint32_t srcIndex, const uint32_t dstIndex)
			{
				if (srcChannels == 1u) {
					return 1.0f;
				}
				return srcIndex == dstIndex ? 1.0f : 0.0f;
			}
		}


		SoftwareMixer::SoftwareMixer()
		{
			for (size_t i = 0; i < static_cast<size_t>(SoundBusId::Count); ++i) {
				busVolume_[i].store(1.0f, std::memory_order_relaxed);
			}
		}


		SoftwareMixer::~SoftwareMixer()
		{
			Finalize();
		}


		bool SoftwareMixer::Initialize(const SoundFormat& outputFormat)
		{
			// 出力はインタリーブ 32bit float 固定(CoreAudio / Oboe いずれもこの形で受けられる)
			if (!outputFormat.IsValid() || !outputFormat.isFloat || outputFormat.bitsPerSample != 32u) {
				return false;
			}
			if (outputFormat.channels > MAX_OUTPUT_CHANNEL_COUNT) {
				return false;
			}

			outputFormat_ = outputFormat;
			initialized_  = true;
			return true;
		}


		void SoftwareMixer::Finalize()
		{
			// 出力スレッドは既に停止している前提。残コマンドとボイスをこのスレッドで片付ける。
			const uint32_t commandWrite = commandWrite_.load(std::memory_order_acquire);
			uint32_t       commandRead  = commandRead_.load(std::memory_order_relaxed);
			while (commandRead != commandWrite) {
				Command& command = commands_[commandRead % COMMAND_QUEUE_CAPACITY];
				command.clip.reset();
				command.type = CommandType::None;
				++commandRead;
			}
			commandRead_.store(commandWrite, std::memory_order_release);

			for (Voice& voice : voices_) {
				voice.clip.reset();
				ResetPlaybackState(voice);
				voice.consumedFrames.store(0u, std::memory_order_relaxed);
				voice.finished.store(false, std::memory_order_relaxed);
				voice.releaseRequested.store(false, std::memory_order_relaxed);
				voice.writeIndex.store(0u, std::memory_order_relaxed);
				voice.readIndex.store(0u, std::memory_order_relaxed);
				voice.submitClosed = false;
				voice.slotState.store(static_cast<uint8_t>(SlotState::Free), std::memory_order_release);
			}

			DrainRetired();
			initialized_ = false;
		}


		/************************************/




		/**
		 * ボイス操作(サウンドスレッド)
		 */
		SoftwareMixer::VoiceId SoftwareMixer::AcquireVoice(const SoundFormat& format, const SoundBusId bus)
		{
			if (!initialized_ || !format.IsValid()) {
				return INVALID_VOICE_ID;
			}
			if (format.channels > MAX_SOURCE_CHANNEL_COUNT || format.BytesPerFrame() == 0u) {
				return INVALID_VOICE_ID;
			}

			for (uint32_t i = 0; i < MAX_VOICE_COUNT; ++i) {
				Voice&  voice    = voices_[i];
				uint8_t expected = static_cast<uint8_t>(SlotState::Free);
				if (!voice.slotState.compare_exchange_strong(expected, static_cast<uint8_t>(SlotState::Acquiring),
				                                             std::memory_order_acq_rel)) {
					continue;
				}

				// Acquiring の間は Render がこのスロットを見ないので、素直に初期化してよい。
				voice.format            = format;
				voice.bus               = bus;
				voice.submitClosed      = false;
				voice.volume            = 1.0f;
				voice.frequencyRatio    = 1.0f;
				voice.matrixSrcChannels = 0u;
				voice.matrixDstChannels = 0u;
				voice.consumedFrames.store(0u, std::memory_order_relaxed);
				voice.finished.store(false, std::memory_order_relaxed);
				voice.releaseRequested.store(false, std::memory_order_relaxed);
				voice.writeIndex.store(0u, std::memory_order_relaxed);
				voice.readIndex.store(0u, std::memory_order_relaxed);
				ResetPlaybackState(voice);

				voice.slotState.store(static_cast<uint8_t>(SlotState::Active), std::memory_order_release);
				return i;
			}
			return INVALID_VOICE_ID;
		}


		void SoftwareMixer::ReleaseVoice(const VoiceId voiceId)
		{
			Voice* voice = GetVoice(voiceId);
			if (voice == nullptr) {
				return;
			}
			// 実際の後始末は Render スレッドが行う(コールバック実行中にボイスが消えないため)。
			voice->releaseRequested.store(true, std::memory_order_release);
		}


		SubmitResult SoftwareMixer::SubmitBuffer(const VoiceId voiceId, const void* data, const uint32_t byteSize,
		                                         const bool endOfStream)
		{
			Voice* voice = GetVoice(voiceId);
			if (voice == nullptr || voice->submitClosed) {
				return SubmitResult::Closed;
			}

			const uint32_t bytesPerFrame = voice->format.BytesPerFrame();
			if (bytesPerFrame == 0u || (byteSize % bytesPerFrame) != 0u) {
				return SubmitResult::InvalidFormat;
			}

			const uint32_t write = voice->writeIndex.load(std::memory_order_relaxed);
			const uint32_t read  = voice->readIndex.load(std::memory_order_acquire);
			if ((write - read) >= VOICE_BLOCK_COUNT) {
				return SubmitResult::WouldBlock;
			}

			// リング未公開スロットはプロデューサの所有物なので、ここでの resize は Render を妨げない。
			Block& block = voice->blocks[write % VOICE_BLOCK_COUNT];
			if (block.data.size() < byteSize) {
				block.data.resize(byteSize);
			}
			if (byteSize > 0u && data != nullptr) {
				std::memcpy(block.data.data(), data, byteSize);
			}
			block.byteSize    = byteSize;
			block.endOfStream = endOfStream;

			voice->writeIndex.store(write + 1u, std::memory_order_release);
			if (endOfStream) {
				voice->submitClosed = true;
			}
			return SubmitResult::Accepted;
		}


		SubmitResult SoftwareMixer::SubmitClipRegion(const VoiceId voiceId, RefSoundClip clip, const uint64_t startFrame,
		                                             const uint64_t frameCount, const LoopRegion& loop, const bool endOfStream)
		{
			Voice* voice = GetVoice(voiceId);
			if (voice == nullptr || voice->submitClosed) {
				return SubmitResult::Closed;
			}
			if (!clip || !(clip->GetFormat() == voice->format)) {
				return SubmitResult::InvalidFormat;
			}

			Command* command = AcquireCommandSlot();
			if (command == nullptr) {
				return SubmitResult::WouldBlock;
			}
			command->type        = CommandType::SubmitClipRegion;
			command->voiceId     = voiceId;
			command->clip        = std::move(clip);
			command->startFrame  = startFrame;
			command->frameCount  = frameCount;
			command->loop        = loop;
			command->endOfStream = endOfStream;
			PublishCommand();

			// ループ中は終端が来ないので閉じない(Sound設計 §3.3(c))。
			if (endOfStream && !loop.IsLooping()) {
				voice->submitClosed = true;
			}
			return SubmitResult::Accepted;
		}


		void SoftwareMixer::Start(const VoiceId voiceId)
		{
			if (GetVoice(voiceId) == nullptr) {
				return;
			}
			Command* command = AcquireCommandSlot();
			if (command == nullptr) {
				return;
			}
			command->type    = CommandType::Start;
			command->voiceId = voiceId;
			PublishCommand();
		}


		void SoftwareMixer::Pause(const VoiceId voiceId)
		{
			if (GetVoice(voiceId) == nullptr) {
				return;
			}
			Command* command = AcquireCommandSlot();
			if (command == nullptr) {
				return;
			}
			command->type    = CommandType::Pause;
			command->voiceId = voiceId;
			PublishCommand();
		}


		void SoftwareMixer::Resume(const VoiceId voiceId)
		{
			if (GetVoice(voiceId) == nullptr) {
				return;
			}
			Command* command = AcquireCommandSlot();
			if (command == nullptr) {
				return;
			}
			command->type    = CommandType::Resume;
			command->voiceId = voiceId;
			PublishCommand();
		}


		void SoftwareMixer::Stop(const VoiceId voiceId)
		{
			Voice* voice = GetVoice(voiceId);
			if (voice == nullptr) {
				return;
			}
			Command* command = AcquireCommandSlot();
			if (command == nullptr) {
				return;
			}
			// Stop 発行時点までの投入だけを破棄する。以降の再投入は Stop の後ろに並ぶ。
			command->type       = CommandType::Stop;
			command->voiceId    = voiceId;
			command->flushIndex = voice->writeIndex.load(std::memory_order_relaxed);
			PublishCommand();

			voice->submitClosed = false;
		}


		void SoftwareMixer::SetVoiceVolume(const VoiceId voiceId, const float volume)
		{
			if (GetVoice(voiceId) == nullptr) {
				return;
			}
			Command* command = AcquireCommandSlot();
			if (command == nullptr) {
				return;
			}
			command->type    = CommandType::SetVolume;
			command->voiceId = voiceId;
			command->value   = volume;
			PublishCommand();
		}


		void SoftwareMixer::SetFrequencyRatio(const VoiceId voiceId, const float ratio)
		{
			if (GetVoice(voiceId) == nullptr) {
				return;
			}
			Command* command = AcquireCommandSlot();
			if (command == nullptr) {
				return;
			}
			command->type    = CommandType::SetFrequencyRatio;
			command->voiceId = voiceId;
			command->value   = ratio;
			PublishCommand();
		}


		void SoftwareMixer::SetOutputMatrix(const VoiceId voiceId, const uint32_t srcChannels,
		                                    const uint32_t dstChannels, const float* matrix)
		{
			if (GetVoice(voiceId) == nullptr || matrix == nullptr) {
				return;
			}
			if (srcChannels == 0u || srcChannels > MAX_SOURCE_CHANNEL_COUNT) {
				return;
			}
			if (dstChannels == 0u || dstChannels > MAX_OUTPUT_CHANNEL_COUNT) {
				return;
			}

			Command* command = AcquireCommandSlot();
			if (command == nullptr) {
				return;
			}
			command->type        = CommandType::SetOutputMatrix;
			command->voiceId     = voiceId;
			command->srcChannels = static_cast<uint16_t>(srcChannels);
			command->dstChannels = static_cast<uint16_t>(dstChannels);
			std::memcpy(command->matrix, matrix, sizeof(float) * srcChannels * dstChannels);
			PublishCommand();
		}


		uint64_t SoftwareMixer::GetConsumedFrames(const VoiceId voiceId) const
		{
			const Voice* voice = GetVoice(voiceId);
			return voice ? voice->consumedFrames.load(std::memory_order_acquire) : 0u;
		}


		uint32_t SoftwareMixer::GetQueuedBufferCount(const VoiceId voiceId) const
		{
			const Voice* voice = GetVoice(voiceId);
			if (voice == nullptr) {
				return 0u;
			}
			const uint32_t write = voice->writeIndex.load(std::memory_order_relaxed);
			const uint32_t read  = voice->readIndex.load(std::memory_order_acquire);
			return write - read;
		}


		bool SoftwareMixer::IsFinished(const VoiceId voiceId) const
		{
			const Voice* voice = GetVoice(voiceId);
			return voice ? voice->finished.load(std::memory_order_acquire) : false;
		}


		/************************************/




		/**
		 * バス / マスタ
		 */
		void SoftwareMixer::SetBusVolume(const SoundBusId bus, const float volume)
		{
			if (bus == SoundBusId::Count) {
				return;
			}
			busVolume_[static_cast<size_t>(bus)].store(volume, std::memory_order_relaxed);
		}


		void SoftwareMixer::SetMasterVolume(const float volume)
		{
			masterVolume_.store(volume, std::memory_order_relaxed);
		}


		float SoftwareMixer::GetBusVolume(const SoundBusId bus) const
		{
			if (bus == SoundBusId::Count) {
				return 0.0f;
			}
			return busVolume_[static_cast<size_t>(bus)].load(std::memory_order_relaxed);
		}


		/************************************/




		/**
		 * ポンプ
		 */
		void SoftwareMixer::Update()
		{
			DrainRetired();
		}


		void SoftwareMixer::Render(float* out, const uint32_t frames)
		{
			if (!initialized_ || out == nullptr || frames == 0u) {
				return;
			}

			const uint32_t dstChannels = outputFormat_.channels;
			const size_t   sampleCount = static_cast<size_t>(frames) * dstChannels;
			std::memset(out, 0, sampleCount * sizeof(float));

			DrainCommands();

			for (uint32_t i = 0; i < MAX_VOICE_COUNT; ++i) {
				Voice& voice = voices_[i];
				if (voice.slotState.load(std::memory_order_acquire) != static_cast<uint8_t>(SlotState::Active)) {
					continue;
				}

				// スロット返却は Render 側で後始末してから Free へ戻す(§3.3(d))。
				if (voice.releaseRequested.load(std::memory_order_acquire)) {
					RetireClip(std::move(voice.clip));
					ResetPlaybackState(voice);
					voice.consumedFrames.store(0u, std::memory_order_relaxed);
					voice.finished.store(false, std::memory_order_relaxed);
					voice.writeIndex.store(0u, std::memory_order_relaxed);
					voice.readIndex.store(0u, std::memory_order_relaxed);
					voice.releaseRequested.store(false, std::memory_order_relaxed);
					voice.slotState.store(static_cast<uint8_t>(SlotState::Free), std::memory_order_release);
					continue;
				}

				if (voice.playState != PlayState::Playing) {
					continue;
				}

				const float busGain = busVolume_[static_cast<size_t>(voice.bus)].load(std::memory_order_relaxed);
				MixVoice(voice, out, frames, voice.volume * busGain);
			}

			// マスタ音量は最後に一括で掛ける。
			const float master = masterVolume_.load(std::memory_order_relaxed);
			if (master != 1.0f) {
				for (size_t i = 0; i < sampleCount; ++i) {
					out[i] *= master;
				}
			}

			renderedFrames_.fetch_add(frames, std::memory_order_relaxed);
		}


		/************************************/




		/**
		 * 内部処理
		 */
		SoftwareMixer::Voice* SoftwareMixer::GetVoice(const VoiceId voiceId)
		{
			if (voiceId >= MAX_VOICE_COUNT) {
				return nullptr;
			}
			Voice& voice = voices_[voiceId];
			if (voice.slotState.load(std::memory_order_acquire) != static_cast<uint8_t>(SlotState::Active)) {
				return nullptr;
			}
			return &voice;
		}


		const SoftwareMixer::Voice* SoftwareMixer::GetVoice(const VoiceId voiceId) const
		{
			if (voiceId >= MAX_VOICE_COUNT) {
				return nullptr;
			}
			const Voice& voice = voices_[voiceId];
			if (voice.slotState.load(std::memory_order_acquire) != static_cast<uint8_t>(SlotState::Active)) {
				return nullptr;
			}
			return &voice;
		}


		SoftwareMixer::Command* SoftwareMixer::AcquireCommandSlot()
		{
			const uint32_t write = commandWrite_.load(std::memory_order_relaxed);
			const uint32_t read  = commandRead_.load(std::memory_order_acquire);
			if ((write - read) >= COMMAND_QUEUE_CAPACITY) {
				return nullptr;
			}

			Command& command = commands_[write % COMMAND_QUEUE_CAPACITY];
			command = Command{};   // 未公開スロットはプロデューサの所有物
			return &command;
		}


		void SoftwareMixer::PublishCommand()
		{
			const uint32_t write = commandWrite_.load(std::memory_order_relaxed);
			commandWrite_.store(write + 1u, std::memory_order_release);
		}


		void SoftwareMixer::DrainCommands()
		{
			const uint32_t write = commandWrite_.load(std::memory_order_acquire);
			uint32_t       read  = commandRead_.load(std::memory_order_relaxed);

			while (read != write) {
				Command& command = commands_[read % COMMAND_QUEUE_CAPACITY];
				ApplyCommand(command);

				// 参照が残っていれば回収リングへ(出力スレッドで解放しない)。
				RetireClip(std::move(command.clip));
				command.type = CommandType::None;

				++read;
				commandRead_.store(read, std::memory_order_release);
			}
		}


		void SoftwareMixer::ApplyCommand(Command& command)
		{
			if (command.voiceId >= MAX_VOICE_COUNT) {
				return;
			}
			Voice& voice = voices_[command.voiceId];
			if (voice.slotState.load(std::memory_order_relaxed) != static_cast<uint8_t>(SlotState::Active)) {
				return;
			}

			switch (command.type) {
			case CommandType::Start:
				voice.finished.store(false, std::memory_order_release);
				voice.playState = PlayState::Playing;
				break;

			case CommandType::Pause:
				if (voice.playState == PlayState::Playing) {
					voice.playState = PlayState::Paused;
				}
				break;

			case CommandType::Resume:
				if (voice.playState == PlayState::Paused) {
					voice.playState = PlayState::Playing;
				}
				break;

			case CommandType::Stop:
			{
				// 停止 + 投入キュー破棄 + 巻き戻し(Sound設計 §3.3(b))
				const uint32_t write = voice.writeIndex.load(std::memory_order_acquire);
				uint32_t       read  = voice.readIndex.load(std::memory_order_relaxed);
				if (static_cast<int32_t>(command.flushIndex - read) > 0) {
					read = command.flushIndex;
				}
				if (static_cast<int32_t>(write - read) < 0) {
					read = write;
				}
				voice.readIndex.store(read, std::memory_order_release);

				RetireClip(std::move(voice.clip));
				ResetPlaybackState(voice);
				voice.consumedFrames.store(0u, std::memory_order_release);
				voice.finished.store(false, std::memory_order_release);
				break;
			}

			case CommandType::SetVolume:
				voice.volume = command.value;
				break;

			case CommandType::SetFrequencyRatio:
				voice.frequencyRatio = command.value > 0.0f ? command.value : 0.0f;
				break;

			case CommandType::SetOutputMatrix:
				voice.matrixSrcChannels = command.srcChannels;
				voice.matrixDstChannels = command.dstChannels;
				std::memcpy(voice.matrix, command.matrix, sizeof(voice.matrix));
				break;

			case CommandType::SubmitClipRegion:
			{
				RetireClip(std::move(voice.clip));
				voice.clip = std::move(command.clip);

				const uint64_t totalFrames = voice.clip ? voice.clip->GetFrameCount() : 0u;
				uint64_t       start       = command.startFrame > totalFrames ? totalFrames : command.startFrame;
				uint64_t       end         = start + command.frameCount;
				if (end > totalFrames) {
					end = totalFrames;
				}

				voice.clipPcm       = voice.clip ? voice.clip->GetPcm() : nullptr;
				voice.clipPos       = start;
				voice.clipRegionEnd = end;
				voice.loop          = command.loop;
				voice.loopDoneCount = 0u;

				// ループ区間を再生領域へクランプする。
				if (voice.loop.IsLooping()) {
					if (voice.loop.startFrame >= end) {
						voice.loop.frameCount = 0u;
					}
					else if (voice.loop.startFrame + voice.loop.frameCount > end) {
						voice.loop.frameCount = end - voice.loop.startFrame;
					}
				}

				voice.clipActive      = (voice.clipPcm != nullptr && start < end);
				voice.clipEndOfStream = command.endOfStream;
				voice.primed          = false;
				voice.phase           = 0.0;
				break;
			}

			default:
				break;
			}
		}


		void SoftwareMixer::MixVoice(Voice& voice, float* out, const uint32_t frames, const float gain)
		{
			const uint32_t srcChannels = voice.format.channels;
			const uint32_t dstChannels = outputFormat_.channels;
			if (srcChannels == 0u || dstChannels == 0u || outputFormat_.sampleRate == 0u) {
				return;
			}

			// リサンプル比 = (入力レート / 出力レート) × ピッチ(SetFrequencyRatio)
			const double step = (static_cast<double>(voice.format.sampleRate) / static_cast<double>(outputFormat_.sampleRate))
			                  * static_cast<double>(voice.frequencyRatio);
			if (!(step > 0.0)) {
				return;
			}

			// 行列はチャンネル数が一致しているときだけ使う(不一致は既定ルーティングへ退避)。
			const bool useMatrix = (voice.matrixSrcChannels == srcChannels && voice.matrixDstChannels == dstChannels);

			// 線形補間には「前フレーム」と「次フレーム」の 2 つが要る。
			if (!voice.primed) {
				if (!FetchSourceFrame(voice, voice.prevFrame)) {
					if (!voice.finished.load(std::memory_order_relaxed)) {
						underrunCount_.fetch_add(1u, std::memory_order_relaxed);
					}
					return;
				}
				if (!FetchSourceFrame(voice, voice.curFrame)) {
					std::memset(voice.curFrame, 0, sizeof(voice.curFrame));
				}
				voice.phase  = 0.0;
				voice.primed = true;
			}

			bool  starved = false;
			float sample[MAX_SOURCE_CHANNEL_COUNT] = {};

			for (uint32_t i = 0; i < frames; ++i) {
				const float t = static_cast<float>(voice.phase);
				for (uint32_t c = 0; c < srcChannels; ++c) {
					sample[c] = Lerp(voice.prevFrame[c], voice.curFrame[c], t);
				}

				float* dst = out + static_cast<size_t>(i) * dstChannels;
				for (uint32_t d = 0; d < dstChannels; ++d) {
					float acc = 0.0f;
					for (uint32_t c = 0; c < srcChannels; ++c) {
						const float level = useMatrix ? voice.matrix[d * srcChannels + c]
						                              : DefaultMatrixGain(srcChannels, c, d);
						acc += sample[c] * level;
					}
					dst[d] += acc * gain;
				}

				voice.phase += step;
				while (voice.phase >= 1.0) {
					voice.phase -= 1.0;
					std::memcpy(voice.prevFrame, voice.curFrame, sizeof(float) * srcChannels);
					if (!FetchSourceFrame(voice, voice.curFrame)) {
						std::memset(voice.curFrame, 0, sizeof(float) * srcChannels);
						starved = true;
					}
				}

				if (voice.playState != PlayState::Playing) {
					break;   // endOfStream 到達で自然終了
				}
			}

			voice.consumedFrames.store(voice.renderConsumedFrames, std::memory_order_release);

			if (starved && !voice.finished.load(std::memory_order_relaxed)) {
				underrunCount_.fetch_add(1u, std::memory_order_relaxed);
			}
		}


		bool SoftwareMixer::FetchSourceFrame(Voice& voice, float* dst)
		{
			const uint32_t srcChannels   = voice.format.channels;
			const uint32_t bytesPerFrame = voice.format.BytesPerFrame();
			if (srcChannels == 0u || bytesPerFrame == 0u) {
				return false;
			}

			// (1) 常駐クリップ(ゼロコピー)
			if (voice.clipActive) {
				if (voice.loop.IsLooping()) {
					const uint64_t loopEnd = voice.loop.startFrame + voice.loop.frameCount;
					if (voice.clipPos >= loopEnd
						&& (voice.loop.loopCount == 0u || voice.loopDoneCount < voice.loop.loopCount)) {
						voice.clipPos = voice.loop.startFrame;
						++voice.loopDoneCount;
					}
				}

				if (voice.clipPos < voice.clipRegionEnd) {
					const uint8_t* frameBase = voice.clipPcm + static_cast<size_t>(voice.clipPos) * bytesPerFrame;
					for (uint32_t c = 0; c < srcChannels; ++c) {
						dst[c] = ReadSample(frameBase, c, voice.format);
					}
					++voice.clipPos;
					++voice.renderConsumedFrames;
					return true;
				}

				// 領域終端。clip 参照はここで手放す(解放はサウンドスレッド)。
				voice.clipActive = false;
				voice.clipPcm    = nullptr;
				RetireClip(std::move(voice.clip));
				if (voice.clipEndOfStream) {
					voice.playState = PlayState::Idle;
					voice.finished.store(true, std::memory_order_release);
					return false;
				}
			}

			// (2) 投入ブロック(コピー済みリング)
			for (;;) {
				const uint32_t write = voice.writeIndex.load(std::memory_order_acquire);
				const uint32_t read  = voice.readIndex.load(std::memory_order_relaxed);
				if (read == write) {
					return false;   // 供給切れ
				}

				Block&         block       = voice.blocks[read % VOICE_BLOCK_COUNT];
				const uint32_t blockFrames = block.byteSize / bytesPerFrame;
				if (voice.blockFrameOffset < blockFrames) {
					const uint8_t* frameBase = block.data.data() + static_cast<size_t>(voice.blockFrameOffset) * bytesPerFrame;
					for (uint32_t c = 0; c < srcChannels; ++c) {
						dst[c] = ReadSample(frameBase, c, voice.format);
					}
					++voice.blockFrameOffset;
					++voice.renderConsumedFrames;
					return true;
				}

				// このブロックは消費し切った。
				const bool endOfStream = block.endOfStream;
				voice.blockFrameOffset = 0u;
				voice.readIndex.store(read + 1u, std::memory_order_release);
				if (endOfStream) {
					voice.playState = PlayState::Idle;
					voice.finished.store(true, std::memory_order_release);
					return false;
				}
			}
		}


		void SoftwareMixer::ResetPlaybackState(Voice& voice)
		{
			voice.playState            = PlayState::Idle;
			voice.clipActive           = false;
			voice.clipPcm              = nullptr;
			voice.clipPos              = 0u;
			voice.clipRegionEnd        = 0u;
			voice.loop                 = LoopRegion{};
			voice.loopDoneCount        = 0u;
			voice.clipEndOfStream      = false;
			voice.blockFrameOffset     = 0u;
			voice.renderConsumedFrames = 0u;
			voice.phase                = 0.0;
			voice.primed               = false;
			std::memset(voice.prevFrame, 0, sizeof(voice.prevFrame));
			std::memset(voice.curFrame, 0, sizeof(voice.curFrame));
		}


		void SoftwareMixer::RetireClip(RefSoundClip&& clip)
		{
			if (!clip) {
				return;
			}

			const uint32_t write = retireWrite_.load(std::memory_order_relaxed);
			const uint32_t read  = retireRead_.load(std::memory_order_acquire);
			if ((write - read) >= RETIRE_QUEUE_CAPACITY) {
				// 回収リングが溢れたときだけ、やむを得ず出力スレッドで解放する。
				// 発生したら Update() の呼び出し頻度が足りていない(統計で検出する)。
				retireOverflowCount_.fetch_add(1u, std::memory_order_relaxed);
				clip.reset();
				return;
			}

			retired_[write % RETIRE_QUEUE_CAPACITY] = std::move(clip);
			retireWrite_.store(write + 1u, std::memory_order_release);
		}


		void SoftwareMixer::DrainRetired()
		{
			const uint32_t write = retireWrite_.load(std::memory_order_acquire);
			uint32_t       read  = retireRead_.load(std::memory_order_relaxed);

			while (read != write) {
				retired_[read % RETIRE_QUEUE_CAPACITY].reset();
				++read;
				retireRead_.store(read, std::memory_order_release);
			}
		}
	}
}
