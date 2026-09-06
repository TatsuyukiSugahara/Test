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
		 * インゲーム HUD
		 */
		void InGameScreen::OnEnter()
		{
			timeText_  = Resolve(FindHandle("TimeText"));
			coinText_  = Resolve(FindHandle("CoinText"));
			speedText_ = Resolve(FindHandle("SpeedText"));
		}


		void InGameScreen::SetHUD(const float timeSec, const uint32_t coinCount, const float speedKmh)
		{
			if (timeText_) {
				if (auto* text = timeText_->GetComponent<aq::ui::UITextComponent>()) {
					const int   minutes = static_cast<int>(timeSec) / 60;
					const float sec     = timeSec - static_cast<float>(minutes) * 60.0f;
					char buf[32];
					std::snprintf(buf, sizeof(buf), "TIME %02d:%05.2f", minutes, sec);
					text->content = buf;
				}
			}

			if (coinText_) {
				if (auto* text = coinText_->GetComponent<aq::ui::UITextComponent>()) {
					char buf[32];
					std::snprintf(buf, sizeof(buf), "COIN %02u", coinCount);
					text->content = buf;
				}
			}

			if (speedText_) {
				if (auto* text = speedText_->GetComponent<aq::ui::UITextComponent>()) {
					// 後退中に "-0" と表示されないよう 0 でクランプする。
					const int kmh = static_cast<int>(speedKmh > 0.0f ? speedKmh : 0.0f);
					char buf[32];
					std::snprintf(buf, sizeof(buf), "%d", kmh);
					text->content = buf;
				}
			}
		}


		/************************************/




		/**
		 * リザルト画面 (P0 仮)
		 */
		void ResultScreen::OnEnter()
		{
			header_   = Resolve(FindHandle("Header"));
			time_     = Resolve(FindHandle("Time"));
			coin_     = Resolve(FindHandle("ResultCoin"));
			rank_     = Resolve(FindHandle("Rank"));
			items_[0] = Resolve(FindHandle("MenuRetry"));
			items_[1] = Resolve(FindHandle("MenuNext"));
			items_[2] = Resolve(FindHandle("MenuTitle"));
		}


		void ResultScreen::SetResult(const bool cleared, const float timeSec, const uint32_t coinCount, const char* rank)
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

			if (coin_) {
				if (auto* text = coin_->GetComponent<aq::ui::UITextComponent>()) {
					char buf[32];
					std::snprintf(buf, sizeof(buf), "COIN  %02u", coinCount);
					text->content = buf;
				}
			}

			// ランクはクリア時のみ表示する。非表示は content を空にする
			// (SetTextAlpha は最小 0.02 にクランプされ、明るい背景ではうっすら見えてしまうため)。
			const bool showRank = cleared && rank && rank[0] != '\0';
			if (rank_) {
				if (auto* text = rank_->GetComponent<aq::ui::UITextComponent>()) {
					text->content = showRank ? rank : "";
				}
			}
			SetTextAlpha(rank_, showRank ? 1.0f : 0.0f);
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
