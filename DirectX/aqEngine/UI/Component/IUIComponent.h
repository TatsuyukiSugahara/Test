#pragma once

namespace aq
{
	namespace ui
	{
		class UIObject;

		class IUIComponent
		{
		public:
			virtual ~IUIComponent() = default;

			UIObject* GetOwner() const { return owner_; }

		protected:
			friend class UIObject;
			UIObject* owner_ = nullptr;
		};

	} // namespace ui
} // namespace aq
