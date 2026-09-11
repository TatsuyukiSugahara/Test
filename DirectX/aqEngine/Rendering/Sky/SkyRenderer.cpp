#include "aq.h"
#include "SkyRenderer.h"
#include "SkyCommand.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/IShaderResourceView.h"


namespace aq
{
	namespace rendering
	{
		namespace
		{
			static constexpr const char* SKY_CUBEMAP_PATH = "Assets/Sky/SkyCube.dds";
			static constexpr const char* SKY_SHADER_PATH  = "Assets/Shader/Skybox.fx";
		}


		bool SkyRenderer::Create()
		{
			auto& gd = graphics::GraphicsDevice::Get();

			// Skybox.fx の VS/PS (同期生成)
			skyVS_ = gd.CreateShader(SKY_SHADER_PATH, "VSMain", graphics::IShader::ShaderType::VS);
			skyPS_ = gd.CreateShader(SKY_SHADER_PATH, "PSMain", graphics::IShader::ShaderType::PS);
			if (!skyVS_ || !skyPS_) {
				aq::StartupMarkf("[sky] shader load FAILED (%s) -> sky disabled", SKY_SHADER_PATH);
				return false;
			}

			// キューブマップ用サンプラ。面の外側を参照しないよう 3 軸とも Clamp。
			graphics::SamplerDesc samplerDesc;
			samplerDesc.filter   = graphics::FilterMode::MinMagMipLinear;
			samplerDesc.addressU = graphics::AddressMode::Clamp;
			samplerDesc.addressV = graphics::AddressMode::Clamp;
			samplerDesc.addressW = graphics::AddressMode::Clamp;
			sampler_ = gd.CreateSamplerState(samplerDesc);
			if (!sampler_) {
				aq::StartupMark("[sky] sampler create FAILED -> sky disabled");
				return false;
			}

			// キューブマップ (非同期ロード。完了は IsReady() でポーリングする)
			cubeMap_ = res::ResourceManager::Get().Load<res::GPUResource>(SKY_CUBEMAP_PATH);
			if (!cubeMap_) {
				aq::StartupMarkf("[sky] cubemap load request FAILED (%s) -> sky disabled",
				                 SKY_CUBEMAP_PATH);
				return false;
			}

			aq::StartupMarkf("[sky] created (VS/PS + sampler ok, loading %s)", SKY_CUBEMAP_PATH);
			return true;
		}


		bool SkyRenderer::IsReady() const
		{
			if (!skyVS_ || !skyPS_ || !sampler_ || !cubeMap_) {
				return false;
			}
			if (!cubeMap_->IsCompleted()) {
				// 失敗は 1 回だけ報告する (毎フレーム呼ばれるため)
				if (cubeMap_->IsFailed()) {
					LogCubemapOnce();
				}
				return false;
			}
			if (!cubeMap_->GetShaderResourceView()) {
				LogCubemapOnce();
				return false;
			}

			LogCubemapOnce();
			return true;
		}


		void SkyRenderer::BuildCommandList(RenderFrame& frame, RenderCommandList& outList) const
		{
			if (!IsReady()) return;

			outList.Enqueue<SkyCommand>(*skyVS_, *skyPS_, *sampler_,
			                            *cubeMap_->GetShaderResourceView(),
			                            frame.camera, tint_);
		}


		void SkyRenderer::LogCubemapOnce() const
		{
			if (logged_) return;
			logged_ = true;

			if (!cubeMap_ || cubeMap_->IsFailed()) {
				aq::StartupMarkf("[sky] cubemap load FAILED (%s) -> sky disabled", SKY_CUBEMAP_PATH);
				return;
			}

			// P1 の「エンジンがロードして isCubemap の SRV を作れる」確認をここで兼ねる。
			// GPUResource は desc を公開していないので data_ (TextureData) を直接見る。
			const res::TextureData* texture = static_cast<const res::TextureData*>(cubeMap_->GetData());
			if (!texture) {
				aq::StartupMarkf("[sky] cubemap has no TextureData (%s) -> sky disabled", SKY_CUBEMAP_PATH);
				return;
			}

			aq::StartupMarkf("[sky] cubemap %s: %ux%u isCubemap=%d array=%u mips=%u srv=%s",
			                 SKY_CUBEMAP_PATH,
			                 texture->desc.width, texture->desc.height,
			                 texture->desc.isCubemap ? 1 : 0,
			                 texture->desc.arraySize, texture->desc.mipLevels,
			                 cubeMap_->GetShaderResourceView() ? "ok" : "null");
		}
	}
}
