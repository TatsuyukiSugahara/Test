#pragma once
//#include <fbxsdk.h>
//#pragma comment(lib, "libfbxsdk-md.lib")
//#pragma comment(lib, "libxml2-md.lib")
//#pragma comment(lib, "zlib-md.lib")


namespace engine
{
	namespace res
	{
		/** ���\�[�X�ǂݍ��݃N���X�ɒ�` */
#define engineResourceLoader(name) \
public:\
	static int32_t ResourceBankID() { return engine::util::ComputeCrc32(#name); }

		/** �����ȃ��\�[�X�o���NID */
		static constexpr int32_t INVALID_RESOURCE_BANK_ID = 0;


		/**
		 * ���\�[�X���N���X
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

		protected:
			void* data_;
			ResourceState state_;

		public:
			ResourceBase() : data_(nullptr), state_(ResourceState::Loading) {}
			virtual ~ResourceBase() {}

			void* GetData() const { return data_; }

			bool IsLoading() const { return state_ == ResourceState::Loading; }
			bool IsCompleted() const { return state_ == ResourceState::Completed; }
			bool IsFaild() const { return state_ == ResourceState::Invalid; }

		public:
			static uint32_t ResourceBankID() { return INVALID_RESOURCE_BANK_ID; }
		};

		/**
		 * ���\�[�X�ǂݍ��݊��N���X
		 */
		class ResourceLoaderBase
		{
		protected:
			const char* requestPath_;

		public:
			ResourceLoaderBase() : requestPath_(nullptr) {}
			virtual ~ResourceLoaderBase() {}
			virtual void Update() = 0;

			template <typename RefResource>
			RefResource Create() { return nullptr; }

			void Request(const char* path) { requestPath_ = path; }
		};




		/*******************************************/


		/**
		 * ���b�V���f�[�^
		 */
		struct MeshData
		{
			std::vector<graphics::VertexData> vertics;
			std::vector<uint32_t> indices;
		};
		class MeshResource : public ResourceBase
		{
			friend class FbxLoader;

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
		 * FBX�ǂݍ���
		 */
		class FbxLoader : public ResourceLoaderBase
		{
			engineResourceLoader(FbxLoader);

		private:
			struct VertexWork
			{
				math::Vector3 position;
				math::Vector3 normal;
				math::Vector2 uv;
				bool isSetting;

				VertexWork() : isSetting(false) {}
			};

		private:
			RefMeshResource resource_;

		public:
			FbxLoader();
			~FbxLoader();

			virtual void Update() override;
			RefMeshResource Create();

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
			friend class PMDLoader;

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
		 * FBX�ǂݍ���
		 */
		class PMDLoader : public ResourceLoaderBase
		{
			engineResourceLoader(PMDLoader);

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
		private:
			RefPMDResource resource_;

		public:
			PMDLoader();
			~PMDLoader();

			virtual void Update() override;
			RefPMDResource Create();

		private:
			bool Loading();
		};




		/*******************************************/


		/**
		 * �e�N�X�`���f�[�^
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
			friend class TextureLoader;

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
		 * GPU���\�[�X�ǂݍ���
		 */
		class TextureLoader : public ResourceLoaderBase
		{
			engineResourceLoader(TextureLoader);

		private:
			RefGPUResource resource_;

		public:
			TextureLoader();
			~TextureLoader();

			virtual void Update() override;
			RefGPUResource Create();

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
		 * ���\�[�X�ێ��p�̃o���N
		 */
		class ResourceBankBase
		{
		public:
			ResourceBankBase() {};
			~ResourceBankBase() {};
		};

		template <typename RefResource>
		class TResourceBank : public ResourceBankBase
		{
		private:
			std::unordered_map<std::string, RefResource> resourceMap_;

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
			 * ���\�[�X���������Ď擾
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
			 * ���\�[�X���܂܂�邩
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
			 * ���\�[�X�o�^
			 */
			void Register(const char* path, RefResource refResource)
			{
				if (Contains(path)) {
					return;
				}
				resourceMap_.insert(std::pair<std::string, RefResource>(path, refResource));
			}
		};




		/*******************************************/


		/** ResourceManager�ł�Load�֐���Load���w�肵�Ȃ��ėǂ��悤�ɂ����� */
		//class ResourceLoaderReflection
		//{
		//public:
		//	/** ���t���N�V�����p�C���^�[�t�F�[�X */
		//	class IReflection
		//	{
		//	public:
		//		std::function<void*> createInstance;
		//	};
		//};

		/*******************************************/


		/**
		 * ���\�[�X�Ǘ��N���X
		 */
		class ResourceManager
		{
		private:
			std::vector<ResourceLoaderBase*> loaders_;
			std::unordered_map<int32_t, ResourceBankBase*> bankMap_;

		private:
			ResourceManager();
			~ResourceManager();


		public:
			/** �X�V */
			void Update();

			/** �ǂݍ��� */
			template <typename RefResource, typename TLoader>
			RefResource Load(const char* path)
			{
				TResourceBank<RefResource>* bank = FindBank<TResourceBank<RefResource>>(TLoader::ResourceBankID());
				if (bank == nullptr) {
 					EngineAssert(false);
				}
				RefResource refResource = bank->Find(path);
				if (refResource) {
					return refResource;
				}

				TLoader* loader = new TLoader();
				loader->Request(path);
				loaders_.push_back(loader);

				refResource = loader->Create();
				bank->Register(path, refResource);

				return refResource;
			}

			/** �o���N�̓o�^ */
			template <typename TResourceBank>
			void RegisterBank(const int32_t bankId)
			{
				if (bankMap_.contains(bankId)) {
					// �o�^�ς�
					EngineAssert(false);
					return;
				}
				bankMap_.insert(std::pair<int32_t, ResourceBankBase*>(bankId, new TResourceBank()));
			}


		private:
			/** �o���N��T�� */
			template <typename TResourceBank>
			TResourceBank* FindBank(const int32_t bankId)
			{
				auto it = bankMap_.find(bankId);
				if (it == bankMap_.end()) {
					return nullptr;
				}
				return static_cast<TResourceBank*>(it->second);
			}


		private:
			void ClearLoaders();
			void ClearBank();


			/**
			 * �V���O���g���p
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
		};
	}
}