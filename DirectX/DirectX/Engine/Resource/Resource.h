#pragma once
#include <atomic>
#include <future>
#include <chrono>
#include "../Graphics/GraphicsTypes.h"
#include "../Graphics/IShaderResourceView.h"

//#include <fbxsdk.h>
//#pragma comment(lib, "libfbxsdk-md.lib")
//#pragma comment(lib, "libxml2-md.lib")
//#pragma comment(lib, "zlib-md.lib")


namespace engine
{
	namespace res
	{
		/** リソースクラスに定義 */
#define engineResource(name) \
public:\
	static int32_t ID() { return engine::util::ComputeCrc32(#name); }




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
			bool IsFaild()     const { return state_.load(std::memory_order_acquire) == ResourceState::Invalid; }

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
			const char* requestPath_;
			RefResource resource_;

		private:
			std::future<void> future_;

		public:
			ResourceLoaderBase() : requestPath_(nullptr), resource_(nullptr) {}
			virtual ~ResourceLoaderBase() {}

			virtual ResourceBase* Create() = 0;

			void Request(const char* path) { requestPath_ = path; }
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

		private:
			/** 派生クラスが実装する実際のロード処理 */
			virtual bool Loading() = 0;
		};




		/*******************************************/


		/**
		 * メッシュデータ
		 */
		struct MeshData
		{
			std::vector<graphics::VertexData> vertics;
			std::vector<uint32_t> indices;
		};
		class MeshResource : public ResourceBase
		{
			engineResource(engine::res::MeshResource);

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

		private:
			inline MeshData* Get() const { return static_cast<MeshData*>(data_); }
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
			engineResource(engine::res::PMDResource);

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
			graphics::IShaderResourceView* srv;

			TextureData() : srv(nullptr) {}
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
			engineResource(engine::res::GPUResource);

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
		};




		/*******************************************/



		/**
		 * リソース保持用バンク基底
		 */
		class ResourceBankBase
		{
		public:
			ResourceBankBase() {};
			~ResourceBankBase() {};
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
			RefResource Find(const char* path)
			{
				auto it = resourceMap_.find(path);
				if (it != resourceMap_.end()) {
					return static_cast<RefResource>(it->second);
				}
				return RefResource(nullptr);
			}
			bool Contains(const char* path) const
			{
				auto it = resourceMap_.find(path);
				if (it != resourceMap_.end()) {
					return true;
				}
				return false;
			}
			void Register(const char* path, RefResource refResource)
			{
				if (Contains(path)) {
					return;
				}
				resourceMap_.insert(ResourcePair(path, refResource));
			}
		};




		/*******************************************/


		/**
		 * リソース管理クラス
		 *
		 * Load<T>() は即座に shared_ptr を返す。実際のロードは ThreadPool 上で非同期実行される。
		 * メインスレッドは resource->IsCompleted() が true になるまで待ってから使用する。
		 * Update() を毎フレーム呼ぶことで完了済みローダーが破棄される。
		 */
		class ResourceManager
		{
		private:
			std::vector<ResourceLoaderBase*> loaders_;


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
				}

				// キャッシュ済みなら即返す
				std::shared_ptr<Resource> refResource = bank->Find(path);
				if (refResource) {
					return refResource;
				}

				ResourceLoaderBase* loader = static_cast<ResourceLoaderBase*>(CreateLoader<Resource>());
				loader->Request(path);

				// Create() の所有権を直接 shared_ptr に渡す（コピー不要・double-free 回避）
				refResource = std::shared_ptr<Resource>(static_cast<Resource*>(loader->Create()));
				loader->SetRefResource(refResource);
				bank->Register(path, refResource);

				// ThreadPool へサブミットして非同期ロード開始
				loader->StartAsync();
				loaders_.push_back(loader);

				return refResource;
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
			}




			/**
			 * リソースバンク
			 * RegisterBank<Resource, TResourceBank<Resource>>() でエンジン・アプリ両側から登録可能
			 */
		private:
			using BankHashMap = std::unordered_map<uint32_t, ResourceBankBase*>;
			using BankPair = std::pair<uint32_t, ResourceBankBase*>;
			static BankHashMap bankMap_;


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
