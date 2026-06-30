#pragma once

// ============================================================
//  サウンドバックエンドの選択（PhysicsBackend.h と同じ流儀）。
//  プラットフォームで自動分岐する（§10）。
//    Windows : XAudio2
//    Android : Oboe（フェーズ5で追加予定）
// ============================================================

#if defined(_WIN32)
#  define SOUND_BACKEND_XAUDIO2
#elif defined(__ANDROID__)
#  define SOUND_BACKEND_OBOE
#endif


#if defined(SOUND_BACKEND_XAUDIO2)

#include "XAudio2/XAudio2SoundBackend.h"
namespace aq { namespace sound { using DefaultSoundBackend = XAudio2SoundBackend; } }

#elif defined(SOUND_BACKEND_OBOE)

// #include "Oboe/OboeSoundBackend.h"
// namespace aq { namespace sound { using DefaultSoundBackend = OboeSoundBackend; } }
#  error "Oboe バックエンドは未実装です（フェーズ5）。"

#else
#  error "サウンドバックエンドが未選択です。SoundBackend.h を確認してください。"
#endif
