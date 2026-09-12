#pragma once

// ============================================================
//  「wav 以外」用デコーダの選択(PadBackend.h / SoundBackend.h と同じ流儀)。
//    Win32 / UWP : Media Foundation(MFDecoder)
//    Mac         : 空デコーダ(NullDecoder)。AudioToolbox ExtAudioFile 実装
//                  (ExtAudioFileDecoder)は P4 で追加する
//    Android     : 空デコーダ(NullDecoder)。現在のアセットは wav のみなので
//                  可搬な WavDecoder で足りる。MediaCodec 実装が要るのは
//                  圧縮音源を入れるときで、その時点で差し替える
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
#include "Sound/Decoder/ExtAudioFileDecoder.h"

namespace aq
{
	namespace sound
	{
		// Mac: AudioToolbox の ExtAudioFile。OS が対応する形式(mp3 / aac / m4a 等)を
		// 16bit PCM へ落とす。MFDecoder と同じ契約(Mac移植設計 §5)。
		using CompressedDecoder = ExtAudioFileDecoder;
	}
}
#elif defined(AQ_PLATFORM_ANDROID)
#include "Sound/Decoder/NullDecoder.h"

namespace aq
{
	namespace sound
	{
		// Android: OS コーデック(MediaCodec)実装は未着手。Open / DecodeFileFully が
		// false を返すので、呼び出し元は「読めないファイル」として通常経路で扱える。
		using CompressedDecoder = NullDecoder;
	}
}
#else
#error "CompressedDecoder: 未対応のプラットフォームです"
#endif
