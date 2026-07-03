#pragma once
// DirectInput / XInput はデスクトップ専用。UWP(Xbox 道A)では使えないため、
// キーボード/マウスは当面 no-op(入力は Phase 4 の GameInput で対応)。
#if !defined(AQ_PLATFORM_UWP)
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <Xinput.h>
#endif
#include <memory>
#include <chrono>
#include "Math/Vector.h"
#include "HID/IPadBackend.h"   // PadButton / PadAxis / PadState / IPadBackend

namespace aq
{
	namespace hid
	{
		// ==========================================
		// Keyboard
		// ==========================================

#if !defined(AQ_PLATFORM_UWP)
		enum class KeyBoardType : uint32_t
		{
			Left  = DIK_LEFT,
			Right = DIK_RIGHT,
			Up    = DIK_UP,
			Down  = DIK_DOWN,

			W     = DIK_W,
			A     = DIK_A,
			S     = DIK_S,
			D     = DIK_D,

			Space  = DIK_SPACE,
			Enter  = DIK_RETURN,
			Escape = DIK_ESCAPE,
		};
#else
		// UWP: DIK_ が無いため中立値。now_[256] のインデックスとして安全なら値は任意。
		enum class KeyBoardType : uint32_t
		{
			Left, Right, Up, Down,
			W, A, S, D,
			Space, Enter, Escape,
		};
#endif


		class KeyBoard
		{
		public:
			KeyBoard()  = default;
			~KeyBoard();

#if !defined(AQ_PLATFORM_UWP)
			HRESULT Initialize(LPDIRECTINPUT8 input);
#endif
			void    Update(float dt);

			bool IsTriggered(KeyBoardType key) const;
			bool IsPressed  (KeyBoardType key) const;
			bool IsReleased (KeyBoardType key) const;
			bool IsLongPress(KeyBoardType key, float thresholdSec = 0.5f) const;

		private:
			static constexpr uint32_t KEY_COUNT = 256;

#if !defined(AQ_PLATFORM_UWP)
			LPDIRECTINPUTDEVICE8 device_ = nullptr;
#endif
			uint8_t              now_[KEY_COUNT]{};
			uint8_t              old_[KEY_COUNT]{};
			float                holdTimers_[KEY_COUNT]{};
		};


		// ==========================================
		// Mouse
		// ==========================================

		enum class MouseButton : uint8_t
		{
			Left, Right, Middle,
		};

		enum class MouseAxis : uint8_t
		{
			DeltaX, DeltaY, WheelDelta,
		};


#if defined(AQ_PLATFORM_UWP)
		// UWP: DIMOUSESTATE2 が無いため、既存の判定コードが参照するフィールドだけ持つ
		// 中立状態(全ゼロ=入力なし)。フィールド名は DIMOUSESTATE2 に合わせる。
		struct MouseStateNeutral
		{
			long    lX = 0;
			long    lY = 0;
			long    lZ = 0;
			uint8_t rgbButtons[8]{};
		};
#endif

		class Mouse
		{
		public:
			Mouse()  = default;
			~Mouse();

#if !defined(AQ_PLATFORM_UWP)
			HRESULT Initialize(LPDIRECTINPUT8 input);
#endif
			void    Update(float dt);

			bool          IsTriggered(MouseButton btn) const;
			bool          IsPressed  (MouseButton btn) const;
			bool          IsReleased (MouseButton btn) const;
			bool          IsLongPress(MouseButton btn, float thresholdSec = 0.5f) const;
			float         GetAxis    (MouseAxis axis) const;
			math::Vector2 GetCursorPos() const;

		private:
#if !defined(AQ_PLATFORM_UWP)
			LPDIRECTINPUTDEVICE8 device_ = nullptr;
			DIMOUSESTATE2        now_{};
			DIMOUSESTATE2        old_{};
#else
			MouseStateNeutral    now_{};
			MouseStateNeutral    old_{};
#endif
			float                holdTimers_[3]{};
		};


		// ==========================================
		// Pad (最大 4 個対応)
		// 生のデバイス取得・振動は IPadBackend に委譲し、本体は正規化状態(PadState)に対する
		// トリガー/長押し等の判定だけを持つ(プラットフォーム非依存)。
		// PadButton / PadAxis / PadState は IPadBackend.h で定義。
		// ==========================================

