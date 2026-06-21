#include "aq.h"
#include "RenderingDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>

namespace aq
{
	namespace rendering
	{
		void RenderingDebugPanel::AddTab(const char* label, IDebugRenderable* panel)
		{
			EngineAssertMsg(label && label[0] != '\0',
				"AddTab: label が空です。GetDebugLabel() の override 漏れの可能性があります");
			EngineAssertMsg(panel != nullptr, "AddTab: panel が nullptr です");
			tabs_.push_back({ label, panel });
		}


		void RenderingDebugPanel::TakeOwnership(std::unique_ptr<IDebugRenderable> panel)
		{
			owned_.push_back(std::move(panel));
		}


		void RenderingDebugPanel::DebugRenderMenu()
		{
			ImGui::MenuItem("Rendering", nullptr, &show_);
		}


		void RenderingDebugPanel::DebugRender()
		{
			if (!show_) return;

			if (ImGui::Begin("Rendering"))
			{
				if (ImGui::BeginTabBar("##rendering"))
				{
					for (auto& tab : tabs_)
					{
						if (tab.panel && ImGui::BeginTabItem(tab.label))
						{
							tab.panel->RenderContent();
							ImGui::EndTabItem();
						}
					}
					ImGui::EndTabBar();
				}
			}
			ImGui::End();
		}
	}
}
#endif
