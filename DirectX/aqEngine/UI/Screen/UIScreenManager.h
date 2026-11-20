#pragma once
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <functional>
#include <unordered_map>
#include "UIScreen.h"

namespace aq
{
	namespace ui
	{
		class UIObject;


		// UIScreenManager: 画面スタックを管理するシングルトン的サブシステム。
		// Push/Pop/Replace はすべて pending 化し、Update() 末尾で一括処理する。
		// これにより、ボタン callback 内の Push/Pop がスタックを壊さない。
		class UIScreenManager
		{
		public:
			UIScreenManager()  = default;
			~UIScreenManager();

			// ---- 画面登録 (起動時に呼ぶ) ----

			// JSON ドキュメントパスと UIScreen 派生クラスのファクトリを登録。
			// documentPath は UIDocumentLoader に渡す (Phase 9 で実装)。
			void Register(
				std::string_view name,
				std::string_view documentPath,
				std::function<std::unique_ptr<UIScreen>()> factory = nullptr);

			// テンプレート版: Register<MyScreen>("name", "path/to/ui.json")
			template<typename T>
			void Register(std::string_view name, std::string_view documentPath)
			{
				Register(name, documentPath, []() { return std::make_unique<T>(); });
			}

			// ---- 画面操作 (pending に積む) ----

			void Push(std::string_view name);
			void Pop();
			void Replace(std::string_view name);
			void Back(); // top の OnBack() が false なら Pop()

			// キャンセル入力 (Esc / パッド B) で UIInputSystem が自動的に Back() するか。
			// 既定は無効: 画面遷移をゲームの状態機械が管理する構成 (AquaDash) では、
			// UI 側の勝手な Pop がゲーム状態と食い違い、空のスタック=何も無い画面を作ってしまう。
			// メニュー階層を UI スタックで組むゲームだけが明示的に有効化する。
			void SetBackNavigationEnabled(bool enabled) { backNavigationEnabled_ = enabled; }
			bool IsBackNavigationEnabled() const        { return backNavigationEnabled_; }

			// ---- 更新 (Application::OnUpdate() から呼ぶ) ----

			// 1. exiting_ でない画面だけ OnUpdate(dt)
			// 2. UIAnimationSystem::Update()
			// 3. ClickWait 中なら経過時間を進め、Click の再生が終わるかタイムアウトしたら clickWaitDone_ を立てる (§20)
			//    Exit 待機中なら経過時間を進め、Exit グループの再生が終わるかタイムアウトしたら
			//    ExitDone (破棄・次画面生成) を実行する
			// 4. FlushPendingOps() (pending 処理 + 画面変化時に UIInputSystem::ClearState())
			// 5. 画面変化があれば UIInputSystem::ClearState()
			void Update(float dt);

			// ---- スタックアクセス ----

			UIScreen* Top()                const;
			UIScreen* GetScreen(int index) const; // 0 = 底, StackSize()-1 = 頂上
			int       StackSize()          const { return static_cast<int>(stack_.size()); }

			// ---- エディタ用 ----

			// 登録済みドキュメントパスを返す。未登録なら空。UI Editor の保存先に使う
			std::string_view GetDocumentPath(std::string_view screenName) const;

			// 先頭画面を同名で即時再生成する (エディタ用)。
			// 内部で Reload 種別の pending op を積むだけ。Replace とは異なり
			// OnExit → OnDestroy → 破棄 → CreateScreen → OnCreate → OnEnter を同じ Flush で連続実行する。
			// 画面遷移用の Exit 待機 (設計書 9 章) はこの経路を通さない。
			void ReloadTopDocument();

			// スタックの全画面を上から即時に破棄する（再生の停止後に UI を空へ戻すために使う）。
			// 各画面で OnExit（Exit 待機中で呼び済みなら省く）→ OnDestroy → ルート UIObject の破棄を行い、
			// 保留中の操作・Click 待ち・Exit 待ちの状態も捨てる。画面の登録（registry）は残す。
			// Update / FlushPendingOps の中から呼ばないこと。
			void Clear();

		private:
			struct PendingOp
			{
				enum class Type { Push, Pop, Replace, Back, Reload } type;
				std::string screenName;
			};

			struct ScreenEntry
			{
				std::string documentPath;
				std::function<std::unique_ptr<UIScreen>()> factory;
			};

			bool FlushPendingOps(); // 戻り値: 画面変化があったか
			std::unique_ptr<UIScreen> CreateScreen(std::string_view name);
			void InstantiateRoot(UIScreen& screen, std::string_view docPath); // JSON からルート UIObject を生成

			// Pop/Replace の先頭画面に Exit 演出を開始する (§9.2)。呼び出し前にスタックが非空であること。
			// Exit グループのクリップが無ければその場で CompleteExit() まで進めて true (完了) を返す。
			// クリップがあれば exitWaiting_ に入って false (待機) を返す。
			bool BeginExit(const PendingOp& op);

			// Exit 待機を終え、先頭画面を破棄して次画面 (Pop: 新しい先頭 / Replace: 新規生成) へ進める (§9.2)
			void CompleteExit();

			// Pop / Replace の BeginExit の前に、先頭画面に再生中の Click があれば ClickWait へ入る (§20.2)。
			// 入ったら true。呼び出し側は起点 op 自身を含めて残りを pendingOps_ の先頭へ戻して Flush を抜ける
			bool TryBeginClickWait();

			std::vector<std::unique_ptr<UIScreen>>          stack_;
			std::vector<PendingOp>                          pendingOps_;
			std::unordered_map<std::string, ScreenEntry>    registry_;
			bool                                            backNavigationEnabled_ = false;

			// ---- Exit 待機 (§9.2) ----
			bool      exitWaiting_ = false; // 先頭画面が Exit グループの再生待ち
			float     exitElapsed_ = 0.f;   // 待機開始からの経過時間
			PendingOp exitOp_;              // 待機完了後に実行する Pop / Replace

			static constexpr float kExitTimeoutSec = 2.0f; // Exit 待機のタイムアウト (§13)

			// ---- Click 演出の完了待ち (§20) ----
			bool      clickWaiting_  = false; // 先頭画面の Click クリップの再生待ち
			float     clickElapsed_  = 0.f;   // 待機開始からの経過時間
			bool      clickWaitDone_ = false; // 待機を終えた。Flush で最初の Pop / Replace が 1 回だけ消費する

			static constexpr float kClickWaitTimeoutSec = 2.0f; // ClickWait のタイムアウト (§20.2)
		};

	} // namespace ui
} // namespace aq
