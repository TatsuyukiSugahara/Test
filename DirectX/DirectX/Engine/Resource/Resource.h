#pragma once
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
			ResourceState state_;

		public:
			ResourceBase() : data_(nullptr), state_(ResourceState::Loading) {}
			virtual ~ResourceBase() {}

			void* GetData() const { return data_; }

			bool IsLoading() const { return state_ == ResourceState::Loading; }
			bool IsCompleted() const { return state_ == ResourceState::Completed; }
			bool IsFaild() const { return state_ == ResourceState::Invalid; }
		};

		/**
		 * リソース読み込み基底クラス
		 */
		class ResourceLoaderBase
		{
		private:
			using RefResource = std::shared_ptr<ResourceBase>;

		protected:
			const char* requestPath_;
			RefResource resource_;

		public:
			ResourceLoaderBase() : requestPath_(nullptr), resource_(nullptr) {}
			virtual ~ResourceLoaderBase() {}
			virtual ResourceBase* Create() = 0;
			virtual void Update() = 0;
			void Request(const char* path) { requestPath_ = path; }
			void SetRefResource(const RefResource& refResource) { resource_ = refResource; }
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
			virtual void Update() override;

		private:
			bool Loading();

		//private:
		//	static int32_t CreateVertexIndex(const VertexWork& vertexWork, const fbxsdk::FbxVector4& normal, const fbxsdk::FbxVector2& uv, std::vector<VertexWork>& vertexWorkList, const int32_t oldIndex, std::vector<math::Vector2Int32>& indexPairList);
		//	static bool IsSetNormalUV(const VertexWork& vertexWork, const fbxsdk::FbxVector4& normal, const fbxsdk::FbxVector2& uv);
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
				//data_ = new PMDData();
			}

			virtual ~PMDResource()
			{
				if (data_) {
					delete data_;
					data_ = nullptr;
				}
			}

			//const std::vector<graphics::VertexData>* GetVertics() const { return &Get()->vertics; }
			//const uint32_t GetVerticsSize() const { return GetVertics()->size(); }
			//const std::vector<uint32_t>* GetIndices()  const { return &Get()->indices; };
			//const uint32_t GetIndicesSize()  const { return GetIndices()->size(); };

		private:
			inline PMDData* Get() const { return static_cast<PMDData*>(data_); }
		};
		using RefPMDResource = std::shared_ptr<PMDResource>;

		/**
		 * FBX読み込み
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
			virtual void Update() override;

		private:
			bool Loading();
		};




		/*******************************************/


		/**
		 * テクスチャデータ
		 */
		struct TextureData
		{
			graphics::ShaderResourceView* srv;

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

			graphics::ShaderResourceView* GetShaderResourceView() const { return Get()->srv; }

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
			virtual void Update() override;

		private:
			bool Loading();
		};

		//class GPUResource : public Resource
		//{
		//private:
		//	graphics::ShaderResourceView shaderResourceView_;

		//public:
		//	GPUResource();
		//	~GPUResource();

		//	bool Initialize(const char* path);

		//	graphics::ShaderResourceView* GetShaderResourceView() { return &shaderResourceView_; }
		//};

		//class GPUResourceLoader : public ResourceLoaderBase<GPUResource>
		//{
		//private:
		//	enum class State : uint8_t
		//	{
		//		Invalid,
		//		Reqesuted,
		//		Loading,
		//		Completed,
		//	};

		//private:
		//	const char* requestPath_;
		//	State state_;

		//public:
		//	GPUResourceLoader();
		//	~GPUResourceLoader();

		//	virtual void Update() override;

		//	virtual bool IsCompleted() { return state_ == State::Completed; }
		//	virtual bool IsFaild() { return state_ == State::Invalid; }

		//public:
		//	void Request(const char* path);
		//};




		/*******************************************/



		/**
		 * リソース保持用のバンク
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
			/**
			 * リソースを検索して取得
			 */
			RefResource Find(const char* path)
			{
				auto it = resourceMap_.find(path);
				if (it != resourceMap_.end()) {
					return static_cast<RefResource>(it->second);
				}
				return RefResource(nullptr);
			}
			/**
			 * リソースが含まれるか
			 */
			bool Contains(const char* path) const
			{
				auto it = resourceMap_.find(path);
				if (it != resourceMap_.end()) {
					return true;
				}
				return false;
			}
			/**
			 * リソース登録
			 */
			void Register(const char* path, RefResource refResource)
			{
				if (Contains(path)) {
					return;
				}
				resourceMap_.insert(ResourcePair(path, refResource));
			}
		};




		/*******************************************/


		/** ResourceManagerでのLoad関数でLoadを指定しなくて良いようにしたい */
		//class ResourceLoaderReflection
		//{
		//public:
		//	/** リフレクション用インターフェース */
		//	class IReflection
		//	{
		//	public:
		//		std::function<void*> createInstance;
		//	};
		//};

		/*******************************************/


		/**
		 * リソース管理クラス
		 */
		class ResourceManager
		{
		private:
			std::vector<ResourceLoaderBase*> loaders_;


		private:
			ResourceManager();
			~ResourceManager();


		public:
			/** 更新 */
			void Update();

			/** 読み込み */
			template <typename Resource>
			std::shared_ptr<Resource> Load(const char* path)
			{
				TResourceBank<Resource>* bank = FindBank<Resource, TResourceBank<Resource>>();
				if (bank == nullptr) {
 					EngineAssert(false);
				}
				std::shared_ptr<Resource> refResource = bank->Find(path);
				if (refResource) {
					return refResource;
				}

				ResourceLoaderBase* loader = static_cast<ResourceLoaderBase*>(CreateLoader<Resource>());
				loader->Request(path);
				loaders_.push_back(loader);

				Resource* resource = static_cast<Resource*>(loader->Create());
				refResource = std::make_shared<Resource>(*resource);
				loader->SetRefResource(refResource);
				bank->Register(path, refResource);

				return refResource;
			}


		private:
			void ClearLoaders();




			/**
			 * シングルトン用
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
			 */
		private:
			using BankHashMap = std::unordered_map<uint32_t, ResourceBankBase*>;
			using BankPair = std::pair<uint32_t, ResourceBankBase*>;
			static BankHashMap bankMap_;


		public:
			/** バンクの登録 */
			template <typename Resource, typename TResourceBank>
			static void RegisterBank()
			{
				if (bankMap_.contains(Resource::ID())) {
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
			/** バンクを探す */
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
			 * リソース読み込みクラスのリフレクション用
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
				loaderHashMap_.insert(ReflectionPair(Resource::ID(), [] { return new Loader(); }));
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