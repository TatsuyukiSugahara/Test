#pragma once
#include <cstdint>

namespace aq
{
	namespace hid
	{
		// ゲームパッドのボタン。値はバックエンド非依存(配列インデックスとして使う)。
		enum class PadButton : uint8_t
		{
			A, B, X, Y,
			LB, RB, LT, RT,
			DUp, DDown, DLeft, DRight,
			Start, Back,
			LStick, RStick,
			Max,
		};

		// ゲームパッドの軸。スティックは [-1, 1]、トリガーは [0, 1] に正規化される。
		enum class PadAxis : uint8_t
		{
			LX, LY, RX, RY, LTrigger, RTrigger,
			Max,
		};

		// 正規化済みのパッド状態。バックエンド(XInput / GameInput 等)非依存。
		struct PadState
		{
			static constexpr uint32_t BUTTON_COUNT = static_cast<uint32_t>(PadButton::Max);
			static constexpr uint32_t AXIS_COUNT   = static_cast<uint32_t>(PadAxis::Max);

			bool  connected = false;
			bool  buttons[BUTTON_COUNT] = {};   // PadButton 順
			float axes[AXIS_COUNT]      = {};   // PadAxis 順 (スティック [-1,1] / トリガー [0,1])
		};

		// パッド入力のプラットフォーム抽象。
		// 生のデバイス取得・振動だけを隠蔽し、トリガー/長押し等の判定ロジックは Pad 側に残す。
		// 実装: XInputPadBackend(Win32 / 道A の回帰確認用)、
		//       将来 GameInputPadBackend(UWP, Phase 4)。
		class IPadBackend
		{
		public:
			virtual ~IPadBackend() = default;

			// index のパッドをポーリングし、正規化状態を out に書き込む(未接続なら connected=false)。
			virtual void Poll(uint32_t index, PadState& out) = 0;

			// 振動。left=低周波(重い) right=高周波(細かい)。各 [0, 1]。
			virtual void SetVibration(uint32_t index, float left, float right) = 0;

			// アダプティブトリガー(L2 / R2)の抵抗。trigger は PadAxis::LTrigger / RTrigger のみ有効。
			// startPos: 抵抗が効き始めるトリガー位置 [0, 1]。strength: 抵抗の強さ [0, 1]。
			// strength = 0 は「解除」(抵抗なし)を意味する。
			// 既定は no-op。ハードが対応しないバックエンド(XInput / WinRT)は実装しない。
			virtual void SetTriggerResistance(uint32_t /*index*/, PadAxis /*trigger*/,
			                                  float /*startPos*/, float /*strength*/) {}
		};
	}
}
