#pragma once
// macOS 専用。他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_MAC)
#include "HID/IPadBackend.h"

namespace aq
{
	namespace hid
	{
		/**
		 * GameController.framework によるパッド入力(設計書/Mac移植設計.md §3.2)。
		 *
		 * `GCController.controllers` の並び順を index に対応させ、`extendedGamepad` の値を
		 * `PadState` へ正規化する。Xbox / DualShock / DualSense / Joy-Con はいずれも
		 * OS 側がこのプロファイルへ正規化してくれるので、Win32 のような HID 直読み
		 * (`DualSensePadBackend`)は要らない。
		 *
		 * スレッド: `InputManager::Update` から**ゲームスレッド 1 本**で呼ばれる。
		 */
		class GameControllerPadBackend : public IPadBackend
		{
		public:
			void Poll(uint32_t index, PadState& out) override;

			/**
			 * 振動。**P2.5 では no-op**。
			 * `GCDeviceHaptics` + `CHHapticEngine` はエンジンの生成/停止の寿命管理が要り、
			 * `CoreHaptics.framework` のリンク追加も伴うため P4 で実装する(設計書 §9 P2.5)。
			 */
			void SetVibration(uint32_t index, float left, float right) override;

			/** アダプティブトリガー。DualSense のみ効く(それ以外は無視される) */
			void SetTriggerResistance(uint32_t index, PadAxis trigger,
			                          float startPos, float strength) override;
		};
	}
}
#endif // AQ_PLATFORM_MAC
