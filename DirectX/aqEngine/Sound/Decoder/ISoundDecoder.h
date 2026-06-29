#pragma once
#include <cstdint>
#include "Sound/SoundTypes.h"


namespace aq
{
	namespace sound
	{
		// ストリーミングデコーダ IF（§5）。ファイルから逐次デコードする。
		// WAV/Ogg などフォーマットごとに実装し、SoundStream はこれ越しに供給を受ける。
		class ISoundDecoder
		{
		public:
			virtual ~ISoundDecoder() = default;

			// path を開き、フォーマットを確定する。成功で true。
			virtual bool Open(const char* path) = 0;

			virtual const SoundFormat& GetFormat() const = 0;

			// 総フレーム数（不明なら 0）。
			virtual uint64_t GetTotalFrames() const = 0;

			// 最大 maxFrames フレームを dst へデコードし、実際に書いたフレーム数を返す。
			// 0 は終端（EOF）。dst は maxFrames * format.BytesPerFrame() バイト以上。
			virtual uint32_t ReadFrames(void* dst, uint32_t maxFrames) = 0;

			// 指定フレームへシーク（ループ用）。成功で true。
			virtual bool Seek(uint64_t frame) = 0;

			// 終端に達したか。
			virtual bool IsEnd() const = 0;
		};
	}
}
