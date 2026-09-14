#pragma once
// Apple 共通(macOS / iOS)。他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_APPLE)
#include <atomic>
#include "Sound/ISoundBackend.h"
#include "Sound/Mixer/SoftwareMixer.h"

namespace aq
{
	namespace sound
	{
		/**
		 * CoreAudio(AudioUnit)による `ISoundBackend` 実装
		 * (設計書/Mac移植設計.md §5 / 設計書/iOS移植設計.md §6)。
		 *
		 * 出力デバイスを 1 つだけ開き、そのレンダーコールバックで `SoftwareMixer::Render` を
		 * 呼ぶ。XAudio2 の「ソースボイス」に相当する意味論はすべてミキサ側が持ち、
		 * こちらは**デバイスを開いて PCM を渡すだけ**に徹する。
		 *
		 * 出力フォーマットは **48kHz / float32 / 2ch のインターリーブ**に固定する。
		 * ミキサの `Render(float* out, frames)` をコールバックのバッファへ直接書けるので、
		 * レンダーコールバック内でのメモリ確保もコピーも発生しない(§5 の実時間契約)。
		 *
		 * macOS と iOS で違うのは次の 3 点だけで、いずれも .mm 側に閉じている:
		 *  - 出力ユニットのサブタイプ(macOS: DefaultOutput / iOS: RemoteIO)
		 *  - 出力遅延の取得先(macOS: HAL のデバイスプロパティ / iOS: AVAudioSession)
		 *  - AVAudioSession のカテゴリ設定と中断通知(**iOS のみ**)
		 *
		 * スレッド:
		 *  - レンダーコールバックは CoreAudio の**出力スレッド**。ここから触ってよいのは
		 *    `SoftwareMixer::Render` と本クラスの `std::atomic` メンバだけ。
		 *  - それ以外(`CreateVoice` / `SetBusVolume` / `Update`)は**サウンドスレッド 1 本**から。
		 *  - `OnSuspend` / `OnResume` と iOS の中断通知は**どちらもメインスレッド**
		 *    (前者は CADisplayLink が回すフレーム、後者は mainQueue へ配送)なので、
		 *    停止理由のフラグ群に排他は要らない。
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

			/** 出力を止めている理由。背面へ回るのと中断が来るのは順序が前後しうるので
			 *  別々に持ち、**両方が下りたときだけ**動かす */
			bool suspended_;
			bool interrupted_;

			/** AudioOutputUnitStart 済みか。二重に止める / 二重に再開するのを防ぐ */
			bool outputRunning_;

#if defined(AQ_PLATFORM_IOS)
			/** AVAudioSessionInterruptionNotification のオブザーバ(NSObject)。
			 *  Objective-C 型をヘッダへ出さないため void* で持つ。MRR なので所有権は自前 */
			void* interruptionObserver_;
#endif

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

			/** 背面へ回った / 前面へ戻った。出力ユニットだけを止める・動かす */
			void OnSuspend() override;
			void OnResume()  override;


		public:
			/** レンダーコールバックの実体(.mm 内の C コールバックから呼ばれる) */
			void RenderFrames(float* out, uint32_t frames);


		private:
			/** 停止理由の現状に合わせて出力ユニットを止める・動かす(二重操作の防止込み) */
			void ApplyOutputRunState();

#if defined(AQ_PLATFORM_IOS)
			/** AVAudioSession の中断通知(メインスレッドから) */
			void OnInterruptionBegan();
			void OnInterruptionEnded(const bool shouldResume);

			/** 中断通知の購読 / 解除。Initialize / Finalize で対称に呼ぶ */
			void RegisterInterruptionObserver();
			void UnregisterInterruptionObserver();
#endif
		};
	}
}
#endif // AQ_PLATFORM_APPLE
