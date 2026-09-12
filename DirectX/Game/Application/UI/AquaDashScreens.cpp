#include "stdafx.h"
#include "AquaDashScreens.h"
#include "UI/UIObject.h"
#include "UI/Component/UITextComponent.h"
#include "UI/Component/UIImageComponent.h"
#include "UI/Component/UITransformComponent.h"
#include "Graphics/IShaderResourceView.h"
#include <cstdio>


namespace app
{
	namespace aquadash
	{
		namespace
		{
			/** ミニマップ描画面の一辺 (px)。InGame.screen.json の Minimap.sizeDelta と一致させる */
			static constexpr float MINIMAP_SIZE_PX = 256.0f;

			/** ミニマップ枠の不透明度。InGame.screen.json の MinimapFrame.color.a と一致させる */
			static constexpr float MINIMAP_FRAME_ALPHA = 0.55f;

			/** コンボゲージの不透明度。InGame.screen.json の ComboGauge.color.a と一致させる */
			static constexpr float COMBO_GAUGE_ALPHA = 0.9f;


			// テキストの不透明度。a==0 はスタイル既定色扱い (不可視にできない) になるため最小値でクランプ。
			void SetTextAlpha(aq::ui::UIObject* obj, const float a)
			{
				if (!obj) { return; }
				if (auto* text = obj->GetComponent<aq::ui::UITextComponent>()) {
					text->color.w = a < 0.02f ? 0.02f : a;
				}
			}


			// 画像の不透明度 (a==0 は完全透明で問題なし)。
			void SetImageAlpha(aq::ui::UIObject* obj, const float a)
			{
				if (!obj) { return; }
				if (auto* image = obj->GetComponent<aq::ui::UIImageComponent>()) {
					image->color.w = a;
				}
			}


			/**
			 * GPUResource の非同期ロード完了後に SRV を解決するラッパー
			 * (UIDocumentLoader が JSON のテクスチャ指定に使っているものと同型)。
			 * Release() は no-op (所有権は GPUResource 側の TextureData が持つ)。
			 */
			class DeferredSRV final : public aq::graphics::IShaderResourceView
			{
			private:
				std::shared_ptr<aq::res::GPUResource> resource_;


			public:
				explicit DeferredSRV(std::shared_ptr<aq::res::GPUResource> resource)
					: resource_(std::move(resource))
				{
				}

				void Release() override {}

				void* GetNativeHandle() const override
				{
					if (!resource_) { return nullptr; }
					const auto* srv = resource_->GetShaderResourceView();
					return srv ? srv->GetNativeHandle() : nullptr;
				}
			};


			// テクスチャパスを非同期ロードし、DeferredSRV でラップして返す。
			std::shared_ptr<aq::graphics::IShaderResourceView> LoadTexture(const char* path)
			{
				if (!path || path[0] == '\0') { return nullptr; }
				auto resource = aq::res::ResourceManager::Get().Load<aq::res::GPUResource>(path);
				if (!resource) { return nullptr; }
				return std::make_shared<DeferredSRV>(std::move(resource));
			}
		}


		/**
		 * タイトル画面
		 */
		void TitleScreen::OnEnter()
		{
			elapsed_    = 0.0f;
			press_      = Resolve(FindHandle("Press"));
			stageLabel_ = Resolve(FindHandle("StageLabel"));
			stageThumb_ = Resolve(FindHandle("StageThumb"));

			// SetStageThumbnail が来るまでは隠した状態で始める。
			SetImageAlpha(stageThumb_, 0.0f);
		}


		void TitleScreen::OnUpdate(const float dt)
		{
			elapsed_ += dt;

			// 周期 1.2 秒の点滅 (前半は高輝度・後半は低輝度のホールド型)。
			const int   n  = static_cast<int>(elapsed_ / 1.2f);
			const float ph = (elapsed_ - static_cast<float>(n) * 1.2f) / 1.2f;
			SetTextAlpha(press_, ph < 0.5f ? 1.0f : 0.15f);
		}


