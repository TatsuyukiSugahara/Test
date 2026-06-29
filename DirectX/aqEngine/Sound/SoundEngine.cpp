#include "aq.h"
#include "SoundEngine.h"
#include "SoundClip.h"
#include "SoundStream.h"
#include "Decoder/WavStreamDecoder.h"
#include "Decoder/MFDecoder.h"
#include <algorithm>
#include <string>


namespace aq
{
	namespace sound
	{
		namespace
		{
			// 拡張子からストリーミングデコーダを選ぶ（§5）。現状 .wav のみ。
			std::unique_ptr<ISoundDecoder> CreateDecoderForPath(const char* path)
			{
				std::string s = path ? path : "";
				std::transform(s.begin(), s.end(), s.begin(),
				               [](unsigned char c) { return static_cast<char>(::tolower(c)); });

				auto endsWith = [&s](const char* ext) {
					const std::string e = ext;
					return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
				};

				if (endsWith(".wav")) {
					return std::make_unique<WavStreamDecoder>();
				}
				// mp3/aac/wma/m4a 等は Media Foundation（Windows）。
				if (endsWith(".mp3") || endsWith(".aac") || endsWith(".m4a")
					|| endsWith(".wma") || endsWith(".mp4") || endsWith(".flac")) {
					return std::make_unique<MFDecoder>();
				}
				return nullptr;
			}
		}


		SoundEngine* SoundEngine::instance_ = nullptr;


		bool SoundEngine::Initialize()
		{
			if (impl_ == nullptr) {
				return false;
			}
			voices_.reserve(64);
			return impl_->Initialize();
		}


		void SoundEngine::Finalize()
		{
			// ボイスはバックエンドのデバイスを参照するので先に破棄する。
			// （SoundStream は呼び出し側所有。Release 前に破棄する契約。）
			voices_.clear();
			sources_.clear();
			streams_.clear();
			if (impl_) {
				impl_->Finalize();
				impl_.reset();
			}
		}


		void SoundEngine::Update(float deltaTime)
		{
			// ワンショットのフェード更新 + 自然終了/フェード完了の回収（§2.1）。
			for (ActiveVoice& slot : voices_) {
				if (!slot.active || !slot.voice) {
					continue;
				}
				bool recycle = false;
				if (slot.fade.active) {
					const bool done = slot.fade.Update(deltaTime);
					slot.voice->SetVolume(slot.fade.current);
					if (done && slot.fade.stopAtEnd) {
						slot.voice->Stop();
						recycle = true;
					}
				}
				if (recycle || slot.voice->IsFinished()) {
					slot.active = false;
					++slot.generation;
					slot.voice.reset();
					slot.clip.reset();
				}
			}

			// バス/マスター音量のフェード更新。
			if (impl_) {
				if (masterFade_.active) {
					masterFade_.Update(deltaTime);
					masterVolume_ = masterFade_.current;
					impl_->SetMasterVolume(masterVolume_);
				}
				for (size_t b = 0; b < static_cast<size_t>(SoundBusId::Count); ++b) {
					if (busFade_[b].active) {
						busFade_[b].Update(deltaTime);
						busVolume_[b] = busFade_[b].current;
						impl_->SetBusVolume(static_cast<SoundBusId>(b), busVolume_[b]);
					}
				}
			}

			// 3D 発音体のフェード更新 → スペーシャライズ更新（§4）。
			for (SourceSlot& slot : sources_) {
				if (slot.active && slot.source) {
					slot.source->UpdateFade(deltaTime);
					slot.source->UpdateSpatialization(listener_);
				}
			}

			// ストリーミングのフェード更新 → 供給ポンプ（背圧制御。§3.3）。
			for (SoundStream* stream : streams_) {
				if (stream) {
					stream->UpdateFade(deltaTime);
					stream->Pump();
				}
			}

			if (impl_) {
				impl_->Update();
			}
		}


