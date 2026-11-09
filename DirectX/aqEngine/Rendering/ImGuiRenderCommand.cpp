#include "aq.h"
#ifdef AQ_IMGUI
#include "ImGuiRenderCommand.h"
#include <imgui/imgui.h>

namespace aq
{
	namespace rendering
	{
		ImGuiRenderCommand::ImGuiRenderCommand(ImDrawData* data)
			: drawData_(data)
		{}

		void ImGuiRenderCommand::Execute(graphics::RenderContext&, FrameContext&) const
		{
			// バックエンド固有の処理(D3D12 の BeginFrameIfNeeded、Vulkan / Metal の drawData 保持など)は
			// 各 IGraphicsDeviceImpl::ImGuiRender が担う。
			graphics::GraphicsDevice::Get().ImGuiRender(drawData_);
		}
	}
}
#endif // AQ_IMGUI
