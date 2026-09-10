#pragma once
// XInput / HID 直読みは Win32 デスクトップ専用。UWP は WinRTGamepadBackend が担当する。
#if defined(AQ_PLATFORM_WIN32)
#include "HID/XInputPadBackend.h"
#include "HID/DualSensePadBackend.h"

namespace aq
{
	namespace hid
	{
		/**
		 * Win32 のパッドバックエンド(XInput + DualSense の合成)
		 * スロットは XInput 優先で埋まり、XInput が未接続のスロットには
		 * 同じ番号の DualSense を割り当てる。振動 / トリガー抵抗は、
		 * 直近の Poll でそのスロットを担当したバックエンドへ転送する。
		 */
		class Win32PadBackend : public IPadBackend
		{
		private:
			static constexpr uint32_t SLOT_COUNT = 4;

			/** スロットを担当しているバックエンド(直近の Poll 結果) */
			enum class Owner : uint8_t
			{
				None,
				XInput,
				DualSense,
			};

			XInputPadBackend    xinput_;
			DualSensePadBackend dualSense_;
			Owner               owners_[SLOT_COUNT]{};


		public:
			void Poll                (uint32_t index, PadState& out) override;
			void SetVibration        (uint32_t index, float left, float right) override;
			void SetTriggerResistance(uint32_t index, PadAxis trigger, float startPos, float strength) override;
		};
	}
}
#endif // AQ_PLATFORM_WIN32
