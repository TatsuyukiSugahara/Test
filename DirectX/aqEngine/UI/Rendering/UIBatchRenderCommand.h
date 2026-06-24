#pragma once
#include <memory>
#include <vector>
#include "UIRenderItem.h"
#include "Rendering/IRenderCommand.h"

namespace aq
{
	namespace ui
	{
		class UIRenderPipeline;

		// GPU に送るドローコール 1 単位 (同 shader/texture の頂点をまとめた区間)
		struct UIDrawRange
		{
			std::shared_ptr<graphics::IShaderResourceView> texture;
			UIShaderType  shaderType   = UIShaderType::Standard;
			uint32_t      indexOffset  = 0;
			uint32_t      indexCount   = 0;
			// CircleGauge 専用 (shaderType == CircleGauge の時のみ有効)
			float fillAmount = 1.f;
			float startAngle = 0.f;
			float clockwise  = 1.f;
			// SdfText 専用 (shaderType == SdfText の時のみ有効)
			SdfTextParams sdfText;
		};

		// フレームの全 UI バッチデータ (ヒープ確保; shared_ptr で Command に渡す)
		struct UIBatchPayload
		{
			std::vector<UIVertex>    vertices;
			std::vector<uint16_t>    indices;
			std::vector<UIDrawRange> drawRanges;
			UIRenderPipeline*        pipeline = nullptr; // 非所有; UIBatchRenderer が所有
		};


		// UIBatchRenderCommand: RenderThread 上で実行される UI 一括描画コマンド。
		// Command 自体は shared_ptr 1 本のみ保持するため、64 KB アリーナ制限に収まる。
		class UIBatchRenderCommand : public rendering::IRenderCommand
		{
		public:
			explicit UIBatchRenderCommand(std::shared_ptr<UIBatchPayload> payload)
				: payload_(std::move(payload))
			{}

			void Execute(graphics::RenderContext& ctx, rendering::FrameContext& fc) const override;

		private:
			std::shared_ptr<UIBatchPayload> payload_;
		};

	} // namespace ui
} // namespace aq
