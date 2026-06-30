#pragma once
#include <cstdint>
#include <vector>
#include "Sound/SoundTypes.h"


namespace aq
{
	namespace sound
	{
		// RIFF/WAVE を全展開デコードする（SE 用）。PCM 整数・IEEE float・
		// WAVE_FORMAT_EXTENSIBLE に対応。ストリーミングは将来 ISoundDecoder へ一般化する。
		class WavDecoder
		{
		public:
			// path から WAV を読み、PCM バイト列とフォーマットを取り出す。成功で true。
			static bool DecodeFile(const char* path, SoundFormat& outFormat, std::vector<uint8_t>& outPcm);

			// メモリ上の WAV バイト列をデコードする。
			static bool DecodeMemory(const uint8_t* data, size_t size, SoundFormat& outFormat, std::vector<uint8_t>& outPcm);
		};
	}
}
