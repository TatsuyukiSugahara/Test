#include "aq.h"
#include "UIContext.h"
#include "UIObject.h"
#include "Input/UIInputSystem.h"
#include "Rendering/UIBatchRenderer.h"
#include <cassert>
#include <stdexcept>

namespace aq
{
	namespace ui
	{
		UIContext* UIContext::sInstance_ = nullptr;


		void UIContext::Initialize()
		{
			assert(!sInstance_);
			sInstance_ = new UIContext();
		}

		UIContext& UIContext::Get()
		{
			assert(sInstance_);
			return *sInstance_;
		}

		void UIContext::Finalize()
		{
			delete sInstance_;
			sInstance_ = nullptr;
		}


		UIContext::UIContext()
		{
			// ID 0 = INVALID_UI_OBJECT_ID のためスロット 0 を番兵として予約
			objectSlots_.push_back({});

			screenManager_ = std::make_unique<UIScreenManager>();
			inputSystem_   = std::make_unique<UIInputSystem>();
			batchRenderer_ = std::make_unique<UIBatchRenderer>();
		}

		UIContext::~UIContext()
		{
			// rootObjects_ が管理する UIObject を先に解放
			// (子 UIObject も UIObject のデストラクタが解放する)
			rootObjects_.clear();
		}


		// ---- UIObject ライフタイム管理 ----------------------------------------

		UIObject* UIContext::CreateObject(std::string_view name)
		{
			// unique_ptr で所有し、raw ptr を返す
			auto obj = std::unique_ptr<UIObject>(new UIObject());
			obj->SetName(name);
			obj->handle_ = Register(obj.get());

			UIObject* ptr = obj.get();
			rootObjects_.push_back(std::move(obj));
			return ptr;
		}

		void UIContext::DestroyObject(UIObject* obj)
		{
			if (!obj) return;

			// 子を先に再帰破棄
			// children_ のコピーを取得してから破棄 (DestroyObject が children_ を変更するため)
			auto children = obj->GetChildren();
			for (auto* child : children)
				DestroyObject(child);

			// 親リンクを外す
			if (obj->GetParent())
				obj->GetParent()->RemoveChild(obj);

			// ハンドルを無効化
			Unregister(obj->handle_);

			// rootObjects_ から所有権を取り出して解放
			auto& roots = rootObjects_;
			auto it = std::find_if(roots.begin(), roots.end(),
				[obj](const std::unique_ptr<UIObject>& u) { return u.get() == obj; });
			if (it != roots.end())
				roots.erase(it);
			// 子の場合は所有者 (UIScreen) 側で管理されるため、ここには存在しない
		}

		void UIContext::DestroyObject(UIObjectHandle handle)
		{
			DestroyObject(Resolve(handle));
		}


		// ---- 中央レジストリ ---------------------------------------------------

		UIObjectHandle UIContext::Register(UIObject* obj)
		{
			UIObjectID id;
			if (!freeList_.empty())
			{
				id = freeList_.back();
				freeList_.pop_back();
				objectSlots_[id].ptr = obj;
				// generation はそのまま (Unregister でインクリメント済み)
			}
			else
			{
				id = static_cast<UIObjectID>(objectSlots_.size());
				objectSlots_.push_back({ obj, 0u });
			}
			return { id, objectSlots_[id].generation };
		}

		void UIContext::Unregister(UIObjectHandle handle)
		{
			if (!handle.IsValid()) return;
			if (handle.id >= objectSlots_.size()) return;
			Slot& slot = objectSlots_[handle.id];
			if (slot.generation != handle.generation) return; // 二重 Unregister 無視
			slot.ptr = nullptr;
			++slot.generation; // 既存 UIObjectHandle をすべて stale にする
			freeList_.push_back(handle.id);
		}

		UIObject* UIContext::Resolve(UIObjectHandle handle) const
		{
			if (!handle.IsValid()) return nullptr;
			if (handle.id >= static_cast<UIObjectID>(objectSlots_.size())) return nullptr;
			const Slot& slot = objectSlots_[handle.id];
			if (slot.generation != handle.generation) return nullptr;
			return slot.ptr;
		}


		// ---- 検索 -------------------------------------------------------------

		UIObject* UIContext::FindByName(std::string_view name) const
		{
			for (auto& root : rootObjects_)
			{
				if (root->GetName() == name) return root.get();
				if (auto* found = root->FindDescendant(name)) return found;
			}
			return nullptr;
		}


		// ---- サブシステム -------------------------------------------------------

		UIScreenManager& UIContext::Screens()       { return *screenManager_; }
		UIInputSystem&   UIContext::GetInputSystem() { return *inputSystem_;   }
		UIBatchRenderer& UIContext::GetBatchRenderer() { return *batchRenderer_; }

	} // namespace ui
} // namespace aq
