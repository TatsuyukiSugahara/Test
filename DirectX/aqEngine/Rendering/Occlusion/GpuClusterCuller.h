#pragma once
#include <memory>
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Rendering/IRenderCommand.h"
#include "Rendering/RenderFrame.h"

namespace aq
{
	namespace graphics { class IShader; class RenderContext; }

	namespace rendering
	{
		struct FrameContext;

		/**
		 * ClusterCull.fx の cbuffer と一致。先頭 176B が有効データ。
		 * perDrawCBPool (= sizeof(VSConstantBuffer) = 192B) を流用するため 192B に揃える
		 * (UpdateSubresource が slot サイズ分コピーするため OOB を防ぐ)。
		 */
		struct ClusterCullCBData
		{
			math::Matrix4x4 world;        // 64
			math::Vector4   planes[6];    // 96 (a,b,c,d ワールド視錐台)
			math::Vector3   camPos;       // 12
			uint32_t        clusterCount; //  4
			math::Vector4   _pad;         // 16 → 192B (perDrawCBPool slot に一致)
		};
		static_assert(sizeof(ClusterCullCBData) == 192, "ClusterCullCBData must match perDrawCBPool slot (192B)");


		/**
		 * GPU 駆動クラスタ(トライアングル)カリング。
		 * メッシュ 1 件につき reset + cull の 2 dispatch で、可視クラスタのインデックスを
		 * 出力IBへ compact しつつ間接引数を構築する。CPU compaction / 転送は不要。
		 */
		class GpuClusterCuller
		{
		public:
			static GpuClusterCuller& Get();

			bool Initialize();   // compute シェーダをロード
			bool IsReady() const { return ready_; }

			/** 1 アイテムを GPU カリング (item の gpuOutIndices / gpuArgs を更新)。レンダースレッドから。 */
			void Cull(graphics::RenderContext& ctx, FrameContext& fc,
			          const RenderItem& item, const CameraData& camera);

		private:
			std::shared_ptr<graphics::IShader> resetCS_;
			std::shared_ptr<graphics::IShader> cullCS_;
			bool ready_ = false;
		};


		/**
		 * 1 アイテムの GPU クラスタカリングを実行する compute コマンド。
		 * G-Buffer / forward 描画の前 (= compute フェーズ) にまとめて積む。
		 */
		class ClusterCullCommand final : public IRenderCommand
		{
		public:
			ClusterCullCommand(const RenderItem& item, const CameraData& camera)
				: item_(item), camera_(camera) {}
			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override
			{
				GpuClusterCuller::Get().Cull(ctx, fc, item_, camera_);
			}

		private:
			RenderItem item_;
			CameraData camera_;
		};
	}
}
