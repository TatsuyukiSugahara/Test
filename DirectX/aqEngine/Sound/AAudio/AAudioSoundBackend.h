#pragma once
// Android 専用。他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_ANDROID)
#include <atomic>
#include "Sound/ISoundBackend.h"
#include "Sound/Mixer/SoftwareMixer.h"

namespace aq
{
	namespace sound
	{
		/**
		 * AAudio による `ISoundBackend` 実装(設計書/Sound設計.md §8)。
		 *
		 * 出力ストリームを 1 本だけ開き、その data callback で `SoftwareMixer::Render` を
		 * 呼ぶ。XAudio2 の「ソースボイス」に相当する意味論はすべてミキサ側が持ち、
		 * こちらは**デバイスを開いて PCM を渡すだけ**に徹する(CoreAudio 版と同型)。
		 *
		 * 出力フォーマットは **48kHz / float32 / 2ch のインターリーブ**を要求する。
		 * ミキサの `Render(float* out, frames)` をコールバックのバッファへ直接書けるので、
		 * data callback 内でのメモリ確保もコピーも発生しない(§3.3 の実時間契約)。
		 * ただし AAudio は要求どおりのレート/チャンネル数で開くとは限らないため、
		 * 実際に開けた値で `outputFormat_` とミキサを作り直す(§8.2)。
		 *
		 * スレッド(§8.3):
		 *  - data callback は AAudio の**オーディオスレッド**。ここから触ってよいのは
		 *    `SoftwareMixer::Render` と本クラスの `std::atomic` メンバだけ。
		 *  - それ以外(`CreateVoice` / `SetBusVolume` / `Update`)は**サウンドスレッド 1 本**から。
		 *
		 * バックグラウンド遷移時の pause/resume とエラーコールバックによるストリーム
		 * 再構築(§8.3)は P5b で対応する。
		 */
		class AAudioSoundBackend : public ISoundBackend
		{
		private:
			/** 出力ストリーム(AAudioStream*)。AAudio 型をヘッダへ出さないため void* で持つ。
			 *  実体は .cpp 側でキャストする */
			void* stream_;

			/** 論理ボイスの本体。オーディオスレッドとサウンドスレッドで共有する */
			SoftwareMixer mixer_;

			/** 実際に開いた出力フォーマット */
			SoundFormat outputFormat_;

			/** data callback が送出した累積フレーム数(出力レート基準) */
			std::atomic<uint64_t> outputFrames_;

			/** デバイスの推定出力遅延 [秒]。初期化時に一度だけ見積もる */
			double latencySeconds_;

			bool initialized_;


		public:
			AAudioSoundBackend();
			~AAudioSoundBackend() override;


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
			/** data callback の実体(.cpp 内の C コールバックから呼ばれる) */
			void RenderFrames(float* out, uint32_t frames);
		};
	}
}
#endif // AQ_PLATFORM_ANDROID
