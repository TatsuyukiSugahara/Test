#pragma once

// ============================================================
//  サウンドバックエンドの選択（PhysicsBackend.h と同じ流儀）。
//  プラットフォームで自動分岐する（§10）。
//    Windows デスクトップ : XAudio2
//    Xbox(UWP / 道A)      : XAudio2（UWP でも XAudio2 2.9 が標準。ほぼ無改修）
//    Mac                  : CoreAudio（AudioUnit + SoftwareMixer）
//    Android              : AAudio（NDK 同梱 + SoftwareMixer）
// ============================================================

// AQ_PLATFORM_WINDOWS_FAMILY は AQ_PLATFORM_WIN32 / AQ_PLATFORM_UWP のときに
// 定義される（PlatformDefs.h）。従来の「_WIN32 || AQ_PLATFORM_UWP」と同じ集合。
#if defined(AQ_PLATFORM_WINDOWS_FAMILY)
#  define SOUND_BACKEND_XAUDIO2
#elif defined(AQ_PLATFORM_MAC)
#  define SOUND_BACKEND_COREAUDIO
#elif defined(AQ_PLATFORM_ANDROID)
#  define SOUND_BACKEND_AAUDIO
#endif


#if defined(SOUND_BACKEND_XAUDIO2)

#include "XAudio2/XAudio2SoundBackend.h"
namespace aq { namespace sound { using DefaultSoundBackend = XAudio2SoundBackend; } }

#elif defined(SOUND_BACKEND_COREAUDIO)

// Mac: AudioUnit（既定の出力デバイス）を 1 つ開き、レンダーコールバックで
// SoftwareMixer::Render を回す（Mac移植設計 §5 / §9 P4a）。
#include "CoreAudio/CoreAudioSoundBackend.h"
namespace aq { namespace sound { using DefaultSoundBackend = CoreAudioSoundBackend; } }

#elif defined(SOUND_BACKEND_NULL)

// どのプラットフォームでも使える無音バックエンド（移植の足場用に残してある）。
#include "NullSoundBackend.h"
namespace aq { namespace sound { using DefaultSoundBackend = NullSoundBackend; } }

#elif defined(SOUND_BACKEND_AAUDIO)

// Android: AAudio の出力ストリームを 1 本開き、data callback で
// SoftwareMixer::Render を回す（Sound設計 §8）。minSdk=33 なので Oboe の
// OpenSL ES フォールバックは不要で、AAudio は NDK に同梱されている。
#include "AAudio/AAudioSoundBackend.h"
namespace aq { namespace sound { using DefaultSoundBackend = AAudioSoundBackend; } }

#else
#  error "サウンドバックエンドが未選択です。SoundBackend.h を確認してください。"
#endif
