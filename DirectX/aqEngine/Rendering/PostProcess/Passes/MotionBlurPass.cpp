#include "aq.h"
#include "MotionBlurPass.h"
#include "Rendering/PostProcess/Commands/MotionBlurPassCommand.h"
#include "Graphics/GraphicsTypes.h"


namespace aq
{
	namespace rendering
	{
		bool MotionBlurPass::Initialize(const uint32_t width, const uint32_t height)
		{
			auto& gd = graphics::GraphicsDevice::Get();

			// 任意機能。シェーダー / RT が用意できないときはブラー無効のまま先へ進む
			// (チェーン全体を止めない)。
			shader_ = gd.CreateShader(
				"Assets/Shader/MotionBlur.fx", "main", graphics::IShader::ShaderType::CS);
			if (shader_)
			{
				graphics::RenderTargetDesc blurDesc;
				blurDesc.width       = width;
				blurDesc.height      = height;
				blurDesc.colorFormat = graphics::PixelFormat::R16G16B16A16_Float;  // HDR (Bloom 入力になる)
				blurDesc.hasDepth    = false;
				outputRTHandle_ = gd.CreateOffscreenRenderTarget(blurDesc);

				MotionBlurCBData blurInit{};
				constantBuffer_ = gd.CreateConstantBuffer(&blurInit, sizeof(blurInit));
			}

			return true;
		}


		bool MotionBlurPass::IsEnabled(const PostProcessContext& context) const
		{
			// 当該フレームのカメラを受け取っていないビュー (分割画面など) では無効。
			return context.hasCamera
			    && strength_ > 0.0f
			    && shader_
			    && constantBuffer_
			    && outputRTHandle_.IsValid()
			    && context.worldPosRT.IsValid();
		}


		RenderTargetHandle MotionBlurPass::Build(
			RenderCommandList&        outList,
			const PostProcessContext& context,
			const RenderTargetHandle  input)
		{
			MotionBlurCBData cb;
			cb.prevViewProj = prevViewProj_;
			cb.screenWidth  = static_cast<float>(context.width);
			cb.screenHeight = static_cast<float>(context.height);
			cb.strength     = strength_;
			cb.padding      = 0.0f;

			outList.Enqueue<MotionBlurPassCommand>(
				shader_.get(), constantBuffer_.get(),
				input, context.worldPosRT, outputRTHandle_, cb);

			return outputRTHandle_;
		}


		void MotionBlurPass::SetFrameCamera(const CameraData& camera)
		{
			// 今フレームの viewProj を計算し、「前フレームぶん」を CB 用に確定させる。
			// 初回は前フレームが無いので今フレームの値を使う (速度ゼロ = ブラーなし)。
			math::Matrix4x4 viewProj;
			viewProj.Mull(camera.viewMatrix, camera.projectionMatrix);

			prevViewProj_ = prevValid_ ? lastViewProj_ : viewProj;
			lastViewProj_ = viewProj;
			prevValid_    = true;
		}
	}
}
