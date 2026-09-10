#pragma once
#include <memory>
#include <chrono>
#include <atomic>
#include "Math/Vector.h"
#include "Graphics/GraphicsTypes.h"    // NativeWindowHandle
#include "HID/IKeyboardBackend.h"      // KeyBoardType / KeyboardState / IKeyboardBackend
#include "HID/IMouseBackend.h"         // MouseState / IMouseBackend
#include "HID/IPadBackend.h"           // PadButton / PadAxis / PadState / IPadBackend

namespace aq
{
	namespace hid
	{
		// ==========================================
		// Keyboard
		// 生のデバイス取得は IKeyboardBackend に委譲し、本体は中立状態(KeyboardState)に対する
		// トリガー/長押し等の判定だけを持つ(プラットフォーム非依存)。
		// KeyBoardType / KeyboardState は IKeyboardBackend.h で定義。
		// ==========================================

		class KeyBoard
		{
		public:
			KeyBoard()  = default;
			~KeyBoard() = default;

			void SetBackend(IKeyboardBackend* backend) { backend_ = backend; }
			void Update(float dt);

			bool IsTriggered(KeyBoardType key) const;
			bool IsPressed  (KeyBoardType key) const;
			bool IsReleased (KeyBoardType key) const;
			bool IsLongPress(KeyBoardType key, float thresholdSec = 0.5f) const;

		private:
			static constexpr uint32_t KEY_COUNT = KeyboardState::KEY_COUNT;

			IKeyboardBackend* backend_ = nullptr;
			KeyboardState     now_{};
			KeyboardState     old_{};
			float             holdTimers_[KEY_COUNT]{};
		};


		// ==========================================
		// Mouse
		// 生のデバイス取得は IMouseBackend に委譲し、本体は中立状態(MouseState)に対する
		// トリガー/長押し等の判定だけを持つ(プラットフォーム非依存)。
		// MouseState は IMouseBackend.h で定義。
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
			~Mouse() = default;

			void SetBackend(IMouseBackend* backend) { backend_ = backend; }
			void Update(float dt);

			bool          IsTriggered(MouseButton btn) const;
			bool          IsPressed  (MouseButton btn) const;
			bool          IsReleased (MouseButton btn) const;
			bool          IsLongPress(MouseButton btn, float thresholdSec = 0.5f) const;
			float         GetAxis    (MouseAxis axis) const;
			math::Vector2 GetCursorPos() const;

		private:
			IMouseBackend* backend_ = nullptr;
			MouseState     now_{};
			MouseState     old_{};
			float          holdTimers_[3]{};
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
			// バックエンドを直接叩く旧 API。互換のために残すが、新規の呼び出しは Rumble を使う。
			void Vibrate     (float left, float right);
			void StopVibration() { Vibrate(0.0f, 0.0f); }

			// ---- 出力バッファ API (ワーカースレッドから呼んでよい) ----
			// ゲーム側のシステムはワーカースレッドで走るため、ここでは値を積むだけにして、
			// バックエンドへの送信はメインスレッドの Update がフレームに 1 回まとめて行う。

			// 振動。durationSec 経過で自動停止する (0 以下なら次の指定まで持続)。
			void Rumble(float left, float right, float durationSec);

			// アダプティブトリガー(L2 / R2)の抵抗。strength = 0 で解除。
			void SetTriggerResistance(PadAxis trigger, float startPos, float strength);

		private:
			// 出力バッファをバックエンドへ適用する (メインスレッド専用)。
			void ApplyOutput(float dt);

		private:
			static constexpr uint32_t BTN_COUNT     = PadState::BUTTON_COUNT;
			static constexpr uint32_t TRIGGER_COUNT = 2;   // 0 = L2 / 1 = R2

			IPadBackend* backend_ = nullptr;
			PadState     now_{};
			PadState     old_{};
			float        holdTimers_[BTN_COUNT]{};
			uint32_t     index_   = 0;

			// 出力バッファ。書き手はワーカースレッド、読み手はメインスレッドの Update だけなので、
			// 値ごとの atomic で足りる (取り違えても 1 フレーム分ずれるだけで破綻しない)。
			// 複数値の整合を厳密に取る必要は無く、ロックは掛けない。
			std::atomic<float> rumbleLeft_      { 0.0f };
			std::atomic<float> rumbleRight_     { 0.0f };
			std::atomic<float> rumbleRemainSec_ { 0.0f };
			std::atomic<float> triggerStartPos_[TRIGGER_COUNT]{};
			std::atomic<float> triggerStrength_[TRIGGER_COUNT]{};

			// 直近にバックエンドへ送った値。差分が出たときだけ送る。
			float appliedRumbleLeft_  = 0.0f;
			float appliedRumbleRight_ = 0.0f;
			float appliedStartPos_[TRIGGER_COUNT]{};
			float appliedStrength_[TRIGGER_COUNT]{};
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

			/** キーボード / マウスのバックエンドを生成して初期化する。成功で true */
			bool      Setup();
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
			// バックエンドは KeyBoard / Mouse / Pad より先に宣言する。
			// これらが生ポインタで参照するため、破棄はバックエンドが後になる必要がある。
			std::unique_ptr<IKeyboardBackend> keyboardBackend_;
			std::unique_ptr<IMouseBackend>    mouseBackend_;
			std::unique_ptr<IPadBackend>      padBackend_;

			std::unique_ptr<KeyBoard>         keyBoard_;
			std::unique_ptr<Mouse>            mouse_;
			Pad                               pads_[MAX_PAD_COUNT];

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
