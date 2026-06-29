#pragma once
#include <cstdint>
#include <memory>
#include "SoundTypes.h"
#include "SoundFwd.h"
#include "ISoundVoice.h"


namespace aq
{
	namespace sound
	{
		// Graphics API Implementor と同じ立ち位置の Bridge implementor（§3.2）。
		// SoundEngine はこのインターフェース越しにのみ XAudio2 / Oboe と話す。
		class ISoundBackend
		{
		public:
			virtual ~ISoundBackend() = default;

			virtual bool Initialize() = 0;
			virtual void Finalize()   = 0;

			// ボイス工場。呼び出し元は実装型を知らない。
			virtual std::unique_ptr<ISoundVoice> CreateVoice(const SoundFormat& format, SoundBusId bus) = 0;

			virtual void SetBusVolume(SoundBusId bus, float volume) = 0;
			virtual void SetMasterVolume(float volume) = 0;

			// デバイス出力クロック（§3.4）。A/V 同期の latency anchor。
			virtual SoundClock GetOutputClock() const = 0;

			// バックエンドのポンプ（Oboe は基本コールバック駆動なので空実装でよい）。
			virtual void Update() {}
		};
	}
}
