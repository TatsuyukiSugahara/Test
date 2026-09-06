#include "stdafx.h"
#include "AquaDashStates.h"
#include "AquaDash/UI/AquaDashScreens.h"
#include "GameInput.h"
#include "GameAction.h"
#include "Sound/SoundClip.h"
#include "Sound/SoundEngine.h"


namespace app
{
	namespace aquadash
	{
		namespace
		{
			/** メニュー項目 (ResultScreen の表示順と一致させる) */
			static constexpr int MENU_RETRY = 0;
			static constexpr int MENU_NEXT  = 1;
			static constexpr int MENU_TITLE = 2;

			// スティック上下をカーソル移動として拾うしきい値。
			static constexpr float STICK_EDGE = 0.5f;

			// 決定 SE (残刃と共用)。
			static const char* DECISION_SE_PATH = "Assets/Sound/Decision.wav";


			// 決定 SE を再生する (クリップは GameFlow::Update で先読み済み)。
			void PlayDecisionSE()
			{
				if (aq::sound::SoundEngine::IsAvailable()) {
					auto clip = aq::res::ResourceManager::Get().Load<aq::sound::SoundClip>(DECISION_SE_PATH);
					aq::sound::SoundEngine::Get().Play(clip, aq::sound::SoundBusId::SE);
				}
			}
		}


		/**
		 * タイトル (P0 仮)
		 */
		void TitleState::OnUpdate(GameFlow& flow, const float /*dt*/)
		{
			if (!GameInput::Get().IsTriggered(GameAction::Confirm)) { return; }

			PlayDecisionSE();

			// P0 はロードなしで直接インゲームへ (ステージ非同期ロードの LoadingState は P1 で挟む)。
			flow.Context().playResult = PlayResult();
			aq::ui::UIContext::Get().Screens().Replace("AquaDashInGame");
			flow.ChangeState(std::make_unique<InGameState>());
		}


		/************************************/




		/**
		 * インゲーム (P0 仮)
		 */
		void InGameState::OnEnter(GameFlow& /*flow*/)
		{
			elapsed_ = 0.0f;
		}


		void InGameState::OnUpdate(GameFlow& flow, const float dt)
		{
			elapsed_ += dt;

			// P0 仮判定: 決定=ゴール到達 / 下入力=落下。ゴール/落下の実判定は P3 で置き換える。
			const bool goal = GameInput::Get().IsTriggered(GameAction::Confirm);
			const bool fall = GameInput::Get().IsTriggered(GameAction::MoveBackward);
			if (!goal && !fall) { return; }

			PlayResult& result  = flow.Context().playResult;
			result.cleared      = goal;
			result.clearTimeSec = elapsed_;

			aq::ui::UIContext::Get().Screens().Replace("AquaDashResult");
			flow.ChangeState(std::make_unique<ResultState>());
		}


		/************************************/




		/**
		 * リザルト
		 */
		void ResultState::OnEnter(GameFlow& flow)
		{
			cursor_     = 0;
			prevStickY_ = 0.0f;

			// Replace 済みの最前面がリザルト画面。結果と初期カーソルを反映する。
			if (auto* screen = static_cast<ResultScreen*>(aq::ui::UIContext::Get().Screens().Top())) {
				const PlayResult& result = flow.Context().playResult;
				screen->SetResult(result.cleared, result.clearTimeSec);
				screen->SetCursor(cursor_);
			}
		}


		void ResultState::OnUpdate(GameFlow& flow, const float /*dt*/)
		{
			auto& input = GameInput::Get();

			// カーソル移動: W/S・D-Pad 上下のトリガ + 左スティック上下のエッジ検出。
			int move = 0;
			if (input.IsTriggered(GameAction::MoveBackward)) { move = +1; }
			if (input.IsTriggered(GameAction::MoveForward))  { move = -1; }

			const float stickY = input.GetStick(GameAction::Move).y;
			if (prevStickY_ < STICK_EDGE && stickY >= STICK_EDGE) {
				move = -1;   // 上入力
			} else if (prevStickY_ > -STICK_EDGE && stickY <= -STICK_EDGE) {
				move = +1;   // 下入力
			}
			prevStickY_ = stickY;

			if (move != 0)
			{
				cursor_ = (cursor_ + move + RESULT_MENU_COUNT) % RESULT_MENU_COUNT;
				if (auto* screen = static_cast<ResultScreen*>(aq::ui::UIContext::Get().Screens().Top())) {
					screen->SetCursor(cursor_);
				}
			}

			if (!input.IsTriggered(GameAction::Confirm)) { return; }

			PlayDecisionSE();

			auto& screens = aq::ui::UIContext::Get().Screens();
			switch (cursor_)
			{
			case MENU_RETRY:
				screens.Replace("AquaDashInGame");
				flow.ChangeState(std::make_unique<InGameState>());
				break;

			case MENU_NEXT:
				// P0 はステージが 1 つのため同じステージを再プレイ (StageRegistry 導入後に次ステージへ)。
				screens.Replace("AquaDashInGame");
				flow.ChangeState(std::make_unique<InGameState>());
				break;

			case MENU_TITLE:
				screens.Replace("AquaDashTitle");
				flow.ChangeState(std::make_unique<TitleState>());
				break;
			}
		}
	}
}
