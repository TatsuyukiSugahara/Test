#pragma once
#include <cstdint>
#include "Math/Vector.h"


namespace aq
{
	namespace audio
	{
		using PlayingId = uint32_t;

		// ゲーム向けの薄い公開 API（§10）。内部で AudioDirector に委譲する。
		// 名前は実行時に CRC32 化される（編集時=名前、実行時=ID）。
		// フェーズ1: LoadBank / PostEvent / StopPlaying / StopAllByKind。

		void LoadBank(const char* path);

		PlayingId PostEvent(const char* eventName, uint64_t gameObject = 0);

		void StopPlaying(PlayingId id, float fadeSeconds = 0.0f);
		void StopAllByKind(const char* kindName, float fadeSeconds = 0.0f);

		// Switch 値を設定（gameObject=0 でグローバル）。例: SetSwitch("Surface","Wood",go)
		void SetSwitch(const char* groupName, const char* valueName, uint64_t gameObject = 0);
		// RTPC 値を設定（gameObject=0 でグローバル）。例: SetRTPC("PlayerSpeed", 4.0f, go)
		void SetRTPC(const char* paramName, float value, uint64_t gameObject = 0);
		// State 値を設定（グローバル）。対応する stateRules が実行される。例: SetState("Music","Combat")
		void SetState(const char* groupName, const char* valueName);

		// 3D GameObject（§11）。位置/速度を登録すると 3D イベントが追従する。
		void RegisterGameObject(uint64_t gameObject);
		void UnregisterGameObject(uint64_t gameObject);
		void SetGameObjectTransform(uint64_t gameObject,
		                            const math::Vector3& position, const math::Vector3& forward,
		                            const math::Vector3& up, const math::Vector3& velocity);
		// リスナー（カメラ）。
		void SetListener(const math::Vector3& position, const math::Vector3& forward,
		                 const math::Vector3& up, const math::Vector3& velocity);
	}
}
