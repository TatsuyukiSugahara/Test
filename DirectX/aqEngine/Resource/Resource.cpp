#include "aq.h"
#include "Resource.h"
#include "Platform/PlatformBudget.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <sstream>
#include <filesystem>
#include <typeinfo>   // 計測: 重いローダーの種別名 (typeid) 出力用
#include "ufbx/ufbx.h"


namespace aq
{
	namespace res
	{
		namespace
		{
			std::string GetLowerExtension(const std::string& path);
			bool LoadTkmMeshFile(const std::string& filePath, MeshData& outMesh);
			bool LoadObjMesh(const std::string& filePath, MeshData& outMesh);
			bool LoadTkmSkeletalMeshFile(const std::string& filePath, SkeletalMeshData& outMesh);
			bool LoadTkaAnimationFile(const std::string& filePath, AnimationClipData& outClip);
			bool LoadFbxStaticMesh(const std::string& filePath, MeshData& outMesh);
			bool LoadFbxSkeletalMesh(const std::string& filePath, SkeletalMeshData& outMesh);
			bool LoadFbxAnimation(const std::string& filePath, const std::string& clipSelector, AnimationClipData& outClip);
			const ufbx_matrix& FbxImportBasis();
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
			if (extension == ".fbx") {
				return LoadFbxStaticMesh(requestPath_, *meshData);
			}
			if (extension == ".obj") {
				return LoadObjMesh(requestPath_, *meshData);
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
			if (extension == ".fbx") {
				return LoadFbxSkeletalMesh(requestPath_, *meshData);
			}
			return false;
		}


		/*******************************************/


