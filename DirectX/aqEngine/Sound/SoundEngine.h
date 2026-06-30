#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <utility>
#include <type_traits>
#include "SoundTypes.h"
#include "SoundHandle.h"
#include "SoundFwd.h"
#include "ISoundBackend.h"
#include "SoundListener.h"
#include "SoundSource.h"
#include "VolumeEnvelope.h"


namespace aq
{
	namespace sound
	{
		// サウンドのコア・シングルトン（Bridge）。GraphicsDevice と同じ Create<TImpl>() パターン（§2.1）。
		// 起動時:
		//   SoundEngine::Create<DefaultSoundBackend>();
		//   SoundEngine::Get().Initialize();
		//
		// ※ フェーズ1（2D ワンショット再生）の実装。3D（SoundSource/Listener/Mixer3D）と
		//    ストリーミング（SoundStream/OpenStream）は後続フェーズで追加する。
		class SoundEngine
		{
		// ── メンバ型 ──
		private:
			struct ActiveVoice
			{
				std::unique_ptr<ISoundVoice> voice;
				RefSoundClip                 clip;        // 念のためエンジン側でも生存保持
				VolumeEnvelope               fade;        // ワンショットのフェード
				SoundBusId                   bus        = SoundBusId::SE;
				uint32_t                     generation = 0;
				bool                         active     = false;
			};

			struct SourceSlot
			{
				std::unique_ptr<SoundSource> source;
				uint32_t                     generation = 0;
				bool                         active     = false;
			};

		// ── メンバ変数 ──
		private:
			std::unique_ptr<ISoundBackend> impl_;
			std::vector<ActiveVoice>       voices_;
			std::vector<SourceSlot>        sources_;
			std::vector<SoundStream*>      streams_;   // 非所有。Pump 対象
			SoundListener                  listener_;

			// バス/マスター音量のフェード状態
			float          masterVolume_ = 1.0f;
			VolumeEnvelope masterFade_;
			float          busVolume_[static_cast<size_t>(SoundBusId::Count)] = { 1.0f, 1.0f, 1.0f, 1.0f };
			float          busDuck_[static_cast<size_t>(SoundBusId::Count)]   = { 1.0f, 1.0f, 1.0f, 1.0f };
			VolumeEnvelope busFade_[static_cast<size_t>(SoundBusId::Count)];

		// ── メンバ関数 ──
		private:
			SoundEngine() = default;
			~SoundEngine() = default;

		public:
			bool Initialize();
			void Finalize();

			// 毎フレーム: 終了ボイスの回収・バックエンドのポンプ（§2.1）。
			void Update(float deltaTime);

			// ワンショット再生（2D）。事前ロード済み clip を渡す（§5.2）。
			// fadeInSeconds > 0 で音量 0 から立ち上げる。
			SoundHandle Play(RefSoundClip clip, SoundBusId bus = SoundBusId::SE, float fadeInSeconds = 0.0f);

			// 3D 発音体の生成（§2.2）。エンジンがプール所有し世代付きハンドルを返す。
			SoundSourceHandle CreateSource(RefSoundClip clip, SoundBusId bus = SoundBusId::SE);
			SoundSource*      Resolve(SoundSourceHandle handle);   // 無効化済みなら nullptr
			void              DestroySource(SoundSourceHandle handle);
			SoundListener&    GetListener() { return listener_; }

			// ストリーミング元を生成（BGM/動画）。キャッシュ非共有の実体（§5.1/§5.2）。
			// 対応拡張子: .wav（Ogg は将来）。
			std::unique_ptr<SoundStream> OpenStream(const char* path, SoundBusId bus = SoundBusId::BGM);
			// プッシュ駆動ストリーム（動画音声など外部が PCM を供給。§11）。
			std::unique_ptr<SoundStream> OpenPushStream(const SoundFormat& format, SoundBusId bus = SoundBusId::Voice);

			// ハンドル操作（§2.4）。世代不一致は安全に no-op。
			bool IsPlaying(SoundHandle handle) const;
			void Stop(SoundHandle handle);
			void SetVolume(SoundHandle handle, float volume);
			void SetPitch(SoundHandle handle, float ratio);     // ピッチ/速度（1.0=等倍）
			void FadeOut(SoundHandle handle, float seconds);    // フェードして自動停止

			// 音量（即時）。
			void SetBusVolume(SoundBusId bus, float volume);
			void SetMasterVolume(float volume);
			// ダッキング用の duck 係数（base とは独立。effective = base × duck）。オーサリング層が使う。
			void SetBusDuck(SoundBusId bus, float gain);

			// 音量フェード（バス/マスター）。
			void FadeBus(SoundBusId bus, float targetVolume, float seconds);
			void FadeMaster(float targetVolume, float seconds);

			// 全体制御（ポーズ/再開/停止）。一時停止メニュー等で使う。
			void PauseAll();
			void ResumeAll();
			void StopAll();

			// バス単位の制御（SE だけ / Voice だけ / BGM だけ 等）。
			void PauseBus(SoundBusId bus);
			void ResumeBus(SoundBusId bus);
			void StopBus(SoundBusId bus);

			// BGM クロスフェード補助。outgoing をフェードアウト、incoming を（再生開始して）フェードイン。
			void Crossfade(SoundStream* outgoing, SoundStream* incoming, float seconds, bool loopIncoming = true);

			SoundClock GetOutputClock() const;

			// SoundStream が自身の生成/破棄時に呼ぶ（Pump 対象の登録/解除）。内部用。
			void RegisterStream(SoundStream* stream);
			void UnregisterStream(SoundStream* stream);

		private:
			// 空きスロットを確保（なければ末尾追加）。
			uint32_t AcquireSlot();
			// ハンドルが現在有効なら ActiveVoice を返す。
			ActiveVoice* ResolveActive(SoundHandle handle);

			uint32_t    AcquireSourceSlot();
			SourceSlot* ResolveSourceSlot(SoundSourceHandle handle);

			// バスの実効音量（base × duck）をバックエンドへ反映する。
			void ApplyBus(SoundBusId bus);

		// ── Singleton ──
		private:
			static SoundEngine* instance_;

		public:
			template <typename TImpl, typename... TArgs>
			static void Create(TArgs&&... args)
			{
				static_assert(std::is_base_of_v<ISoundBackend, TImpl>,
				              "TImpl must implement ISoundBackend");
				if (instance_ == nullptr) {
					instance_ = new SoundEngine();
					instance_->impl_ = std::make_unique<TImpl>(std::forward<TArgs>(args)...);
				}
			}
			static SoundEngine& Get() { return *instance_; }
			static bool IsAvailable() { return instance_ != nullptr; }
			static void Release()
			{
				if (instance_) {
					delete instance_;
					instance_ = nullptr;
				}
			}
		};
	}
}
