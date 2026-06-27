#include "aq.h"
#include "GpuClusterCuller.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/IShader.h"
#include "Graphics/IGpuBuffer.h"
#include "Math/Bounds.h"


namespace aq
{
	namespace rendering
	{
		GpuClusterCuller& GpuClusterCuller::Get()
		{
			static GpuClusterCuller instance;
			return instance;
		}


		bool GpuClusterCuller::Initialize()
		{
			auto& gd = graphics::GraphicsDevice::Get();
			resetCS_ = gd.CreateShader("Assets/Shader/ClusterCullReset.fx", "main", graphics::IShader::ShaderType::CS);
			cullCS_  = gd.CreateShader("Assets/Shader/ClusterCull.fx",      "main", graphics::IShader::ShaderType::CS);
			ready_   = (resetCS_ != nullptr && cullCS_ != nullptr);
			if (!ready_) EngineAssertMsg(false, "GPU クラスタカリング シェーダのロードに失敗");
			return ready_;
		}


		void GpuClusterCuller::Cull(graphics::RenderContext& ctx, FrameContext& fc,
		                            const RenderItem& item, const CameraData& camera)
		{
			if (!ready_ || item.clusterCount == 0 ||
			    !item.gpuClusters || !item.gpuSrcIndices || !item.gpuOutIndices || !item.gpuArgs)
				return;

			graphics::IShaderResourceView*  clustersSRV = item.gpuClusters->AsSRV();
			graphics::IShaderResourceView*  srcSRV      = item.gpuSrcIndices->AsSRV();
			graphics::IUnorderedAccessView* outUAV      = item.gpuOutIndices->AsUAV();
			graphics::IUnorderedAccessView* argsUAV     = item.gpuArgs->AsUAV();
			if (!clustersSRV || !srcSRV || !outUAV || !argsUAV) return;

			// 定数バッファ (perDrawCBPool を流用: 192B slot に 176B を書く)
			graphics::IConstantBuffer* cb = fc.perDrawCBPool ? fc.perDrawCBPool->Allocate() : nullptr;
			if (!cb) return;

			ClusterCullCBData data;
			data.world = item.worldMatrix;
			math::Matrix4x4 viewProj;
			viewProj.Mull(camera.viewMatrix, camera.projectionMatrix);
			math::Frustum frustum;
			frustum.FromViewProjection(viewProj);
			frustum.GetPlanes(data.planes);
			data.camPos       = camera.position;
			data.clusterCount = item.clusterCount;
			ctx.UpdateSubresource(*cb, data);

			// 1) reset: 間接引数を {0,1,0,0,0} へ (u0 = args, u1 未使用)
			ctx.CSSetShader(*resetCS_);
			ctx.CSUnsetUnorderedAccessView(1);
			ctx.CSSetUnorderedAccessView(0, *argsUAV);
			ctx.Dispatch(1, 1, 1);
			ctx.CSUnsetUnorderedAccessView(0);
			ctx.UavBarrier(*item.gpuArgs);  // reset → cull の可視化

			// 2) cull: t0=clusters t1=srcIndices / u0=outIndices u1=args
			ctx.CSSetShader(*cullCS_);
			ctx.CSSetConstantBuffer(0, *cb);
			ctx.CSSetShaderResource(0, *clustersSRV);
			ctx.CSSetShaderResource(1, *srcSRV);
			ctx.CSSetUnorderedAccessView(0, *outUAV);
			ctx.CSSetUnorderedAccessView(1, *argsUAV);
			ctx.Dispatch((item.clusterCount + 63u) / 64u, 1, 1);
			ctx.CSUnsetShaderResource(0);
			ctx.CSUnsetShaderResource(1);
			ctx.CSUnsetUnorderedAccessView(0);
			ctx.CSUnsetUnorderedAccessView(1);
			ctx.CSUnsetShader();
		}
	}
}
