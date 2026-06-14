#include "../EnginePreCompile.h"
#include "../Graphics/GraphicsDevice.h"
#include "Resource.h"
#include "../Engine.h"
#include <cctype>
#include <cstring>
#include <cstdio>


namespace engine
{
	namespace res
	{
		namespace
		{
			std::string GetLowerExtension(const std::string& path);
			bool LoadTkmMeshFile(const std::string& filePath, MeshData& outMesh);
		}

		/*******************************************/


		void ResourceLoaderBase::StartAsync()
		{
			loadSucceeded_.store(false, std::memory_order_release);
			future_ = util::ThreadPool::Get().Submit([this] {
				bool succeeded = false;
				try {
					succeeded = Loading();
				}
				catch (...) {
					succeeded = false;
				}
				loadSucceeded_.store(succeeded, std::memory_order_release);
			});
		}


		bool ResourceLoaderBase::Finish()
		{
			if (!IsFinished()) {
				return false;
			}

			bool succeeded = loadSucceeded_.load(std::memory_order_acquire);
			if (succeeded) {
				try {
					succeeded = FinalizeLoading();
				}
				catch (...) {
					succeeded = false;
				}
			}

			resource_->SetState(succeeded
				? ResourceBase::ResourceState::Completed
				: ResourceBase::ResourceState::Invalid);
			return true;
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


		MeshLoader::MeshLoader()
		{
		}


		MeshLoader::~MeshLoader()
		{
		}


		bool MeshLoader::Loading()
		{
			MeshData* meshData = static_cast<MeshData*>(resource_->data_);
			if (!meshData) {
				return false;
			}

			const std::string extension = GetLowerExtension(requestPath_);
			if (extension == ".tkm") {
				return LoadTkmMeshFile(requestPath_, *meshData);
			}

			return false;
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
			fopen_s(&fp, requestPath_.c_str(), "rb");
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


		namespace
		{
			std::string ToLowerString(std::string text)
			{
				for (char& c : text) {
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
				return text;
			}

			std::string GetLowerExtension(const std::string& path)
			{
				const size_t dotPos = path.find_last_of('.');
				if (dotPos == std::string::npos) {
					return std::string();
				}
				return ToLowerString(path.substr(dotPos));
			}

			std::string ResolveSiblingPath(const std::string& basePath, const std::string& fileName)
			{
				if (fileName.empty()) {
					return std::string();
				}

				std::string resolved = basePath;
				std::replace(resolved.begin(), resolved.end(), '\\', '/');
				const size_t slashPos = resolved.find_last_of('/');
				if (slashPos == std::string::npos) {
					return fileName;
				}
				return resolved.substr(0, slashPos + 1) + fileName;
			}

			std::string ReplaceExtension(std::string path, const char* extension)
			{
				const size_t dotPos = path.find_last_of('.');
				if (dotPos == std::string::npos) {
					path += extension;
				}
				else {
					path.replace(dotPos, std::string::npos, extension);
				}
				return path;
			}

			engine::graphics::PixelFormat PixelFormatFromDXGI(DXGI_FORMAT fmt)
			{
				using PF = engine::graphics::PixelFormat;
				switch (fmt) {
					case DXGI_FORMAT_R8G8B8A8_UNORM:      return PF::R8G8B8A8_Unorm;
					case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return PF::R8G8B8A8_Unorm_SRGB;
					case DXGI_FORMAT_B8G8R8A8_UNORM:      return PF::B8G8R8A8_Unorm;
					case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return PF::B8G8R8A8_Unorm_SRGB;
					case DXGI_FORMAT_R16G16B16A16_FLOAT:  return PF::R16G16B16A16_Float;
					case DXGI_FORMAT_R32_FLOAT:            return PF::R32_Float;
					case DXGI_FORMAT_R32G32B32A32_FLOAT:  return PF::R32G32B32A32_Float;
					case DXGI_FORMAT_BC1_UNORM:            return PF::BC1_Unorm;
					case DXGI_FORMAT_BC1_UNORM_SRGB:       return PF::BC1_Unorm_SRGB;
					case DXGI_FORMAT_BC2_UNORM:            return PF::BC2_Unorm;
					case DXGI_FORMAT_BC2_UNORM_SRGB:       return PF::BC2_Unorm_SRGB;
					case DXGI_FORMAT_BC3_UNORM:            return PF::BC3_Unorm;
					case DXGI_FORMAT_BC3_UNORM_SRGB:       return PF::BC3_Unorm_SRGB;
					case DXGI_FORMAT_BC4_UNORM:            return PF::BC4_Unorm;
					case DXGI_FORMAT_BC5_UNORM:            return PF::BC5_Unorm;
					case DXGI_FORMAT_BC6H_UF16:            return PF::BC6H_UFloat16;
					case DXGI_FORMAT_BC7_UNORM:            return PF::BC7_Unorm;
					case DXGI_FORMAT_BC7_UNORM_SRGB:       return PF::BC7_Unorm_SRGB;
					default:                                return PF::Unknown;
				}
			}

			namespace tkm
			{
				constexpr std::uint8_t VERSION = 100;

#pragma pack(push, 1)
				struct Header
				{
					std::uint8_t version;
					std::uint8_t isFlatShading;
					std::uint16_t numMeshParts;
				};

				struct MeshPartsHeader
				{
					std::uint32_t numMaterial;
					std::uint32_t numVertex;
					std::uint8_t indexSize;
					std::uint8_t pad[3];
				};

				struct Vertex
				{
					float pos[3];
					float normal[3];
					float uv[2];
					float weights[4];
					std::int16_t indices[4];
				};
#pragma pack(pop)

				struct MaterialNames
				{
					std::string albedo;
					std::string normal;
					std::string specular;
					std::string reflection;
					std::string refraction;
				};
			}

			template <typename T>
			bool ReadValue(FILE* fp, T& value)
			{
				return std::fread(&value, sizeof(T), 1, fp) == 1;
			}

			bool ReadTkmString(FILE* fp, std::string& outText)
			{
				std::uint32_t length = 0;
				if (!ReadValue(fp, length)) {
					return false;
				}

				outText.clear();
				if (length == 0) {
					return true;
				}
				if (length > 64 * 1024) {
					return false;
				}

				// TKM writes fileNameLen followed by fileNameLen + 1 bytes,
				// including the null terminator.
				std::vector<char> buffer(length + 1);
				if (std::fread(buffer.data(), length + 1, 1, fp) != 1) {
					return false;
				}
				buffer[length] = '\0';
				outText = buffer.data();
				return true;
			}

			bool ReadTkmMaterial(FILE* fp, tkm::MaterialNames& material)
			{
				return ReadTkmString(fp, material.albedo)
					&& ReadTkmString(fp, material.normal)
					&& ReadTkmString(fp, material.specular)
					&& ReadTkmString(fp, material.reflection)
					&& ReadTkmString(fp, material.refraction);
			}

			bool ReadTkmIndex(
				FILE* fp,
				bool is16BitIndex,
				uint32_t baseVertex,
				uint32_t maxVertex,
				std::vector<uint32_t>& outIndices)
			{
				uint32_t rawIndex = 0;
				if (is16BitIndex) {
					std::uint16_t index = 0;
					if (!ReadValue(fp, index)) {
						return false;
					}
					rawIndex = index;
				}
				else {
					std::uint32_t index = 0;
					if (!ReadValue(fp, index)) {
						return false;
					}
					rawIndex = index;
				}

				if (rawIndex == 0) {
					return false;
				}

				const uint32_t zeroBasedIndex = rawIndex - 1;
				if (zeroBasedIndex >= maxVertex) {
					return false;
				}

				outIndices.push_back(baseVertex + zeroBasedIndex);
				return true;
			}

			void BuildNormals(std::vector<engine::graphics::VertexData>& vertices, const std::vector<uint32_t>& indices)
			{
				for (auto& vertex : vertices) {
					vertex.normal.Set(0.0f, 0.0f, 0.0f);
				}

				for (size_t i = 0; i + 2 < indices.size(); i += 3) {
					const uint32_t i0 = indices[i + 0];
					const uint32_t i1 = indices[i + 1];
					const uint32_t i2 = indices[i + 2];
					if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
						continue;
					}

					const auto edge01 = vertices[i1].position - vertices[i0].position;
					const auto edge02 = vertices[i2].position - vertices[i0].position;
					engine::math::Vector3 normal;
					normal.Cross(edge01, edge02);
					if (!normal.TryNormalize()) {
						continue;
					}

					vertices[i0].normal.Add(normal);
					vertices[i1].normal.Add(normal);
					vertices[i2].normal.Add(normal);
				}

				for (auto& vertex : vertices) {
					if (!vertex.normal.TryNormalize()) {
						vertex.normal.Set(0.0f, 1.0f, 0.0f);
					}
				}
			}

			bool LoadTkmMeshFile(const std::string& filePath, MeshData& outMesh)
			{
				FILE* fp = nullptr;
				fopen_s(&fp, filePath.c_str(), "rb");
				if (!fp) {
					return false;
				}

				auto closeFile = [&fp]() {
					if (fp) {
						std::fclose(fp);
						fp = nullptr;
					}
				};

				tkm::Header header = {};
				if (!ReadValue(fp, header) || header.version != tkm::VERSION || header.numMeshParts == 0) {
					closeFile();
					return false;
				}

				outMesh.vertics.clear();
				outMesh.indices.clear();
				outMesh.material = {};

				for (uint32_t meshNo = 0; meshNo < header.numMeshParts; ++meshNo) {
					tkm::MeshPartsHeader meshHeader = {};
					if (!ReadValue(fp, meshHeader) || meshHeader.numVertex == 0 || meshHeader.numMaterial == 0) {
						closeFile();
						return false;
					}
					if (meshHeader.indexSize != 2 && meshHeader.indexSize != 4) {
						closeFile();
						return false;
					}

					std::vector<tkm::MaterialNames> materials(meshHeader.numMaterial);
					for (auto& material : materials) {
						if (!ReadTkmMaterial(fp, material)) {
							closeFile();
							return false;
						}

						if (outMesh.material.albedo.empty() && !material.albedo.empty()) {
							outMesh.material.albedo = ReplaceExtension(ResolveSiblingPath(filePath, material.albedo), ".dds");
						}
						if (outMesh.material.normal.empty() && !material.normal.empty()) {
							outMesh.material.normal = ReplaceExtension(ResolveSiblingPath(filePath, material.normal), ".dds");
						}
						if (outMesh.material.specular.empty() && !material.specular.empty()) {
							outMesh.material.specular = ReplaceExtension(ResolveSiblingPath(filePath, material.specular), ".dds");
						}
						// TKM フォーマットは emissive を持たないため省略
					}

					const uint32_t baseVertex = static_cast<uint32_t>(outMesh.vertics.size());
					outMesh.vertics.reserve(outMesh.vertics.size() + meshHeader.numVertex);
					for (uint32_t vertexNo = 0; vertexNo < meshHeader.numVertex; ++vertexNo) {
						tkm::Vertex src = {};
						if (!ReadValue(fp, src)) {
							closeFile();
							return false;
						}

						engine::graphics::VertexData dst = {};
						dst.position.Set(src.pos[0], src.pos[1], src.pos[2]);
						dst.normal.Set(src.normal[0], src.normal[1], src.normal[2]);
						dst.uv.Set(src.uv[0], src.uv[1]);
						// TKM はタンジェントデータを持たないため安全なデフォルトを設定する
						// (normalize(0,0,0) による NaN を防ぐ)
						dst.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
						outMesh.vertics.push_back(dst);
					}

					const bool is16BitIndex = meshHeader.indexSize == 2;
					for (uint32_t materialNo = 0; materialNo < meshHeader.numMaterial; ++materialNo) {
						std::int32_t numPolygon = 0;
						if (!ReadValue(fp, numPolygon) || numPolygon < 0) {
							closeFile();
							return false;
						}

						const uint32_t numIndex = static_cast<uint32_t>(numPolygon) * 3;
						outMesh.indices.reserve(outMesh.indices.size() + numIndex);
						for (uint32_t indexNo = 0; indexNo < numIndex; ++indexNo) {
							if (!ReadTkmIndex(fp, is16BitIndex, baseVertex, meshHeader.numVertex, outMesh.indices)) {
								closeFile();
								return false;
							}
						}
					}
				}

				closeFile();
				if (outMesh.vertics.empty() || outMesh.indices.empty()) {
					return false;
				}

				BuildNormals(outMesh.vertics, outMesh.indices);
				return true;
			}
		}

		bool TextureLoader::Loading()
		{
			wchar_t filePath[256];
			size_t ret;
			mbstowcs_s(&ret, filePath, requestPath_.c_str(), ArraySize(filePath));

			DirectX::TexMetadata info;
			std::unique_ptr<DirectX::ScratchImage> image = std::make_unique<DirectX::ScratchImage>();

			// DDS is common for tkm materials; other image types stay on WIC.
			const std::string extension = GetLowerExtension(requestPath_);
			HRESULT hr = extension == ".dds"
				? DirectX::LoadFromDDSFile(filePath, DirectX::DDS_FLAGS_NONE, &info, *image)
				: DirectX::LoadFromWICFile(filePath, DirectX::WIC_FLAGS_NONE, &info, *image);
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
			info = image->GetMetadata();

			const DirectX::Image* imgs = image->GetImages();
			const size_t imageCount = image->GetImageCount();
			const size_t mipLevels = info.mipLevels > 0 ? info.mipLevels : 1;
			const size_t arraySize = info.arraySize > 0 ? info.arraySize : 1;
			const size_t subresourceCount = mipLevels * arraySize;
			std::vector<const DirectX::Image*> sourceImages;
			std::vector<size_t> sourceOffsets;
			sourceImages.reserve(subresourceCount);
			sourceOffsets.reserve(subresourceCount);

			size_t totalImageSize = 0;
			for (size_t item = 0; item < arraySize; ++item) {
				for (size_t mip = 0; mip < mipLevels; ++mip) {
					const size_t imageIndex = info.ComputeIndex(mip, item, 0);
					if (imageIndex >= imageCount) {
						return false;
					}

					const DirectX::Image& src = imgs[imageIndex];
					const size_t subresourceSize = src.slicePitch > 0
						? src.slicePitch
						: src.rowPitch * src.height;
					if (!src.pixels || subresourceSize == 0) {
						return false;
					}

					sourceOffsets.push_back(totalImageSize);
					sourceImages.push_back(&src);
					totalImageSize += subresourceSize;
				}
			}

			TextureData* textureData = static_cast<TextureData*>(resource_->data_);
			textureData->desc.width     = static_cast<uint32_t>(info.width);
			textureData->desc.height    = static_cast<uint32_t>(info.height);
			textureData->desc.arraySize = static_cast<uint32_t>(arraySize);
			textureData->desc.mipLevels = static_cast<uint32_t>(mipLevels);
			textureData->desc.isCubemap = info.IsCubemap();
			textureData->desc.format    = PixelFormatFromDXGI(info.format);
			textureData->rowPitch       = static_cast<uint32_t>(sourceImages[0]->rowPitch);
			textureData->slicePitch     = static_cast<uint32_t>(sourceImages[0]->slicePitch);

			textureData->pixels.resize(totalImageSize);
			textureData->subresources.resize(sourceImages.size());
			for (size_t index = 0; index < sourceImages.size(); ++index) {
				const DirectX::Image& src = *sourceImages[index];
				const size_t subresourceSize = src.slicePitch > 0
					? src.slicePitch
					: src.rowPitch * src.height;
				uint8_t* dstPixels = textureData->pixels.data() + sourceOffsets[index];
				std::memcpy(dstPixels, src.pixels, subresourceSize);

				textureData->subresources[index].pixels     = dstPixels;
				textureData->subresources[index].rowPitch   = static_cast<uint32_t>(src.rowPitch);
				textureData->subresources[index].slicePitch = static_cast<uint32_t>(src.slicePitch);
			}

			return true;
		}


		bool TextureLoader::FinalizeLoading()
		{
			TextureData* textureData = static_cast<TextureData*>(resource_->data_);
			if (!textureData || textureData->pixels.empty()) {
				return false;
			}

			engine::graphics::ImageData imgData;
			imgData.pixels     = textureData->pixels.data();
			imgData.rowPitch   = textureData->rowPitch;
			imgData.slicePitch = textureData->slicePitch;
			imgData.subresources = textureData->subresources.empty()
				? nullptr
				: textureData->subresources.data();
			imgData.subresourceCount = static_cast<uint32_t>(textureData->subresources.size());

			auto srv = engine::graphics::GraphicsDevice::Get().CreateTexture2D(textureData->desc, imgData);
			if (!srv) {
				return false;
			}

			textureData->srv = srv.release();
			textureData->pixels.clear();
			textureData->pixels.shrink_to_fit();
			textureData->subresources.clear();
			textureData->subresources.shrink_to_fit();
			return true;
		}


		/*******************************************/


		ShaderLoader::ShaderLoader()
		{
		}


		ShaderLoader::~ShaderLoader()
		{
		}


		bool ShaderLoader::Loading()
		{
			ShaderData* shaderData = static_cast<ShaderData*>(resource_->data_);
			if (!shaderData) {
				return false;
			}
			return engine::graphics::ParseShaderResourceKey(requestPath_, shaderData->desc);
		}


		bool ShaderLoader::FinalizeLoading()
		{
			ShaderData* shaderData = static_cast<ShaderData*>(resource_->data_);
			if (!shaderData) {
				return false;
			}

			shaderData->shader = engine::graphics::GraphicsDevice::Get().CreateShader(
				shaderData->desc.filePath.c_str(),
				shaderData->desc.entryFuncName.c_str(),
				shaderData->desc.shaderType);
			return shaderData->shader != nullptr;
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
			std::vector<ResourceLoaderBase*> loaders;
			{
				std::lock_guard<std::mutex> lock(loadersMutex_);
				loaders.swap(loaders_);
			}

			for (auto* loader : loaders) {
				loader->Wait();
				loader->Finish();
				delete loader;
			}
		}


		void ResourceManager::Update()
		{
			ClearFinishedLoaders();
		}


		void ResourceManager::ClearFinishedLoaders()
		{
			std::vector<ResourceLoaderBase*> finishedLoaders;
			{
				std::lock_guard<std::mutex> lock(loadersMutex_);
				auto it = loaders_.begin();
				while (it != loaders_.end()) {
					ResourceLoaderBase* loader = *it;
					if (loader->IsFinished()) {
						finishedLoaders.push_back(loader);
						it = loaders_.erase(it);
					}
					else {
						++it;
					}
				}
			}

			for (auto* loader : finishedLoaders) {
				loader->Finish();
				delete loader;
			}
		}
	}
}
