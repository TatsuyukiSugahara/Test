#include "aq.h"
#include "TonemapPass.h"
#include "Rendering/PostProcess/Commands/TonemapPassCommand.h"
#include "Graphics/GraphicsTypes.h"


namespace aq
{
	namespace rendering
	{
		bool TonemapPass::Initialize(const uint32_t width, const uint32_t height)
		{
			auto& gd = graphics::GraphicsDevice::Get();

			compositeShader_ = gd.CreateShader(
				"Assets/Shader/BloomComposite.fx", "main", graphics::IShader::ShaderType::CS);
			if (!compositeShader_)
			{
				EngineAssertMsg(false, "Bloom シェーダーのロードに失敗しました");
				return false;
			}

			// 最終 RT のみ LDR (トーンマップ済みの表示出力)。
			graphics::RenderTargetDesc finalDesc;
			finalDesc.width       = width;
			finalDesc.height      = height;
			finalDesc.colorFormat = graphics::PixelFormat::R8G8B8A8_Unorm;  // トーンマップ後 LDR 表示
			finalDesc.hasDepth    = false;

			finalRTHandle_ = gd.CreateOffscreenRenderTarget(finalDesc);
			if (!finalRTHandle_.IsValid())
			{
				EngineAssertMsg(false, "Bloom RT の生成に失敗しました");
				return false;
			}

			// 定数バッファ
			BloomCBData initData{};
			bloomCB_ = gd.CreateConstantBuffer(&initData, sizeof(initData));
			if (!bloomCB_)
			{
				EngineAssertMsg(false, "Bloom CB の生成に失敗しました");
				return false;
			}

			return true;
		}


		bool TonemapPass::IsEnabled(const PostProcessContext&) const
		{
			// HDR → LDR 変換が無いと表示できないため常時 ON。
			return true;
		}


		RenderTargetHandle TonemapPass::Build(
			RenderCommandList&        outList,
			const PostProcessContext& context,
			const RenderTargetHandle  input)
		{
			outList.Enqueue<TonemapPassCommand>(
				compositeShader_.get(),
				bloomCB_.get(),
				input,
				bloomRTHandle_,
				finalRTHandle_,
				bloomIntensity_,
				context.width,
				context.height,
				exposure_,
				static_cast<uint32_t>(tonemapMode_),
				whitePoint_,
				applyGamma_ ? 1u : 0u);

			return finalRTHandle_;
		}
	}
}
