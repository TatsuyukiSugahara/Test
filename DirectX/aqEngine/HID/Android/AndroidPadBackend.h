#pragma once
// Android 専用。他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_ANDROID)
#include "HID/IPadBackend.h"

namespace aq
{
	namespace hid
	{
		/**
		 * 物理コントローラの入力(設計書/Android移植設計.md の入力節)。
		 *
		 * 実体は `AndroidInputSink` にあり、ここは読み出すだけ。
		 * ボタン/軸の写像と正規化(符号・デッドゾーン)は sink 側に置いてある
		 * (キーコードと軸番号が投入側から素のまま届くため、対応表を通過点に
		 *  持たせると二重に持ち替えることになる)。
		 *
		 * タッチの仮想スティックと束ねて 1 つのパッドに見せるのは
		 * `CompositePadBackend` の役目。こちらは「物理パッドが繋がっていなければ
		 * 未接続」を正直に返す。
		 *
		 * スレッド: `InputManager::Update` から**ゲームスレッド 1 本**で呼ばれる。
		 */
		class AndroidPadBackend : public IPadBackend
		{
		public:
			/**
			 * パッド状態の取得。
			 *
			 * NDK の `AInputEvent` には「どのデバイスの何番目か」を並べる API が無く、
			 * 届くイベントは全パッド分が混ざる。そのため **index 0 だけを実体として扱い**、
			 * 1 以上は未接続を返す(複数パッドが要るゲームではないため。
			 * 必要になったら `AInputEvent_getDeviceId` でスロットを分ける)。
			 */
			void Poll(uint32_t index, PadState& out) override;

			/**
			 * 振動。**no-op**。
			 * NDK には振動の API が無く、`Vibrator` / `VibratorManager` は Java 側にしか
			 * 無いため JNI 経由の呼び出しが要る。NativeActivity のままでも書けるが、
			 * コントローラの振動は JNI + 機種差の検証が必要で、入力取り込みとは別作業。
			 */
			void SetVibration(uint32_t index, float left, float right) override;
		};
	}
}
#endif // AQ_PLATFORM_ANDROID