		SoundHandle SoundEngine::Play(RefSoundClip clip, SoundBusId bus, float fadeInSeconds)
		{
			if (impl_ == nullptr || !clip) {
				return SoundHandle{};
			}
			if (!clip->IsCompleted()) {
				EnginePrintf("[Sound] Play: clip がまだロード完了していません（no-op）。\n");
				return SoundHandle{};
			}

			std::unique_ptr<ISoundVoice> voice = impl_->CreateVoice(clip->GetFormat(), bus);
			if (voice == nullptr) {
				return SoundHandle{};
			}

			const uint64_t   frameCount = clip->GetFrameCount();
			const LoopRegion loop       = clip->GetDefaultLoop();

			const SubmitResult result =
				voice->SubmitClipRegion(clip, 0, frameCount, loop, /*endOfStream*/ !loop.IsLooping());
			if (result != SubmitResult::Accepted) {
				return SoundHandle{};
			}

			const uint32_t index = AcquireSlot();
			ActiveVoice&   slot  = voices_[index];
			slot.voice  = std::move(voice);
			slot.clip   = std::move(clip);
			slot.bus    = bus;
			slot.active = true;
			slot.fade.SetImmediate(1.0f);

			if (fadeInSeconds > 0.0f) {
				slot.voice->SetVolume(0.0f);
				slot.fade.current = 0.0f;
				slot.fade.FadeTo(1.0f, fadeInSeconds, false);
			}
			slot.voice->Start();

			return SoundHandle{ index, slot.generation };
		}


		bool SoundEngine::IsPlaying(SoundHandle handle) const
		{
			// const 経由でも論理的には参照のみ。const_cast で共通ロジックを使う。
			ActiveVoice* slot = const_cast<SoundEngine*>(this)->ResolveActive(handle);
			return slot != nullptr && slot->voice && !slot->voice->IsFinished();
		}


		void SoundEngine::Stop(SoundHandle handle)
		{
			ActiveVoice* slot = ResolveActive(handle);
			if (slot == nullptr) {
				return;
			}
			if (slot->voice) {
				slot->voice->Stop();
			}
			slot->active = false;
			++slot->generation;
			slot->voice.reset();
			slot->clip.reset();
		}


		std::unique_ptr<SoundStream> SoundEngine::OpenStream(const char* path, SoundBusId bus)
		{
			if (impl_ == nullptr || path == nullptr) {
				return nullptr;
			}

			std::unique_ptr<ISoundDecoder> decoder = CreateDecoderForPath(path);
			if (decoder == nullptr) {
				EnginePrintf("[Sound] OpenStream: 未対応フォーマットです: %s\n", path);
				return nullptr;
			}
			if (!decoder->Open(path)) {
				EnginePrintf("[Sound] OpenStream: 開けませんでした: %s\n", path);
				return nullptr;
			}

			std::unique_ptr<ISoundVoice> voice = impl_->CreateVoice(decoder->GetFormat(), bus);
			if (voice == nullptr) {
				return nullptr;
			}

			// SoundStream のコンストラクタが RegisterStream(this) を呼ぶ。
			return std::make_unique<SoundStream>(*this, std::move(voice), std::move(decoder), bus);
		}


		void SoundEngine::SetVolume(SoundHandle handle, float volume)
		{
			ActiveVoice* slot = ResolveActive(handle);
			if (slot && slot->voice) {
				slot->voice->SetVolume(volume);
			}
		}


		SoundSourceHandle SoundEngine::CreateSource(RefSoundClip clip, SoundBusId bus)
		{
			if (impl_ == nullptr || !clip) {
				return SoundSourceHandle{};
			}
			if (!clip->IsCompleted()) {
				EnginePrintf("[Sound] CreateSource: clip がまだロード完了していません（no-op）。\n");
				return SoundSourceHandle{};
			}

			std::unique_ptr<ISoundVoice> voice = impl_->CreateVoice(clip->GetFormat(), bus);
			if (voice == nullptr) {
				return SoundSourceHandle{};
			}

			const uint32_t index = AcquireSourceSlot();
			SourceSlot&    slot  = sources_[index];
			slot.source = std::make_unique<SoundSource>(std::move(voice), std::move(clip), bus);
			slot.active = true;

			return SoundSourceHandle{ index, slot.generation };
		}


