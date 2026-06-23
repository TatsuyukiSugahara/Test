#include "aq.h"
#include "UIObject.h"
#include "UIContext.h"
#include <algorithm>
#include <cassert>

namespace aq
{
	namespace ui
	{
		UIObject::~UIObject()
		{
			// デストラクタは UIContext::DestroyObject() からのみ呼ばれる。
			// UIContext が Unregister を管理するため、ここでは何もしない。
		}


		void UIObject::AddChild(UIObject* child)
		{
			assert(child && child != this && child->parent_ == nullptr);
			child->parent_       = this;
			child->siblingIndex_ = static_cast<int>(children_.size());
			children_.push_back(child);
		}

		void UIObject::RemoveChild(UIObject* child)
		{
			auto it = std::find(children_.begin(), children_.end(), child);
			if (it == children_.end()) return;
			(*it)->parent_ = nullptr;
			children_.erase(it);
			// siblingIndex を詰め直す
			for (int i = 0; i < static_cast<int>(children_.size()); ++i)
				children_[i]->siblingIndex_ = i;
		}

		UIObject* UIObject::FindChild(std::string_view name) const
		{
			for (auto* c : children_)
				if (c->name_ == name) return c;
			return nullptr;
		}

		UIObject* UIObject::FindDescendant(std::string_view name) const
		{
			for (auto* c : children_)
			{
				if (c->name_ == name) return c;
				if (auto* found = c->FindDescendant(name)) return found;
			}
			return nullptr;
		}

		bool UIObject::IsActiveInHierarchy() const
		{
			if (!activeSelf_) return false;
			if (parent_) return parent_->IsActiveInHierarchy();
			return true;
		}

		void UIObject::SetActive(bool active)
		{
			activeSelf_ = active;
		}

	} // namespace ui
} // namespace aq
