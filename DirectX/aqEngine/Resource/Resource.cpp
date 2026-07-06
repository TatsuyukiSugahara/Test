#include "aq.h"
#include "Resource.h"
#include "Platform/PlatformBudget.h"
#include <cctype>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <typeinfo>   // 計測: 重いローダーの種別名 (typeid) 出力用


namespace aq
{
	namespace res
	{
		namespace
		{
			std::string GetLowerExtension(const std::string& path);
			bool LoadTkmMeshFile(const std::string& filePath, MeshData& outMesh);
			bool LoadTkmSkeletalMeshFile(const std::string& filePath, SkeletalMeshData& outMesh);
			bool LoadTkaAnimationFile(const std::string& filePath, AnimationClipData& outClip);
		}

		/*******************************************/


		// ファイルサイズをプラットフォーム予算と照合する。超過はログして false(ロード拒否)。
		bool CheckFileBudget(size_t fileBytes, const char* path)
		{
			if (platform::IsWithinSingleFileBudget(fileBytes)) {
				return true;
			}
			char msg[300];
			snprintf(msg, sizeof(msg),
				"  [resource] rejected (exceeds maxSingleFileBytes): %s (%llu bytes)",
				path ? path : "?", static_cast<unsigned long long>(fileBytes));
			aq::StartupLog(msg);
			return false;
		}


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
			if (fileSize > 0 && !CheckFileBudget(static_cast<size_t>(fileSize), requestPath_.c_str())) {
				fclose(fp);
				return false;
			}
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


		bool SkeletalMeshLoader::Loading()
		{
			SkeletalMeshData* meshData = static_cast<SkeletalMeshData*>(resource_->data_);
			if (!meshData) return false;

			const std::string extension = GetLowerExtension(requestPath_);
			if (extension == ".tkm") {
				return LoadTkmSkeletalMeshFile(requestPath_, *meshData);
			}
			return false;
		}


		/*******************************************/