		SoundSource* SoundEngine::Resolve(SoundSourceHandle handle)
		{
			SourceSlot* slot = ResolveSourceSlot(handle);
			return slot ? slot->source.get() : nullptr;
		}


		void SoundEngine::DestroySource(SoundSourceHandle handle)
		{
			SourceSlot* slot = ResolveSourceSlot(handle);
			if (slot == nullptr) {
				return;
			}
			if (slot->source) {
				slot->source->Stop();
			}
			slot->active = false;
			++slot->generation;
			slot->source.reset();
		}


		void SoundEngine::RegisterStream(SoundStream* stream)
		{
			if (stream) {
				streams_.push_back(stream);
			}
		}


		void SoundEngine::UnregisterStream(SoundStream* stream)
		{
			streams_.erase(std::remove(streams_.begin(), streams_.end(), stream), streams_.end());
		}


		void SoundEngine::SetPitch(SoundHandle handle, float ratio)
		{
			ActiveVoice* slot = ResolveActive(handle);
			if (slot && slot->voice) {
				slot->voice->SetFrequencyRatio(ratio);
			}
		}


		void SoundEngine::FadeOut(SoundHandle handle, float seconds)
		{
			ActiveVoice* slot = ResolveActive(handle);
			if (slot == nullptr || !slot->voice) {
				return;
			}
			// 現在の音量から 0 へフェードし、完了時に停止・回収する（Update が処理）。
			slot->fade.FadeTo(0.0f, seconds, /*stopWhenDone*/ true);
		}


		void SoundEngine::SetBusVolume(SoundBusId bus, float volume)
		{
			busVolume_[static_cast<size_t>(bus)] = volume;
			busFade_[static_cast<size_t>(bus)].SetImmediate(volume);
			if (impl_) {
				impl_->SetBusVolume(bus, volume);
			}
		}


		void SoundEngine::SetMasterVolume(float volume)
		{
			masterVolume_ = volume;
			masterFade_.SetImmediate(volume);
			if (impl_) {
				impl_->SetMasterVolume(volume);
			}
		}


		void SoundEngine::FadeBus(SoundBusId bus, float targetVolume, float seconds)
		{
			busFade_[static_cast<size_t>(bus)].current = busVolume_[static_cast<size_t>(bus)];
			busFade_[static_cast<size_t>(bus)].FadeTo(targetVolume, seconds, false);
		}


		void SoundEngine::FadeMaster(float targetVolume, float seconds)
		{
			masterFade_.current = masterVolume_;
			masterFade_.FadeTo(targetVolume, seconds, false);
		}


		void SoundEngine::PauseAll()
		{
			for (ActiveVoice& slot : voices_) {
				if (slot.active && slot.voice) { slot.voice->Pause(); }
			}
			for (SourceSlot& slot : sources_) {
				if (slot.active && slot.source) { slot.source->Pause(); }
			}
			for (SoundStream* stream : streams_) {
				if (stream) { stream->Pause(); }
			}
		}


		void SoundEngine::ResumeAll()
		{
			for (ActiveVoice& slot : voices_) {
				if (slot.active && slot.voice) { slot.voice->Resume(); }
			}
			for (SourceSlot& slot : sources_) {
				if (slot.active && slot.source) { slot.source->Resume(); }
			}
			for (SoundStream* stream : streams_) {
				if (stream) { stream->Resume(); }
			}
		}


