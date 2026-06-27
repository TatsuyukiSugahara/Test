#pragma once
#include <memory>
#include <vector>
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Math/Bounds.h"
#include "Graphics/Meshlet.h"
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
			float           nearZ = 0.1f;
			float           farZ  = 1000.0f;
		};


		/** テクスチャスロット番号 (t0-t3) */
		enum class TextureSlot : uint32_t
		{
			Albedo            = 0,
			Normal            = 1,
			Specular          = 2,  // forward (Blinn-Phong) 用
			MetallicRoughness = 2,  // PBR 用 alias: R=metallic, G=roughness
			Emissive          = 3,
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
			/** G-Buffer パス専用 PS。nullptr = forward-only アイテム。 */
			std::shared_ptr<graphics::IShader>        gbufferPS;

			std::shared_ptr<graphics::IShaderResourceView>
				textures[static_cast<uint32_t>(TextureSlot::Count)];

			uint32_t                  indexCount  = 0;
			math::Matrix4x4           worldMatrix;
			uint32_t                  layer       = 0;
			graphics::MaterialCBData  materialCB;
			bool                      castShadow    = false;
			bool                      receiveShadow = false;

			// カリング用ローカル空間 AABB。hasBounds == false のアイテムは
			// バウンディング未確定としてフラスタムカリングの対象外 (常に可視) とする。
			math::AABB                localBounds;
			bool                      hasBounds     = false;

			// トライアングル(クラスタ)カリング用。clusters/reorderedIndices はリソースを
			// alias した共有ポインタ (コピー安価)。cullIndexBuffer は compact 描画用の動的IB。
			// いずれか nullptr ならクラスタカリングせず通常描画する。
			std::shared_ptr<graphics::IIndexBuffer>                        cullIndexBuffer;
			std::shared_ptr<const std::vector<graphics::MeshCluster>>      clusters;
			std::shared_ptr<const std::vector<uint32_t>>                   reorderedIndices;

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
		 * 投影デカール用 定数バッファ (b0)。
		 * perDrawCBPool (= sizeof(VSConstantBuffer) = 192B) から Allocate するため
		 * サイズを 192B に固定する。Decal.fx の cbuffer DecalCB と一致させること。
		 */
		struct DecalCBData
		{
			math::Matrix4x4 worldToDecal;   // ワールド座標 → デカールローカル ([-0.5,0.5]^3)
			math::Vector4   color;          // rgb=色ティント, a=不透明度
			math::Vector4   forward;        // xyz=投影軸(ワールド), w=角度フェード下限 (cosθ)
			math::Vector4   pad[6];         // 192B へのパディング
		};
		static_assert(sizeof(DecalCBData) == 192,
		              "DecalCBData must match perDrawCBPool slot size (192B)");


		// デカール描画1件分のデータ。
		struct DecalRenderItem
		{
			std::shared_ptr<graphics::IShaderResourceView> texture;  // デカールテクスチャ (t0)
			DecalCBData                                    cb;
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
			std::vector<RenderItem>      items;        // deferred (opaque, gbufferPS あり)
			std::vector<RenderItem>      forwardItems;  // forward  (透明・特殊マテリアル)
			std::vector<OceanRenderItem> oceanItems;   // 海描画用
			std::vector<DecalRenderItem> decalItems;   // 投影デカール (GBuffer へ書き戻し)

			void Clear() { items.clear(); forwardItems.clear(); oceanItems.clear(); decalItems.clear(); }
		};
	}
}
