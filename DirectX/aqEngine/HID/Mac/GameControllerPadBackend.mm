#include "aq.h"
// macOS 以外では空 TU。
#if defined(AQ_PLATFORM_MAC)
#include "HID/Mac/GameControllerPadBackend.h"
#import <GameController/GameController.h>


namespace aq
{
	namespace hid
	{
		namespace
		{
			/**
			 * index 番目のコントローラを返す(未接続なら nil)。
			 *
			 * `GCController.controllers` は接続順の配列で、抜き差しで並びが変わる。
			 * Win32 の XInput もスロットの空きを詰めない/詰める挙動が機種で異なるため、
			 * 「index = 現在つながっているコントローラの並び順」という同じ扱いに揃える。
			 */
			GCController* ControllerAt(uint32_t index)
			{
				NSArray<GCController*>* controllers = [GCController controllers];
				if (index >= [controllers count])
				{
					return nil;
				}
				return [controllers objectAtIndex:index];
			}


			/** 押されていれば true(ボタンが無いプロファイルでは nil が来るので false) */
			bool IsPressed(GCControllerButtonInput* button)
			{
				return button != nil && [button isPressed];
			}


			/** トリガーの値 [0, 1](無ければ 0) */
			float TriggerValue(GCControllerButtonInput* button)
			{
				return button != nil ? [button value] : 0.0f;
			}
		}


		void GameControllerPadBackend::Poll(uint32_t index, PadState& out)
		{
			out = {};

			@autoreleasepool
			{
				GCController* controller = ControllerAt(index);
				if (controller == nil)
				{
					return;
				}

				// extendedGamepad は「両スティック + 十字 + ABXY + LB/RB + LT/RT」を持つ標準形。
				// これを持たないコントローラ(古いリモコン等)は未接続として扱う。
				GCExtendedGamepad* pad = [controller extendedGamepad];
				if (pad == nil)
				{
					return;
				}

				out.connected = true;

				out.buttons[static_cast<uint32_t>(PadButton::A)]  = IsPressed([pad buttonA]);
				out.buttons[static_cast<uint32_t>(PadButton::B)]  = IsPressed([pad buttonB]);
				out.buttons[static_cast<uint32_t>(PadButton::X)]  = IsPressed([pad buttonX]);
				out.buttons[static_cast<uint32_t>(PadButton::Y)]  = IsPressed([pad buttonY]);

				out.buttons[static_cast<uint32_t>(PadButton::LB)] = IsPressed([pad leftShoulder]);
				out.buttons[static_cast<uint32_t>(PadButton::RB)] = IsPressed([pad rightShoulder]);
				out.buttons[static_cast<uint32_t>(PadButton::LT)] = IsPressed([pad leftTrigger]);
				out.buttons[static_cast<uint32_t>(PadButton::RT)] = IsPressed([pad rightTrigger]);

				GCControllerDirectionPad* dpad = [pad dpad];
				out.buttons[static_cast<uint32_t>(PadButton::DUp)]    = IsPressed([dpad up]);
				out.buttons[static_cast<uint32_t>(PadButton::DDown)]  = IsPressed([dpad down]);
				out.buttons[static_cast<uint32_t>(PadButton::DLeft)]  = IsPressed([dpad left]);
				out.buttons[static_cast<uint32_t>(PadButton::DRight)] = IsPressed([dpad right]);

				// Start / Back は初期の GCExtendedGamepad には無く、後から追加された任意プロパティ。
				// 無い機種では nil が返るので IsPressed が false を返す。
				out.buttons[static_cast<uint32_t>(PadButton::Start)] = IsPressed([pad buttonMenu]);
				out.buttons[static_cast<uint32_t>(PadButton::Back)]  = IsPressed([pad buttonOptions]);

				out.buttons[static_cast<uint32_t>(PadButton::LStick)] = IsPressed([pad leftThumbstickButton]);
				out.buttons[static_cast<uint32_t>(PadButton::RStick)] = IsPressed([pad rightThumbstickButton]);

				// スティックは GameController が [-1, 1] に正規化済み。Y は上が正で
				// XInput と同じ向きなので、そのまま渡してよい。
				GCControllerDirectionPad* leftStick  = [pad leftThumbstick];
				GCControllerDirectionPad* rightStick = [pad rightThumbstick];
				out.axes[static_cast<uint32_t>(PadAxis::LX)] = [[leftStick  xAxis] value];
				out.axes[static_cast<uint32_t>(PadAxis::LY)] = [[leftStick  yAxis] value];
				out.axes[static_cast<uint32_t>(PadAxis::RX)] = [[rightStick xAxis] value];
				out.axes[static_cast<uint32_t>(PadAxis::RY)] = [[rightStick yAxis] value];

				out.axes[static_cast<uint32_t>(PadAxis::LTrigger)] = TriggerValue([pad leftTrigger]);
				out.axes[static_cast<uint32_t>(PadAxis::RTrigger)] = TriggerValue([pad rightTrigger]);
			}
		}


		void GameControllerPadBackend::SetVibration(uint32_t /*index*/, float /*left*/, float /*right*/)
		{
			// TODO(P4): GCDeviceHaptics + CHHapticEngine で実装する(設計書 §9 P2.5 / P4)。
		}


		void GameControllerPadBackend::SetTriggerResistance(uint32_t index, PadAxis trigger,
		                                                    float startPos, float strength)
		{
			if (trigger != PadAxis::LTrigger && trigger != PadAxis::RTrigger)
			{
				return;
			}

			@autoreleasepool
			{
				GCController* controller = ControllerAt(index);
				if (controller == nil)
				{
					return;
				}

				// アダプティブトリガーを持つのは DualSense だけ。それ以外は何もしない。
				GCPhysicalInputProfile* profile = [controller physicalInputProfile];
				if (![profile isKindOfClass:[GCDualSenseGamepad class]])
				{
					return;
				}
				GCDualSenseGamepad* dualSense = static_cast<GCDualSenseGamepad*>(profile);

				GCDualSenseAdaptiveTrigger* adaptive = (trigger == PadAxis::LTrigger)
					? [dualSense leftTrigger]
					: [dualSense rightTrigger];
				if (adaptive == nil)
				{
					return;
				}

				// strength = 0 は「解除」。IPadBackend のコメントに合わせる。
				if (strength <= 0.0f)
				{
					[adaptive setModeOff];
					return;
				}
				[adaptive setModeFeedbackWithStartPosition:startPos resistiveStrength:strength];
			}
		}
	}
}
#endif // AQ_PLATFORM_MAC
