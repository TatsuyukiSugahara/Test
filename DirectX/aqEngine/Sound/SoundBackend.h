#pragma once

// ============================================================
//  サウンドバックエンドの選択（PhysicsBackend.h と同じ流儀）。
//  プラットフォームで自動分岐する（§10）。
//    Windows デスクトップ : XAudio2
//    Xbox(UWP / 道A)      : XAudio2（UWP でも XAudio2 2.9 が標準。ほぼ無改修）
//    Mac / iOS            : CoreAudio（AudioUnit + SoftwareMixer）
//    Android              : AAudio（NDK 同梱 + SoftwareMixer）
// ============================================================

// AQ_PLATFORM_WINDOWS_FAMILY は AQ_PLATFORM_WIN32 / AQ_PLATFORM_UWP のときに
// 定義される（PlatformDefs.h）。従来の「_WIN32 || AQ_PLATFORM_UWP」と同じ集合。
#if defined(AQ_PLATFORM_WINDOWS_FAMILY)
#  define SOUND_BACKEND_XAUDIO2
#elif defined(AQ_PLATFORM_APPLE)
// Mac / iOS 共通（AQ_PLATFORM_APPLE = AQ_PLATFORM_MAC | AQ_PLATFORM_IOS）。
// iOS の CoreAudio は既定出力デバイスではなく RemoteIO（AudioUnit）を開き、
// AVAudioSession でカテゴリと中断を扱う点だけが macOS と違う。
// SoftwareMixer::Render を回す構造は共通なので、その差は CoreAudioSoundBackend の
// 中に閉じ込めてバックエンドは 1 つで共有する（設計書/iOS移植設計.md §6）。
#  define SOUND_BACKEND_COREAUDIO
#elif defined(AQ_PLATFORM_ANDROID)
#  define SOUND_BACKEND_AAUDIO
#endif


#if defined(SOUND_BACKEND_XAUDIO2)

#include "XAudio2/XAudio2SoundBackend.h"
namespace aq { namespace sound { using DefaultSoundBackend = XAudio2SoundBackend; } }

#elif defined(SOUND_BACKEND_COREAUDIO)

// Mac / iOS: AudioUnit（macOS は既定の出力デバイス、iOS は RemoteIO）を 1 つ開き、
// レンダーコールバックで SoftwareMixer::Render を回す
// （Mac移植設計 §5 / §9 P4a、iOS移植設計 §6 / §9 P4）。
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