		void TitleScreen::SetStageName(const char* text)
		{
			if (!stageLabel_) { return; }
			if (auto* label = stageLabel_->GetComponent<aq::ui::UITextComponent>()) {
				label->content = text ? text : "";
			}
		}


		void TitleScreen::SetStageThumbnail(const char* path)
		{
			if (!stageThumb_) { return; }
			auto* image = stageThumb_->GetComponent<aq::ui::UIImageComponent>();
			if (!image) { return; }

			auto texture = LoadTexture(path);
			if (!texture) {
				SetImageAlpha(stageThumb_, 0.0f);
				return;
			}

			// 生成 PNG の見た目をそのまま出すため白でティントしない。
			image->texture = texture;
			image->color   = { 1.0f, 1.0f, 1.0f, 1.0f };
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

			comboText_  = Resolve(FindHandle("ComboText"));
			comboGauge_ = Resolve(FindHandle("ComboGauge"));

			trickText_ = Resolve(FindHandle("TrickText"));
			ghostDeltaText_ = Resolve(FindHandle("GhostDeltaText"));

			minimapFrame_  = Resolve(FindHandle("MinimapFrame"));
			minimap_       = Resolve(FindHandle("Minimap"));
			minimapMarker_ = Resolve(FindHandle("MinimapMarker"));

			// SetMinimapTexture が来るまでは隠した状態で始める。
			SetImageAlpha(minimapFrame_,  0.0f);
			SetImageAlpha(minimap_,       0.0f);
			SetImageAlpha(minimapMarker_, 0.0f);

			// コンボは最初の SetHUD が倍率を渡してくるまで隠しておく。
			SetImageAlpha(comboGauge_, 0.0f);
			SetTextAlpha(comboText_,   0.0f);

			// トリック表示も同様に、滞空してトリックが始まるまで隠しておく。
			SetTextAlpha(trickText_, 0.0f);
			SetTextAlpha(ghostDeltaText_, 0.0f);
		}


		void InGameScreen::SetHUD(const float timeSec, const uint32_t coinCount, const float speedKmh,
		                          const uint32_t comboMultiplier, const float comboRate,
		                          const uint32_t trickCount, const bool trickActive,
		                          const float ghostDeltaSec, const bool hasGhost)
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

			// 倍率 1 は「コンボ無し」なので倍率もゲージも出さない。非表示は content を空にしてから
			// アルファを落とす (SetTextAlpha は最小 0.02 にクランプされ、うっすら残ってしまうため)。
			const bool showCombo = comboMultiplier > 1u;
			if (comboText_) {
				if (auto* text = comboText_->GetComponent<aq::ui::UITextComponent>()) {
					if (showCombo) {
						char buf[16];
						std::snprintf(buf, sizeof(buf), "x%u", comboMultiplier);
						text->content = buf;
					} else {
						text->content = "";
					}
				}
			}
			SetTextAlpha(comboText_, showCombo ? 1.0f : 0.0f);

			// ゲージは fillAmount で右端を削る (矩形の左端は動かないので左詰めで縮む)。
			if (comboGauge_) {
				if (auto* image = comboGauge_->GetComponent<aq::ui::UIImageComponent>()) {
					image->fillAmount = aq::math::Clamp01(comboRate);
				}
			}
			SetImageAlpha(comboGauge_, showCombo ? COMBO_GAUGE_ALPHA : 0.0f);

			// トリックも倍率表示と同じ流儀で、非トリック時は content を空にしてからアルファを落とす。
			// 1 回転目を回している間はまだ完了数が 0 なので、倍率を付けずに "TRICK" だけ出す。
			if (trickText_) {
				if (auto* text = trickText_->GetComponent<aq::ui::UITextComponent>()) {
					if (!trickActive) {
						text->content = "";
					} else if (trickCount > 0u) {
						char buf[32];
						std::snprintf(buf, sizeof(buf), "TRICK x%u", trickCount);
						text->content = buf;
					} else {
						text->content = "TRICK";
					}
				}
			}
			SetTextAlpha(trickText_, trickActive ? 1.0f : 0.0f);

