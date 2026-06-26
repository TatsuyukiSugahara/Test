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
			static constexpr uint32_t MAX_VERTICES = 65536u;
			static constexpr uint32_t MAX_INDICES  = MAX_VERTICES / 4u * 6u;  // = 98304

			UIRenderPipeline();
			~UIRenderPipeline();

			UIRenderPipeline(const UIRenderPipeline&)            = delete;
			UIRenderPipeline& operator=(const UIRenderPipeline&) = delete;

			bool IsReady() const { return ready_; }

			// VB / IB に頂点・インデックスデータを書き込む (レンダースレッドから呼ぶ)
			void Upload(const std::vector<UIVertex>& vtx, const std::vector<uint16_t>& idx);

			// VS / IA / VB / IB / サンプラーをバインド
			void BindCommon(graphics::RenderContext& ctx);

			// PS をバインド (Standard / CircleGauge / SdfText)
			void BindPS(graphics::RenderContext& ctx, UIShaderType type);

			// CircleGauge 用 CB を更新して PS スロット 0 にバインド
			void UpdateCircleGaugeCB(graphics::RenderContext& ctx,
			                         float fillAmount, float startAngle, float clockwise);

			// SdfText 用 CB を更新して PS スロット 1 にバインド
			void UpdateSdfTextCB(graphics::RenderContext& ctx,
			                     const struct SdfTextParams& params);

		private:
			std::unique_ptr<graphics::IShader>         vs_;
			std::unique_ptr<graphics::IShader>         standardPS_;
			std::unique_ptr<graphics::IShader>         circleGaugePS_;
			std::unique_ptr<graphics::IShader>         sdfTextPS_;
			std::unique_ptr<graphics::IVertexBuffer>   vb_;          // DYNAMIC
			std::unique_ptr<graphics::IIndexBuffer>    ib_;          // DYNAMIC, R16_UINT
			std::unique_ptr<graphics::ISamplerState>   sampler_;
			std::unique_ptr<graphics::IConstantBuffer> gaugeCB_;
			std::unique_ptr<graphics::IConstantBuffer> sdfTextCB_;
			bool                                       ready_ = false;
		};

	} // namespace ui
} // namespace aq
