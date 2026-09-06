#include "stdafx.h"
#include "AquaDashScreens.h"
#include "UI/UIObject.h"
#include "UI/Component/UITextComponent.h"
#include <cstdio>


namespace app
{
	namespace aquadash
	{
		namespace
		{
			// テキストの不透明度。a==0 はスタイル既定色扱い (不可視にできない) になるため最小値でクランプ。
			void SetTextAlpha(aq::ui::UIObject* obj, const float a)
			{
				if (!obj) { return; }
				if (auto* text = obj->GetComponent<aq::ui::UITextComponent>()) {
					text->color.w = a < 0.02f ? 0.02f : a;
				}
			}
		}


		/**
		 * タイトル画面 (P0 仮)
		 */
		void TitleScreen::OnEnter()
		{
			elapsed_ = 0.0f;
			press_   = Resolve(FindHandle("Press"));
		}


		void TitleScreen::OnUpdate(const float dt)
		{
			elapsed_ += dt;

			// 周期 1.2 秒の点滅 (前半は高輝度・後半は低輝度のホールド型)。
			const int   n  = static_cast<int>(elapsed_ / 1.2f);
			const float ph = (elapsed_ - static_cast<float>(n) * 1.2f) / 1.2f;
			SetTextAlpha(press_, ph < 0.5f ? 1.0f : 0.15f);
		}


		/************************************/




		/**
		 * リザルト画面 (P0 仮)
		 */
		void ResultScreen::OnEnter()
		{
			header_   = Resolve(FindHandle("Header"));
			time_     = Resolve(FindHandle("Time"));
			items_[0] = Resolve(FindHandle("MenuRetry"));
			items_[1] = Resolve(FindHandle("MenuNext"));
			items_[2] = Resolve(FindHandle("MenuTitle"));
		}


		void ResultScreen::SetResult(const bool cleared, const float timeSec)
		{
			if (header_) {
				if (auto* text = header_->GetComponent<aq::ui::UITextComponent>()) {
					text->content = cleared ? "STAGE CLEAR" : "GAME OVER";
					if (cleared) {
						text->color = { 1.0f, 0.85f, 0.3f, 1.0f };
					} else {
						text->color = { 0.9f, 0.3f, 0.3f, 1.0f };
					}
				}
			}

			if (time_) {
				if (auto* text = time_->GetComponent<aq::ui::UITextComponent>()) {
					const int minutes = static_cast<int>(timeSec) / 60;
					const float sec   = timeSec - static_cast<float>(minutes) * 60.0f;
					char buf[32];
					std::snprintf(buf, sizeof(buf), "TIME  %02d:%05.2f", minutes, sec);
					text->content = buf;
				}
			}
		}


		void ResultScreen::SetCursor(const int index)
		{
			for (int i = 0; i < RESULT_MENU_COUNT; ++i) {
				if (!items_[i]) { continue; }
				auto* text = items_[i]->GetComponent<aq::ui::UITextComponent>();
				if (!text) { continue; }

				if (i == index) {
					text->color = { 1.0f, 1.0f, 1.0f, 1.0f };
					text->scale = 1.15f;
				} else {
					text->color = { 0.6f, 0.6f, 0.6f, 0.45f };
					text->scale = 1.0f;
				}
			}
		}
	}
}