		bool AnimationLoader::Loading()
		{
			AnimationClipData* clipData = static_cast<AnimationClipData*>(resource_->data_);
			if (!clipData) return false;

			// 複数クリップ選択子: "path.fbx#index" または "path.fbx#clipName"。
			// '#' 以降をクリップ指定として切り出し、実ファイルパスは '#' より前とする。
			std::string path = requestPath_;
			std::string clipSelector;
			const size_t hashPos = path.find('#');
			if (hashPos != std::string::npos) {
				clipSelector = path.substr(hashPos + 1);
				path = path.substr(0, hashPos);
			}

			const std::string extension = GetLowerExtension(path);
			if (extension == ".tka") {
				return LoadTkaAnimationFile(path, *clipData);
			}
			if (extension == ".fbx") {
				return LoadFbxAnimation(path, clipSelector, *clipData);
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

			// FBX ファイル名 (パス区切りを含みうる) から純粋なファイル名部分を取り出す
			std::string FbxBaseName(const ufbx_string& s)
			{
				std::string name(s.data ? s.data : "", s.length);
				const size_t slash = name.find_last_of("/\\");
				return slash == std::string::npos ? name : name.substr(slash + 1);
			}

			// ufbx テクスチャを実在するファイルパスへ解決する。拡張子は元のまま保持し
			// (TextureLoader は .dds=DDS / それ以外=WIC(.png/.jpg 等) で読める)、
			// 候補を実在チェックして最初に見つかったものを返す:
			//   1. ufbx が解決した絶対パス filename
			//   2. FBX と同ディレクトリ + 相対パス(サブフォルダ保持)
			//   3. FBX と同ディレクトリ + ファイル名のみ (参照パスと実配置がずれる典型に対応)
			// resolvedFbxPath は ufbx_load_file が成功した実パス(絶対/CWD相対解決済)。
			std::string FbxResolveTexturePath(const std::string& resolvedFbxPath, const ufbx_texture* tex)
			{
				if (!tex) return std::string();

				std::vector<std::string> candidates;
				if (tex->filename.length > 0) {
					candidates.emplace_back(tex->filename.data, tex->filename.length);
				}
				if (tex->relative_filename.length > 0) {
					std::string rel(tex->relative_filename.data, tex->relative_filename.length);
					std::replace(rel.begin(), rel.end(), '\\', '/');
					candidates.push_back(ResolveSiblingPath(resolvedFbxPath, rel));
					const size_t slash = rel.find_last_of('/');
					candidates.push_back(ResolveSiblingPath(
						resolvedFbxPath, slash == std::string::npos ? rel : rel.substr(slash + 1)));
				}

				for (const std::string& c : candidates) {
					std::error_code ec;
					if (std::filesystem::exists(c, ec) && !ec) {
						return c;
					}
				}
				return candidates.empty() ? std::string() : candidates.back();
			}

			// マテリアルのアルベド(拡散/ベースカラー)テクスチャを解決する。
			std::string FbxAlbedoTexturePath(const std::string& resolvedFbxPath, const ufbx_material* mat)
			{
				if (!mat) return std::string();
				const ufbx_texture* tex = mat->fbx.diffuse_color.texture;
				if (!tex) tex = mat->pbr.base_color.texture;
				return FbxResolveTexturePath(resolvedFbxPath, tex);
			}

			// FBX を静的メッシュとして読み込む (スキニングなし)。
			// ufbx で左手系 Y-up・メートル単位へ正規化し、三角形スープとして展開する。
			bool LoadFbxStaticMesh(const std::string& filePath, MeshData& outMesh)
			{
				ufbx_load_opts opts = {};
				opts.target_axes            = ufbx_axes_left_handed_y_up; // DirectX 左手 Y-up (FBXの宣言軸から変換)
				// 単位変換は使わない (手元FBXは単位メタデータが不整合で約1/100に潰れるため)
				opts.generate_missing_normals = true;

				ufbx_scene* scene = nullptr;
				ufbx_error  error = {};
				std::string resolvedFbxPath = filePath;
				for (const std::string& candidate : BuildResourcePathCandidates(filePath)) {
					scene = ufbx_load_file(candidate.c_str(), &opts, &error);
					if (scene) { resolvedFbxPath = candidate; break; }
				}
				if (!scene) {
					return false;
				}

				outMesh.vertics.clear();
				outMesh.indices.clear();
				outMesh.material = {};

				std::vector<uint32_t> triIndices;

				for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
					const ufbx_node* node = scene->nodes.data[ni];
					if (node->is_root || node->mesh == nullptr) {
						continue;
					}
					const ufbx_mesh* mesh = node->mesh;

					const ufbx_matrix basis = FbxImportBasis();
					const ufbx_matrix geomToWorld = ufbx_matrix_mul(&basis, &node->geometry_to_world);
					const ufbx_matrix normalMatrix = ufbx_matrix_for_normals(&geomToWorld);
					// 左手系変換で geometry_to_world が鏡映(det<0)になると三角形の巻き順が
					// 反転し、バックフェースカリングで表裏が逆になる(裏から顔が透ける)。
					// det<0 のときは三角形内の頂点順を反転して巻き順を戻す。
					const bool flipWinding = ufbx_matrix_determinant(&geomToWorld) < 0.0;

					triIndices.resize(mesh->max_face_triangles * 3);

					if (outMesh.material.albedo.empty() && mesh->materials.count > 0) {
						outMesh.material.albedo = FbxAlbedoTexturePath(resolvedFbxPath, mesh->materials.data[0]);
					}

					for (size_t fi = 0; fi < mesh->faces.count; ++fi) {
						const ufbx_face face = mesh->faces.data[fi];
						const uint32_t numTris = ufbx_triangulate_face(
							triIndices.data(), triIndices.size(), mesh, face);

						for (uint32_t ti = 0; ti < numTris * 3; ++ti) {
							const uint32_t k  = ti % 3u;
							const uint32_t ix = flipWinding
								? triIndices[(ti - k) + (2u - k)] : triIndices[ti];

							aq::graphics::VertexData dst = {};

							const ufbx_vec3 p  = ufbx_get_vertex_vec3(&mesh->vertex_position, ix);
							const ufbx_vec3 wp = ufbx_transform_position(&geomToWorld, p);
							dst.position.Set(
								static_cast<float>(wp.x),
								static_cast<float>(wp.y),
								static_cast<float>(wp.z));

							if (mesh->vertex_normal.exists) {
								const ufbx_vec3 n  = ufbx_get_vertex_vec3(&mesh->vertex_normal, ix);
								const ufbx_vec3 wn = ufbx_transform_direction(&normalMatrix, n);
								dst.normal.Set(
									static_cast<float>(wn.x),
									static_cast<float>(wn.y),
									static_cast<float>(wn.z));
								dst.normal.TryNormalize();
							}

							if (mesh->vertex_uv.exists) {
								const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ix);
								// FBX の UV 原点は左下。DirectX は左上なので V を反転する。
								dst.uv.Set(static_cast<float>(uv.x), 1.0f - static_cast<float>(uv.y));
							}

							dst.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };

							outMesh.indices.push_back(static_cast<uint32_t>(outMesh.vertics.size()));
							outMesh.vertics.push_back(dst);
						}
					}
				}

