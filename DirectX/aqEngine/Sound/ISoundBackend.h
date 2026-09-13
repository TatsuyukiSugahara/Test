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

			// バックエンドのポンプ（コールバック駆動のバックエンドでも、
			// 回収キューの掃除やストリーム再構築はここで行う）。
			virtual void Update() {}

			// アプリがバックグラウンドへ回った / 戻ってきた（§8.3）。
			// 出力デバイスを持ち続けるプラットフォームでは何もしなくてよいので既定は no-op。
			// Android は窓を失う＝背面なので、ここで出力ストリームを止める。
			virtual void OnSuspend() {}
			virtual void OnResume()  {}
		};
	}
}
