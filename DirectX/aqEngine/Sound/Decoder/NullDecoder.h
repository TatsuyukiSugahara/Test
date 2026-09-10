#pragma once
#include <cstdint>
#include <vector>
#include "ISoundDecoder.h"


namespace aq
{
	namespace sound
	{
		// 常にデコードに失敗する空デコーダ(Mac の P2〜P3 用)。
		// OS のコーデックに任せる形式(mp3/aac/m4a 等)の実装が入るまでの placeholder で、
		// ストリーミング(ISoundDecoder)と全展開(DecodeFileFully)の両方に同じ契約で応える。
		// Open が false を返すため、呼び出し元は「読めないファイル」として通常経路で扱える。
		// TODO(P4): Sound/Decoder/ExtAudioFileDecoder.{h,mm}(AudioToolbox)へ差し替える。
		class NullDecoder : public ISoundDecoder
		{
		// ── メンバ変数 ──
		private:
			SoundFormat format_;   // 常に無効(IsValid() == false)

		// ── メンバ関数 ──
		public:
			NullDecoder() = default;
			~NullDecoder() override = default;

			bool               Open(const char* /*path*/) override { return false; }
			const SoundFormat& GetFormat() const override { return format_; }
			uint64_t           GetTotalFrames() const override { return 0u; }
			uint32_t           ReadFrames(void* /*dst*/, uint32_t /*maxFrames*/) override { return 0u; }
			bool               Seek(uint64_t /*frame*/) override { return false; }
			bool               IsEnd() const override { return true; }

		// ── static ──
		public:
			// MFDecoder::DecodeFileFully と同じシグネチャ(SoundClip 用)。常に失敗。
			static bool DecodeFileFully(const char* /*path*/, SoundFormat& outFormat,
			                            std::vector<uint8_t>& outPcm)
			{
				outFormat = SoundFormat{};
				outPcm.clear();
				return false;
			}
		};
	}
}
