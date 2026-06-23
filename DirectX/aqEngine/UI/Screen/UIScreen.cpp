#include "aq.h"
#include "UIScreen.h"
#include "UI/UIObject.h"
#include "UI/UIContext.h"

namespace aq
{
	namespace ui
	{
		UIObjectHandle UIScreen::FindHandle(std::string_view name) const
		{
			if (!root_) return UIObjectHandle::Invalid();
			UIObject* found = root_->FindDescendant(name);
			return found ? found->GetHandle() : UIObjectHandle::Invalid();
		}

		UIObject* UIScreen::Resolve(UIObjectHandle handle) const
		{
			return UIContext::Get().Resolve(handle);
		}

	} // namespace ui
} // namespace aq
