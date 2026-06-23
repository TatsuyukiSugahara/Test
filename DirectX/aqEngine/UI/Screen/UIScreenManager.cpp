#include "aq.h"
#include "UIScreenManager.h"
#include "UIScreen.h"
#include "UI/UIContext.h"
#include "UI/UIObject.h"
#include "UI/Input/UIInputSystem.h"
#include "UI/Animation/UIAnimationSystem.h"
#include "UI/Resource/UIDocumentLoader.h"
#include <cassert>

namespace aq
{
	namespace ui
	{
		UIScreenManager::~UIScreenManager() = default;


		// ---- 登録 ---------------------------------------------------------------

		void UIScreenManager::Register(
			std::string_view name,
			std::string_view documentPath,
			std::function<std::unique_ptr<UIScreen>()> factory)
		{
			ScreenEntry entry;
			entry.documentPath = documentPath;
			entry.factory      = factory ? std::move(factory)
			                             : []() { return std::make_unique<UIScreen>(); };
			registry_[std::string(name)] = std::move(entry);
		}


		// ---- 画面操作 (pending) -------------------------------------------------

		void UIScreenManager::Push(std::string_view name)
		{
			pendingOps_.push_back({ PendingOp::Type::Push, std::string(name) });
		}

		void UIScreenManager::Pop()
		{
			pendingOps_.push_back({ PendingOp::Type::Pop, {} });
		}

		void UIScreenManager::Replace(std::string_view name)
		{
			pendingOps_.push_back({ PendingOp::Type::Replace, std::string(name) });
		}

		void UIScreenManager::Back()
		{
			pendingOps_.push_back({ PendingOp::Type::Back, {} });
		}


		// ---- 更新 ---------------------------------------------------------------

		void UIScreenManager::Update(float dt)
		{
			// 1. 全スタック画面のアニメーション更新
			UIAnimationSystem::Update(*this, dt);

			// 2. pending ops 処理
			bool changed = FlushPendingOps();

			// 3. 画面変化があれば入力状態を完全リセット (ダングリング防止)
			if (changed)
				UIContext::Get().GetInputSystem().ClearState();
		}


		// ---- スタックアクセス ---------------------------------------------------

		UIScreen* UIScreenManager::Top() const
		{
			return stack_.empty() ? nullptr : stack_.back().get();
		}

		UIScreen* UIScreenManager::GetScreen(int index) const
		{
			if (index < 0 || index >= static_cast<int>(stack_.size())) return nullptr;
			return stack_[index].get();
		}


		// ---- 内部 ---------------------------------------------------------------

		bool UIScreenManager::FlushPendingOps()
		{
			if (pendingOps_.empty()) return false;

			bool changed = false;
			auto ops     = std::move(pendingOps_); // swap out して再入を防ぐ
			pendingOps_.clear();

			for (auto& op : ops)
			{
				switch (op.type)
				{
				case PendingOp::Type::Push:
				{
					if (!stack_.empty()) stack_.back()->OnPause();
					auto screen = CreateScreen(op.screenName);
					if (screen)
					{
						screen->OnCreate();
						screen->OnEnter();
						stack_.push_back(std::move(screen));
						changed = true;
					}
					break;
				}

				case PendingOp::Type::Pop:
				{
					if (!stack_.empty())
					{
						stack_.back()->OnExit();
						stack_.back()->OnDestroy();
						// ルート UIObject を破棄
						if (UIObject* root = stack_.back()->root_)
							UIContext::Get().DestroyObject(root);
						stack_.pop_back();
						if (!stack_.empty()) stack_.back()->OnResume();
						changed = true;
					}
					break;
				}

				case PendingOp::Type::Replace:
				{
					if (!stack_.empty())
					{
						stack_.back()->OnExit();
						stack_.back()->OnDestroy();
						if (UIObject* root = stack_.back()->root_)
							UIContext::Get().DestroyObject(root);
						stack_.pop_back();
						changed = true;
					}
					auto screen = CreateScreen(op.screenName);
					if (screen)
					{
						screen->OnCreate();
						screen->OnEnter();
						stack_.push_back(std::move(screen));
					}
					break;
				}

				case PendingOp::Type::Back:
				{
					UIScreen* top = Top();
					if (!top || !top->OnBack())
					{
						// OnBack() が false → Pop
						pendingOps_.push_back({ PendingOp::Type::Pop, {} });
					}
					break;
				}
				}
			}

			// Back が追加した Pop を再帰処理
			if (!pendingOps_.empty())
				changed |= FlushPendingOps();

			return changed;
		}

		std::unique_ptr<UIScreen> UIScreenManager::CreateScreen(std::string_view name)
		{
			auto it = registry_.find(std::string(name));
			if (it == registry_.end()) return nullptr;

			auto screen   = it->second.factory();
			screen->name_ = name;
			InstantiateRoot(*screen, it->second.documentPath);
			return screen;
		}

		void UIScreenManager::InstantiateRoot(UIScreen& screen, std::string_view docPath)
		{
			screen.root_ = UIDocumentLoader::Load(screen.GetName(), docPath, UIContext::Get());
		}

	} // namespace ui
} // namespace aq
