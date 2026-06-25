#pragma once
#include "HID/ActionInput.h"
#include "GameAction.h"

namespace app
{
	class GameInput
	{
	public:
		static void       Initialize();
		static GameInput& Get() { return *sInstance_; }
		static void       Finalize();

		bool IsTriggered(GameAction action) const                        { return vpad_.IsTriggered(action);          }
		bool IsPressed  (GameAction action) const                        { return vpad_.IsPressed(action);            }
		bool IsReleased (GameAction action) const                        { return vpad_.IsReleased(action);           }
		bool IsLongPress(GameAction action, float threshold = 0.5f) const { return vpad_.IsLongPress(action, threshold); }
		aq::math::Vector2 GetStick(GameAction action) const          { return vpad_.GetStick(action);               }

		// left: 低周波モーター (重い振動)  right: 高周波モーター (細かい振動)  各 [0, 1]
		void Vibrate     (float left, float right, uint32_t padIndex = 0) { aq::hid::InputManager::Get().Vibrate(padIndex, left, right); }
		void StopVibration(uint32_t padIndex = 0)                         { aq::hid::InputManager::Get().StopVibration(padIndex);        }

		void UseKeyboardMap() { vpad_.SetActionMap(&keyboardMap_); }
		void UseGamepadMap () { vpad_.SetActionMap(&gamepadMap_);  }

	private:
		GameInput();
		void SetupMaps();

	private:
		aq::hid::ActionMap              keyboardMap_;
		aq::hid::ActionMap              gamepadMap_;
		aq::hid::ActionInput<GameAction> vpad_;
		static GameInput*                   sInstance_;
	};
}
