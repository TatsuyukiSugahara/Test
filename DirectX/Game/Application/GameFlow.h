#pragma once
#include "ECS/Entity.h"
#include "Level/LevelManager.h"   // aq::level::LevelLoadHandle
#include "Math/Vector.h"
#include "UI/Screen/UIScreen.h"

namespace app
{
	// タイトル画面。画像 + "Press A" テキストは JSON。決定入力の判定は GameFlow が行う。
	class TitleScreen : public aq::ui::UIScreen
	{
	};




	// ローディング画面。"Now Loading" の末尾ドットを増減させて読み込み中を演出する。
	class LoadingScreen : public aq::ui::UIScreen
	{
	public:
		void OnEnter()          override;
		void OnUpdate(float dt) override;

	private:
		float dotTimer_ = 0.0f;
		int   dotCount_ = 0;
		int   dotDir_   = 1;
	};




	// 旧 IScene / SceneManager / BattleScene を置換するゲームフロー。
	// Title → 決定で非同期 Level ロード(Loading 表示) → Playing、の状態機械。
	// 3D 世界(地形/カメラ/プレイヤー/ステアリング)のセットアップと影の注視点も担う。
	class GameFlow
	{
	// ── メンバ変数 ──
	private:
		enum class State
		{
			Title,
			Loading,
			Playing,
		};

		State                      state_      = State::Title;
		aq::ecs::EntityHandle      playerHandle_;
		aq::level::LevelLoadHandle loadHandle_;
		float                      loadTimer_  = 0.0f;

		static GameFlow* instance_;

	// ── メンバ関数 ──
	public:
		void Update(const float dt);

		// 影の投影中心(プレイヤー位置。未生成時は原点)。
		aq::math::Vector3 GetFocusPosition() const;

	private:
		GameFlow() {}

		// タイトル → ロード開始時に 3D 世界(地形/カメラ/ライト/プレイヤー/ステアリング)を生成する。
		void SetupWorld();

	// ── シングルトン ──
	public:
		static void Create();
		static GameFlow& Get() { return *instance_; }
		static void Release();

		// UI 画面登録 + タイトル表示(Create の後に一度呼ぶ)。
		void Initialize();
	};
}
