#include "stdafx.h"
#include "GameViewInput.h"
#include "HID/Input.h"
#include "Platform/IPlatform.h"
// InputEventsQueue / ImGuiInputEvent を読むため。Editor で imgui_internal.h を使うのはこのファイルだけ
#include <imgui/imgui_internal.h>


namespace editor
{
	namespace
	{
		/** 修飾キー(転送対象)。並びは keySent_ の末尾の枠の並び */
		static constexpr ImGuiKey MOD_KEYS[] = { ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt, ImGuiMod_Super };
		static_assert(sizeof(MOD_KEYS) / sizeof(MOD_KEYS[0]) == 4, "GameViewInput::MOD_KEY_COUNT と揃える");


		/** 前回までに読んだものより新しいイベントか(EventId は 1 からの連番。0 は ID を振られないビューポートのイベント) */
		bool IsNewEvent(const ImGuiInputEvent& e, const uint32_t lastEventId)
		{
			return e.EventId != 0 && e.EventId > lastEventId;
		}


		/** 位置のイベントがウィンドウ上の実在の位置か(ウィンドウの外へ出たとき win32 は -FLT_MAX を積む) */
		bool IsValidMousePos(const float x, const float y)
		{
			return x > -FLT_MAX && y > -FLT_MAX;
		}
	}


	void GameViewInput::BeginFrame(ImGuiContext* gameContext)
	{
		ImGuiContext* editorContext = ImGui::GetCurrentContext();
		if (gameContext == nullptr || editorContext == nullptr) {
			return;
		}

		aq::Engine& engine = aq::Engine::Get();
		const aq::platform::IPlatform* platform = engine.GetPlatform();
		const bool appActive = (platform != nullptr) ? platform->IsAppActive() : true;

		// ImGui の内部構造(ImGuiContext::InputEventsQueue / ImGuiInputEvent)に頼る唯一の箇所。ImGui を更新して形が変わったらここを直す
		const ImVector<ImGuiInputEvent>& queue = editorContext->InputEventsQueue;

		// ゲーム側のフォーカスはフレームで 1 つの値にする(このフレームで中を押したなら、その時点からではなくフレーム全体でフォーカスあり)。
		// フレームの途中で false → true と切り替えると、ゲーム用はフォーカス喪失で全キーを離さないのに控えだけ消え、キーが押されっぱなしになるため
		bool pressedInside = false;
		{
			bool  known = mouseKnown_;
			float x     = mouseX_;
			float y     = mouseY_;
			for (const ImGuiInputEvent& e : queue) {
				if (!IsNewEvent(e, lastEventId_)) {
					continue;
				}
				if (e.Type == ImGuiInputEventType_MousePos) {
					known = IsValidMousePos(e.MousePos.PosX, e.MousePos.PosY);
					x     = e.MousePos.PosX;
					y     = e.MousePos.PosY;
				} else if (e.Type == ImGuiInputEventType_MouseButton && e.MouseButton.Down && IsInside(known, x, y)) {
					pressedInside = true;
					break;
				}
			}
		}
		const bool gameFocus = appActive && (prevState_.focused || pressedInside);

		ImGui::SetCurrentContext(gameContext);
		ImGuiIO& gameIO = ImGui::GetIO();

		// フォーカス。false を受けたゲーム用は全キー・全ボタンを離した扱いにするので、控えも揃えて消す
		if (gameFocus != gameFocus_) {
			gameIO.AddFocusEvent(gameFocus);
			gameFocus_ = gameFocus;
			if (!gameFocus) {
				ClearSentInputs();
			}
		}

		// 画像の矩形やエディタのマウス使用が前フレームで変わった分を、このフレームのイベントより先に反映する
		ForwardMousePos(gameIO);

		// 積まれた順に 1 回ずつ読んで転送する(位置 → 押下のような順序をゲーム用でも保つ)
		for (const ImGuiInputEvent& e : queue) {
			if (!IsNewEvent(e, lastEventId_)) {
				continue;
			}
			lastEventId_ = e.EventId;

			switch (e.Type) {
			case ImGuiInputEventType_MousePos:
				mouseKnown_ = IsValidMousePos(e.MousePos.PosX, e.MousePos.PosY);
				mouseX_     = e.MousePos.PosX;
				mouseY_     = e.MousePos.PosY;
				ForwardMousePos(gameIO);
				break;

			case ImGuiInputEventType_MouseButton: {
				const int button = e.MouseButton.Button;
				if (button < 0 || button >= ImGuiMouseButton_COUNT) {
					break;
				}
				if (e.MouseButton.Down) {
					// 押下は中のときだけ。中なら位置は直前の ForwardMousePos で送ってある
					if (IsInside(mouseKnown_, mouseX_, mouseY_)) {
						gameIO.AddMouseButtonEvent(button, true);
						buttonSent_[button] = true;
					}
				} else if (buttonSent_[button]) {
					// 離しは押下を送ったボタンなら場所を問わず送る(外へドラッグしても離しが届く)
					gameIO.AddMouseButtonEvent(button, false);
					buttonSent_[button] = false;
					// 外で離して追従が終わったら、マウスが外れた扱いにする
					ForwardMousePos(gameIO);
				}
				break;
			}

			case ImGuiInputEventType_MouseWheel:
				if (IsInside(mouseKnown_, mouseX_, mouseY_)) {
					gameIO.AddMouseWheelEvent(e.MouseWheel.WheelX, e.MouseWheel.WheelY);
				}
				break;

			case ImGuiInputEventType_Key: {
				// マウスの別名やゲームパッドは扱わない(別名は AddKeyEvent で assert する)
				const int slot = GetKeySlot(e.Key.Key);
				if (slot < 0) {
					break;
				}
				if (e.Key.Down) {
					if (gameFocus_) {
						gameIO.AddKeyEvent(e.Key.Key, true);
						keySent_[slot] = true;
					}
				} else if (keySent_[slot]) {
					gameIO.AddKeyEvent(e.Key.Key, false);
					keySent_[slot] = false;
				}
				break;
			}

			case ImGuiInputEventType_Text:
				if (gameFocus_) {
					gameIO.AddInputCharacter(e.Text.Char);
				}
				break;

			default:
				// win32 のフォーカス、ビューポートなどは送らない
				break;
			}
		}

		ImGui::SetCurrentContext(editorContext);

		// ゲーム本体の入力。埋め込み起動のフォーカス条件も OR して、AppHost が先に入れた値を消さない
		aq::hid::InputManager& inputManager = aq::hid::InputManager::Get();
		inputManager.SetFocusBlocked((engine.IsEmbedded() && !engine.HasInputFocus()) || !gameFocus_);

		aq::hid::PointerView pointerView;
		pointerView.enabled        = prevState_.visible;
		pointerView.minX           = prevState_.imageMinX;
		pointerView.minY           = prevState_.imageMinY;
		pointerView.maxX           = prevState_.imageMaxX;
		pointerView.maxY           = prevState_.imageMaxY;
		pointerView.treatAsOutside = prevState_.editorHoldsMouse;
		inputManager.SetPointerView(pointerView);
	}


