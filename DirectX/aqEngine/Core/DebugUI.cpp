#include "aq.h"
#include "DebugUI.h"
#ifdef AQ_DEBUG_IMGUI
#include "IDebugRenderable.h"
#include <imgui/imgui.h>
#include "Util/Profiler.h"
#include <iterator>

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
		// カテゴリを登録順で収集し、同名カテゴリの項目を 1 つのドロップダウンメニューへまとめる。
		auto categoryOf = [](IDebugRenderable* item) -> const char*
		{
			const char* c = item->GetDebugCategory();
			return (c && c[0] != '\0') ? c : "Misc";
		};

		std::vector<const char*> categories;
		for (auto* item : items_)
		{
			const char* cat = categoryOf(item);
			bool found = false;
			for (const char* c : categories)
				if (std::strcmp(c, cat) == 0) { found = true; break; }
			if (!found) categories.push_back(cat);
		}

		// 並び順を固定する（プレビュー準拠）。未知カテゴリは first-seen 順、"Misc" は末尾。
		auto priority = [](const char* cat) -> int
		{
			static const char* const kOrder[] = { "Rendering", "UI", "Tools", "Profiling" };
			for (int i = 0; i < static_cast<int>(std::size(kOrder)); ++i)
				if (std::strcmp(cat, kOrder[i]) == 0) return i;
			if (std::strcmp(cat, "Misc") == 0) return 1000;   // 末尾
			return 100;                                       // 未知は中間（first-seen 安定）
		};
		std::stable_sort(categories.begin(), categories.end(),
			[&](const char* a, const char* b) { return priority(a) < priority(b); });

		for (const char* cat : categories)
		{
			if (ImGui::BeginMenu(cat))
			{
				for (auto* item : items_)
					if (std::strcmp(categoryOf(item), cat) == 0)
						item->DebugRenderMenu();
				ImGui::EndMenu();
			}
		}
	}

	void DebugUI::DebugRenderAll()
	{
		for (auto* item : items_)
		{
			AQ_PROFILE_SCOPE(item->GetDebugLabel());
			item->DebugRender();
		}
	}
}
#endif
