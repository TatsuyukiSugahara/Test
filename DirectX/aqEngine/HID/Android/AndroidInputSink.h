#pragma once
// Android 専用。他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_ANDROID)
#include "HID/IPadBackend.h"
#include "HID/ITouchBackend.h"

namespace aq
{
	namespace hid
	{
		/**
		 * AInputEvent を受けて、タッチ/物理パッドの現在状態を溜めておくバッファ。
		 *
		 * 投入するのは `PlatformAndroid`(native_app_glue の `onInputEvent`)、
		 * 取り出すのは `AndroidTouchBackend` / `AndroidPadBackend`。
		 * **どちらも Android 専用コード**なので、この型は `IPlatform` にも
		 * `ITouchBackend` / `IPadBackend` にも露出させない(Mac の `CocoaInputSink` と同じ構造)。
		 *
		 * Android 依存の値(`AInputEvent` / `AMotionEvent_*` / `AKeyEvent_*`)は投入側で
		 * 取り出し済みのものを受け取る。本ヘッダに Android のヘッダは現れない。
		 *
		 * スレッド: 投入も取得も**ゲームスレッド 1 本**から呼ばれる
		 * (`android_main` のループが `PumpEvents` → `Update` → `InputManager::Update` の
		 *  順に回し、入力イベントは `PumpEvents` の中で配送される)。
		 * そのためロックを持たない。別スレッドから触らないこと。
		 */
		class AndroidInputSink
		{
		// ── メンバ変数 ──
		private:
			/**
			 * タッチ 1 点の保持。`TouchPoint` と 1 対 1 に対応する。
			 *
			 * `id` が負の間は未使用スロット。指が離れても**次の取得まではスロットを残す**
			 * (`ITouchBackend` の契約が「離した瞬間のフレームだけ pressed = false で 1 回返る」)。
			 */
			struct TouchSlot
			{
				int32_t id = -1;
				float   x  = 0.0f;
				float   y  = 0.0f;

				/** OS から見て今も触れているか */
				bool down = false;

				/**
				 * 前回の取得以降に押下イベントが来たか。
				 *
				 * タッチも物理パッドも**離散イベント**で届く。1 フレームが長引いた場合
				 * (ロード中のヒッチなど)、そのフレームの中で押して離すところまで進んでしまい、
				 * レベルだけ見ていると押下を取りこぼす。「取得までに一度でも押された」ことを
				 * 憶えておき、その回では押下として返す。次の取得では実レベル(離されていれば
				 * false)に戻るので、タップ判定が 1 回成立する。
				 */
				bool pressedSinceFetch = false;
			};

			/** タッチ点。配列の先頭から詰めずに、id で引いたスロットをそのまま使う */
			TouchSlot touches_[TouchState::MAX_POINT_COUNT]{};

			/** PadButton 順のボタン状態(現在の実レベル)と、その押下取りこぼし対策 */
			bool padButtons_[PadState::BUTTON_COUNT]{};
			bool padPressedSinceFetch_[PadState::BUTTON_COUNT]{};

			/**
			 * ハットスイッチ由来の十字キー(0 = 上 / 1 = 下 / 2 = 左 / 3 = 右)。
			 *
			 * 機種によって十字キーが `AKEYCODE_DPAD_*` ではなく `AXIS_HAT_X/Y` で届く。
			 * キー由来のレベルを上書きしないよう別に持ち、取得時に OR する。
			 */
			bool padHat_[4]{};

			/** スティック(Android の生値。符号は取得時に XInput 系へ揃える) */
			float stickLX_ = 0.0f;
			float stickLY_ = 0.0f;
			float stickRX_ = 0.0f;
			float stickRY_ = 0.0f;

			/**
			 * トリガー [0, 1]。機種によって `AXIS_LTRIGGER/RTRIGGER` と
			 * `AXIS_BRAKE/GAS` のどちらで届くかが変わるため両方を持ち、取得時に大きい方を採る。
			 */
			float triggerL_ = 0.0f;
			float triggerR_ = 0.0f;
			float brake_    = 0.0f;
			float gas_      = 0.0f;

			/**
			 * パッドのイベントを一度でも受けたか。
			 *
			 * Android には「パッドが繋がっているか」を問い合わせる NDK API が無いので、
			 * イベントが来たことをもって接続とみなす(未接続なら正直に false を返す)。
			 */
			bool padEventSeen_ = false;


		// ── メンバ関数 ──
		private:
			AndroidInputSink() = default;


			/**
			 * 投入側 (PlatformAndroid::OnInputEvent)
			 */
		public:
			/** 指が触れた。座標はクライアント領域の左上原点・ピクセル */
			void OnTouchDown(const int32_t pointerId, const float x, const float y);

			/** 指が動いた。未知の id なら押下として拾い直す(down の取りこぼし対策) */
			void OnTouchMove(const int32_t pointerId, const float x, const float y);

			/** 指が離れた。スロットは次の取得まで残す */
			void OnTouchUp(const int32_t pointerId, const float x, const float y);

			/**
			 * ジェスチャが OS に奪われた(`ACTION_CANCEL`)。
			 * 押下の取りこぼし対策も一緒に落として、幻のタップが成立しないようにする。
			 */
			void OnTouchCancel();

			/**
			 * パッドのボタン。keyCode は `AKEYCODE_*`。
			 * @return 写像にあるキーコードなら true(呼び出し側はイベントを消費してよい)
			 */
			bool OnPadButton(const int32_t keyCode, const bool pressed);

			/** パッドの軸。axis は `AMOTION_EVENT_AXIS_*`、value は `AMotionEvent_getAxisValue` の生値 */
			void OnPadAxis(const int32_t axis, const float value);

			/** ウィンドウが非アクティブになった。押下状態を落として持ち越さない */
			void OnFocusLost();


			/**
			 * 取得側 (AndroidTouchBackend / AndroidPadBackend)
			 */
		public:
			/**
			 * タッチ状態を書き出す。
			 * 触れている点に加え、**前回の取得以降に一度でも触れた点**も押下として返す。
			 */
			void FetchTouch(TouchState& out);

			/**
			 * パッド状態を書き出す。
			 * 軸は XInput 実装と同じ向き・同じデッドゾーン処理で正規化して返す。
			 */
			void FetchPad(PadState& out);


		private:
			/** id に対応するスロット。無ければ nullptr。create = true なら空きスロットを割り当てる */
			TouchSlot* FindTouchSlot(const int32_t pointerId, const bool create);


		public:
			static AndroidInputSink& Get();


		private:
			/**
			 * スティックのデッドゾーン処理と [-1, 1] への再正規化。
			 * 式は `XInputPadBackend::NormalizeAxis` と同じ(閾値未満を 0 にして、
			 * 残りを線形に引き伸ばす)。Android の軸は既に [-1, 1] なので入力が float になるだけ。
			 */
			static float NormalizeStick(const float value, const float deadZone);
		};
	}
}
#endif // AQ_PLATFORM_ANDROID
