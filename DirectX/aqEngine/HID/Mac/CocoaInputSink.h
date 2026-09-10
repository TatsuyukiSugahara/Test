#pragma once
// macOS 専用。他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_MAC)
#include "HID/IKeyboardBackend.h"
#include "HID/IMouseBackend.h"

namespace aq
{
	namespace hid
	{
		/**
		 * Cocoa のイベントを受けて、キーボード/マウスの現在状態を溜めておくバッファ。
		 *
		 * 投入するのは `PlatformMac::PumpEvents`、取り出すのは `CocoaKeyboardBackend` /
		 * `CocoaMouseBackend`。**どちらも Mac 専用コード**なので、この型は `IPlatform` にも
		 * `IKeyboardBackend` にも露出させない(設計書/Mac移植設計.md §3.2)。
		 *
		 * Cocoa 依存の値(NSEvent / NSView 座標系)は投入側で解決済みのものを受け取る。
		 * 本ヘッダと実装に Objective-C は現れないため、ファイルは `.cpp`。
		 *
		 * スレッド: 投入も取得も**ゲームスレッド 1 本**から呼ばれる
		 * (`Engine::RunGame` が `PumpEvents` → `Update` → `InputManager::Update` の順に回す)。
		 * そのためロックを持たない。別スレッドから触らないこと。
		 */
		class CocoaInputSink
		{
		public:
			static CocoaInputSink& Get();


			// ------------------------------------------------------------------
			//  投入側 (PlatformMac::PumpEvents)
			// ------------------------------------------------------------------

			/** キー押下/解放。macKeyCode は NSEvent.keyCode(Carbon 仮想キーコード) */
			void OnKey(uint16_t macKeyCode, bool pressed);

			/**
			 * 修飾キーの変化(NSEvent.modifierFlags)。
			 * Command を押している間 macOS は keyUp を配送しないため、Command が離された
			 * タイミングで押下状態を一掃して「押しっぱなし」を防ぐ。
			 */
			void OnModifierFlagsChanged(bool commandPressed);

			/** マウスボタン。buttonNumber は NSEvent.buttonNumber(0=左 / 1=右 / 2=中) */
			void OnMouseButton(int32_t buttonNumber, bool pressed);

			/**
			 * マウス移動。client は**ビュー左上原点**へ変換済みの座標、delta は NSEvent の相対量。
			 * 座標変換を投入側に寄せているのは、NSView を持っているのが `PlatformMac` だけのため。
			 */
			void OnMouseMove(float clientX, float clientY, float deltaX, float deltaY);

			/** ホイール。delta は NSEvent.scrollingDeltaY(上方向が正) */
			void OnScrollWheel(float delta);

			/** ウィンドウが非アクティブになった。押下状態を落として持ち越さない */
			void OnFocusLost();


			// ------------------------------------------------------------------
			//  取得側 (CocoaKeyboardBackend / CocoaMouseBackend)
			// ------------------------------------------------------------------

			/**
			 * キー状態を書き出す。
			 * 現在押されているものに加え、**前回の取得以降に一度でも押されたキー**も
			 * 押下として返す(下記 pressedSinceFetch_ の説明を参照)。
			 */
			void FetchKeyboard(KeyboardState& out);

			/** マウス状態を書き出す。相対量(dx/dy/wheel)は取り出した分だけ消費する */
			void FetchMouse(MouseState& out);


		private:
			CocoaInputSink() = default;

			/** KeyBoardType 順のキー状態(押下 = 0x80)。現在の実レベル */
			KeyboardState keyboard_{};

			/**
			 * 前回の Fetch 以降に押下イベントが来たか(キー / マウスボタン)。
			 *
			 * DirectInput はデバイスの**現在状態**をサンプリングするが、Cocoa は離散イベントで
			 * 届く。1 フレームが長引いた場合(ロード中のヒッチなど)、そのフレームの中で
			 * 押して離すところまで進んでしまい、レベルだけ見ていると押下を取りこぼす。
			 * 「取得までに一度でも押された」ことを憶えておき、その回では押下として返す。
			 * 次の取得では実レベル(離されていれば 0)に戻るので、トリガー判定が 1 回成立する。
			 */
			uint8_t pressedSinceFetch_[KeyboardState::KEY_COUNT]{};

			uint8_t mouseButtons_[MouseState::BUTTON_COUNT]{};
			uint8_t mousePressedSinceFetch_[MouseState::BUTTON_COUNT]{};

			/** ビュー左上原点のカーソル位置 */
			float cursorX_ = 0.0f;
			float cursorY_ = 0.0f;

			/**
			 * 相対量の溜め。Fetch で整数部だけを渡し、小数部は次フレームへ持ち越す
			 * (毎フレーム切り捨てると細かい動きが消えるため)。
			 */
			float pendingDeltaX_ = 0.0f;
			float pendingDeltaY_ = 0.0f;
			float pendingWheel_  = 0.0f;

			bool commandHeld_ = false;
		};
	}
}
#endif // AQ_PLATFORM_MAC
