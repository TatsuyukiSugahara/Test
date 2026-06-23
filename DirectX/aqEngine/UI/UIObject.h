#pragma once
#include <memory>
#include <vector>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include "UITypes.h"
#include "Component/IUIComponent.h"

namespace aq
{
	namespace ui
	{
		// UIObject: コンポーネントを保持する UI ツリーノード。
		// 生成・破棄は UIContext 経由で行う。
		// スタックオブジェクトや直接 new は禁止 (中央レジストリ管理のため)。
		class UIObject
		{
		public:
			// ---- コンポーネント操作 ----

			template<typename T, typename... Args>
			T* AddComponent(Args&&... args)
			{
				static_assert(std::is_base_of_v<IUIComponent, T>, "T must derive from IUIComponent");
				auto comp         = std::make_unique<T>(std::forward<Args>(args)...);
				comp->owner_      = this;
				T* ptr            = comp.get();
				components_[std::type_index(typeid(T))] = std::move(comp);
				return ptr;
			}

			template<typename T>
			T* GetComponent() const
			{
				auto it = components_.find(std::type_index(typeid(T)));
				if (it == components_.end()) return nullptr;
				return static_cast<T*>(it->second.get());
			}

			template<typename T>
			bool HasComponent() const { return GetComponent<T>() != nullptr; }

			// ---- 子 UIObject ----

			void       AddChild(UIObject* child);
			void       RemoveChild(UIObject* child);
			UIObject*  FindChild(std::string_view name) const;       // 直下のみ
			UIObject*  FindDescendant(std::string_view name) const;  // 再帰検索

			UIObject*              GetParent()   const { return parent_; }
			const std::vector<UIObject*>& GetChildren() const { return children_; }
			int                    GetSiblingIndex() const { return siblingIndex_; }

			// ---- アクティブ状態 ----

			bool IsActiveSelf()  const { return activeSelf_; }
			bool IsActiveInHierarchy() const;
			void SetActive(bool active);

			// ---- 識別子 ----

			std::string_view  GetName()   const { return name_; }
			void              SetName(std::string_view name) { name_ = name; }
			UIObjectHandle    GetHandle() const { return handle_; }

		private:
			friend class UIContext;

			// UIContext 経由でのみ生成
			UIObject() = default;

		public:
			~UIObject();

			std::string                name_;
			UIObjectHandle             handle_;
			bool                       activeSelf_    = true;

			UIObject*                  parent_        = nullptr;
			std::vector<UIObject*>     children_;
			int                        siblingIndex_  = 0;

			std::unordered_map<std::type_index, std::unique_ptr<IUIComponent>> components_;
		};

	} // namespace ui
} // namespace aq
