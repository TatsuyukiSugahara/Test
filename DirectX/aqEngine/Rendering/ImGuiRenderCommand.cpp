#include "aq.h"
#ifdef AQ_IMGUI
#include "ImGuiRenderCommand.h"
#include <imgui/imgui.h>
#ifdef ENGINE_GRAPHICS_D3D11
#include <imgui/imgui_impl_dx11.h>
#elif defined(ENGINE_GRAPHICS_D3D12)
#include "Graphics/GraphicsDevice.h"
#include "Graphics/D3D12/D3D12GraphicsDeviceImpl.h"
#include "Graphics/D3D12/D3D12ImGui.h"
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
#elif defined(ENGINE_GRAPHICS_D3D12)
			// 描画パス末尾。現在のRTV(表示RT)へ ImGui を記録する。
			auto* impl = static_cast<graphics::D3D12GraphicsDeviceImpl*>(
				graphics::GraphicsDevice::Get().GetImplRaw());
			impl->BeginFrameIfNeeded();
			graphics::D3D12ImGui::Render(drawData_, impl->GetCommandList());
#endif
		}
	}
}
#endif // AQ_IMGUI
