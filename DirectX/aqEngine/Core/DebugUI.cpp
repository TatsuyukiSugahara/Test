#include "aq.h"
#include "DebugUI.h"
#ifdef AQ_DEBUG_IMGUI
#include "IDebugRenderable.h"
#include <algorithm>

namespace aq
{
	DebugUI* DebugUI::instance_ = nullptr;


	void DebugUI::Initialize()
	{
		if (!instance_)
			instance_ = new DebugUI();
	}

	DebugUI& DebugUI::Get()
	{
		return *instance_;
	}

	bool DebugUI::IsAvailable()
	{
		return instance_ != nullptr;
	}

	void DebugUI::Finalize()
	{
		delete instance_;
		instance_ = nullptr;
	}

	void DebugUI::Register(IDebugRenderable* item)
	{
		if (item)
			items_.push_back(item);
	}

	void DebugUI::Unregister(IDebugRenderable* item)
	{
		items_.erase(std::remove(items_.begin(), items_.end(), item), items_.end());
	}

	void DebugUI::DebugRenderMenuAll()
	{
		for (auto* item : items_)
			item->DebugRenderMenu();
	}

	void DebugUI::DebugRenderAll()
	{
		for (auto* item : items_)
			item->DebugRender();
	}
}
#endif
