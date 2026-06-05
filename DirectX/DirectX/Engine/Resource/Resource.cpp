#include "../EnginePreCompile.h"
#include "../Graphics/GraphicsDevice.h"
#include "Resource.h"
#include "../Engine.h"


namespace engine
{
	namespace res
	{
		/*******************************************/


		void ResourceLoaderBase::StartAsync()
		{
			future_ = util::ThreadPool::Get().Submit([this] {
				if (!Loading()) {
					resource_->SetState(ResourceBase::ResourceState::Invalid);
					return;
				}
				resource_->SetState(ResourceBase::ResourceState::Completed);
			});
		}


		/*******************************************/


		FbxLoader::FbxLoader()
		{
		}


		FbxLoader::~FbxLoader()
		{
		}


		bool FbxLoader::Loading()
		{
			return true;
		}


		/*******************************************/


		PMDLoader::PMDLoader()
		{
		}


		PMDLoader::~PMDLoader()
		{
		}


		bool PMDLoader::Loading()
		{
			FILE* fp = nullptr;
			fopen_s(&fp, requestPath_, "rb");
			if (fp == nullptr) {
				return false;
			}

			fseek(fp, 0, SEEK_END);
			const long fileSize = ftell(fp);
			fseek(fp, 0, SEEK_SET);

			uint8_t* binHead = new uint8_t[fileSize];
			fread_s(binHead, sizeof(uint8_t) * fileSize, sizeof(uint8_t), static_cast<size_t>(fileSize), fp);
			fclose(fp);

			uint8_t* bin = binHead;

			// ヘッダー
			PMDData::Header* header = reinterpret_cast<PMDData::Header*>(bin);
			bin += sizeof(PMDData::Header);

			Vertex* vertex = reinterpret_cast<Vertex*>(bin);

			delete[] binHead;
			return true;
		}


		/*******************************************/


		TextureLoader::TextureLoader()
		{
		}


		TextureLoader::~TextureLoader()
		{
		}


		bool TextureLoader::Loading()
		{
			wchar_t filePath[256];
			size_t ret;
			mbstowcs_s(&ret, filePath, requestPath_, ArraySize(filePath));

			DirectX::TexMetadata info;
			std::unique_ptr<DirectX::ScratchImage> image = std::make_unique<DirectX::ScratchImage>();

			// 画像読み込み
			HRESULT hr = DirectX::LoadFromWICFile(filePath, DirectX::WIC_FLAGS_NONE, &info, *image);
			if (FAILED(hr)) {
				info = {};
				return false;
			}
			if (info.mipLevels == 1) {
				std::unique_ptr<DirectX::ScratchImage> mipImage = std::make_unique<DirectX::ScratchImage>();
				hr = DirectX::GenerateMipMaps(image->GetImages(), image->GetImageCount(), image->GetMetadata(), DirectX::TEX_FILTER_DEFAULT, 0, *mipImage);
				if (SUCCEEDED(hr)) {
					image = std::move(mipImage);
				}
			}

			const DirectX::Image* imgs = image->GetImages();
			const size_t baseIdx = info.ComputeIndex(0, 0, 0);

			engine::graphics::Texture2DDesc texDesc;
			texDesc.width        = static_cast<uint32_t>(info.width);
			texDesc.height       = static_cast<uint32_t>(info.height);
			texDesc.arraySize    = static_cast<uint32_t>(info.arraySize);
			texDesc.mipLevels    = 1;
			texDesc.isCubemap    = info.IsCubemap();
			texDesc.nativeFormat = static_cast<uint32_t>(info.format);

			engine::graphics::ImageData imgData;
			imgData.pixels    = imgs[baseIdx].pixels;
			imgData.rowPitch  = static_cast<uint32_t>(imgs[baseIdx].rowPitch);
			imgData.slicePitch = static_cast<uint32_t>(imgs[baseIdx].slicePitch);

			TextureData* textureData = static_cast<TextureData*>(resource_->data_);
			textureData->srv = engine::graphics::GraphicsDevice::Get().CreateTexture2D(texDesc, imgData).release();

			return true;
		}


		/*******************************************/


		ResourceManager::BankHashMap ResourceManager::bankMap_;
		ResourceManager::ReflectionHashMap ResourceManager::loaderHashMap_;
		ResourceManager* ResourceManager::instance_ = nullptr;


		ResourceManager::ResourceManager()
		{
			loaders_.clear();
		}


		ResourceManager::~ResourceManager()
		{
			// 全ローダーの完了を待ってから破棄
			// (ThreadPool はまだ生きているので Submit 済みタスクは完走する)
			for (auto* loader : loaders_) {
				loader->Wait();
				delete loader;
			}
			loaders_.clear();
		}


		void ResourceManager::Update()
		{
			ClearFinishedLoaders();
		}


		void ResourceManager::ClearFinishedLoaders()
		{
			loaders_.erase(
				std::remove_if(loaders_.begin(), loaders_.end(),
					[](ResourceLoaderBase* loader) {
						if (loader->IsFinished()) {
							delete loader;
							return true;
						}
						return false;
					}),
				loaders_.end());
		}
	}
}
