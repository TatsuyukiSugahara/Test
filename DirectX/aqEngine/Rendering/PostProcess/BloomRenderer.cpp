#include "aq.h"
#include "BloomRenderer.h"
#include "BloomPassCommand.h"
#include "Rendering/RenderCommandList.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/IShader.h"


namespace aq
{
	namespace rendering
	{
		bool BloomRenderer::Initialize(uint32_t width, uint32_t height, float threshold, float intensity, uint32_t blurPasses)
		{
			threshold_  = threshold;
			intensity_  = intensity;
			blurPasses_ = (blurPasses < 1) ? 1 : (blurPasses > kMaxLevels) ? kMaxLevels : blurPasses;
			width_      = width;
			height_     = height;

			auto& gd = graphics::GraphicsDevice::Get();

			// シェーダーロード
			extractShader_ = gd.CreateShader(
				"Assets/Shader/BloomBrightExtract.fx", "main", graphics::IShader::ShaderType::CS);
			dualBlurDownShader_ = gd.CreateShader(
				"Assets/Shader/DualBlurDown.fx", "main", graphics::IShader::ShaderType::CS);
			dualBlurUpShader_ = gd.CreateShader(
				"Assets/Shader/DualBlurUp.fx", "main", graphics::IShader::ShaderType::CS);
			dualBlurUpAccumShader_ = gd.CreateShader(
				"Assets/Shader/DualBlurUpAccum.fx", "main", graphics::IShader::ShaderType::CS);
			compositeShader_ = gd.CreateShader(
				"Assets/Shader/BloomComposite.fx", "main", graphics::IShader::ShaderType::CS);

			if (!extractShader_ || !dualBlurDownShader_ || !dualBlurUpShader_ || !dualBlurUpAccumShader_ || !compositeShader_)
			{
				EngineAssertMsg(false, "Bloom シェーダーのロードに失敗しました");
				return false;
			}

			// リニアクランプサンプラー
			graphics::SamplerDesc samplerDesc;
			samplerDesc.filter   = graphics::FilterMode::MinMagMipLinear;
			samplerDesc.addressU = graphics::AddressMode::Clamp;
			samplerDesc.addressV = graphics::AddressMode::Clamp;
			sampler_ = gd.CreateSamplerState(samplerDesc);
			if (!sampler_)
			{
				EngineAssertMsg(false, "Bloom サンプラーの生成に失敗しました");
				return false;
			}

			// 全解像度 RT（輝度抽出後にアップサンプル結果でも上書きして再利用する）
			graphics::RenderTargetDesc fullDesc;
			fullDesc.width       = width;
			fullDesc.height      = height;
			fullDesc.colorFormat = graphics::PixelFormat::R8G8B8A8_Unorm;
			fullDesc.hasDepth    = false;

			brightRTHandle_ = gd.CreateOffscreenRenderTarget(fullDesc);
			finalRTHandle_  = gd.CreateOffscreenRenderTarget(fullDesc);

			if (!brightRTHandle_.IsValid() || !finalRTHandle_.IsValid())
			{
				EngineAssertMsg(false, "Bloom RT の生成に失敗しました");
				return false;
			}

			// ピラミッド RT：pyramid[i] のサイズ = (width >> (i+1)) × (height >> (i+1))
			for (uint32_t i = 0; i < kMaxLevels; ++i)
			{
				graphics::RenderTargetDesc desc;
				desc.width       = (width  >> (i + 1)) > 0u ? (width  >> (i + 1)) : 1u;
				desc.height      = (height >> (i + 1)) > 0u ? (height >> (i + 1)) : 1u;
				desc.colorFormat = graphics::PixelFormat::R8G8B8A8_Unorm;
				desc.hasDepth    = false;
				pyramidRTHandles_[i] = gd.CreateOffscreenRenderTarget(desc);
				if (!pyramidRTHandles_[i].IsValid())
				{
					EngineAssertMsg(false, "Bloom ピラミッド RT の生成に失敗しました");
					return false;
				}
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


		void BloomRenderer::BuildPostProcessCommandList(
			RenderCommandList& outList,
			RenderTargetHandle sceneRT,
			uint32_t           width,
			uint32_t           height) const
		{
			outList.Enqueue<BloomPassCommand>(
				extractShader_.get(),
				dualBlurDownShader_.get(),
				dualBlurUpShader_.get(),
				dualBlurUpAccumShader_.get(),
				compositeShader_.get(),
				sampler_.get(),
				bloomCB_.get(),
				sceneRT,
				brightRTHandle_,
				pyramidRTHandles_,
				finalRTHandle_,
				threshold_,
				intensity_,
				blurPasses_,
				width,
				height);
		}
	}
}
