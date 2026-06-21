#pragma once
#include "Ocean/OceanData.h"
#include "Graphics/StaticMesh.h"
#include "Rendering/RenderFrame.h"

namespace aq
{
	namespace ocean
	{
		// ============================================================
		// OceanMesh — 平坦なグリッドメッシュ
		//
		// HeightmapChunk と同じ流れで構築するが高さ情報は持たない。
		// Gerstner 波の変位はすべてシェーダー側で行う。
		//
		// 使い方:
		//   OceanMesh mesh;
		//   mesh.Initialize(params);
		//   mesh.Update(position, rotation, scale);  // Transform から毎フレーム呼ぶ
		//   rendering::OceanRenderItem item;
		//   mesh.FillRenderItem(item, params, time);
		// ============================================================
		class OceanMesh
		{
		public:
			void Initialize(const OceanParams& params);
			void Update(const math::Vector3& position,
			            const math::Quaternion& rotation,
			            const math::Vector3& scale);

			// 描画に必要なデータを OceanRenderItem へ書き込む
			// time: aq::Engine::GetTotalTime() を渡す
			bool FillRenderItem(rendering::OceanRenderItem& item,
			                    const OceanParams& params,
			                    float time) const;

		private:
			graphics::StaticMesh mesh_;
		};
	}
}
