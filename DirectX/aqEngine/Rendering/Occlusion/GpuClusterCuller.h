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
		class HiZRenderer;

		/**
		 * ClusterCull.fx の b0 cbuffer と一致 (単一 CBV)。先頭 164B が有効データ。
		 * perDrawCBPool (= sizeof(VSConstantBuffer) = 192B) を流用するため 192B に揃える
		 * (D3D12ConstantBuffer::Update が slot サイズ分コピーするため OOB を防ぐ)。
		 * フラスタム平面は viewProj からシェーダ内で導出するため別途持たない。
		 */
		struct ClusterCullCBData
		{
			math::Matrix4x4 world;        // 64
			math::Matrix4x4 viewProj;     // 64 clip空間 view*proj
			math::Vector3   camPos;       // 12
			uint32_t        clusterCount; //  4 -> 144
			float           hiZW;         //  4 バインドした Hi-Z レベル幅 (0=Hi-Z無効)
			float           hiZH;         //  4 同 高さ
			float           nearZ;        //  4
			float           farZ;         //  4 -> 160
			uint32_t        hiZValid;     //  4 0=Hi-Z無効(オクリュージョンスキップ)
			float           _pad[7];      // 28 -> 192
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

			/** Hi-Z オクリュージョンを供給する (任意)。null ならクラスタ判定はフラスタム+バックフェースのみ。 */
			void SetHiZSource(HiZRenderer* hiZ) { hiZSource_ = hiZ; }

			/** 1 アイテムを GPU カリング (item の gpuOutIndices / gpuArgs を更新)。レンダースレッドから。 */
			void Cull(graphics::RenderContext& ctx, FrameContext& fc,
			          const RenderItem& item, const CameraData& camera);

		private:
			std::shared_ptr<graphics::IShader> resetCS_;
			std::shared_ptr<graphics::IShader> cullCS_;
			HiZRenderer* hiZSource_ = nullptr;  // 任意。Hi-Z オクリュージョン供給元 (寿命は Application)
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
