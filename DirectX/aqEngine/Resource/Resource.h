#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <future>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Graphics/GraphicsTypes.h"
#include "Graphics/IShader.h"
#include "Graphics/IShaderResourceView.h"
#include "Graphics/Shader.h"
#include "Graphics/Meshlet.h"
#include "Math/Bounds.h"
#include "Util/CRC32.h"
#include "Util/ThreadPool.h"

//#include <fbxsdk.h>
//#pragma comment(lib, "libfbxsdk-md.lib")
//#pragma comment(lib, "libxml2-md.lib")
//#pragma comment(lib, "zlib-md.lib")


namespace aq
{
	namespace res
	{
		/** リソースクラスに定義 */
#define engineResource(name) \
public:\
	static int32_t ID() { return aq::util::ComputeCrc32(#name); }


		/**
		 * FBX 内のアニメーションスタック名を列挙する (エディタのクリップ候補表示用)。
		 * geometry/embedded をスキップして軽量ロードする。失敗時は out は空。
		 * 名前は "path.fbx#<名前>" の <名前> として AnimationComponent に使える。
		 */
		void GetFbxAnimationClipNames(const std::string& fbxPath, std::vector<std::string>& out);


		/**
		 * ファイルサイズをプラットフォーム予算(PlatformBudget)と照合する。
		 * maxSingleFileBytes 超過なら診断ログを出して false を返す(ロード拒否)。予算内なら true。
		 * ローダーの Loading()(ワーカースレッド)からファイルサイズ確定後に呼ぶ。path は診断用。
		 * Win32 は maxSingleFileBytes==0(無制限)のため常に true。
		 */
		bool CheckFileBudget(size_t fileBytes, const char* path);




		/**
		 * リソース基底クラス
		 */
		class ResourceBase
		{
		public:
			enum class ResourceState : uint8_t
			{
				Invalid,
				Loading,
				Completed,
			};

		public:
			void* data_;

		private:
			std::atomic<ResourceState> state_;

		public:
			ResourceBase() : data_(nullptr), state_(ResourceState::Loading) {}
			virtual ~ResourceBase() {}

			void* GetData() const { return data_; }

			bool IsLoading()   const { return state_.load(std::memory_order_acquire) == ResourceState::Loading; }
			bool IsCompleted() const { return state_.load(std::memory_order_acquire) == ResourceState::Completed; }
			bool IsFailed()    const { return state_.load(std::memory_order_acquire) == ResourceState::Invalid; }
			bool IsFaild()     const { return IsFailed(); }

			// ローダースレッドから呼ぶ状態更新
			void SetState(ResourceState s) { state_.store(s, std::memory_order_release); }
		};


		/**
		 * リソース読み込み基底クラス (NVI パターン)
		 *
		 * StartAsync() でスレッドプールにロードをサブミットし非同期実行する。
		 * IsFinished() でポーリング、Wait() でブロッキング完了待機。
		 */
		class ResourceLoaderBase
		{
		private:
			using RefResource = std::shared_ptr<ResourceBase>;

		protected:
			std::string requestPath_;
			RefResource resource_;

		private:
			std::future<void> future_;
			std::atomic<bool> loadSucceeded_;

		public:
			ResourceLoaderBase() : requestPath_(), resource_(nullptr), loadSucceeded_(false) {}
			virtual ~ResourceLoaderBase() {}

			virtual ResourceBase* Create() = 0;

			void Request(const char* path) { requestPath_ = path ? path : ""; }
			const std::string& GetRequestPath() const { return requestPath_; }   // 計測ログ用
			void SetRefResource(const RefResource& refResource) { resource_ = refResource; }

			/** ThreadPool::Get() へロードをサブミットする */
			void StartAsync();

			/** ロードが完了（成功・失敗問わず）しているか — ポーリング用 */
			bool IsFinished() const
			{
				if (!future_.valid()) return false;
				return future_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
			}

			/** ロード完了まで呼び出しスレッドをブロック — デストラクタ用 */
			void Wait()
			{
				if (future_.valid()) future_.wait();
			}

			/** worker 完了後、メインスレッドで GPU/API 側の仕上げを行う */
			bool Finish();

		private:
			/** 派生クラスが実装する実際のロード処理 */
			virtual bool Loading() = 0;

			/** Loading 成功後にメインスレッドで実行する処理 */
			virtual bool FinalizeLoading() { return true; }
		};




		/*******************************************/


		/**
		 * マテリアルのテクスチャパス群
		 * 各スロットは空文字列 = 未設定
		 */
		struct MaterialTexturePaths
		{
			std::string albedo;
			std::string normal;
			std::string specular;
			std::string emissive;
		};

		/**
		 * メッシュデータ
		 */
		struct MeshData
		{
			std::vector<graphics::VertexData> vertics;
			std::vector<uint32_t>             indices;
			MaterialTexturePaths              material;
		};
		class MeshResource : public ResourceBase
		{
			engineResource(aq::res::MeshResource);

		public:
			MeshResource()
				: ResourceBase()
			{
				data_ = new MeshData();
			}

			virtual ~MeshResource()
			{
				delete data_;
				data_ = nullptr;
			}

			const std::vector<graphics::VertexData>* GetVertics() const { return &Get()->vertics; }
			const uint32_t GetVerticsSize() const { return static_cast<uint32_t>(GetVertics()->size()); }
			const std::vector<uint32_t>* GetIndices()  const { return &Get()->indices; };
			const uint32_t GetIndicesSize()  const { return static_cast<uint32_t>(GetIndices()->size()); };

			const MaterialTexturePaths& GetMaterial()     const { return Get()->material; }
			const std::string& GetTexturePath()           const { return Get()->material.albedo; }
			bool HasTexturePath()                         const { return !Get()->material.albedo.empty(); }

			/**
			 * ローカル空間の AABB を返す (カリング用)。
			 * 初回呼び出し時に頂点列から計算してキャッシュする。
			 * 複数システムが同一 wave で並列実行される (SystemManager) ため、call_once で 1 回だけ生成する。
			 */
			const math::AABB& GetLocalAABB() const
			{
				std::call_once(aabbOnce_, [this]()
				{
					math::AABBBuilder builder;
					for (const graphics::VertexData& v : Get()->vertics)
						builder.Add(v.position);
					localAABB_    = builder.Build();
					aabbComputed_ = true;
				});
				return localAABB_;
			}

			/** トライアングルカリング用クラスタ (メッシュレット)。初回アクセスで生成しキャッシュ。 */
			const std::vector<graphics::MeshCluster>& GetClusters() const
			{
				EnsureClusters();
				return clusters_;
			}
			/** クラスタ順に並べ替えたインデックス列 (compact 描画の元データ)。 */
			const std::vector<uint32_t>& GetReorderedIndices() const
			{
				EnsureClusters();
				return reorderedIndices_;
			}

		private:
			inline MeshData* Get() const { return static_cast<MeshData*>(data_); }

			// 並列システムから同時に到達しうるため call_once で 1 回だけ生成する
			// (共有メンバ clusters_/reorderedIndices_ への同時書き込みを防ぐ)。
			void EnsureClusters() const
			{
				std::call_once(clustersOnce_, [this]()
				{
					std::vector<math::Vector3> positions;
					positions.reserve(Get()->vertics.size());
					for (const graphics::VertexData& v : Get()->vertics)
						positions.push_back(v.position);
					graphics::GenerateClusters(positions, Get()->indices, 64u, clusters_, reorderedIndices_);
					clustersComputed_ = true;
				});
			}

			mutable math::AABB     localAABB_;
			mutable bool           aabbComputed_ = false;
			mutable std::once_flag aabbOnce_;
			mutable std::vector<graphics::MeshCluster> clusters_;
			mutable std::vector<uint32_t>              reorderedIndices_;
			mutable bool           clustersComputed_ = false;
			mutable std::once_flag clustersOnce_;
		};
		using RefMeshResource = std::shared_ptr<MeshResource>;


		/**
		 * FBX読み込み
		 */
		class FbxLoader : public ResourceLoaderBase
		{
		private:
			struct VertexWork
			{
				math::Vector3 position;
				math::Vector3 normal;
				math::Vector2 uv;
				bool isSetting;

				VertexWork() : isSetting(false) {}
			};

		public:
			FbxLoader();
			~FbxLoader();
			virtual ResourceBase* Create() override
			{
				return new MeshResource();
			}

		private:
			bool Loading() override;
		};

		class MeshLoader : public ResourceLoaderBase
		{
		public:
			MeshLoader();
			~MeshLoader();
			virtual ResourceBase* Create() override
			{
				return new MeshResource();
			}

		private:
			bool Loading() override;
		};


		/*******************************************/


		/**
		 * ボーン情報
		 */
		struct BoneData
		{
			std::string     name;
			int32_t         parentIndex;       // -1 = ルートボーン
			math::Matrix4x4 inverseBindPose;   // 逆バインドポーズ行列 (スキニング計算用)
		};

		/**
		 * スケルタルメッシュデータ (ボーンあり)
		 */
		struct SkeletalMeshData
		{
			std::vector<graphics::SkinnedVertexData> vertices;
			std::vector<uint32_t>                   indices;
			std::vector<BoneData>                   bones;
			MaterialTexturePaths                    material;
		};

		class SkeletalMeshResource : public ResourceBase
		{
			engineResource(aq::res::SkeletalMeshResource);

		public:
			SkeletalMeshResource() : ResourceBase() { data_ = new SkeletalMeshData(); }

			virtual ~SkeletalMeshResource()
			{
				delete static_cast<SkeletalMeshData*>(data_);
				data_ = nullptr;
			}

			uint32_t GetVertexCount() const { return static_cast<uint32_t>(Get()->vertices.size()); }
			uint32_t GetIndexCount()  const { return static_cast<uint32_t>(Get()->indices.size());  }
			uint32_t GetBoneCount()   const { return static_cast<uint32_t>(Get()->bones.size());    }
			const std::vector<graphics::SkinnedVertexData>& GetVertices() const { return Get()->vertices; }
			const std::vector<uint32_t>&                    GetIndices()  const { return Get()->indices;  }
			const std::vector<BoneData>&                    GetBones()    const { return Get()->bones;    }
			const MaterialTexturePaths&                     GetMaterial() const { return Get()->material; }

			/**
			 * バインドポーズの頂点から計算したローカル AABB (カリング用)。
			 * アニメで頂点が膨らむ分はマージンを呼び出し側で付与すること。
			 * 初回呼び出し時に計算してキャッシュする。
			 */
			const math::AABB& GetLocalAABB() const
			{
				std::call_once(aabbOnce_, [this]()
				{
					math::AABBBuilder builder;
					for (const graphics::SkinnedVertexData& v : Get()->vertices)
						builder.Add(v.position);
					localAABB_    = builder.Build();
					aabbComputed_ = true;
				});
				return localAABB_;
			}

			/** トライアングルカリング用クラスタ (バインドポーズ基準)。初回アクセスで生成。 */
			const std::vector<graphics::MeshCluster>& GetClusters() const
			{
				EnsureClusters();
				return clusters_;
			}
			/** クラスタ順に並べ替えたインデックス列 (compact 描画の元データ)。 */
			const std::vector<uint32_t>& GetReorderedIndices() const
			{
				EnsureClusters();
				return reorderedIndices_;
			}

		private:
			inline SkeletalMeshData* Get() const { return static_cast<SkeletalMeshData*>(data_); }

			// 並列システムから同時に到達しうるため call_once で 1 回だけ生成する
			// (共有メンバ clusters_/reorderedIndices_ への同時書き込みを防ぐ)。
			void EnsureClusters() const
			{
				std::call_once(clustersOnce_, [this]()
				{
					std::vector<math::Vector3> positions;
					positions.reserve(Get()->vertices.size());
					for (const graphics::SkinnedVertexData& v : Get()->vertices)
						positions.push_back(v.position);
					graphics::GenerateClusters(positions, Get()->indices, 64u, clusters_, reorderedIndices_);
					clustersComputed_ = true;
				});
			}

			mutable math::AABB     localAABB_;
			mutable bool           aabbComputed_ = false;
			mutable std::once_flag aabbOnce_;
			mutable std::vector<graphics::MeshCluster> clusters_;
			mutable std::vector<uint32_t>              reorderedIndices_;
			mutable bool           clustersComputed_ = false;
			mutable std::once_flag clustersOnce_;
		};
		using RefSkeletalMeshResource = std::shared_ptr<SkeletalMeshResource>;

		/**
		 * スケルタルメッシュ読み込み (TKM v101 形式: ボーン情報付き)
		 */
		class SkeletalMeshLoader : public ResourceLoaderBase
		{
		public:
			SkeletalMeshLoader() = default;
			~SkeletalMeshLoader() = default;
			virtual ResourceBase* Create() override { return new SkeletalMeshResource(); }

		private:
			bool Loading() override;
		};


		/*******************************************/


		/**
		 * アニメーションキーフレーム
		 */
		struct AnimationKeyframe
		{
			float            time;
			math::Vector3    translation;
			math::Quaternion rotation;
			math::Vector3    scale;
		};

		/**
		 * アニメーションクリップデータ
		 */
		struct AnimationClipData
		{
			float                                       duration;
			uint32_t                                    boneCount;
			std::vector<std::vector<AnimationKeyframe>> boneKeyframes; // [boneIndex][keyframeIndex]
		};

		class AnimationResource : public ResourceBase
		{
			engineResource(aq::res::AnimationResource);

		public:
			AnimationResource() : ResourceBase() { data_ = new AnimationClipData(); }

			virtual ~AnimationResource()
			{
				delete static_cast<AnimationClipData*>(data_);
				data_ = nullptr;
			}

			const AnimationClipData* GetClipData() const
			{
				return static_cast<const AnimationClipData*>(data_);
			}
			float    GetDuration()  const { return GetClipData()->duration;  }
			uint32_t GetBoneCount() const { return GetClipData()->boneCount; }

		private:
			inline AnimationClipData* Get() const { return static_cast<AnimationClipData*>(data_); }
		};
		using RefAnimationResource = std::shared_ptr<AnimationResource>;

		/**
		 * アニメーション読み込み (TKA 形式)
		 */
		class AnimationLoader : public ResourceLoaderBase
		{
		public:
			AnimationLoader() = default;
			~AnimationLoader() = default;
			virtual ResourceBase* Create() override { return new AnimationResource(); }

		private:
			bool Loading() override;
		};




		/*******************************************/



		struct PMDData
		{
			struct Header
			{
				char signature[4];
				float version;
				uint8_t size;
				uint8_t encode;
				uint8_t addUvCount;
				uint8_t vertexIndexSize;
				uint8_t textureIndexSize;
				uint8_t materialIndexSize;
				uint8_t bornIndexSize;
				uint8_t morphIndexSize;
				uint8_t rigidIndexSize;
			};
			struct Information
			{
				char modelName[6];
				char commnet[256];
			};
		};
		class PMDResource : public ResourceBase
		{
			engineResource(aq::res::PMDResource);

		public:
			PMDResource()
				: ResourceBase()
			{
			}

			virtual ~PMDResource()
			{
				if (data_) {
					delete data_;
					data_ = nullptr;
				}
			}

		private:
			inline PMDData* Get() const { return static_cast<PMDData*>(data_); }
		};
		using RefPMDResource = std::shared_ptr<PMDResource>;


		/**
		 * PMD読み込み
		 */
		class PMDLoader : public ResourceLoaderBase
		{
		private:
			struct Vertex
			{
				math::Vector3 position;
				math::Vector3 normal;
				math::Vector2 uv;
				uint16_t boneNo[2];
				uint8_t boneWeight;
				uint8_t edgeFlg;
			};

		public:
			PMDLoader();
			~PMDLoader();
			virtual ResourceBase* Create() override
			{
				return new PMDResource();
			}

		private:
			bool Loading() override;
		};




		/*******************************************/


		/**
		 * テクスチャデータ
		 */
		struct TextureData
		{
			graphics::Texture2DDesc desc;
			std::vector<uint8_t> pixels;
			std::vector<graphics::ImageSubresourceData> subresources;
			uint32_t rowPitch;
			uint32_t slicePitch;
			graphics::IShaderResourceView* srv;

			TextureData()
				: desc()
				, pixels()
				, subresources()
				, rowPitch(0)
				, slicePitch(0)
				, srv(nullptr)
			{
			}
			~TextureData()
			{
				if (srv) {
					delete srv;
					srv = nullptr;
				}
			}
		};
		class GPUResource : public ResourceBase
		{
			engineResource(aq::res::GPUResource);

		public:
			GPUResource()
				: ResourceBase()
			{
				data_ = new TextureData();
			}

			virtual ~GPUResource()
			{
				delete data_;
				data_ = nullptr;
			}

			graphics::IShaderResourceView* GetShaderResourceView() const { return Get()->srv; }

		private:
			inline TextureData* Get() const { return static_cast<TextureData*>(data_); }
		};
		using RefGPUResource = std::shared_ptr<GPUResource>;


		/**
		 * GPUリソース読み込み
		 */
		class TextureLoader : public ResourceLoaderBase
		{
		public:
			TextureLoader();
			~TextureLoader();
			virtual ResourceBase* Create() override
			{
				return new GPUResource();
			}

		private:
			bool Loading() override;
			bool FinalizeLoading() override;
		};


		/**
		 * シェーダーデータ
		 *
		 * requestPath_ は graphics::BuildShaderResourceKey() で生成したキーを受け取る。
		 * worker thread では記述子の解析だけを行い、GPU/API オブジェクト生成は
		 * ResourceManager::Update() から呼ばれる FinalizeLoading() で行う。
		 */
		struct ShaderData
		{
			graphics::ShaderResourceDesc desc;
			std::unique_ptr<graphics::IShader> shader;
		};
		class ShaderResource : public ResourceBase
		{
			engineResource(aq::res::ShaderResource);

		public:
			ShaderResource()
				: ResourceBase()
			{
				data_ = new ShaderData();
			}

			virtual ~ShaderResource()
			{
				delete data_;
				data_ = nullptr;
			}

			graphics::IShader* GetShader() const { return Get()->shader.get(); }

		private:
			inline ShaderData* Get() const { return static_cast<ShaderData*>(data_); }
		};
		using RefShaderResource = std::shared_ptr<ShaderResource>;


		class ShaderLoader : public ResourceLoaderBase
		{
		public:
			ShaderLoader();
			~ShaderLoader();
			virtual ResourceBase* Create() override
			{
				return new ShaderResource();
			}

		private:
			bool Loading() override;
			bool FinalizeLoading() override;
		};




		/*******************************************/



		/**
		 * リソース保持用バンク基底
		 */
		class ResourceBankBase
		{
		public:
			ResourceBankBase() {};
			virtual ~ResourceBankBase() {};

			/**
			 * bank だけが参照している未使用リソース (use_count()==1) を解放し、解放数を返す。
			 * シーン切替時などに ResourceManager::UnloadUnused() 経由で呼ばれる。
			 */
			virtual size_t EvictUnused() { return 0; }
		};

		template <typename Resource>
		class TResourceBank : public ResourceBankBase
		{
		private:
			using RefResource = std::shared_ptr<Resource>;
			using ResourceHashMap = std::unordered_map<std::string, RefResource>;
			using ResourcePair = std::pair<std::string, RefResource>;

		private:
			ResourceHashMap resourceMap_;
			mutable std::mutex mutex_;

		public:
			TResourceBank()
			{
				resourceMap_.clear();
			}
			virtual ~TResourceBank()
			{
				resourceMap_.clear();
			}

		public:
			/** キャッシュ済みリソースを返す。なければ nullptr */
			RefResource Find(const std::string& path)
			{
				std::lock_guard<std::mutex> lock(mutex_);
				auto it = resourceMap_.find(path);
				if (it != resourceMap_.end()) {
					if (!it->second || it->second->IsFailed()) {
						resourceMap_.erase(it);
						return RefResource(nullptr);
					}
					return it->second;
				}
				return RefResource(nullptr);
			}
			std::pair<RefResource, bool> RegisterOrGet(const std::string& path, RefResource refResource)
			{
				std::lock_guard<std::mutex> lock(mutex_);
				auto it = resourceMap_.find(path);
				if (it != resourceMap_.end()) {
					if (it->second && !it->second->IsFailed()) {
						return std::make_pair(it->second, false);
					}
					resourceMap_.erase(it);
				}
				resourceMap_.insert(ResourcePair(path, refResource));
				return std::make_pair(refResource, true);
			}

			/**
			 * bank だけが参照している未使用リソースを解放する。解放数を返す。
			 * ロード中はローダが resource_ を保持するため use_count>=2 で残り、誤解放しない。
			 * 呼び出し側が shared_ptr を保持しているリソースも use_count>=2 で残る。
			 */
			size_t EvictUnused() override
			{
				std::lock_guard<std::mutex> lock(mutex_);
				size_t removed = 0;
				for (auto it = resourceMap_.begin(); it != resourceMap_.end(); ) {
					if (!it->second || it->second.use_count() == 1) {
						it = resourceMap_.erase(it);
						++removed;
					} else {
						++it;
					}
				}
				return removed;
			}

			/**
			 * 指定パスをキャッシュから外す。除去できたら true。
			 * 既存の shared_ptr 保持者があっても寿命は shared_ptr が管理するため安全
			 * (bank が参照を手放すだけで、以後のロードは再取得になる)。
			 */
			bool UnloadOne(const std::string& path)
			{
				std::lock_guard<std::mutex> lock(mutex_);
				return resourceMap_.erase(path) > 0;
			}
		};




		/*******************************************/


		/**
		 * リソース管理クラス
		 *
		 * Load<T>() は即座に shared_ptr を返す。実際のロードは ThreadPool 上で非同期実行される。
		 * メインスレッドは resource->IsCompleted() が true になるまで待ってから使用する。
		 * Update() を毎フレーム呼ぶことで完了済みローダーが破棄される。
		 *
		 * リソースはキャッシュ (bank) が shared_ptr で保持し続けるが、Unload<T>(path) /
		 * UnloadUnused() でキャッシュから外してメモリを解放できる (シーン切替時など)。
		 * 呼び出し側が保持している shared_ptr の寿命は影響を受けない。
		 */
		class ResourceManager
		{
		private:
			std::vector<ResourceLoaderBase*> loaders_;
			std::mutex loadersMutex_;


		private:
			ResourceManager();
			~ResourceManager();


		public:
			/** 完了済みローダーを破棄する。毎フレーム呼ぶ */
			void Update();

			/** 非同期ロードを開始し shared_ptr を返す */
			template <typename Resource>
			std::shared_ptr<Resource> Load(const char* path)
			{
				TResourceBank<Resource>* bank = FindBank<Resource, TResourceBank<Resource>>();
				if (bank == nullptr) {
 					EngineAssert(false);
					return nullptr;
				}

				const std::string key = NormalizeResourcePath(path);

				// キャッシュ済みなら即返す
				std::shared_ptr<Resource> refResource = bank->Find(key);
				if (refResource) {
					return refResource;
				}

				ResourceLoaderBase* loader = static_cast<ResourceLoaderBase*>(CreateLoader<Resource>());
				if (loader == nullptr) {
					return nullptr;
				}

				// Create() の所有権を直接 shared_ptr に渡す（コピー不要・double-free 回避）
				refResource = std::shared_ptr<Resource>(static_cast<Resource*>(loader->Create()));
				if (!refResource) {
					delete loader;
					return nullptr;
				}

				const auto registerResult = bank->RegisterOrGet(key, refResource);
				if (!registerResult.second) {
					delete loader;
					return registerResult.first;
				}

				loader->Request(key.c_str());
				loader->SetRefResource(refResource);

				{
					std::lock_guard<std::mutex> lock(loadersMutex_);
					loaders_.push_back(loader);
				}

				// ThreadPool へサブミットして非同期ロード開始
				loader->StartAsync();

				return refResource;
			}

			RefShaderResource LoadShader(
				const char* filePath,
				const char* entryFuncName,
				graphics::IShader::ShaderType shaderType)
			{
				const std::string key = graphics::BuildShaderResourceKey(filePath, entryFuncName, shaderType);
				return Load<ShaderResource>(key.c_str());
			}


			/**
			 * 指定パスのリソースをキャッシュ (bank) から外す。除去できたら true。
			 * 呼び出し側が shared_ptr を保持していてもクラッシュしない (寿命は shared_ptr が管理)。
			 * bank が参照を手放すだけで、以後同じパスを Load すると再ロードされる。
			 */
			template <typename Resource>
			bool Unload(const char* path)
			{
				auto* bank = FindBank<Resource, TResourceBank<Resource>>();
				if (bank == nullptr) {
					return false;
				}
				return bank->UnloadOne(NormalizeResourcePath(path));
			}

			/**
			 * どこからも使われていない (bank だけが参照している) リソースを全型から解放する。
			 * 解放したリソース数を返す。シーン切替の直後など、参照が切れた後に呼ぶ。
			 * ロード中・利用中のリソースは use_count>=2 のため解放されない (安全)。
			 */
			size_t UnloadUnused()
			{
				size_t removed = 0;
				for (auto& bank : bankMap_) {
					if (bank.second != nullptr) {
						removed += bank.second->EvictUnused();
					}
				}
				return removed;
			}


		private:
			/** 完了・失敗済みのローダーだけ破棄する */
			void ClearFinishedLoaders();




			/**
			 * シングルトン
			 */
		private:
			static ResourceManager* instance_;

		public:
			static void Initialize()
			{
				if (instance_ == nullptr) {
					instance_ = new ResourceManager();
				}
			}
			static ResourceManager& Get() { return *instance_; }
			static void Finalize()
			{
				if (instance_) {
					delete instance_;
					instance_ = nullptr;
				}
				ClearBank();
				ClearReflection();
			}




			/**
			 * リソースバンク
			 * RegisterBank<Resource, TResourceBank<Resource>>() でエンジン・アプリ両側から登録可能
			 */
		private:
			using BankHashMap = std::unordered_map<uint32_t, ResourceBankBase*>;
			using BankPair = std::pair<uint32_t, ResourceBankBase*>;
			static BankHashMap bankMap_;

			static std::string NormalizeResourcePath(const char* path)
			{
				std::string normalized = path ? path : "";
				std::replace(normalized.begin(), normalized.end(), '\\', '/');
				if (normalized.compare(0, 2, "./") == 0) {
					normalized.erase(0, 2);
				}
				return normalized;
			}


		public:
			template <typename Resource, typename TResourceBank>
			static void RegisterBank()
			{
				if (bankMap_.count(Resource::ID()) > 0) {
					// 登録済み
					EngineAssert(false);
					return;
				}
				bankMap_.insert(BankPair(Resource::ID(), new TResourceBank()));
			}

			static void ClearBank()
			{
				if (bankMap_.size() == 0) {
					return;
				}
				for (auto it : bankMap_) {
					auto* ptr = it.second;
					delete ptr;
					ptr = nullptr;
				}
				bankMap_.clear();
			}


		private:
			template <typename Resource, typename TResourceBank>
			TResourceBank* FindBank()
			{
				auto it = bankMap_.find(Resource::ID());
				if (it == bankMap_.end()) {
					return nullptr;
				}
				return static_cast<TResourceBank*>(it->second);
			}




			/**
			 * ローダークラスのリフレクション
			 * Reflection<Resource, Loader>() でエンジン・アプリ両側から登録可能
			 */
		private:
			using ReflectionFunc = std::function<void*()>;
			using ReflectionHashMap = std::unordered_map<uint32_t, ReflectionFunc>;
			using ReflectionPair = std::pair<uint32_t, ReflectionFunc>;

		private:
			static ReflectionHashMap loaderHashMap_;

		public:
			template <typename Resource, typename Loader>
			static void Reflection()
			{
				loaderHashMap_.insert(ReflectionPair(Resource::ID(), [] { return static_cast<void*>(new Loader()); }));
			}

			static void ClearReflection()
			{
				loaderHashMap_.clear();
			}

		private:
			template<typename Resource>
			static void* CreateLoader()
			{
				const auto& it = loaderHashMap_.find(Resource::ID());
				if (it != loaderHashMap_.end()) {
					return it->second();
				}
				EngineAssert(false);
				return nullptr;
			}
		};
	}
}
