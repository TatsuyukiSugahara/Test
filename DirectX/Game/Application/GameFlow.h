#pragma once
#include "ECS/Entity.h"
#include "Level/LevelManager.h"   // aq::level::LevelLoadHandle
#include "Math/Vector.h"
#include "UI/Screen/UIScreen.h"
#include <memory>

namespace app
{
	class GameFlow;


	/**
	 * ゲーム状態の基底。1 状態 1 クラスで表現する。
	 * リザルト / ポーズ / タイトル選択など状態を増やすときは、このクラスを継承した状態を足すだけでよい
	 * (中央の switch を編集しない)。プレイヤー用 IState/StateMachine と同じ流儀。
	 */
	class IGameState
	{
	public:
		virtual ~IGameState() = default;

		virtual void OnEnter (GameFlow& /*flow*/)                    {}
		virtual void OnUpdate(GameFlow& /*flow*/, const float /*dt*/) {}
		virtual void OnExit  (GameFlow& /*flow*/)                    {}
	};




	/**
	 * タイトル画面 (案A「墨」)。和紙の地に筆文字「残刃」がにじんで登場し、朱の落款「侍」、
	 * 下部に点滅する PRESS ANY BUTTON。レイアウトは Title.screen.json、入退場アニメは本クラスの
	 * OnUpdate が時間駆動する。決定入力の判定は TitleState 側で行い、押下時に RequestStart() で
	 * フラッシュ演出を開始、IsStartFlashDone() が真になったら遷移する。
	 */
	class TitleScreen : public aq::ui::UIScreen
	{
	// ── メンバ変数 ──
	private:
		/** 時間・状態 */
		float elapsed_      = 0.0f;   // OnEnter からの経過秒
		bool  startPressed_ = false;  // 決定入力後のフラッシュ開始フラグ
		float flashTime_    = 0.0f;   // フラッシュ開始からの経過秒

		/** アニメ対象 UIObject (名前解決は OnEnter で一度だけ行いキャッシュ) */
		aq::ui::UIObject* eyebrow_   = nullptr;
		aq::ui::UIObject* kanji_     = nullptr;
		aq::ui::UIObject* sealOuter_ = nullptr;
		aq::ui::UIObject* sealInner_ = nullptr;
		aq::ui::UIObject* sealChar_  = nullptr;
		aq::ui::UIObject* romaji_    = nullptr;
		aq::ui::UIObject* press_     = nullptr;
		aq::ui::UIObject* flash_     = nullptr;

	// ── メンバ関数 ──
	public:
		void OnEnter()          override;
		void OnUpdate(float dt) override;

		/** 決定入力時に TitleState から呼ぶ。白フラッシュ演出を開始する。 */
		void RequestStart();

		/** フラッシュが十分進み、次画面へ遷移してよいか。 */
		bool IsStartFlashDone() const;
	};




	/**
	 * ローディング画面。"Now Loading" の末尾ドットを増減させて読み込み中を演出する。
	 */
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




	/**
	 * 旧 IScene / SceneManager / BattleScene を置換する最上位ゲーム進行。
	 * 状態オブジェクト(IGameState 派生)を 1 つ保持して駆動する状態機械。3D 世界のセットアップと
	 * 影の注視点、状態間で共有するデータ(プレイヤー / 非同期ロードハンドル)も持つ。
	 */
	class GameFlow
	{
	// ── メンバ変数 ──
	private:
		/** 状態関連 */
		std::unique_ptr<IGameState> current_;
		std::unique_ptr<IGameState> pending_;   // 次状態 (Update 境界で適用し、更新中の自己破棄を避ける)

		/** 状態間共有データ */
		aq::ecs::EntityHandle       playerHandle_;
		aq::level::LevelLoadHandle  loadHandle_;
		bool                        preloaded_ = false;   // UI テクスチャの事前ロードを一度だけ行う

		static GameFlow* instance_;

	// ── メンバ関数 ──
	public:
		void Update(const float dt);

		// 影の投影中心 (プレイヤー位置。未生成時は原点)。
		aq::math::Vector3 GetFocusPosition() const;

		// 状態遷移。次の Update 境界で current_ を差し替える (state の OnUpdate 内から安全に呼べる)。
		void ChangeState(std::unique_ptr<IGameState> next);

		// ---- 状態から使う共有機能・データ ----
		// 3D 世界 (地形/カメラ/ライト/プレイヤー/ステアリング) を生成する。
		void SetupWorld();

		aq::level::LevelLoadHandle&       LoadHandle()       { return loadHandle_; }
		void SetLoadHandle(const aq::level::LevelLoadHandle& handle) { loadHandle_ = handle; }

	private:
		GameFlow() {}

	// ── シングルトン ──
	public:
		static void      Create();
		static GameFlow& Get() { return *instance_; }
		static void      Release();

		// UI 画面登録 + 初期状態 (タイトル) 開始。Create の後に一度呼ぶ。
		void Initialize();
	};
}
