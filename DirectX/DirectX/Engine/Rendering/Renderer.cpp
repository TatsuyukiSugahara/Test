#include "Renderer.h"
#include "DrawItemCommand.h"
#include "FrameContext.h"
#include "../Graphics/RenderContext.h"
#include "../Graphics/GraphicsTypes.h"
#include "../Graphics/GraphicsDevice.h"
#include "../Graphics/Lighting.h"


namespace engine
{
	namespace rendering
	{
		void Renderer::BuildCommandList(const RenderFrame& frame, RenderCommandList& outList) const
		{
			for (const RenderItem& item : frame.items)
			{
				RecordDrawItem(item, frame.camera, outList);
			}
		}


#if _DEBUG
		void Renderer::RenderDebugSync(graphics::RenderContext& context, const RenderFrame& frame)
		{
			// デバッグ専用の同期実行パス。このフレーム専用の CB プールを一時生成する。
			// プロダクションコードは BuildCommandList + RenderThread の永続プールを使うこと。
			// 必ずレンダースレッドから呼ぶこと（D3D11 immediate context はスレッド非安全）。
			ConstantBufferPool perDrawPool(sizeof(graphics::VSConstantBuffer));
			ConstantBufferPool materialPool(sizeof(graphics::MaterialCBData));

			auto lightingCB = graphics::GraphicsDevice::Get().CreateConstantBuffer(
				&frame.lighting, sizeof(frame.lighting));
			context.UpdateSubresource(*lightingCB, frame.lighting);

			FrameContext fc { &perDrawPool, &materialPool, lightingCB.get() };

			RenderCommandList list;
			BuildCommandList(frame, list);
			list.Execute(context, fc);
		}
#endif


		void Renderer::RecordDrawItem(
			const RenderItem&  item,
			const CameraData&  camera,
			RenderCommandList& outList) const
		{
			outList.Enqueue<DrawItemCommand>(item, camera);
		}
	}
}
