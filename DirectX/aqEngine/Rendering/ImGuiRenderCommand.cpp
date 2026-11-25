#include "aq.h"
#ifdef AQ_IMGUI
#include "ImGuiRenderCommand.h"
#include "Rendering/FrameContext.h"
#include <imgui/imgui.h>

namespace aq
{
	namespace rendering
	{
		ImGuiRenderCommand::ImGuiRenderCommand(ImDrawData* data)
			: drawData_(data)
		{}

		void ImGuiRenderCommand::Execute(graphics::RenderContext&, FrameContext& frameContext) const
		{
			// バックエンド固有の処理(D3D12 の BeginFrameIfNeeded、Vulkan / Metal の drawData 保持など)は
			// 各 IGraphicsDeviceImpl::ImGuiRender が担う。
			// 描画先の displayRT を先に渡す(Metal のエディタ構成が使う。ほかのバックエンドは何もしない)。
			graphics::GraphicsDevice::Get().SetImGuiRenderTarget(frameContext.displayRT);
			graphics::GraphicsDevice::Get().ImGuiRender(drawData_);
		}
	}
}
#endif // AQ_IMGUI
