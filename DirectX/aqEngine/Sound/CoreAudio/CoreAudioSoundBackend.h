#pragma once
// macOS 専用。他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_MAC)
#include <atomic>
#include "Sound/ISoundBackend.h"
#include "Sound/Mixer/SoftwareMixer.h"

namespace aq
{
	namespace sound
	{
		/**
		 * CoreAudio(AudioUnit)による `ISoundBackend` 実装(設計書/Mac移植設計.md §5)。
		 *
		 * 出力デバイスを 1 つだけ開き、そのレンダーコールバックで `SoftwareMixer::Render` を
		 * 呼ぶ。XAudio2 の「ソースボイス」に相当する意味論はすべてミキサ側が持ち、
		 * こちらは**デバイスを開いて PCM を渡すだけ**に徹する。
		 *
		 * 出力フォーマットは **48kHz / float32 / 2ch のインターリーブ**に固定する。
		 * ミキサの `Render(float* out, frames)` をコールバックのバッファへ直接書けるので、
		 * レンダーコールバック内でのメモリ確保もコピーも発生しない(§5 の実時間契約)。
		 *
		 * スレッド:
		 *  - レンダーコールバックは CoreAudio の**出力スレッド**。ここから触ってよいのは
		 *    `SoftwareMixer::Render` と本クラスの `std::atomic` メンバだけ。
		 *  - それ以外(`CreateVoice` / `SetBusVolume` / `Update`)は**サウンドスレッド 1 本**から。
		 */
		class CoreAudioSoundBackend : public ISoundBackend
		{
		private:
			/** AudioUnit(AudioComponentInstance)。Objective-C 型ではないが CoreAudio 型を
			 *  ヘッダへ出さないため void* で持つ。実体は .mm 側でキャストする */
			void* outputUnit_;

			/** 論理ボイスの本体。出力スレッドとサウンドスレッドで共有する */
			SoftwareMixer mixer_;

			/** 実際に開いた出力フォーマット */
			SoundFormat outputFormat_;

			/** レンダーコールバックが送出した累積フレーム数(出力レート基準) */
			std::atomic<uint64_t> outputFrames_;

			/** デバイスの推定出力遅延 [秒]。初期化時に一度だけ問い合わせる */
			double latencySeconds_;

			bool initialized_;


		public:
			CoreAudioSoundBackend();
			~CoreAudioSoundBackend() override;


		public:
			bool Initialize() override;
			void Finalize()   override;

			std::unique_ptr<ISoundVoice> CreateVoice(const SoundFormat& format, SoundBusId bus) override;

			void SetBusVolume(SoundBusId bus, float volume) override;
			void SetMasterVolume(float volume) override;

			SoundClock GetOutputClock() const override;

			/** サウンドスレッドから毎フレーム。ミキサの回収キューを掃く */
			void Update() override;


		public:
			/** レンダーコールバックの実体(.mm 内の C コールバックから呼ばれる) */
			void RenderFrames(float* out, uint32_t frames);
		};
	}
}
#endif // AQ_PLATFORM_MAC