				ufbx_free_scene(scene);

				if (outMesh.vertics.empty() || outMesh.indices.empty()) {
					return false;
				}
				return true;
			}

			// FBX 取込み基準回転 (座標系補正)。
			// 手元アセット群は FBX の宣言軸(up=+Y)と実ジオメトリの向きがずれており、
			// そのままだと寝る/顔が下を向く。取込み時に一定回転を掛けて立て直す。
			// ★ここを変えると全FBXの取込み向きが変わる (エンティティ変換はゲーム操作用に不干渉)。
			//   単位(無回転)にするには rotation を {0,0,0,1} にする。
			// 頂点(geometry_to_world)とボーン(node_to_world; bind/anim)の両方へ前段で一貫適用
			// するため、スキニングは保たれる (回転は det=+1 なのでワインディング判定にも不干渉)。
			const ufbx_matrix& FbxImportBasis()
			{
				static const ufbx_matrix kBasis = []() {
					ufbx_transform t = ufbx_identity_transform;
					// 取込み向きの補正回転をクォータニオン(x,y,z,w)で指定する。
					// 既定は無補正(単位)。アセットが寝る/顔が下を向く場合は下記から選ぶ:
					//   X軸 +90°: x= 0.70710678f, w= 0.70710678f
					//   X軸 -90°: x=-0.70710678f, w= 0.70710678f
					//   Y軸 180°: y= 1.0f,          w= 0.0f
					//   Z軸 +90°: z= 0.70710678f, w= 0.70710678f
					// 例) X軸 -90°:
					//   t.rotation.x = -0.70710678f; t.rotation.w = 0.70710678f;
					t.rotation.x = 0.0f;
					t.rotation.y = 0.0f;
					t.rotation.z = 0.0f;
					t.rotation.w = 1.0f; // 無補正
					return ufbx_transform_to_matrix(&t);
				}();
				return kBasis;
			}

			// ufbx 行列(列ベクトル 4x3)→ エンジン行列(行ベクトル 4x4)。
			// ufbx の各列基底をエンジンの各行へ写す (TKS ローダーと同じ転置規約)。
			aq::math::Matrix4x4 FbxToEngineMatrix(const ufbx_matrix& m)
			{
				return aq::math::Matrix4x4(
					static_cast<float>(m.m00), static_cast<float>(m.m10), static_cast<float>(m.m20), 0.0f,
					static_cast<float>(m.m01), static_cast<float>(m.m11), static_cast<float>(m.m21), 0.0f,
					static_cast<float>(m.m02), static_cast<float>(m.m12), static_cast<float>(m.m22), 0.0f,
					static_cast<float>(m.m03), static_cast<float>(m.m13), static_cast<float>(m.m23), 1.0f);
			}

