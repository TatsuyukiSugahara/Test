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

			// ---- 更新 (Application::OnUpdate() から呼ぶ) ----

			// 1. 全スタック画面のアニメーション更新
			// 2. FlushPendingOps() (pending 処理 + 画面変化時に UIInputSystem::ClearState())
			void Update(float dt);

			// ---- スタックアクセス ----

			UIScreen* Top()                const;
			UIScreen* GetScreen(int index) const; // 0 = 底, StackSize()-1 = 頂上
			int       StackSize()          const { return static_cast<int>(stack_.size()); }

		private:
			struct PendingOp
			{
				enum class Type { Push, Pop, Replace, Back } type;
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

			std::vector<std::unique_ptr<UIScreen>>          stack_;
			std::vector<PendingOp>                          pendingOps_;
			std::unordered_map<std::string, ScreenEntry>    registry_;
		};

	} // namespace ui
} // namespace aq
