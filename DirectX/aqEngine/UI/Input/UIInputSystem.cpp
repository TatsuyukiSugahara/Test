#include "aq.h"
#include "UIInputSystem.h"
#include "UI/UIContext.h"
#include "UI/UIObject.h"
#include "UI/Screen/UIScreen.h"
#include "UI/Screen/UIScreenManager.h"
#include "UI/Component/UIButtonComponent.h"
#include "UI/Component/UITransformComponent.h"
#include "UI/Component/UICanvasComponent.h"
#include "HID/Input.h"
#include <cfloat>

namespace aq
{
	namespace ui
	{
		// =========================================================================
		// Update
		// =========================================================================

		void UIInputSystem::Update(UIScreenManager& screens, hid::InputManager& input)
		{
			(void)input; // free function 版を使うため、引数は suppress 参照のみに使用
			UpdatePointer(screens, input);
			UpdateFocus(screens, input);
		}

		void UIInputSystem::ClearState()
		{
			ResetButtonState(m_hoveredButton);
			ResetButtonState(m_focusedButton);
			m_hoveredButton = UIObjectHandle::Invalid();
			m_focusedButton = UIObjectHandle::Invalid();
			m_hoveredScreen = nullptr;
		}


		// =========================================================================
		// Pointer (マウス)
		// =========================================================================

		void UIInputSystem::UpdatePointer(UIScreenManager& screens, hid::InputManager&)
		{
			// マウスが ImGui に抑制されているなら UI HitTest をスキップ
			if (hid::InputManager::Get().IsMouseSuppressed()) return;

			math::Vector2 clientPos = hid::GetMouseCursorPos();

			// HitTest 内で各スクリーンの canvas 解像度に合わせて座標変換する
			UIScreen* hitScreen = nullptr;
			UIObject* hit = HitTest(screens, clientPos, hitScreen);
			UIObjectHandle hitHandle = hit ? hit->GetHandle() : UIObjectHandle::Invalid();

			// ホバー変化を検知
			if (hitHandle != m_hoveredButton)
			{
				if (UIObject* prev = UIContext::Get().Resolve(m_hoveredButton))
					FireHoverExit(prev, m_hoveredScreen, screens);

				m_hoveredButton = hitHandle;
				m_hoveredScreen = hitScreen;

				if (UIObject* next = UIContext::Get().Resolve(m_hoveredButton))
					FireHoverEnter(next, m_hoveredScreen, screens);
			}

			// isPressed 更新
			if (UIObject* hov = UIContext::Get().Resolve(m_hoveredButton))
			{
				if (auto* btn = hov->GetComponent<UIButtonComponent>())
					btn->isPressed = hid::IsMousePressed(hid::MouseButton::Left);
			}

			// ポインタ Submit: ホバー中に左クリックトリガー
			if (m_hoveredButton.IsValid() && hid::IsMouseTriggered(hid::MouseButton::Left))
			{
				if (UIObject* btn = UIContext::Get().Resolve(m_hoveredButton))
					FireClick(btn, m_hoveredScreen, screens);
			}
		}


		// =========================================================================
		// Focus (キーボード / Pad)
		// =========================================================================

		void UIInputSystem::UpdateFocus(UIScreenManager& screens, hid::InputManager&)
		{
			if (hid::InputManager::Get().IsKeyboardSuppressed()) return;

			UIScreen* target = GetFocusTargetScreen(screens);
			if (!target) return;

			// Submit
			if (IsSubmit() && m_focusedButton.IsValid())
			{
				if (UIObject* btn = UIContext::Get().Resolve(m_focusedButton))
					FireClick(btn, target, screens);
			}

			// Cancel → Back
			if (IsCancel())
				screens.Back();

			// 方向キーによるフォーカス移動 (Phase 3 拡張予定: シンプルな上下のみ実装)
			// TODO: フォーカスグループを使った順次移動
		}


		// =========================================================================
		// HitTest
		// =========================================================================

		UIObject* UIInputSystem::HitTest(UIScreenManager& screens, math::Vector2 clientPos,
		                                  UIScreen*& outScreen) const
		{
			outScreen = nullptr;
			// スタック上 → 下 の順に走査。各スクリーンの canvas 解像度で座標変換。
			for (int i = screens.StackSize() - 1; i >= 0; --i)
			{
				UIScreen* screen = screens.GetScreen(i);
				if (!screen || !screen->GetRoot()) continue;

				math::Vector2 canvasPos = clientPos;
				if (auto* canvas = screen->GetRoot()->GetComponent<UICanvasComponent>())
					canvasPos = ScaleClientToCanvas(clientPos, canvas->clientSize, canvas->resolution);

				UIObject* hit = HitTestObject(screen->GetRoot(), canvasPos);
				if (hit)
				{
					outScreen = screen;
					return hit;
				}

				if (screen->blocksRaycast) break; // 背面に通さない
			}
			return nullptr;
		}