		void SoundEngine::StopAll()
		{
			for (ActiveVoice& slot : voices_) {
				if (slot.active && slot.voice) {
					slot.voice->Stop();
					slot.active = false;
					++slot.generation;
					slot.voice.reset();
					slot.clip.reset();
				}
			}
			for (SourceSlot& slot : sources_) {
				if (slot.active && slot.source) { slot.source->Stop(); }
			}
			for (SoundStream* stream : streams_) {
				if (stream) { stream->Stop(); }
			}
		}


		void SoundEngine::PauseBus(SoundBusId bus)
		{
			for (ActiveVoice& slot : voices_) {
				if (slot.active && slot.voice && slot.bus == bus) { slot.voice->Pause(); }
			}
			for (SourceSlot& slot : sources_) {
				if (slot.active && slot.source && slot.source->GetBus() == bus) { slot.source->Pause(); }
			}
			for (SoundStream* stream : streams_) {
				if (stream && stream->GetBus() == bus) { stream->Pause(); }
			}
		}


		void SoundEngine::ResumeBus(SoundBusId bus)
		{
			for (ActiveVoice& slot : voices_) {
				if (slot.active && slot.voice && slot.bus == bus) { slot.voice->Resume(); }
			}
			for (SourceSlot& slot : sources_) {
				if (slot.active && slot.source && slot.source->GetBus() == bus) { slot.source->Resume(); }
			}
			for (SoundStream* stream : streams_) {
				if (stream && stream->GetBus() == bus) { stream->Resume(); }
			}
		}


		void SoundEngine::StopBus(SoundBusId bus)
		{
			for (ActiveVoice& slot : voices_) {
				if (slot.active && slot.voice && slot.bus == bus) {
					slot.voice->Stop();
					slot.active = false;
					++slot.generation;
					slot.voice.reset();
					slot.clip.reset();
				}
			}
			for (SourceSlot& slot : sources_) {
				if (slot.active && slot.source && slot.source->GetBus() == bus) { slot.source->Stop(); }
			}
			for (SoundStream* stream : streams_) {
				if (stream && stream->GetBus() == bus) { stream->Stop(); }
			}
		}


		void SoundEngine::Crossfade(SoundStream* outgoing, SoundStream* incoming, float seconds, bool loopIncoming)
		{
			if (incoming) {
				incoming->Play(loopIncoming ? LoopRegion{ 0, 1, 0 } : LoopRegion{});
				incoming->FadeIn(seconds);
			}
			if (outgoing) {
				outgoing->FadeOut(seconds);
			}
		}


		SoundClock SoundEngine::GetOutputClock() const
		{
			return impl_ ? impl_->GetOutputClock() : SoundClock{};
		}


		uint32_t SoundEngine::AcquireSlot()
		{
			for (uint32_t i = 0; i < static_cast<uint32_t>(voices_.size()); ++i) {
				if (!voices_[i].active) {
					return i;
				}
			}
			voices_.emplace_back();
			return static_cast<uint32_t>(voices_.size() - 1);
		}


		SoundEngine::ActiveVoice* SoundEngine::ResolveActive(SoundHandle handle)
		{
			if (!handle.IsValid() || handle.index >= voices_.size()) {
				return nullptr;
			}
			ActiveVoice& slot = voices_[handle.index];
			if (!slot.active || slot.generation != handle.generation) {
				return nullptr;
			}
			return &slot;
		}


		uint32_t SoundEngine::AcquireSourceSlot()
		{
			for (uint32_t i = 0; i < static_cast<uint32_t>(sources_.size()); ++i) {
				if (!sources_[i].active) {
					return i;
				}
			}
			sources_.emplace_back();
			return static_cast<uint32_t>(sources_.size() - 1);
		}


		SoundEngine::SourceSlot* SoundEngine::ResolveSourceSlot(SoundSourceHandle handle)
		{
			if (!handle.IsValid() || handle.index >= sources_.size()) {
				return nullptr;
			}
			SourceSlot& slot = sources_[handle.index];
			if (!slot.active || slot.generation != handle.generation) {
				return nullptr;
			}
			return &slot;
		}
	}
}
