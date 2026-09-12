#include "aq.h"
#include "HID/CompositePadBackend.h"


namespace aq
{
	namespace hid
	{
		namespace
		{
			/** 軸の合成に使う絶対値 */
			inline float AxisMagnitude(const float value)
			{
				return value < 0.0f ? -value : value;
			}
		}


		CompositePadBackend::CompositePadBackend()
			: backends_()
		{
		}


		void CompositePadBackend::Add(std::unique_ptr<IPadBackend> backend)
		{
			if (!backend) { return; }

			backends_.push_back(std::move(backend));
		}


		void CompositePadBackend::Poll(uint32_t index, PadState& out)
		{
			out = {};

			for (auto& backend : backends_) {
				PadState state;
				backend->Poll(index, state);
				if (!state.connected) { continue; }

				out.connected = true;

				// ボタンは OR。どちらのデバイスで押しても同じ操作になる。
				for (uint32_t i = 0; i < PadState::BUTTON_COUNT; ++i) {
					out.buttons[i] = out.buttons[i] || state.buttons[i];
				}

				// 軸は絶対値の大きい方。状態を持たないので、物理パッドと仮想パッドを
				// 同時に触ってもどちらが効くか結果から読める。
				for (uint32_t i = 0; i < PadState::AXIS_COUNT; ++i) {
					if (AxisMagnitude(state.axes[i]) > AxisMagnitude(out.axes[i])) {
						out.axes[i] = state.axes[i];
					}
				}
			}
		}


		void CompositePadBackend::SetVibration(uint32_t index, float left, float right)
		{
			// 全バックエンドへ転送し、実体を持つものだけが反応する
			// (仮想パッドのように振動を持たない実装は no-op)。
			for (auto& backend : backends_) {
				backend->SetVibration(index, left, right);
			}
		}


		void CompositePadBackend::SetTriggerResistance(uint32_t index, PadAxis trigger, float startPos, float strength)
		{
			for (auto& backend : backends_) {
				backend->SetTriggerResistance(index, trigger, startPos, strength);
			}
		}
	}
}