		UIObject* UIInputSystem::HitTestObject(UIObject* obj, math::Vector2 canvasPos)
		{
			if (!obj || !obj->IsActiveInHierarchy()) return nullptr;

			// 子から先に判定 (前面優先)
			const auto& children = obj->GetChildren();
			for (int i = static_cast<int>(children.size()) - 1; i >= 0; --i)
			{
				UIObject* hit = HitTestObject(children[i], canvasPos);
				if (hit) return hit;
			}

			// 自身の判定: UIButtonComponent を持ち、UITransform の worldRect に含まれるか
			auto* btn       = obj->GetComponent<UIButtonComponent>();
			auto* transform = obj->GetComponent<UITransformComponent>();
			if (btn && btn->interactable && transform && transform->active)
			{
				if (transform->worldRect.Contains(canvasPos))
					return obj;
			}
			return nullptr;
		}

		UIScreen* UIInputSystem::GetFocusTargetScreen(UIScreenManager& screens)
		{
			for (int i = screens.StackSize() - 1; i >= 0; --i)
			{
				UIScreen* screen = screens.GetScreen(i);
				if (screen && screen->blocksInput) return screen;
			}
			return nullptr;
		}


		// =========================================================================
		// 座標変換
		// =========================================================================

		math::Vector2 UIInputSystem::ScaleClientToCanvas(
			math::Vector2 clientPos,
			math::Vector2 clientSize,
			math::Vector2 canvasResolution)
		{
			if (clientSize.x <= 0.f || clientSize.y <= 0.f) return clientPos;
			return {
				clientPos.x * (canvasResolution.x / clientSize.x),
				clientPos.y * (canvasResolution.y / clientSize.y)
			};
		}


		// =========================================================================
		// 入力判定 (free function 版 HID API)
		// =========================================================================

		bool UIInputSystem::IsSubmit()
		{
			return hid::IsKeyTriggered(hid::KeyBoardType::Space)
				|| hid::IsKeyTriggered(hid::KeyBoardType::Enter)
				|| hid::IsPadTriggered(0, hid::PadButton::A);
		}

		bool UIInputSystem::IsCancel()
		{
			return hid::IsKeyTriggered(hid::KeyBoardType::Escape)
				|| hid::IsPadTriggered(0, hid::PadButton::B);
		}

		bool UIInputSystem::IsDirectionUp()
		{
			return hid::IsKeyTriggered(hid::KeyBoardType::Up)
				|| hid::IsKeyTriggered(hid::KeyBoardType::W)
				|| hid::IsPadTriggered(0, hid::PadButton::DUp);
		}

		bool UIInputSystem::IsDirectionDown()
		{
			return hid::IsKeyTriggered(hid::KeyBoardType::Down)
				|| hid::IsKeyTriggered(hid::KeyBoardType::S)
				|| hid::IsPadTriggered(0, hid::PadButton::DDown);
		}


		// =========================================================================
		// Callback 発火
		// =========================================================================

		void UIInputSystem::FireClick(UIObject* btn, UIScreen* screen, UIScreenManager& screens)
		{
			auto* comp = btn->GetComponent<UIButtonComponent>();
			if (!comp || !comp->interactable || !screen) return;

			UIClickEvent e{ *btn, *screen, screens };
			if (comp->onClick) comp->onClick(e);
		}

		void UIInputSystem::FireHoverEnter(UIObject* btn, UIScreen* screen, UIScreenManager& screens)
		{
			auto* comp = btn->GetComponent<UIButtonComponent>();
			if (!comp) return;
			comp->isHovered = true;

			if (!screen) return;
			UIClickEvent e{ *btn, *screen, screens };
			if (comp->onHoverEnter) comp->onHoverEnter(e);
		}

		void UIInputSystem::FireHoverExit(UIObject* btn, UIScreen* screen, UIScreenManager& screens)
		{
			auto* comp = btn->GetComponent<UIButtonComponent>();
			if (!comp) return;
			comp->isHovered = false;
			comp->isPressed = false;

			if (!screen) return;
			UIClickEvent e{ *btn, *screen, screens };
			if (comp->onHoverExit) comp->onHoverExit(e);
		}

		void UIInputSystem::FireFocusEnter(UIObject* btn, UIScreen* screen, UIScreenManager& screens)
		{
			auto* comp = btn->GetComponent<UIButtonComponent>();
			if (!comp) return;
			comp->isFocused = true;

			if (!screen) return;
			UIClickEvent e{ *btn, *screen, screens };
			if (comp->onFocusEnter) comp->onFocusEnter(e);
		}

		void UIInputSystem::FireFocusExit(UIObject* btn, UIScreen* screen, UIScreenManager& screens)
		{
			auto* comp = btn->GetComponent<UIButtonComponent>();
			if (!comp) return;
			comp->isFocused = false;

			if (!screen) return;
			UIClickEvent e{ *btn, *screen, screens };
			if (comp->onFocusExit) comp->onFocusExit(e);
		}

		void UIInputSystem::ResetButtonState(UIObjectHandle handle)
		{
			UIObject* obj = UIContext::Get().Resolve(handle);
			if (!obj) return;
			auto* btn = obj->GetComponent<UIButtonComponent>();
			if (!btn) return;
			btn->isHovered = false;
			btn->isFocused = false;
			btn->isPressed = false;
		}

	} // namespace ui
} // namespace aq
