#pragma once
#include "IRenderCommand.h"
#include "RenderFrame.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * b2: 草などの風揺れパラメータ。perDrawCBPool (=sizeof(VSConstantBuffer)=192B) のスロットを
		 * 借りるため 192B に揃える (GpuClusterCuller の ClusterCullCBData と同じ流儀)。
		 * InstancedGrass.fx の cbuffer WindCB と一致させること。
		 */
		struct InstancedWindCBData
		{
			math::Vector4 windParams;      // 16 x=経過時間[s], y=揺れ幅[m], z=周波数, w=未使用
			math::Vector4 windDirection;   // 16 xyz=風向, w=未使用 -> 32
			float         _pad[40];        // 160 -> 192
		};
		static_assert(sizeof(InstancedWindCBData) == 192, "InstancedWindCBData must match perDrawCBPool slot (192B)");




		/**
		 * 1 メッシュを instanceCount 個まとめて描く1ドローぶんのコマンド。
		 * Execute() で b0(view/projection)を確定し、slot0=共有ジオメトリ・slot1=per-instance
		 * ワールド行列(動的VB)をバインドして DrawIndexedInstanced する。
		 * world は per-instance ストリームから取るため b0 の world は使わない。
		 */
		class InstancedDrawItemCommand final : public IRenderCommand
		{
		public:
			InstancedDrawItemCommand(const InstancedRenderItem& item, const CameraData& camera);
			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;

		private:
			InstancedRenderItem item_;
			CameraData          camera_;
		};
	}
}
