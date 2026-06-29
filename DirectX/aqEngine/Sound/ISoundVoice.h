#pragma once
#include <cstdint>
#include "SoundTypes.h"
#include "SoundFwd.h"


namespace aq
{
	namespace sound
	{
		// 1 音源インスタンスの最小 IF（§3.1）。
		// XAudio2 のソースボイスモデルを採用し、Oboe 側はミキサ内論理ボイスで同義を満たす。
		// 実時間契約は設計書 §3.3〜§3.5（所有権・寿命・状態遷移・スレッド・クロック）。
		class ISoundVoice
		{
		public:
			virtual ~ISoundVoice() = default;

			virtual bool Initialize(const SoundFormat& format) = 0;

			// PCM 供給（ストリーミング）。バックエンドが内部コピーするので呼び出し側は
			// 即解放してよい（§3.3a）。満杯時は WouldBlock を返す。
			virtual SubmitResult SubmitBuffer(const void* data, uint32_t byteSize, bool endOfStream) = 0;

			// 常駐クリップのゼロコピー再生。RefSoundClip を保持して clip 生存を保証する（§3.3a）。
			// ループは submit 時に確定する（§3.3c）。
			virtual SubmitResult SubmitClipRegion(RefSoundClip clip, uint64_t startFrame, uint64_t frameCount,
			                                      const LoopRegion& loop, bool endOfStream) = 0;

			// 再生状態（§3.3b の状態機械）
			virtual void Start()  = 0;   // 停止/一時停止 → 再生
			virtual void Pause()  = 0;   // 位置保持で停止（キュー維持）
			virtual void Resume() = 0;   // Pause からの再開
			virtual void Stop()   = 0;   // 停止し投入キューを破棄して巻き戻す（flush）

			// 全体音量（1.0 = 等倍）。3D の出力行列やバスゲインとは別の per-instance ゲイン。
			virtual void SetVolume(float volume) = 0;

			// ピッチ / ドップラー（1.0 = 等倍）。リサンプル比に反映される
			virtual void SetFrequencyRatio(float ratio) = 0;

			// 出力ルーティング行列（入力ch × 出力ch, row-major）。3D パン結果を Mixer3D が渡す（§4）。
			virtual void SetOutputMatrix(uint32_t srcChannels, uint32_t dstChannels, const float* matrix) = 0;

			// このボイスが再生し終えた「入力フレーム数」。media clock の anchor（§3.5）。
			virtual uint64_t GetConsumedFrames() const = 0;

			// 未消費の投入バッファ数（背圧制御用の残量）
			virtual uint32_t GetQueuedBufferCount() const = 0;

			// 再生が自然終了したか（endOfStream 投入後にキューが尽きた）。エンジンの回収判定に使う。
			virtual bool IsFinished() const = 0;
		};
	}
}
