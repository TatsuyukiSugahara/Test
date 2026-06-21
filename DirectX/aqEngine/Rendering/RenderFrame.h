#pragma once
#include <memory>
#include <vector>
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Graphics/IBuffer.h"
#include "Graphics/IShader.h"
#include "Graphics/ISamplerState.h"
#include "Graphics/IShaderResourceView.h"
#include "Graphics/Lighting.h"
#include "Shadow/ShadowData.h"
#include "Ocean/OceanData.h"


namespace aq
{
	namespace rendering
	{
		/** Camera snapshot copied on the game thread. */
		struct CameraData
		{
			math::Matrix4x4 viewMatrix;
			math::Matrix4x4 projectionMatrix;
			math::Vector3   position;
		};


		/** テクスチャスロット番号 (t0-t3) */
		enum class TextureSlot : uint32_t
		{
			Albedo   = 0,
			Normal   = 1,
			Specular = 2,
			Emissive = 3,
			Count,
		};


		/**
		 * One draw call worth of data.
		 *
		 * All GPU resources are held via shared_ptr so the render thread can safely
		 * consume this struct one frame after it was built.  StaticMesh::FillRenderItem
		 * uses the aliasing constructor so that shader/texture ResourceBase owners are
		 * kept alive transitively.
		 *
		 * Constant buffer data (world/view/proj) is stored as plain matrices; the actual
		 * IConstantBuffer is allocated per-draw from FrameContext::perDrawCBPool at
		 * execute time, not bound to the mesh.
		 */
		struct RenderItem
		{
			std::shared_ptr<graphics::IVertexBuffer>  vertexBuffer;
			std::shared_ptr<graphics::IIndexBuffer>   indexBuffer;
			std::shared_ptr<graphics::ISamplerState>  samplerState;
			std::shared_ptr<graphics::IShader>        vs;
			std::shared_ptr<graphics::IShader>        ps;

			std::shared_ptr<graphics::IShaderResourceView>
				textures[static_cast<uint32_t>(TextureSlot::Count)];

			uint32_t                  indexCount  = 0;
			math::Matrix4x4           worldMatrix;
			uint32_t                  layer       = 0;
			graphics::MaterialCBData  materialCB;
			bool                      castShadow    = false;
			bool                      receiveShadow = false;

			// SkeletalMesh 用ボーン行列。nullptr = StaticMesh (スキンなし)
			// shared_ptr で共有することでフレーム跨ぎのコピーコストを抑える
			std::shared_ptr<std::vector<math::Matrix4x4>> boneMatrices;
		};


		// 海描画1件分のデータ (RenderItem + OceanCB)
		struct OceanRenderItem
		{
			RenderItem          base;
			ocean::OceanCBData  oceanCB;
		};


		/**
		 * One frame's rendering snapshot.
		 * Built by the game thread; consumed by Renderer / RenderThread.
		 */
		struct RenderFrame
		{
			CameraData                   camera;
			graphics::LightingData       lighting;
			ShadowCBData                 shadow;
			std::vector<RenderItem>      items;
			std::vector<OceanRenderItem> oceanItems;  // 海描画用

			void Clear() { items.clear(); oceanItems.clear(); }
		};
	}
}
