#include "aq.h"
#ifdef AQ_IMGUI
#include "ImGuiRenderCommand.h"
#include <imgui/imgui.h>
// imgui の D3D11 バックエンド依存。D3D12 選択時は ImGui_ImplDX12 系に差し替えるまで描画は no-op。
#ifdef ENGINE_GRAPHICS_D3D11
#include <imgui/imgui_impl_dx11.h>
#endif

namespace aq
{
	namespace rendering
	{
		ImGuiRenderCommand::ImGuiRenderCommand(ImDrawData* data)
			: drawData_(data)
		{}

		void ImGuiRenderCommand::Execute(graphics::RenderContext&, FrameContext&) const
		{
#ifdef ENGINE_GRAPHICS_D3D11
			ImGui_ImplDX11_RenderDrawData(drawData_);
#endif
		}
	}
}
#endif // AQ_IMGUI