	bool GameViewInput::IsInside(const bool known, const float x, const float y) const
	{
		if (!known || !prevState_.visible || prevState_.editorHoldsMouse) {
			return false;
		}
		return x >= prevState_.imageMinX && x < prevState_.imageMaxX
		    && y >= prevState_.imageMinY && y < prevState_.imageMaxY;
	}


	bool GameViewInput::IsAnyButtonSent() const
	{
		for (const bool sent : buttonSent_) {
			if (sent) {
				return true;
			}
		}
		return false;
	}


	void GameViewInput::ForwardMousePos(ImGuiIO& gameIO)
	{
		const float imageW = prevState_.imageMaxX - prevState_.imageMinX;
		const float imageH = prevState_.imageMaxY - prevState_.imageMinY;
		const bool  follow = IsInside(mouseKnown_, mouseX_, mouseY_) || IsAnyButtonSent();

		if (follow && mouseKnown_ && imageW > 0.0f && imageH > 0.0f) {
			// ゲーム用の io.DisplaySize はレンダー解像度なので、画像の中の相対位置をレンダー解像度へ写す
			aq::Engine& engine = aq::Engine::Get();
			const float renderW = static_cast<float>(engine.GetRenderWidth());
			const float renderH = static_cast<float>(engine.GetRenderHeight());
			gameIO.AddMousePosEvent((mouseX_ - prevState_.imageMinX) / imageW * renderW,
			                        (mouseY_ - prevState_.imageMinY) / imageH * renderH);
			mousePosSent_ = true;
			return;
		}

		if (mousePosSent_) {
			gameIO.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
			mousePosSent_ = false;
		}
	}


	void GameViewInput::ClearSentInputs()
	{
		for (bool& sent : buttonSent_) {
			sent = false;
		}
		for (bool& sent : keySent_) {
			sent = false;
		}
	}


	int GameViewInput::GetKeySlot(const ImGuiKey key)
	{
		if (key >= ImGuiKey_NamedKey_BEGIN && key < ImGuiKey_GamepadStart) {
			return static_cast<int>(key) - static_cast<int>(ImGuiKey_NamedKey_BEGIN);
		}
		for (int i = 0; i < MOD_KEY_COUNT; ++i) {
			if (key == MOD_KEYS[i]) {
				return KEYBOARD_KEY_COUNT + i;
			}
		}
		return -1;
	}
}