			// ゴーストとの時間差 (P23)。正 = 遅れているので赤、負 = 勝っているので緑。
			// 符号を必ず付ける (+1.23 / -0.45)。ゴーストが無い走行では非表示。
			if (ghostDeltaText_) {
				if (auto* text = ghostDeltaText_->GetComponent<aq::ui::UITextComponent>()) {
					if (!hasGhost) {
						text->content = "";
					} else {
						char buf[32];
						std::snprintf(buf, sizeof(buf), "%+.2f", ghostDeltaSec);
						text->content = buf;
						text->color = (ghostDeltaSec > 0.0f)
							? aq::math::Vector4(1.00f, 0.35f, 0.30f, 1.0f)    // 遅れ
							: aq::math::Vector4(0.40f, 1.00f, 0.50f, 1.0f);   // 勝ち
					}
				}
			}
			SetTextAlpha(ghostDeltaText_, hasGhost ? 1.0f : 0.0f);
		}


		void InGameScreen::SetMinimapTexture(const std::shared_ptr<aq::graphics::IShaderResourceView>& texture)
		{
			const bool visible = minimap_ && texture != nullptr;
			SetImageAlpha(minimapFrame_,  visible ? MINIMAP_FRAME_ALPHA : 0.0f);
			SetImageAlpha(minimapMarker_, visible ? 1.0f : 0.0f);
			if (!minimap_) { return; }

			// 俯瞰ベイクの RT をそのまま貼る。色は素の見た目を出すため白でティントしない。
			if (auto* image = minimap_->GetComponent<aq::ui::UIImageComponent>()) {
				if (visible) {
					image->texture = texture;
					image->color   = { 1.0f, 1.0f, 1.0f, 1.0f };
				} else {
					image->color.w = 0.0f;
				}
			}
		}


		void InGameScreen::SetMinimapMarker(const float u, const float v)
		{
			if (!minimapMarker_) { return; }
			auto* transform = minimapMarker_->GetComponent<aq::ui::UITransformComponent>();
			if (!transform) { return; }

			// マーカーがミニマップ矩形の外へ飛び出さないよう、中心基準へ変換する前に丸める。
			transform->localPosition.x = (aq::math::Clamp01(u) - 0.5f) * MINIMAP_SIZE_PX;
			transform->localPosition.y = (aq::math::Clamp01(v) - 0.5f) * MINIMAP_SIZE_PX;
		}


		/************************************/




		/**
		 * リザルト画面 (P0 仮)
		 */
		void ResultScreen::OnEnter()
		{
			header_    = Resolve(FindHandle("Header"));
			time_      = Resolve(FindHandle("Time"));
			coin_      = Resolve(FindHandle("ResultCoin"));
			score_     = Resolve(FindHandle("ScoreText"));
			bestCombo_ = Resolve(FindHandle("BestComboText"));
			rank_      = Resolve(FindHandle("Rank"));
			items_[0]  = Resolve(FindHandle("MenuRetry"));
			items_[1]  = Resolve(FindHandle("MenuNext"));
			items_[2]  = Resolve(FindHandle("MenuTitle"));
		}


		void ResultScreen::SetResult(const bool cleared, const float timeSec, const uint32_t coinCount,
		                             const uint32_t score, const uint32_t bestCombo, const char* rank)
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

			if (score_) {
				if (auto* text = score_->GetComponent<aq::ui::UITextComponent>()) {
					char buf[32];
					std::snprintf(buf, sizeof(buf), "SCORE  %u", score);
					text->content = buf;
				}
			}

			if (bestCombo_) {
				if (auto* text = bestCombo_->GetComponent<aq::ui::UITextComponent>()) {
					char buf[32];
					std::snprintf(buf, sizeof(buf), "BEST COMBO  %u", bestCombo);
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
