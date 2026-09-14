#pragma once
// Apple 共通(macOS / iOS)。他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_APPLE)
#include "HID/IPadBackend.h"

namespace aq
{
	namespace hid
	{
		/**
		 * GameController.framework によるパッド入力(設計書/Mac移植設計.md §3.2)。
		 *
		 * GameController.framework は macOS と iOS で同一 API なので、本実装は
		 * **Apple 共通**として `HID/Apple/` に置く(設計書/iOS移植設計.md §5.1)。
		 * iOS では `CompositePadBackend` の「物理側」として使い、仮想パッドと合成する。
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
#endif // AQ_PLATFORM_APPLE
