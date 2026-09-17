#include "aq.h"
#include "UIScreenManager.h"
#include "UIScreen.h"
#include "UI/UIObject.h"
#include "UI/Input/UIInputSystem.h"
#include "UI/Animation/UIAnimationSystem.h"
#include "UI/Animation/UIAnimationClip.h"
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
			// 1. exiting_ でない画面だけ OnUpdate (Exit 待機中はゲーム状態を進めない)
			for (auto& screen : stack_)
			{
				if (!screen->exiting_)
					screen->OnUpdate(dt);
			}

			// 2. アニメーション更新
			UIAnimationSystem::Update(*this, dt);

			bool changed = false;

			// 3. Exit 待機の進行 (§9.2)
			if (exitWaiting_)
			{
				exitElapsed_ += dt;

				UIScreen* top          = Top();
				bool      stillPlaying = top && UIAnimationSystem::IsAnimationGroupPlaying(top->root_, kUIAnimGroupExit);
				if (!stillPlaying || exitElapsed_ >= kExitTimeoutSec)
				{
					if (stillPlaying)
					{
						// ループする Exit クリップ等でグループが終わらない場合の保険 (§4.5 / §13)
						EnginePrintf("[UIScreen] exit animation timed out (%.1fs): '%s'\n",
							kExitTimeoutSec, std::string(top->GetName()).c_str());
					}
					CompleteExit();
					changed = true;
				}
			}

			// 4. pending ops 処理
			changed |= FlushPendingOps();

			// 5. 画面変化があれば入力状態を完全リセット (ダングリング防止)
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


		// ---- エディタ用 -----------------------------------------------------------

		std::string_view UIScreenManager::GetDocumentPath(std::string_view screenName) const
		{
			auto it = registry_.find(std::string(screenName));
			if (it == registry_.end()) return {};
			return it->second.documentPath;
		}

		void UIScreenManager::ReloadTopDocument()
		{
			if (stack_.empty()) return;
			pendingOps_.push_back({ PendingOp::Type::Reload, std::string(Top()->GetName()) });
		}


		// ---- 内部 ---------------------------------------------------------------

		bool UIScreenManager::FlushPendingOps()
		{
			if (exitWaiting_) return false; // Exit 待機中は何もしない (op は pendingOps_ に残す)
			if (pendingOps_.empty()) return false;

			bool changed = false;
			auto ops     = std::move(pendingOps_); // swap out して再入を防ぐ
			pendingOps_.clear();

			for (size_t i = 0; i < ops.size(); ++i)
			{
				auto& op = ops[i];
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
						UIAnimationSystem::PlayGroup(screen->root_, kUIAnimGroupEnter); // OnEnter 完了後に起動 (§8.1)
						stack_.push_back(std::move(screen));
						changed = true;
					}
					break;
				}

				case PendingOp::Type::Pop:
				{
					if (stack_.empty()) break; // 対象が無い。何もしない

					if (!BeginExit(op))
					{
						// Exit 待機に入った。未処理の残り op を順序を保って先頭へ戻し、Flush を抜ける
						pendingOps_.insert(pendingOps_.begin(),
							std::make_move_iterator(ops.begin() + i + 1),
							std::make_move_iterator(ops.end()));
						return true; // Exit 開始も画面変化として扱う
					}
					changed = true;
					break;
				}

				case PendingOp::Type::Replace:
				{
					if (stack_.empty())
					{
						// Exit する対象が無い。従来どおり生成だけ行う
						auto screen = CreateScreen(op.screenName);
						if (screen)
						{
							screen->OnCreate();
							screen->OnEnter();
							UIAnimationSystem::PlayGroup(screen->root_, kUIAnimGroupEnter);
							stack_.push_back(std::move(screen));
							changed = true;
						}
						break;
					}

					if (!BeginExit(op))
					{
						pendingOps_.insert(pendingOps_.begin(),
							std::make_move_iterator(ops.begin() + i + 1),
							std::make_move_iterator(ops.end()));
						return true;
					}
					changed = true;
					break;
				}

				case PendingOp::Type::Reload:
				{
					// 従来の Replace と同じ即時処理。Exit 待機の状態機械は通さない (§3.3)
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
						UIAnimationSystem::PlayGroup(screen->root_, kUIAnimGroupEnter);
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

		bool UIScreenManager::BeginExit(const PendingOp& op)
		{
			UIScreen* top = stack_.back().get(); // 呼び出し元がスタック非空を保証する

			// ExitStart: 入力停止 → OnExit → Exit グループ再生 (§9.2)
			top->exiting_ = true;
			UIContext::Get().GetInputSystem().ClearState();
			top->OnExit();
			UIAnimationSystem::PlayGroup(top->root_, kUIAnimGroupExit);

			if (!UIAnimationSystem::IsAnimationGroupPlaying(top->root_, kUIAnimGroupExit))
			{
				// Exit クリップが 1 本も無い。待たずに破棄まで進める
				exitOp_ = op;
				CompleteExit();
				return true;
			}

			exitWaiting_ = true;
			exitElapsed_ = 0.f;
			exitOp_      = op;
			return false;
		}

		void UIScreenManager::CompleteExit()
		{
			// ExitDone: 先頭画面の破棄 → 次画面へ (§9.2)
			UIScreen* top = stack_.back().get();
			top->OnDestroy();
			if (UIObject* root = top->root_)
				UIContext::Get().DestroyObject(root);
			stack_.pop_back();

			if (exitOp_.type == PendingOp::Type::Pop)
			{
				if (!stack_.empty()) stack_.back()->OnResume();
			}
			else // Replace
			{
				auto screen = CreateScreen(exitOp_.screenName);
				if (screen)
				{
					screen->OnCreate();
					screen->OnEnter();
					UIAnimationSystem::PlayGroup(screen->root_, kUIAnimGroupEnter); // OnEnter 完了後に起動 (§8.1)
					stack_.push_back(std::move(screen));
				}
			}

			exitWaiting_ = false;
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