		class Pad
		{
		public:
			Pad() = default;

			void SetIndex  (uint32_t index)      { index_ = index; }
			void SetBackend(IPadBackend* backend) { backend_ = backend; }
			void Update(float dt);

			bool  IsConnected() const { return now_.connected; }
			bool  IsTriggered(PadButton btn) const;
			bool  IsPressed  (PadButton btn) const;
			bool  IsReleased (PadButton btn) const;
			bool  IsLongPress(PadButton btn, float thresholdSec = 0.5f) const;
			float GetAxis    (PadAxis axis) const;

			// left: 低周波モーター (重い振動)  right: 高周波モーター (細かい振動)  各 [0, 1]
			void Vibrate     (float left, float right);
			void StopVibration() { Vibrate(0.0f, 0.0f); }

		private:
			static constexpr uint32_t BTN_COUNT = PadState::BUTTON_COUNT;

			IPadBackend* backend_ = nullptr;
			PadState     now_{};
			PadState     old_{};
			float        holdTimers_[BTN_COUNT]{};
			uint32_t     index_   = 0;
		};


		// ==========================================
		// InputManager
		// ==========================================

		constexpr uint32_t MAX_PAD_COUNT = 4;


		class InputManager
		{
		public:
			InputManager();
			~InputManager();

			HRESULT   Setup();
			void      Update();

			KeyBoard* GetKeyBoardPtr() { return keyBoard_.get(); }
			Mouse*    GetMousePtr()    { return mouse_.get(); }
			Pad*      GetPad(uint32_t index);
			void      Vibrate      (uint32_t padIndex, float left, float right);
			void      StopVibration(uint32_t padIndex);

			/** ImGui がキーボード入力を使用中は true に設定する。wrapper 関数が false/0 を返す。 */
			void SuppressKeyboard(bool suppress) { suppressKeyboard_ = suppress; }
			/** ImGui がマウス入力を使用中は true に設定する。wrapper 関数が false/0 を返す。 */
			void SuppressMouse   (bool suppress) { suppressMouse_    = suppress; }
			bool IsKeyboardSuppressed() const    { return suppressKeyboard_; }
			bool IsMouseSuppressed()    const    { return suppressMouse_;    }

			static void          Initialize() { if (!sInstance_) sInstance_ = new InputManager(); }
			static InputManager& Get()        { return *sInstance_; }
			static void          Finalize()   { if (sInstance_) { delete sInstance_; sInstance_ = nullptr; } }

		private:
#if !defined(AQ_PLATFORM_UWP)
			LPDIRECTINPUT8               input_ = nullptr;
#endif
			std::unique_ptr<KeyBoard>    keyBoard_;
			std::unique_ptr<Mouse>       mouse_;
			std::unique_ptr<IPadBackend> padBackend_;
			Pad                          pads_[MAX_PAD_COUNT];

			using Clock = std::chrono::high_resolution_clock;
			Clock::time_point lastTime_;

			bool suppressKeyboard_ = false;
			bool suppressMouse_    = false;

			static InputManager* sInstance_;
		};


		// ==========================================
		// Free function wrappers
		// ==========================================

		bool IsKeyTriggered(KeyBoardType key);
		bool IsKeyPressed  (KeyBoardType key);
		bool IsKeyReleased (KeyBoardType key);
		bool IsKeyLongPress(KeyBoardType key, float thresholdSec = 0.5f);

		bool          IsMouseTriggered(MouseButton btn);
		bool          IsMousePressed  (MouseButton btn);
		bool          IsMouseReleased (MouseButton btn);
		bool          IsMouseLongPress(MouseButton btn, float thresholdSec = 0.5f);
		float         GetMouseAxis    (MouseAxis axis);
		math::Vector2 GetMouseCursorPos();

		bool  IsPadTriggered(uint32_t padIndex, PadButton btn);
		bool  IsPadPressed  (uint32_t padIndex, PadButton btn);
		bool  IsPadReleased (uint32_t padIndex, PadButton btn);
		bool  IsPadLongPress(uint32_t padIndex, PadButton btn, float thresholdSec = 0.5f);
		float GetPadAxis    (uint32_t padIndex, PadAxis axis);
		bool  IsPadConnected(uint32_t padIndex);
	}
}
