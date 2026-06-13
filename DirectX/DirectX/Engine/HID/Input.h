#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <Xinput.h>
#include <memory>
#include <chrono>
#include "../Math/Vector.h"

namespace engine
{
	namespace hid
	{
		// ==========================================
		// Keyboard
		// ==========================================

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

			Space = DIK_SPACE,
		};


		class KeyBoard
		{
		public:
			KeyBoard()  = default;
			~KeyBoard();

			HRESULT Initialize(LPDIRECTINPUT8 input);
			void    Update(float dt);

			bool IsTriggered(KeyBoardType key) const;
			bool IsPressed  (KeyBoardType key) const;
			bool IsReleased (KeyBoardType key) const;
			bool IsLongPress(KeyBoardType key, float thresholdSec = 0.5f) const;

		private:
			static constexpr uint32_t KEY_COUNT = 256;

			LPDIRECTINPUTDEVICE8 device_ = nullptr;
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


		class Mouse
		{
		public:
			Mouse()  = default;
			~Mouse();

			HRESULT Initialize(LPDIRECTINPUT8 input);
			void    Update(float dt);

			bool          IsTriggered(MouseButton btn) const;
			bool          IsPressed  (MouseButton btn) const;
			bool          IsReleased (MouseButton btn) const;
			bool          IsLongPress(MouseButton btn, float thresholdSec = 0.5f) const;
			float         GetAxis    (MouseAxis axis) const;
			math::Vector2 GetCursorPos() const;

		private:
			LPDIRECTINPUTDEVICE8 device_ = nullptr;
			DIMOUSESTATE2        now_{};
			DIMOUSESTATE2        old_{};
			float                holdTimers_[3]{};
		};


		// ==========================================
		// Pad (XInput, 最大 4 個対応)
		// ==========================================

		enum class PadButton : uint8_t
		{
			A, B, X, Y,
			LB, RB, LT, RT,
			DUp, DDown, DLeft, DRight,
			Start, Back,
			LStick, RStick,
			Max,
		};

		enum class PadAxis : uint8_t
		{
			LX, LY, RX, RY, LTrigger, RTrigger,
		};


		class Pad
		{
		public:
			Pad() = default;

			void SetIndex(uint32_t index) { index_ = index; }
			void Update(float dt);

			bool  IsConnected() const { return connected_; }
			bool  IsTriggered(PadButton btn) const;
			bool  IsPressed  (PadButton btn) const;
			bool  IsReleased (PadButton btn) const;
			bool  IsLongPress(PadButton btn, float thresholdSec = 0.5f) const;
			float GetAxis    (PadAxis axis) const;

			// left: 低周波モーター (重い振動)  right: 高周波モーター (細かい振動)  各 [0, 1]
			void Vibrate     (float left, float right);
			void StopVibration() { Vibrate(0.0f, 0.0f); }

		private:
			bool  GetButtonState(const XINPUT_STATE& state, PadButton btn) const;
			float NormalizeAxis (SHORT value, SHORT deadZone) const;

		private:
			static constexpr uint32_t BTN_COUNT = static_cast<uint32_t>(PadButton::Max);

			XINPUT_STATE now_{};
			XINPUT_STATE old_{};
			float        holdTimers_[BTN_COUNT]{};
			bool         connected_ = false;
			uint32_t     index_     = 0;
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

			static void          Initialize() { if (!sInstance_) sInstance_ = new InputManager(); }
			static InputManager& Get()        { return *sInstance_; }
			static void          Finalize()   { if (sInstance_) { delete sInstance_; sInstance_ = nullptr; } }

		private:
			LPDIRECTINPUT8            input_ = nullptr;
			std::unique_ptr<KeyBoard> keyBoard_;
			std::unique_ptr<Mouse>    mouse_;
			Pad                       pads_[MAX_PAD_COUNT];

			using Clock = std::chrono::high_resolution_clock;
			Clock::time_point lastTime_;

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
