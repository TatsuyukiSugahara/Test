#pragma once
#include <cstdint>


namespace aq
{
	namespace sound
	{
		// ── バス（グループボリューム）──
		enum class SoundBusId : uint8_t
		{
			Master,
			BGM,
			SE,
			Voice,

			Count,
		};


		// ── PCM フォーマット ──
		// frame = 全チャンネルぶんの 1 サンプル。bytesPerFrame = channels * bitsPerSample / 8。
		struct SoundFormat
		{
			uint32_t sampleRate    = 0;
			uint16_t channels      = 0;
			uint16_t bitsPerSample = 0;
			bool     isFloat       = false;   // true: IEEE float / false: 符号付き整数 PCM

			uint32_t BytesPerFrame() const
			{
				return static_cast<uint32_t>(channels) * (bitsPerSample / 8u);
			}
			bool IsValid() const
			{
				return sampleRate != 0u && channels != 0u && bitsPerSample != 0u;
			}
			bool operator==(const SoundFormat& rhs) const
			{
				return sampleRate == rhs.sampleRate
					&& channels == rhs.channels
					&& bitsPerSample == rhs.bitsPerSample
					&& isFloat == rhs.isFloat;
			}
		};


		// ── 3D 距離減衰モデル ──
		enum class AttenuationModel : uint8_t
		{
			None,
			Linear,
			Inverse,
			Exponential,
		};


		// ── ループ区間（submit 時に確定。§3.3c）──
		struct LoopRegion
		{
			uint64_t startFrame = 0;   // ループ先頭（フレーム）
			uint64_t frameCount = 0;   // 0 = ループ無効
			uint32_t loopCount  = 0;   // 0 = 無限

			bool IsLooping() const { return frameCount != 0u; }
		};


		// ── PCM 投入結果（背圧制御。§3.1 / Medium）──
		enum class SubmitResult : uint8_t
		{
			Accepted,       // 受理（キューに積んだ）
			WouldBlock,     // 内部リング/ブロック満杯。後で再投入
			InvalidFormat,  // format 不一致
			Closed,         // 既に endOfStream 済み/停止中で受け付けない
		};


		// ── デバイス出力クロック（§3.4）──
		// A/V 同期の latency anchor。media time そのものではない（それは MediaClock）。
		struct SoundClock
		{
			uint64_t outputFrames  = 0;     // 出力レートで送出済みのフレーム総数（デバイス基準）
			uint32_t sampleRate    = 0;     // 出力サンプルレート
			double   latencySeconds = 0.0;  // 推定デバイス出力遅延
			uint64_t underrunCount = 0;     // 累積アンダーラン回数
		};


		// ── ストリーム media クロック（§3.5）──
		// 「いま聞こえている音声が media の何秒目か」。動画 A/V 同期の権威。
		struct MediaClock
		{
			double presentedMediaSeconds = 0.0;
			bool   valid                 = false;
		};
	}
}
