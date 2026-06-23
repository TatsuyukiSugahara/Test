#pragma once
#include "UI/UITypes.h"
#include "Math/Vector.h"

namespace aq
{
	namespace hid { class InputManager; }

	namespace ui
	{
		class UIObject;
		class UIScreen;
		class UIScreenManager;
		class UICanvasComponent;


		// UIInputSystem: aq::hid の入力を UI イベントに変換する。
		// UIScreenManager::Update() の直前に Application::OnUpdate() から呼ぶ。
		class UIInputSystem
		{
		public:
			UIInputSystem()  = default;
			~UIInputSystem() = default;

			// メインの更新エントリポイント
			void Update(UIScreenManager& screens, hid::InputManager& input);

			// 画面遷移後に呼ぶ: ホバー/フォーカス状態をクリア
			// ハンドルを無効化するだけでなく、コンポーネントのフラグも落とす
			void ClearState();

		private:
			// ---- ポインター入力 (マウス) ----
			void UpdatePointer(UIScreenManager& screens, hid::InputManager& input);

			// ---- フォーカス入力 (キーボード/Pad) ----
			void UpdateFocus(UIScreenManager& screens, hid::InputManager& input);

			// ---- HitTest ----
			// スタックを上から走査。blocksRaycast でカット。
			UIObject* HitTest(UIScreenManager& screens, math::Vector2 canvasPos) const;
			static UIObject* HitTestObject(UIObject* obj, math::Vector2 canvasPos);

			// ---- フォーカスターゲット画面 ----
			// blocksInput が true の最初の画面を返す
			static UIScreen* GetFocusTargetScreen(UIScreenManager& screens);

			// ---- 座標変換 ----
			// マウスクライアント座標 → キャンバス座標
			static math::Vector2 ScaleClientToCanvas(
				math::Vector2 clientPos,
				math::Vector2 clientSize,
				math::Vector2 canvasResolution);

			// ---- 入力判定 ----
			static bool IsSubmit();
			static bool IsCancel();
			static bool IsDirectionUp();
			static bool IsDirectionDown();

			// ---- callback 発火 ----
			void FireClick     (UIObject* btn, UIScreenManager& screens);
			void FireHoverEnter(UIObject* btn, UIScreenManager& screens);
			void FireHoverExit (UIObject* btn, UIScreenManager& screens);
			void FireFocusEnter(UIObject* btn, UIScreenManager& screens);
			void FireFocusExit (UIObject* btn, UIScreenManager& screens);

			// ハンドルが指すボタンのコンポーネントフラグを false にリセット
			void ResetButtonState(UIObjectHandle handle);

			UIObjectHandle m_hoveredButton;
			UIObjectHandle m_focusedButton;
		};

	} // namespace ui
} // namespace aq
