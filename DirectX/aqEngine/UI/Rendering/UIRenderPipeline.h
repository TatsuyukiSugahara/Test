#pragma once
#include <memory>
#include <vector>
#include "UI/UITypes.h"
#include "Graphics/IShader.h"
#include "Graphics/IBuffer.h"
#include "Graphics/ISamplerState.h"

namespace aq
{
	namespace graphics { class RenderContext; }

	namespace ui
	{
		struct UIVertex;

		// UI 描画パイプライン。
		// シェーダー / 動的 VB・IB / サンプラー / CircleGauge 用 CB を保持する。
		// UIBatchRenderer が所有し、UIBatchPayload に非所有ポインタ (pipeline*) を渡す。
		// UIBatchRenderCommand::Execute() からレンダースレッド上で呼ばれる。
		class UIRenderPipeline
		{
		public:
			// 1フレームで描画できる最大クワッド数 (4 頂点 / クワッド, 6 インデックス / クワッド)
			static constexpr uint32_t kMaxVertices = 65536u;
			static constexpr uint32_t kMaxIndices  = kMaxVertices / 4u * 6u;  // = 98304

			UIRenderPipeline();
			~UIRenderPipeline();

			UIRenderPipeline(const UIRenderPipeline&)            = delete;
			UIRenderPipeline& operator=(const UIRenderPipeline&) = delete;

			bool IsReady() const { return ready_; }

			// VB / IB に頂点・インデックスデータを書き込む (レンダースレッドから呼ぶ)
			void Upload(const std::vector<UIVertex>& vtx, const std::vector<uint16_t>& idx);

			// VS / IA / VB / IB / サンプラーをバインド
			void BindCommon(graphics::RenderContext& ctx);

			// PS をバインド (Standard / CircleGauge)
			void BindPS(graphics::RenderContext& ctx, UIShaderType type);

			// CircleGauge 用 CB を更新して PS スロット 0 にバインド
			void UpdateCircleGaugeCB(graphics::RenderContext& ctx,
			                         float fillAmount, float startAngle, float clockwise);

			// DrawIndexed (startIndex オフセットあり, D3D11 直呼び)
			void DrawIndexed(uint32_t indexCount, uint32_t startIndex);

		private:
			std::unique_ptr<graphics::IShader>         vs_;
			std::unique_ptr<graphics::IShader>         standardPS_;
			std::unique_ptr<graphics::IShader>         circleGaugePS_;
			std::unique_ptr<graphics::IVertexBuffer>   vb_;          // DYNAMIC
			std::unique_ptr<graphics::ISamplerState>   sampler_;
			std::unique_ptr<graphics::IConstantBuffer> gaugeCB_;
			void*    ib_              = nullptr;  // ID3D11Buffer* R16_UINT DYNAMIC
			uint32_t ibCapacityBytes_ = 0;
			bool     ready_           = false;
		};

	} // namespace ui
} // namespace aq
