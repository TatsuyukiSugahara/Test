#include "aq.h"
#include "IBLEnvironment.h"
#include "Graphics/GraphicsTypes.h"


namespace aq
{
	namespace rendering
	{
		namespace
		{
			/** ロードが終わった (完了または失敗) か。要求できなかった (null) ものも終わった扱い */
			static bool IsSettled(const res::RefGPUResource& texture)
			{
				return !texture || texture->IsCompleted() || texture->IsFailed();
			}


			/** GPUResource は desc を公開していないので data_ (TextureData) を直接見る (SkyRenderer と同じ) */
			static const res::TextureData* GetTextureData(const res::RefGPUResource& texture)
			{
				if (!texture) return nullptr;
				return static_cast<const res::TextureData*>(texture->GetData());
			}
		}


		bool IBLEnvironment::Create(const char* irradiancePath, const char* specularPath, const char* brdfLutPath)
		{
			auto& gd = graphics::GraphicsDevice::Get();

			// キューブ / LUT 共用のサンプラ。面の外側・LUT の端を参照しないよう 3 軸とも Clamp。
			graphics::SamplerDesc samplerDesc;
			samplerDesc.filter   = graphics::FilterMode::MinMagMipLinear;
			samplerDesc.addressU = graphics::AddressMode::Clamp;
			samplerDesc.addressV = graphics::AddressMode::Clamp;
			samplerDesc.addressW = graphics::AddressMode::Clamp;
			sampler_ = gd.CreateSamplerState(samplerDesc);
			if (!sampler_) {
				aq::StartupMark("[ibl] sampler create FAILED -> ibl disabled");
				return false;
			}

			// 3 本の非同期ロード (完了は IsReady() でポーリングする)。
			// パスはロード完了/失敗のログに出すので保持しておく。
			irradiancePath_ = irradiancePath;
			specularPath_   = specularPath;
			brdfLutPath_    = brdfLutPath;
			irradiance_ = res::ResourceManager::Get().Load<res::GPUResource>(irradiancePath);
			specular_   = res::ResourceManager::Get().Load<res::GPUResource>(specularPath);
			brdfLut_    = res::ResourceManager::Get().Load<res::GPUResource>(brdfLutPath);
			if (!irradiance_ || !specular_ || !brdfLut_) {
				aq::StartupMarkf("[ibl] load request FAILED (irradiance=%s specular=%s brdfLut=%s) -> ibl disabled",
				                 irradiance_ ? "ok" : "null",
				                 specular_   ? "ok" : "null",
				                 brdfLut_    ? "ok" : "null");
				// IsReady() からの失敗ログを重ねて出さない
				logged_ = true;
				return false;
			}

			aq::StartupMarkf("[ibl] created (sampler ok, loading %s / %s / %s)",
			                 irradiancePath, specularPath, brdfLutPath);
			return true;
		}


		bool IBLEnvironment::IsReady() const
		{
			if (!sampler_ || !irradiance_ || !specular_ || !brdfLut_) {
				return false;
			}

			// 1 本でもロード中なら待つ (ログはそろってから 1 回だけ出す)
			if (!IsSettled(irradiance_) || !IsSettled(specular_) || !IsSettled(brdfLut_)) {
				return false;
			}

			// 毎フレーム呼ばれるため、結果の報告は 1 回だけ
			LogOnce();

			return IsTextureReady(irradiance_, true)
			    && IsTextureReady(specular_, true)
			    && IsTextureReady(brdfLut_, false);
		}


		float IBLEnvironment::GetSpecularMaxMip() const
		{
			// 固定値にせずロード後の段数から取る (空を差し替えて段数が変わっても合う)
			const res::TextureData* texture = GetTextureData(specular_);
			if (!texture || texture->desc.mipLevels == 0) {
				return 0.0f;
			}
			return static_cast<float>(texture->desc.mipLevels - 1);
		}


		bool IBLEnvironment::IsTextureReady(const res::RefGPUResource& texture, const bool expectCubemap)
		{
			if (!texture || !texture->IsCompleted()) {
				return false;
			}
			if (!texture->GetShaderResourceView()) {
				return false;
			}

			const res::TextureData* data = GetTextureData(texture);
			if (!data) {
				return false;
			}
			// キューブを 2D として (またはその逆に) バインドすると、Metal / Vulkan で型が合わない
			return data->desc.isCubemap == expectCubemap;
		}


		void IBLEnvironment::LogOnce() const
		{
			if (logged_) return;
			logged_ = true;

			LogTexture("irradiance", irradiancePath_, irradiance_);
			LogTexture("specular",   specularPath_,   specular_);
			LogTexture("brdfLut",    brdfLutPath_,    brdfLut_);

			const bool ready = IsTextureReady(irradiance_, true)
			                && IsTextureReady(specular_, true)
			                && IsTextureReady(brdfLut_, false);
			if (ready) {
				aq::StartupMarkf("[ibl] ready (specularMaxMip=%.0f)", GetSpecularMaxMip());
			} else {
				aq::StartupMark("[ibl] not usable -> ibl disabled (PSMain で続行)");
			}
		}


		void IBLEnvironment::LogTexture(const char* label, const std::string& path, const res::RefGPUResource& texture)
		{
			if (!texture || texture->IsFailed()) {
				aq::StartupMarkf("[ibl] %s load FAILED (%s)", label, path.c_str());
				return;
			}

			const res::TextureData* data = GetTextureData(texture);
			if (!data) {
				aq::StartupMarkf("[ibl] %s has no TextureData (%s)", label, path.c_str());
				return;
			}

			aq::StartupMarkf("[ibl] %s %s: %ux%u isCubemap=%d array=%u mips=%u srv=%s",
			                 label, path.c_str(),
			                 data->desc.width, data->desc.height,
			                 data->desc.isCubemap ? 1 : 0,
			                 data->desc.arraySize, data->desc.mipLevels,
			                 texture->GetShaderResourceView() ? "ok" : "null");
		}
	}
}