			// スキンクラスタが参照するボーンノードを、シーンのノード走査順という
			// 決定的な順序で収集する。スケルタルメッシュ側とアニメ側の両ローダーが
			// 同じ順序を使うことで、ボーンインデックスが一致する (両者は別リソース)。
			void CollectFbxBoneNodes(const ufbx_scene* scene, std::vector<ufbx_node*>& outBoneNodes)
			{
				outBoneNodes.clear();
				std::unordered_map<const ufbx_node*, bool> isBone;
				for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
					const ufbx_mesh* mesh = scene->meshes.data[mi];
					for (size_t di = 0; di < mesh->skin_deformers.count; ++di) {
						const ufbx_skin_deformer* skin = mesh->skin_deformers.data[di];
						for (size_t ci = 0; ci < skin->clusters.count; ++ci) {
							const ufbx_skin_cluster* c = skin->clusters.data[ci];
							if (c->bone_node) isBone[c->bone_node] = true;
						}
					}
				}
				for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
					ufbx_node* node = scene->nodes.data[ni];
					if (isBone.find(node) != isBone.end()) {
						outBoneNodes.push_back(node);
					}
				}
			}

			// 各ボーンの親ボーンインデックス(なければ -1)を求める。ノード階層を上に辿り、
			// 最初に見つかったボーンを親とする(非ボーン中間ノードは飛ばす)。
			// scene->nodes は親→子順なので parents[i] < i が保証される(トポロジカル順)。
			std::vector<int32_t> ComputeFbxBoneParents(const std::vector<ufbx_node*>& boneNodes)
			{
				std::unordered_map<const ufbx_node*, int32_t> idxOf;
				for (int32_t i = 0; i < static_cast<int32_t>(boneNodes.size()); ++i) {
					idxOf[boneNodes[i]] = i;
				}
				std::vector<int32_t> parents(boneNodes.size(), -1);
				for (int32_t i = 0; i < static_cast<int32_t>(boneNodes.size()); ++i) {
					const ufbx_node* p = boneNodes[i]->parent;
					while (p) {
						const auto it = idxOf.find(p);
						if (it != idxOf.end()) { parents[i] = it->second; break; }
						p = p->parent;
					}
				}
				return parents;
			}

			// FBX をスケルタルメッシュとして読み込む。
			// 設計: 頂点を ufbx ワールド空間へ焼き (geometry_to_world = target_axes の
			// 軸変換込み)、逆バインドを world→bone(bind)=invert(bone_to_world) とする。
			// ボーンは実階層 (parentIndex=親ボーン) を持たせ、アニメ側は各フレームの
			// 親ボーン相対ローカル変換を焼き込む。CalcBoneMatrices が local を累積して
			// world を再構築し、skin = invert(bone_to_world_bind) * bone_to_world_current
			// で解ける。★親相対にする理由: 左手系変換で bone_to_world は det=-1(鏡映)に
			//   なり、world をそのまま TRS 分解すると鏡映が負スケールに入りフレーム間補間で
			//   退化・歪む。親相対 local = invert(parent_world)*child_world は det=+1(真の
			//   回転)となり鏡映が相殺され、補間がクリーンになる。
			// 単位変換(target_unit_meters)は使わない: 手元FBXは単位メタデータが不整合で
			// 適用すると約1/100に潰れるため、ジオメトリ素の数値スケールを保つ。
			bool LoadFbxSkeletalMesh(const std::string& filePath, SkeletalMeshData& outMesh)
			{
				ufbx_load_opts opts = {};
				opts.target_axes              = ufbx_axes_left_handed_y_up; // DirectX 左手 Y-up (FBXの宣言軸から変換)
				opts.generate_missing_normals = true;

				ufbx_scene* scene = nullptr;
				ufbx_error  error = {};
				std::string resolvedFbxPath = filePath;
				for (const std::string& candidate : BuildResourcePathCandidates(filePath)) {
					scene = ufbx_load_file(candidate.c_str(), &opts, &error);
					if (scene) { resolvedFbxPath = candidate; break; }
				}
				if (!scene) {
					return false;
				}

				std::vector<ufbx_node*> boneNodes;
				CollectFbxBoneNodes(scene, boneNodes);
				if (boneNodes.empty()) {
					ufbx_free_scene(scene);
					return false;
				}

				std::unordered_map<const ufbx_node*, uint32_t> boneIndexOf;
				for (uint32_t i = 0; i < boneNodes.size(); ++i) {
					boneIndexOf[boneNodes[i]] = i;
				}
				const std::vector<int32_t> boneParents = ComputeFbxBoneParents(boneNodes);
				const ufbx_matrix basis = FbxImportBasis(); // 取込み基準回転

				outMesh.vertices.clear();
				outMesh.indices.clear();
				outMesh.bones.clear();
				outMesh.material = {};

				outMesh.bones.resize(boneNodes.size());
				for (uint32_t i = 0; i < boneNodes.size(); ++i) {
					const ufbx_matrix boneToWorld = ufbx_matrix_mul(&basis, &boneNodes[i]->node_to_world);
					const ufbx_matrix worldToBone = ufbx_matrix_invert(&boneToWorld);
					outMesh.bones[i].name = std::string(
						boneNodes[i]->name.data ? boneNodes[i]->name.data : "", boneNodes[i]->name.length);
					outMesh.bones[i].parentIndex     = boneParents[i];
					outMesh.bones[i].inverseBindPose = FbxToEngineMatrix(worldToBone);
				}

				std::vector<uint32_t> triIndices;

				for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
					const ufbx_node* node = scene->nodes.data[ni];
					if (node->is_root || node->mesh == nullptr) {
						continue;
					}
					const ufbx_mesh* mesh = node->mesh;
					if (mesh->skin_deformers.count == 0) {
						continue;
					}
					const ufbx_skin_deformer* skin = mesh->skin_deformers.data[0];

					const ufbx_matrix geomToWorld  = ufbx_matrix_mul(&basis, &node->geometry_to_world);
					const ufbx_matrix normalMatrix = ufbx_matrix_for_normals(&geomToWorld);
					// 鏡映(det<0)で反転する巻き順を戻す (静的ローダーと同じ理由)。
					const bool flipWinding = ufbx_matrix_determinant(&geomToWorld) < 0.0;
					triIndices.resize(mesh->max_face_triangles * 3);

					if (outMesh.material.albedo.empty() && mesh->materials.count > 0) {
						outMesh.material.albedo = FbxAlbedoTexturePath(resolvedFbxPath, mesh->materials.data[0]);
					}

					for (size_t fi = 0; fi < mesh->faces.count; ++fi) {
						const ufbx_face face = mesh->faces.data[fi];
						const uint32_t numTris = ufbx_triangulate_face(
							triIndices.data(), triIndices.size(), mesh, face);

						for (uint32_t ti = 0; ti < numTris * 3; ++ti) {
							const uint32_t k  = ti % 3u;
							const uint32_t ix = flipWinding
								? triIndices[(ti - k) + (2u - k)] : triIndices[ti];
							const uint32_t vi = mesh->vertex_position.indices.data[ix]; // メッシュ頂点index

							aq::graphics::SkinnedVertexData dst = {};

							// ワールド(bind)空間へ焼き込む (逆バインドが world→bone のため)
							const ufbx_vec3 p  = ufbx_get_vertex_vec3(&mesh->vertex_position, ix);
							const ufbx_vec3 wp = ufbx_transform_position(&geomToWorld, p);
							dst.position.Set(
								static_cast<float>(wp.x), static_cast<float>(wp.y), static_cast<float>(wp.z));

							if (mesh->vertex_normal.exists) {
								const ufbx_vec3 n  = ufbx_get_vertex_vec3(&mesh->vertex_normal, ix);
								const ufbx_vec3 wn = ufbx_transform_direction(&normalMatrix, n);
								dst.normal.Set(
									static_cast<float>(wn.x), static_cast<float>(wn.y), static_cast<float>(wn.z));
								dst.normal.TryNormalize();
							}

							if (mesh->vertex_uv.exists) {
								const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ix);
								dst.uv.Set(static_cast<float>(uv.x), 1.0f - static_cast<float>(uv.y));
							}

							dst.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };

							// スキンウェイト: 影響度降順の先頭4本を取り、正規化する
							float    w[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
							uint32_t b[4] = { 0u, 0u, 0u, 0u };
							if (vi < skin->vertices.count) {
								const ufbx_skin_vertex sv = skin->vertices.data[vi];
								const uint32_t take = sv.num_weights < 4u ? sv.num_weights : 4u;
								for (uint32_t k = 0; k < take; ++k) {
									const ufbx_skin_weight sw = skin->weights.data[sv.weight_begin + k];
									const ufbx_skin_cluster* c = skin->clusters.data[sw.cluster_index];
									if (c->bone_node) {
										const auto it = boneIndexOf.find(c->bone_node);
										if (it != boneIndexOf.end()) {
											b[k] = it->second;
											w[k] = static_cast<float>(sw.weight);
										}
									}
								}
							}
							const float sum = w[0] + w[1] + w[2] + w[3];
							if (sum > 1e-6f) {
								w[0] /= sum; w[1] /= sum; w[2] /= sum; w[3] /= sum;
							} else {
								w[0] = 1.0f; // フォールバック: 先頭ボーンに束縛
							}
							dst.boneWeights.Set(w[0], w[1], w[2], w[3]);
							dst.boneIndices[0] = b[0];
							dst.boneIndices[1] = b[1];
							dst.boneIndices[2] = b[2];
							dst.boneIndices[3] = b[3];

							outMesh.indices.push_back(static_cast<uint32_t>(outMesh.vertices.size()));
							outMesh.vertices.push_back(dst);
						}
					}
				}

				ufbx_free_scene(scene);

				if (outMesh.vertices.empty() || outMesh.indices.empty() || outMesh.bones.empty()) {
					return false;
				}
				return true;
			}

			// FBX アニメーションを読み込む。clipSelector で対象アニメスタックを選ぶ:
			//   空       → 先頭スタック(index 0)
			//   数字     → そのインデックスのスタック
			//   それ以外 → その名前のスタック (見つからなければ先頭)
			// 選んだスタックを 30fps でサンプリングし、各ボーンの「親ボーン相対ローカル変換」
			// を TRS へ分解してキーフレーム化する。
			// local = invert(parent_bone_to_world) * bone_to_world (親が無い根は world のまま)。
			// CalcBoneMatrices が local を親から累積して world を再構築し、逆バインドと合わせ
			// skin = invert(B2W_bind) * B2W_current で解ける。★親相対にするのは、左手系変換で
			//   生じる鏡映(det=-1)を親子間で相殺し、非根ボーンの TRS を真の回転(補間安全)に
			//   するため (world 直接だと鏡映が負スケールに入り補間で歪む)。
			// 単位変換は使わない (スケルタル側と同じ理由)。
			bool LoadFbxAnimation(const std::string& filePath, const std::string& clipSelector, AnimationClipData& outClip)
			{
				ufbx_load_opts opts = {};
				opts.target_axes = ufbx_axes_left_handed_y_up; // 同上

				ufbx_scene* scene = nullptr;
				ufbx_error  error = {};
				for (const std::string& candidate : BuildResourcePathCandidates(filePath)) {
					scene = ufbx_load_file(candidate.c_str(), &opts, &error);
					if (scene) break;
				}
				if (!scene) {
					return false;
				}
				if (scene->anim_stacks.count == 0) {
					ufbx_free_scene(scene);
					return false;
				}

				std::vector<ufbx_node*> boneNodes;
				CollectFbxBoneNodes(scene, boneNodes);
				if (boneNodes.empty()) {
					ufbx_free_scene(scene);
					return false;
				}
				const std::vector<int32_t> boneParents = ComputeFbxBoneParents(boneNodes);
				const ufbx_matrix basis = FbxImportBasis(); // 取込み基準回転 (スケルタル側と一致)

				// clipSelector からアニメスタックを選択する
				size_t stackIndex = 0;
				if (!clipSelector.empty()) {
					const bool allDigits = clipSelector.find_first_not_of("0123456789") == std::string::npos;
					if (allDigits) {
						const size_t idx = static_cast<size_t>(std::stoul(clipSelector));
						if (idx < scene->anim_stacks.count) stackIndex = idx;
					} else {
						for (size_t i = 0; i < scene->anim_stacks.count; ++i) {
							const ufbx_string& n = scene->anim_stacks.data[i]->name;
							if (std::string(n.data ? n.data : "", n.length) == clipSelector) {
								stackIndex = i;
								break;
							}
						}
					}
				}
				const ufbx_anim_stack* stack = scene->anim_stacks.data[stackIndex];
				const double t0  = stack->time_begin;
				const double t1  = stack->time_end;
				constexpr double kFps = 30.0;
				double dur = t1 - t0;
				if (dur < 0.0) dur = 0.0;
				uint32_t numFrames = static_cast<uint32_t>(std::floor(dur * kFps + 0.5)) + 1u;
				if (numFrames < 1u) numFrames = 1u;

				const uint32_t numBones = static_cast<uint32_t>(boneNodes.size());
				outClip.boneCount = numBones;
				outClip.duration  = static_cast<float>(dur);
				outClip.boneKeyframes.assign(numBones, {});

				for (uint32_t f = 0; f < numFrames; ++f) {
					const double time = t0 + static_cast<double>(f) / kFps;
					ufbx_evaluate_opts eopts = {};
					ufbx_error everr = {};
					ufbx_scene* evScene = ufbx_evaluate_scene(scene, stack->anim, time, &eopts, &everr);
					if (!evScene) {
						continue;
					}

					for (uint32_t bi = 0; bi < numBones; ++bi) {
						const ufbx_node* src = boneNodes[bi];
						const ufbx_node* evNode = (src->typed_id < evScene->nodes.count)
							? evScene->nodes.data[src->typed_id] : nullptr;
						const ufbx_matrix rawChild = evNode ? evNode->node_to_world : src->node_to_world;
						// 取込み基準回転を前段で掛ける (根はこれを保持し、非根では親子で相殺)
						const ufbx_matrix childWorld = ufbx_matrix_mul(&basis, &rawChild);

						// 親ボーン相対ローカル = invert(parent_world) * child_world (鏡映相殺)
						ufbx_matrix local = childWorld;
						if (boneParents[bi] >= 0) {
							const ufbx_node* parentSrc = boneNodes[boneParents[bi]];
							const ufbx_node* evParent = (parentSrc->typed_id < evScene->nodes.count)
								? evScene->nodes.data[parentSrc->typed_id] : nullptr;
							if (evParent) {
								const ufbx_matrix parentWorld = ufbx_matrix_mul(&basis, &evParent->node_to_world);
								const ufbx_matrix invParent = ufbx_matrix_invert(&parentWorld);
								local = ufbx_matrix_mul(&invParent, &childWorld);
							}
						}
						const ufbx_transform tr = ufbx_matrix_to_transform(&local);

						aq::res::AnimationKeyframe kf;
						kf.time = static_cast<float>(static_cast<double>(f) / kFps);
						kf.translation.Set(
							static_cast<float>(tr.translation.x),
							static_cast<float>(tr.translation.y),
							static_cast<float>(tr.translation.z));
						kf.scale.Set(
							static_cast<float>(tr.scale.x),
							static_cast<float>(tr.scale.y),
							static_cast<float>(tr.scale.z));
						kf.rotation.Set(
							static_cast<float>(tr.rotation.x),
							static_cast<float>(tr.rotation.y),
							static_cast<float>(tr.rotation.z),
							static_cast<float>(tr.rotation.w));

						// 連続性: 直前フレームと同半球へクォータニオンを揃える
						if (!outClip.boneKeyframes[bi].empty()) {
							const auto& pr = outClip.boneKeyframes[bi].back().rotation;
							const float d = pr.x * kf.rotation.x + pr.y * kf.rotation.y
								+ pr.z * kf.rotation.z + pr.w * kf.rotation.w;
							if (d < 0.0f) {
								kf.rotation.Set(-kf.rotation.x, -kf.rotation.y, -kf.rotation.z, -kf.rotation.w);
							}
						}
						outClip.boneKeyframes[bi].push_back(kf);
					}
					ufbx_free_scene(evScene);
				}

				ufbx_free_scene(scene);
				return true;
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


			// Wavefront OBJ を静的メッシュとして読み込む (AqParticleExporter が書き出す
			// パーティクルメッシュ用)。v/vt/vn/f に対応。UV は V 反転 (OBJ 左下原点 →
			// DirectX 左上)。座標系・巻き順は Unity=DX とも左手 Y-up のためそのまま。
			bool LoadObjMesh(const std::string& filePath, MeshData& outMesh)
			{
				FILE* fp = nullptr;
				fopen_s(&fp, filePath.c_str(), "rb");
				if (fp == nullptr) {
					return false;
				}
				fseek(fp, 0, SEEK_END);
				const long fileSize = ftell(fp);
				if (fileSize > 0 && !CheckFileBudget(static_cast<size_t>(fileSize), filePath.c_str())) {
					fclose(fp);
					return false;
				}
				fseek(fp, 0, SEEK_SET);
				std::string text(static_cast<size_t>(fileSize > 0 ? fileSize : 0), '\0');
				if (fileSize > 0) {
					fread_s(&text[0], text.size(), 1, static_cast<size_t>(fileSize), fp);
				}
				fclose(fp);

				std::vector<math::Vector3> positions;
				std::vector<math::Vector2> uvs;
				std::vector<math::Vector3> normals;

				// "v/t/n" 組み合わせ → 出力頂点 index のキャッシュ
				std::unordered_map<uint64_t, uint32_t> vertexCache;

				auto packKey = [](int v, int t, int n) -> uint64_t {
					// 各 21bit (最大約200万頂点) に詰める。0 = 未指定。
					return (static_cast<uint64_t>(v & 0x1fffff))
					     | (static_cast<uint64_t>(t & 0x1fffff) << 21)
					     | (static_cast<uint64_t>(n & 0x1fffff) << 42);
				};

				std::istringstream stream(text);
				std::string line;
				std::vector<uint32_t> faceIndices;   // 1 ポリゴンぶんの出力 index (三角化用)
				while (std::getline(stream, line)) {
					if (line.empty() || line[0] == '#') {
						continue;
					}
					std::istringstream ls(line);
					std::string tag;
					ls >> tag;
					if (tag == "v") {
						math::Vector3 p; ls >> p.x >> p.y >> p.z; positions.push_back(p);
					} else if (tag == "vt") {
						math::Vector2 t; ls >> t.x >> t.y; t.y = 1.0f - t.y; uvs.push_back(t);
					} else if (tag == "vn") {
						math::Vector3 n; ls >> n.x >> n.y >> n.z; normals.push_back(n);
					} else if (tag == "f") {
						faceIndices.clear();
						std::string vtx;
						while (ls >> vtx) {
							// "v", "v/t", "v//n", "v/t/n"
							int vi = 0, ti = 0, ni = 0;
							size_t s1 = vtx.find('/');
							if (s1 == std::string::npos) {
								vi = std::atoi(vtx.c_str());
							} else {
								vi = std::atoi(vtx.substr(0, s1).c_str());
								size_t s2 = vtx.find('/', s1 + 1);
								if (s2 == std::string::npos) {
									ti = std::atoi(vtx.substr(s1 + 1).c_str());
								} else {
									std::string tstr = vtx.substr(s1 + 1, s2 - s1 - 1);
									if (!tstr.empty()) ti = std::atoi(tstr.c_str());
									ni = std::atoi(vtx.substr(s2 + 1).c_str());
								}
							}
							if (vi == 0) continue;   // 不正/負 index は無視
							uint64_t key = packKey(vi, ti, ni);
							auto it = vertexCache.find(key);
							uint32_t outIndex;
							if (it != vertexCache.end()) {
								outIndex = it->second;
							} else {
								graphics::VertexData vd;
								vd.position = (vi >= 1 && vi <= static_cast<int>(positions.size())) ? positions[vi - 1] : math::Vector3(0.0f, 0.0f, 0.0f);
								vd.uv       = (ti >= 1 && ti <= static_cast<int>(uvs.size()))       ? uvs[ti - 1]       : math::Vector2(0.0f, 0.0f);
								vd.normal   = (ni >= 1 && ni <= static_cast<int>(normals.size()))   ? normals[ni - 1]   : math::Vector3(0.0f, 1.0f, 0.0f);
								vd.tangent  = math::Vector4(1.0f, 0.0f, 0.0f, 1.0f);
								outIndex = static_cast<uint32_t>(outMesh.vertics.size());
								outMesh.vertics.push_back(vd);
								vertexCache.emplace(key, outIndex);
							}
							faceIndices.push_back(outIndex);
						}
						// 三角形ファン化 (v0, vk, vk+1)
						for (size_t k = 1; k + 1 < faceIndices.size(); ++k) {
							outMesh.indices.push_back(faceIndices[0]);
							outMesh.indices.push_back(faceIndices[k]);
							outMesh.indices.push_back(faceIndices[k + 1]);
						}
					}
				}

				return !outMesh.vertics.empty() && !outMesh.indices.empty();
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

			// DDS は tkm マテリアルで多用。TGA は WIC 非対応のため専用ローダ。
			// それ以外 (.png/.jpg 等) は WIC。
			const std::string extension = GetLowerExtension(requestPath_);
			HRESULT hr =
				  extension == ".dds" ? DirectX::LoadFromDDSFile(filePath, DirectX::DDS_FLAGS_NONE, &info, *image)
				: extension == ".tga" ? DirectX::LoadFromTGAFile(filePath, DirectX::TGA_FLAGS_NONE, &info, *image)
				:                       DirectX::LoadFromWICFile(filePath, DirectX::WIC_FLAGS_NONE, &info, *image);
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


		// FBX 内のアニメーションスタック名を列挙する (エディタのクリップ候補表示用)。
		// geometry/embedded はスキップして高速化する。失敗時は out を空にする。
		void GetFbxAnimationClipNames(const std::string& fbxPath, std::vector<std::string>& out)
		{
			out.clear();
			ufbx_load_opts opts = {};
			opts.ignore_geometry = true;
			opts.ignore_embedded = true;

			ufbx_scene* scene = nullptr;
			ufbx_error  error = {};
			for (const std::string& candidate : BuildResourcePathCandidates(fbxPath)) {
				scene = ufbx_load_file(candidate.c_str(), &opts, &error);
				if (scene) break;
			}
			if (!scene) return;

			out.reserve(scene->anim_stacks.count);
			for (size_t i = 0; i < scene->anim_stacks.count; ++i) {
				const ufbx_string& n = scene->anim_stacks.data[i]->name;
				out.emplace_back(n.data ? n.data : "", n.length);
			}
			ufbx_free_scene(scene);
		}


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