		bool AnimationLoader::Loading()
		{
			AnimationClipData* clipData = static_cast<AnimationClipData*>(resource_->data_);
			if (!clipData) return false;

			const std::string extension = GetLowerExtension(requestPath_);
			if (extension == ".tka") {
				return LoadTkaAnimationFile(requestPath_, *clipData);
			}
			return false;
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

			void PushUniquePath(std::vector<std::string>& paths, const std::string& path)
			{
				if (!path.empty() && std::find(paths.begin(), paths.end(), path) == paths.end()) {
					paths.push_back(path);
				}
			}

			std::string FindProjectRoot()
			{
				static std::string cachedRoot;
				if (!cachedRoot.empty()) {
					return cachedRoot;
				}

				// プラットフォームがコンテンツ基点を返す場合(UWP のパッケージ install
				// フォルダ等)は、それを基点に採用し、ソースツリーの上方探索は行わない。
				// sandbox では Game/Assets を遡れないため。Win32 は nullptr を返すので
				// 従来どおり下の探索にフォールバックする。
				if (const char* contentRoot = aq::Engine::Get().GetContentRoot()) {
					cachedRoot = contentRoot;
					return cachedRoot;
				}

				std::error_code ec;
				std::filesystem::path dir = std::filesystem::current_path(ec);
				if (ec) {
					return std::string();
				}

				while (!dir.empty()) {
					if (std::filesystem::exists(dir / "Game" / "Assets", ec) && !ec) {
						cachedRoot = dir.generic_string();
						return cachedRoot;
					}
					if (dir == dir.root_path()) {
						break;
					}
					dir = dir.parent_path();
				}

				cachedRoot = std::filesystem::current_path(ec).generic_string();
				return cachedRoot;
			}

			std::vector<std::string> BuildResourcePathCandidates(std::string path)
			{
				std::replace(path.begin(), path.end(), '\\', '/');

				std::vector<std::string> paths;
				PushUniquePath(paths, path);

				std::filesystem::path fsPath(path);
				if (fsPath.is_absolute()) {
					return paths;
				}

				// UWP でもパッケージ内にソースツリー相対構造を再現するため、デスクトップと同じ規則で解決。
				const std::filesystem::path root(FindProjectRoot());
				if (path.rfind("Assets/", 0) == 0) {
					PushUniquePath(paths, (root / "Game" / path).generic_string());
				}
				else if (path.rfind("Game/Assets/", 0) == 0) {
					PushUniquePath(paths, (root / path).generic_string());
				}
				else {
					PushUniquePath(paths, (root / path).generic_string());
				}
				return paths;
			}

			bool OpenBinaryReadWithFallback(
				const std::string& filePath,
				FILE** fp,
				std::string* openedPath = nullptr)
			{
				if (!fp) {
					return false;
				}

				*fp = nullptr;
				for (const std::string& path : BuildResourcePathCandidates(filePath)) {
					if (fopen_s(fp, path.c_str(), "rb") == 0 && *fp) {
					// 予算照合: サイズ取得 → 照合 → 先頭へ巻き戻し。超過なら拒否。
					std::fseek(*fp, 0, SEEK_END);
					const long budgetSize = std::ftell(*fp);
					std::fseek(*fp, 0, SEEK_SET);
					if (budgetSize > 0 && !CheckFileBudget(static_cast<size_t>(budgetSize), path.c_str())) {
						std::fclose(*fp);
						*fp = nullptr;
						return false;
					}
						if (openedPath) {
							*openedPath = path;
						}
						return true;
					}
				}
				return false;
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

			aq::graphics::PixelFormat PixelFormatFromDXGI(DXGI_FORMAT fmt)
			{
				using PF = aq::graphics::PixelFormat;
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
				constexpr std::uint8_t VERSION_SKELETAL = 101;

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

			void BuildSkinnedNormals(std::vector<aq::graphics::SkinnedVertexData>& vertices, const std::vector<uint32_t>& indices)
			{
				for (auto& vertex : vertices) {
					vertex.normal.Set(0.0f, 0.0f, 0.0f);
				}
				for (size_t i = 0; i + 2 < indices.size(); i += 3) {
					const uint32_t i0 = indices[i + 0];
					const uint32_t i1 = indices[i + 1];
					const uint32_t i2 = indices[i + 2];
					if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) continue;

					const auto edge01 = vertices[i1].position - vertices[i0].position;
					const auto edge02 = vertices[i2].position - vertices[i0].position;
					aq::math::Vector3 normal;
					normal.Cross(edge01, edge02);
					if (!normal.TryNormalize()) continue;

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

			void BuildNormals(std::vector<aq::graphics::VertexData>& vertices, const std::vector<uint32_t>& indices)
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
					aq::math::Vector3 normal;
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
				// 予算照合(超過なら拒否)。
				if (fp) {
					std::fseek(fp, 0, SEEK_END);
					const long budgetSize = std::ftell(fp);
					std::fseek(fp, 0, SEEK_SET);
					if (budgetSize > 0 && !CheckFileBudget(static_cast<size_t>(budgetSize), filePath.c_str())) {
						std::fclose(fp);
						return false;
					}
				}
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

						aq::graphics::VertexData dst = {};
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

			bool LoadTksSkeletonFile(const std::string& filePath, std::vector<aq::res::BoneData>& outBones)
			{
				FILE* fp = nullptr;
				if (!OpenBinaryReadWithFallback(filePath, &fp)) return false;

				uint32_t numBones = 0;
				if (fread(&numBones, 4, 1, fp) != 1 || numBones == 0 || numBones > 1024) {
					fclose(fp);
					return false;
				}

				outBones.resize(numBones);
				for (uint32_t i = 0; i < numBones; ++i) {
					uint8_t nameLen = 0;
					if (fread(&nameLen, 1, 1, fp) != 1) { fclose(fp); return false; }

					std::vector<char> nameBuf(static_cast<size_t>(nameLen) + 1);
					if (fread(nameBuf.data(), nameLen + 1, 1, fp) != 1) { fclose(fp); return false; }
					nameBuf[nameLen] = '\0';
					outBones[i].name = nameBuf.data();

					if (fread(&outBones[i].parentIndex, 4, 1, fp) != 1) { fclose(fp); return false; }

					float skip[12];
					if (fread(skip, sizeof(skip), 1, fp) != 1) { fclose(fp); return false; }

					float f[12];
					if (fread(f, sizeof(f), 1, fp) != 1) { fclose(fp); return false; }

					// Unity col-major 3x3 + translation → DirectX row-major Matrix4x4
					outBones[i].inverseBindPose = aq::math::Matrix4x4(
						f[0], f[1], f[2], 0.0f,
						f[3], f[4], f[5], 0.0f,
						f[6], f[7], f[8], 0.0f,
						f[9], f[10], f[11], 1.0f
					);
				}

				fclose(fp);
				return true;
			}

			bool LoadTkmSkeletalMeshFile(const std::string& filePath, SkeletalMeshData& outMesh)
			{
				FILE* fp = nullptr;
				std::string openedPath;
				if (!OpenBinaryReadWithFallback(filePath, &fp, &openedPath)) return false;
				const std::string& assetBasePath = openedPath.empty() ? filePath : openedPath;

				auto closeFile = [&fp]() {
					if (fp) { std::fclose(fp); fp = nullptr; }
				};

				tkm::Header header = {};
				if (!ReadValue(fp, header) ||
				    (header.version != tkm::VERSION && header.version != tkm::VERSION_SKELETAL) ||
				    header.numMeshParts == 0) {
					closeFile();
					return false;
				}

				outMesh.vertices.clear();
				outMesh.indices.clear();
				outMesh.bones.clear();
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
						if (!ReadTkmMaterial(fp, material)) { closeFile(); return false; }
						if (outMesh.material.albedo.empty() && !material.albedo.empty()) {
							outMesh.material.albedo = ReplaceExtension(ResolveSiblingPath(assetBasePath, material.albedo), ".dds");
						}
						if (outMesh.material.normal.empty() && !material.normal.empty()) {
							outMesh.material.normal = ReplaceExtension(ResolveSiblingPath(assetBasePath, material.normal), ".dds");
						}
						if (outMesh.material.specular.empty() && !material.specular.empty()) {
							outMesh.material.specular = ReplaceExtension(ResolveSiblingPath(assetBasePath, material.specular), ".dds");
						}
					}

					const uint32_t baseVertex = static_cast<uint32_t>(outMesh.vertices.size());
					outMesh.vertices.reserve(outMesh.vertices.size() + meshHeader.numVertex);
					for (uint32_t vertexNo = 0; vertexNo < meshHeader.numVertex; ++vertexNo) {
						tkm::Vertex src = {};
						if (!ReadValue(fp, src)) { closeFile(); return false; }

						aq::graphics::SkinnedVertexData dst = {};
						dst.position.Set(src.pos[0], src.pos[1], src.pos[2]);
						dst.normal.Set(src.normal[0], src.normal[1], src.normal[2]);
						dst.uv.Set(src.uv[0], src.uv[1]);
						dst.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
						dst.boneWeights.Set(src.weights[0], src.weights[1], src.weights[2], src.weights[3]);
						// TKM uses -1 for unused bone slots; clamp to 0 (weight is 0 so contribution is zero)
						dst.boneIndices[0] = src.indices[0] < 0 ? 0u : static_cast<uint32_t>(src.indices[0]);
						dst.boneIndices[1] = src.indices[1] < 0 ? 0u : static_cast<uint32_t>(src.indices[1]);
						dst.boneIndices[2] = src.indices[2] < 0 ? 0u : static_cast<uint32_t>(src.indices[2]);
						dst.boneIndices[3] = src.indices[3] < 0 ? 0u : static_cast<uint32_t>(src.indices[3]);
						outMesh.vertices.push_back(dst);
					}

					const bool is16BitIndex = meshHeader.indexSize == 2;
					for (uint32_t materialNo = 0; materialNo < meshHeader.numMaterial; ++materialNo) {
						std::int32_t numPolygon = 0;
						if (!ReadValue(fp, numPolygon) || numPolygon < 0) { closeFile(); return false; }

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
				if (outMesh.vertices.empty() || outMesh.indices.empty()) return false;

				BuildSkinnedNormals(outMesh.vertices, outMesh.indices);

				const std::string tksPath = ReplaceExtension(assetBasePath, ".tks");
				if (!LoadTksSkeletonFile(tksPath, outMesh.bones) || outMesh.bones.empty()) {
					return false;
				}

				return true;
			}

			bool LoadTkaAnimationFile(const std::string& filePath, AnimationClipData& outClip)
			{
				FILE* fp = nullptr;
				if (!OpenBinaryReadWithFallback(filePath, &fp)) return false;

				int32_t numEntries = 0, padding = 0;
				if (fread(&numEntries, 4, 1, fp) != 1 || numEntries <= 0) { fclose(fp); return false; }
				fread(&padding, 4, 1, fp);

#pragma pack(push, 1)
				struct TkaEntry {
					int32_t boneIdx;
					float   time;
					float   rot[9];
					float   trans[3];
				};
#pragma pack(pop)
				static_assert(sizeof(TkaEntry) == 56, "TkaEntry size mismatch");

				std::vector<TkaEntry> entries(static_cast<size_t>(numEntries));
				if (fread(entries.data(), sizeof(TkaEntry), static_cast<size_t>(numEntries), fp)
					!= static_cast<size_t>(numEntries)) {
					fclose(fp);
					return false;
				}
				fclose(fp);

				uint32_t numBones = 0;
				for (int32_t i = 1; i < numEntries; ++i) {
					if (entries[i].boneIdx == 0) {
						numBones = static_cast<uint32_t>(i);
						break;
					}
				}
				if (numBones == 0) return false;

				const uint32_t numFrames = static_cast<uint32_t>(numEntries) / numBones;
				if (numFrames == 0) return false;
				constexpr float kFps = 30.0f;

				const auto isRootAxisCorrection = [](const TkaEntry& e) {
					constexpr float kRotEpsilon = 0.001f;
					constexpr float kTransEpsilon = 0.001f;
					const float expected[9] = {
						1.0f, 0.0f,  0.0f,
						0.0f, 0.0f, -1.0f,
						0.0f, 1.0f,  0.0f
					};
					if (e.boneIdx != 0) {
						return false;
					}
					if (std::fabs(e.trans[0]) > kTransEpsilon ||
					    std::fabs(e.trans[1]) > kTransEpsilon ||
					    std::fabs(e.trans[2]) > kTransEpsilon) {
						return false;
					}
					for (int i = 0; i < 9; ++i) {
						if (std::fabs(e.rot[i] - expected[i]) > kRotEpsilon) {
							return false;
						}
					}
					return true;
				};

				bool stripRootAxisCorrection = true;
				for (uint32_t frame = 0; frame < numFrames; ++frame) {
					const TkaEntry& e = entries[static_cast<size_t>(frame) * numBones];
					if (!isRootAxisCorrection(e)) {
						stripRootAxisCorrection = false;
						break;
					}
				}

				outClip.boneCount = numBones;
				outClip.duration  = static_cast<float>(numFrames) / kFps;
				outClip.boneKeyframes.assign(numBones, {});
				float maxKeyTime = 0.0f;

				for (uint32_t frame = 0; frame < numFrames; ++frame) {
					for (uint32_t bone = 0; bone < numBones; ++bone) {
						const TkaEntry& e = entries[static_cast<size_t>(frame) * numBones + bone];

						aq::res::AnimationKeyframe kf;
						kf.time = e.time;
						kf.translation.Set(e.trans[0], e.trans[1], e.trans[2]);
						kf.scale.Set(1.0f, 1.0f, 1.0f);

						// Unity col-major 3x3 rotation → DirectX row-major → quaternion
						DirectX::XMFLOAT4X4 rotMat(
							e.rot[0], e.rot[1], e.rot[2], 0.0f,
							e.rot[3], e.rot[4], e.rot[5], 0.0f,
							e.rot[6], e.rot[7], e.rot[8], 0.0f,
							0.0f,     0.0f,     0.0f,     1.0f
						);
						DirectX::XMVECTOR qv =
							DirectX::XMQuaternionRotationMatrix(DirectX::XMLoadFloat4x4(&rotMat));
						qv = DirectX::XMQuaternionNormalize(qv);
						if (stripRootAxisCorrection && bone == 0) {
							kf.translation.Set(0.0f, 0.0f, 0.0f);
							qv = DirectX::XMQuaternionIdentity();
						}
						if (!outClip.boneKeyframes[bone].empty()) {
							const auto& prevRot = outClip.boneKeyframes[bone].back().rotation;
							const DirectX::XMVECTOR prevQ = DirectX::XMLoadFloat4(
								reinterpret_cast<const DirectX::XMFLOAT4*>(&prevRot));
							if (DirectX::XMVectorGetX(DirectX::XMVector4Dot(prevQ, qv)) < 0.0f) {
								qv = DirectX::XMVectorNegate(qv);
							}
						}
						DirectX::XMStoreFloat4(
							reinterpret_cast<DirectX::XMFLOAT4*>(&kf.rotation), qv);

						maxKeyTime = std::max(maxKeyTime, kf.time);
						outClip.boneKeyframes[bone].push_back(kf);
					}
				}

				if (maxKeyTime > 0.0f && numFrames > 1) {
					outClip.duration = maxKeyTime + (maxKeyTime / static_cast<float>(numFrames - 1));
				}

				return true;
			}
		}

		bool TextureLoader::Loading()
		{
			// 予算照合(best-effort): パスが解決できればファイルサイズを照合。
			{
				std::error_code budgetEc;
				const auto budgetSize = std::filesystem::file_size(requestPath_, budgetEc);
				if (!budgetEc && !CheckFileBudget(static_cast<size_t>(budgetSize), requestPath_.c_str())) {
					return false;
				}
			}

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

			aq::graphics::ImageData imgData;
			imgData.pixels     = textureData->pixels.data();
			imgData.rowPitch   = textureData->rowPitch;
			imgData.slicePitch = textureData->slicePitch;
			imgData.subresources = textureData->subresources.empty()
				? nullptr
				: textureData->subresources.data();
			imgData.subresourceCount = static_cast<uint32_t>(textureData->subresources.size());

			auto srv = aq::graphics::GraphicsDevice::Get().CreateTexture2D(textureData->desc, imgData);
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
			if (!aq::graphics::ParseShaderResourceKey(requestPath_, shaderData->desc)) {
				return false;
			}

			// 重い実行時コンパイル(D3DCompile)はワーカースレッドで行い、メインスレッドの
			// ロードヒッチを避ける。D3D12 の CreateShader はデバイス非依存(bytecode 生成のみ)で、
			// D3D12Shader::Load をスレッド安全化済みなのでここで呼べる。
			shaderData->shader = aq::graphics::GraphicsDevice::Get().CreateShader(
				shaderData->desc.filePath.c_str(),
				shaderData->desc.entryFuncName.c_str(),
				shaderData->desc.shaderType);
			return shaderData->shader != nullptr;
		}


		bool ShaderLoader::FinalizeLoading()
		{
			// コンパイルは Loading()(ワーカー)で完了済み。ここでは生成結果の検証のみ。
			ShaderData* shaderData = static_cast<ShaderData*>(resource_->data_);
			return shaderData && shaderData->shader != nullptr;
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

			if (finishedLoaders.empty()) return;

			// 計測: このフレームで仕上げ(FinalizeLoading = GPU アップロード等)したローダー数と所要時間。
			const auto profStart = std::chrono::steady_clock::now();

			// テクスチャアップロードをバッチ化: この仕上げループ内の CreateTexture2D の
			// コピー実行/GPU 待機を End で 1 回にまとめ、per-texture の N×WaitForGPU を排す。
			aq::graphics::GraphicsDevice::Get().BeginBatchedTextureUploads();
			for (auto* loader : finishedLoaders) {
				const auto lt0 = std::chrono::steady_clock::now();
				loader->Finish();
				const double lms = std::chrono::duration<double, std::milli>(
					std::chrono::steady_clock::now() - lt0).count();
				if (lms > 3.0) {   // 重いローダーだけ種別とパスを出す (犯人特定用)
					EnginePrintf("[LoadProf]   slow finish: %6.2f ms  %-28s (%s)\n",
						lms, typeid(*loader).name(), loader->GetRequestPath().c_str());
				}
				delete loader;
			}
			aq::graphics::GraphicsDevice::Get().EndBatchedTextureUploads();

			const double ms = std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - profStart).count();
			EnginePrintf("[LoadProf] finalize: %u loaders, %.2f ms\n",
				static_cast<uint32_t>(finishedLoaders.size()), ms);
		}
	}
}
