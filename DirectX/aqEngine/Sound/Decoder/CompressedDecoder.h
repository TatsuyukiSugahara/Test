#pragma once

// ============================================================
//  「wav 以外」用デコーダの選択(PadBackend.h / SoundBackend.h と同じ流儀)。
//    Win32 / UWP : Media Foundation(MFDecoder)
//    Mac         : AudioToolbox ExtAudioFile(ExtAudioFileDecoder。P4 で追加)
//
//  mp3 / aac / wma / m4a / mp4 / flac など、OS のコーデックに任せる形式が対象。
//  .wav は可搬な WavDecoder / WavStreamDecoder が直接扱うので本ヘッダは通さない。
//
//  用途は 2 つあり、どちらの実装も同じシグネチャを備える契約:
//    - ストリーミング  : ISoundDecoder 実装として使う
//    - 全展開(SoundClip): static bool DecodeFileFully(const char*, SoundFormat&, std::vector<uint8_t>&)
// ============================================================

#if defined(AQ_PLATFORM_WIN32) || defined(AQ_PLATFORM_UWP)
#include "Sound/Decoder/MFDecoder.h"

namespace aq
{
	namespace sound
	{
		// Windows(デスクトップ / UWP): Media Foundation。
		using CompressedDecoder = MFDecoder;
	}
}
#elif defined(AQ_PLATFORM_MAC)

// TODO(P4): Sound/Decoder/ExtAudioFileDecoder.{h,mm} を追加し、下記を有効化する。
//   #include "Sound/Decoder/ExtAudioFileDecoder.h"
//   namespace aq { namespace sound { using CompressedDecoder = ExtAudioFileDecoder; } }
//
// P1 時点では Mac 向けの実コンパイルを行わないため、ここでは #error にせず
// エイリアス未定義のまま通す(Mac移植設計 §9 P1 / P4)。CompressedDecoder を
// 参照する SoundClip.cpp / SoundEngine.cpp は P4 で ExtAudioFileDecoder と同時に通る。

#endif
