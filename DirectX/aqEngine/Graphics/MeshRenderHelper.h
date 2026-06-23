#pragma once
#include <cstring>
#include <memory>
#include "IBuffer.h"
#include "IShader.h"
#include "ISamplerState.h"
#include "Resource/Resource.h"
#include "Rendering/RenderFrame.h"
#include "Math/Matrix.h"
#include "Lighting.h"


namespace aq
{
	namespace graphics
	{
		/**
		 * StaticMesh / SkeletalMesh に共通する RenderItem 充填処理。
		 * TMaterialCB には MaterialCBData または PBRMaterialCBData を渡せる。
		 * sizeof(TMaterialCB) == sizeof(MaterialCBData) でなければコンパイルエラー。
		 */
		template<typename TMaterialCB>
		inline bool FillRenderItemBase(
			rendering::RenderItem&                  item,
			bool                                    isInitialized,
			const res::RefShaderResource&           vsRes,
			const res::RefShaderResource&           psRes,
			const std::shared_ptr<IVertexBuffer>&   vb,
			const std::shared_ptr<IIndexBuffer>&    ib,
			const std::shared_ptr<ISamplerState>&   sampler,
			const res::RefGPUResource               gpuResources[],
			uint32_t                                indexCount,
			const math::Matrix4x4&                  world,
			const TMaterialCB&                      mat)
		{
			static_assert(sizeof(TMaterialCB) == sizeof(graphics::MaterialCBData),
			              "TMaterialCB size must match MaterialCBData for GPU upload");

			if (!isInitialized)                                    return false;
			if (!vsRes || !psRes)                                  return false;
			if (!vsRes->IsCompleted() || !psRes->IsCompleted())    return false;

			IShader* vs = vsRes->GetShader();
			IShader* ps = psRes->GetShader();
			if (!vs || !ps) return false;

			item.vertexBuffer = vb;
			item.indexBuffer  = ib;
			item.samplerState = sampler;
			item.vs = std::shared_ptr<IShader>(vsRes, vs);
			item.ps = std::shared_ptr<IShader>(psRes, ps);

			constexpr uint32_t slotCount = static_cast<uint32_t>(rendering::TextureSlot::Count);
			for (uint32_t i = 0; i < slotCount; ++i)
			{
				if (gpuResources[i])
				{
					IShaderResourceView* srv = gpuResources[i]->GetShaderResourceView();
					item.textures[i] = srv
						? std::shared_ptr<IShaderResourceView>(gpuResources[i], srv)
						: nullptr;
				}
				else
				{
					item.textures[i] = nullptr;
				}
			}

			item.indexCount  = indexCount;
			item.worldMatrix = world;
			item.layer       = 0;
			// raw bytes コピー: GPU は register(b2) のバイト列を見るだけなので型が異なっても正しく動く
			std::memcpy(&item.materialCB, &mat, sizeof(item.materialCB));

			return true;
		}
	}
}
