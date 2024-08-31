#include "../EnginePreCompile.h"
#include "../Graphics/GPUBuffer.h"
#include "../Graphics/RenderContext.h"
#include "Resource.h"
#include "../Engine.h"


namespace engine
{
	namespace res
	{
		/**
		 * FBX���\�[�X�ǂݍ���
		 */
		FbxLoader::FbxLoader()
			: ResourceLoaderBase()
			, resource_(nullptr)
		{
		}


		FbxLoader::~FbxLoader()
		{
		}


		void FbxLoader::Update()
		{
			if (resource_->IsCompleted()) {
				return;
			}

			if (!Loading()) {
				// ���s
				EngineAssertMsg(false, "FbxLoader / �ǂݍ��݂Ɏ��s���܂����B\n");
				resource_->state_ = ResourceBase::ResourceState::Invalid;
				return;
			}
			resource_->state_ = ResourceBase::ResourceState::Completed;
		}


		bool FbxLoader::Loading()
		{
			return true;
		}
		//{
		//	// �}�l�[�W���[������
		//	fbxsdk::FbxManager* fbxManager = fbxsdk::FbxManager::Create();
		//	// �C���|�[�^�[������
		//	FbxImporter* importer = FbxImporter::Create(fbxManager, "");
		//	if (!importer->Initialize(requestPath_, -1, fbxManager->GetIOSettings())) {
		//		return false;
		//	}

		//	// �V�[���쐬
		//	FbxScene* scene = FbxScene::Create(fbxManager, "");
		//	importer->Import(scene);
		//	importer->Destroy();

		//	// �O�p�|���S���ɂ���
		//	FbxGeometryConverter geometryConverter(fbxManager);
		//	if (!geometryConverter.Triangulate(scene, true)) {
		//		return false;
		//	}

		//	// ���b�V���擾
		//	FbxMesh* mesh = scene->GetSrcObject<FbxMesh>();
		//	if (!mesh) {
		//		return false;
		//	}

		//	MeshData* meshData = static_cast<MeshData*>(resource_->data_);

		//	// UV�Z�b�g���̎擾
		//	FbxStringList uvSetNameList;
		//	mesh->GetUVSetNames(uvSetNameList);
		//	const char* uvSetName = uvSetNameList.GetStringAt(0);

		//	// ���_���W���X�g
		//	std::vector<VertexWork> vertexWorkList;
		//	for (int32_t i = 0; i < mesh->GetControlPointsCount(); ++i) {
		//		fbxsdk::FbxVector4 point = mesh->GetControlPointAt(i);
		//		VertexWork vertexWork;
		//		vertexWork.position.Set(static_cast<float>(point[0]), static_cast<float>(point[1]), static_cast<float>(point[2]));
		//		vertexWorkList.push_back(vertexWork);
		//	}
		//	// ���_���̏��擾
		//	std::vector<math::Vector2Int32> workIndexPairList;
		//	for (int32_t polygonIndex = 0; polygonIndex < mesh->GetPolygonCount(); ++polygonIndex) {
		//		for (int32_t vertexIndex = 0; vertexIndex < mesh->GetPolygonSize(polygonIndex); ++vertexIndex) {
		//			// �C���f�b�N�X
		//			int32_t index = mesh->GetPolygonVertex(polygonIndex, vertexIndex);
		//			// ���_���W
		//			VertexWork& vertexWork = vertexWorkList[index];
		//			// �@�����W
		//			fbxsdk::FbxVector4 normal;
		//			mesh->GetPolygonVertexNormal(polygonIndex, vertexIndex, normal);
		//			// UV���W
		//			fbxsdk::FbxVector2 uv;
		//			bool isUnMapped;
		//			mesh->GetPolygonVertexUV(polygonIndex, vertexIndex, uvSetName, uv, isUnMapped);
		//			// �@����UV�ݒ肳��Ă��Ȃ���ΐݒ�
		//			if (!vertexWork.isSetting) {
		//				vertexWork.normal.Set(static_cast<float>(normal[0]), static_cast<float>(normal[1]), static_cast<float>(normal[2]));
		//				vertexWork.uv.Set(static_cast<float>(uv[0]), static_cast<float>(uv[1]));
		//				vertexWork.isSetting = true;
		//			}
		//			else if (!IsSetNormalUV(vertexWork, normal, uv)) {
		//				// ���꒸�_�C���f�b�N�X���Ƃ��Ă��@����UV���قȂ�ꍇ�͐V���ȃC���f�b�N�X�Ƃ���
		//				index = CreateVertexIndex(vertexWork, normal, uv, vertexWorkList, index, workIndexPairList);
		//			}
		//			meshData->indices.push_back(index);
		//		}
		//	}
		//	// ���_��񐶐�
		//	const uint32_t workListSize = static_cast<uint32_t>(vertexWorkList.size());
		//	static_cast<MeshData*>(resource_->data_)->vertics.resize(workListSize);
		//	for (uint32_t i = 0; i < workListSize; ++i) {
		//		const VertexWork& work = vertexWorkList[i];

		//		graphics::VertexData buffer;
		//		buffer.position.Set(work.position);
		//		buffer.normal.Set(work.normal);
		//		buffer.uv.Set(work.uv);

		//		meshData->vertics[i] = buffer;
		//	}

		//	return true;
		//}


		RefMeshResource FbxLoader::Create()
		{
			resource_ = RefMeshResource(new MeshResource());
			return resource_;
		}


		//int32_t FbxLoader::CreateVertexIndex(const VertexWork& vertexWork, const fbxsdk::FbxVector4& normal, const fbxsdk::FbxVector2& uv, std::vector<VertexWork>& vertexWorkList, const int32_t oldIndex, std::vector<math::Vector2Int32>& indexPairList)
		//{
		//	// ���łɐ�������Ă���ꍇ�́A���̃C���f�b�N�X��Ԃ�
		//	for (int32_t i = 0; i < indexPairList.size(); ++i) {
		//		const int32_t newIndex = indexPairList[i].y;
		//		if (oldIndex != indexPairList[i].x) continue;
		//		if (!IsSetNormalUV(vertexWorkList[newIndex], normal, uv)) continue;
		//		return newIndex;
		//	}
		//	// �V���ȃC���f�b�N�X���쐬���ĕԂ�
		//	VertexWork work;
		//	work.position.Set(vertexWork.position);
		//	work.normal.Set(static_cast<float>(normal[0]), static_cast<float>(normal[1]), static_cast<float>(normal[2]));
		//	work.uv.Set(static_cast<float>(uv[0]), static_cast<float>(uv[1]));
		//	work.isSetting = true;
		//	vertexWorkList.push_back(work);
		//	const int32_t newIndex = static_cast<int32_t>(vertexWorkList.size()) - 1;
		//	indexPairList.push_back(math::Vector2Int32(oldIndex, newIndex));
		//	return newIndex;
		//}


		//bool FbxLoader::IsSetNormalUV(const VertexWork& vertexWork, const fbxsdk::FbxVector4& normal, const fbxsdk::FbxVector2& uv)
		//{
		//	if (!vertexWork.normal.IsEquals(math::Vector3(static_cast<float>(normal[0]), static_cast<float>(normal[1]), static_cast<float>(normal[2])))) return false;
		//	if (!vertexWork.uv.IsEquals(math::Vector2(static_cast<float>(uv[0]), static_cast<float>(uv[1])))) return false;
		//	return true;
		//}




		/*******************************************/


		PMDLoader::PMDLoader()
			: ResourceLoaderBase()
			, resource_(nullptr)
		{
		}


		PMDLoader::~PMDLoader()
		{
		}


		void PMDLoader::Update()
		{
			if (resource_->IsCompleted()) {
				return;
			}

			if (!Loading()) {
				// ���s
				EngineAssertMsg(false, "FbxLoader / �ǂݍ��݂Ɏ��s���܂����B\n");
				resource_->state_ = ResourceBase::ResourceState::Invalid;
				return;
			}
			resource_->state_ = ResourceBase::ResourceState::Completed;
		}


		bool PMDLoader::Loading()
		{
			// �t�@�C�����J��
			FILE* fp = nullptr;
			fopen_s(&fp, requestPath_, "rb");

			if (fp == nullptr) {
				return false;
			}

			// �t�@�C���T�C�Y�擾
			fpos_t fileSize;
			fseek(fp, 0, SEEK_END);
			fgetpos(fp, &fileSize);
			fseek(fp, 0, SEEK_SET);
			// �t�@�C���f�[�^�擾
			uint8_t* bin = new uint8_t[fileSize];
			fileSize = fread_s(bin, sizeof(uint8_t)*fileSize, sizeof(uint8_t), static_cast<size_t>(fileSize), fp);
			fclose(fp);

			// �w�b�_�[
			PMDData::Header* header = reinterpret_cast<PMDData::Header*>(bin);
			bin += sizeof(PMDData::Header);

			//PMDData::Information* information = reinterpret_cast<PMDData::Information*>(bin);
			//bin += 183;//sizeof(PMDData::Information);

			// ���_���
			//uint32_t vertexCount = *reinterpret_cast<uint32_t*>(bin);
			//bin += sizeof(uint32_t);
			
			

			Vertex* vertex = reinterpret_cast<Vertex*>(bin);

			//PMDData data;
			return true;
		}


		RefPMDResource PMDLoader::Create()
		{
			resource_ = RefPMDResource(new PMDResource());
			return resource_;
		}




		/*******************************************/


		/**
		 * FBX���\�[�X�ǂݍ���
		 */
		TextureLoader::TextureLoader()
			: ResourceLoaderBase()
			, resource_(nullptr)
		{
		}


		TextureLoader::~TextureLoader()
		{
		}


		void TextureLoader::Update()
		{
			if (resource_->IsCompleted()) {
				return;
			}

			if (!Loading()) {
				// ���s
				EngineAssertMsg(false, "TextureLoader / �ǂݍ��݂Ɏ��s���܂����B\n");
				resource_->state_ = ResourceBase::ResourceState::Invalid;
				return;
			}
			resource_->state_ = ResourceBase::ResourceState::Completed;
		}


		bool TextureLoader::Loading()
		{
			// �}���`�o�C�g�����񂩂烏�C�h������֕ϊ�
			wchar_t filePath[256];
			size_t ret;
			mbstowcs_s(&ret, filePath, requestPath_, ArraySize(filePath));

			DirectX::TexMetadata info;
			std::unique_ptr<DirectX::ScratchImage> image = std::make_unique<DirectX::ScratchImage>();

			// �摜�ǂݍ���
			HRESULT hr = DirectX::LoadFromWICFile(filePath, DirectX::WIC_FLAGS_NONE, &info, *image);
			if (FAILED(hr)) {
				info = {};
				return false;
			}
			// �~�b�v�}�b�v
			if (info.mipLevels == 1) {
				std::unique_ptr<DirectX::ScratchImage> mipImage = std::make_unique<DirectX::ScratchImage>();
				hr = DirectX::GenerateMipMaps(image->GetImages(), image->GetImageCount(), image->GetMetadata(), DirectX::TEX_FILTER_DEFAULT, 0, *mipImage);
				if (SUCCEEDED(hr)) {
					image = std::move(mipImage);
				}
			}

			TextureData* texureData = static_cast<TextureData*>(resource_->data_);
			texureData->srv = engine::graphics::Texture::Create2D(info, image->GetImages());

			return true;
		}


		RefGPUResource TextureLoader::Create()
		{
			resource_ = RefGPUResource(new GPUResource());
			return resource_;
		}

		//GPUResource::GPUResource()
		//	: shaderResourceView_()
		//{
		//}

		//GPUResource::~GPUResource()
		//{
		//}


		//bool GPUResource::Initialize(const char* path)
		//{
		//	// �}���`�o�C�g�����񂩂烏�C�h������֕ϊ�
		//	wchar_t filePath[256];
		//	size_t ret;
		//	mbstowcs_s(&ret, filePath, path, ArraySize(filePath));
		//	
		//	DirectX::TexMetadata info;
		//	std::unique_ptr<DirectX::ScratchImage> image = std::make_unique<DirectX::ScratchImage>();

		//	// �摜�ǂݍ���
		//	HRESULT hr = DirectX::LoadFromWICFile(filePath, DirectX::WIC_FLAGS_NONE, &info, *image);
		//	if (FAILED(hr)) {
		//		info = {};
		//		return false;
		//	}
		//	// �~�b�v�}�b�v
		//	if (info.mipLevels == 1) {
		//		std::unique_ptr<DirectX::ScratchImage> mipImage = std::make_unique<DirectX::ScratchImage>();
		//		hr = DirectX::GenerateMipMaps(image->GetImages(), image->GetImageCount(), image->GetMetadata(), DirectX::TEX_FILTER_DEFAULT, 0, *mipImage);
		//		if (SUCCEEDED(hr)) {
		//			image = std::move(mipImage);
		//		}
		//	}

		//	//DirectX::CreateTexture(Engine::Get().GetD3DDevice(), image->GetImages(), image->GetImageCount(), info, shaderResourceView_.GetBody());

		//	D3D11_TEXTURE2D_DESC desc = {};
		//	desc.Width = static_cast<UINT>(info.width);
		//	desc.Height = static_cast<UINT>(info.height);
		//	desc.MipLevels = 1;//static_cast<UINT>(info.mipLevels);
		//	desc.ArraySize = static_cast<UINT>(info.arraySize);
		//	desc.Format = info.format;
		//	desc.SampleDesc.Count = 1;
		//	desc.SampleDesc.Quality = 0;
		//	desc.Usage = D3D11_USAGE_DEFAULT;// usage;
		//	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;//bindFlags;
		//	desc.CPUAccessFlags = 0;// cpuAccessFlags;
		//	if (info.IsCubemap()) {
		//		desc.MiscFlags = 0/*miscFlags*/ | D3D11_RESOURCE_MISC_TEXTURECUBE;
		//	} else {
		//		desc.MiscFlags = 0/*miscFlags*/ & ~static_cast<uint32_t>(D3D11_RESOURCE_MISC_TEXTURECUBE);
		//	}

		//	const size_t index = info.ComputeIndex(0, 0, 0);
		//	const DirectX::Image& img = image->GetImages()[index];

		//	std::unique_ptr<D3D11_SUBRESOURCE_DATA[]> initData(new (std::nothrow) D3D11_SUBRESOURCE_DATA[info.arraySize]);
		//	initData[0].pSysMem = img.pixels;
		//	initData[0].SysMemPitch = static_cast<DWORD>(img.rowPitch);
		//	initData[0].SysMemSlicePitch = static_cast<DWORD>(img.rowPitch);
		//	
		//	ID3D11Texture2D* ppResource = nullptr;
		//	hr = Engine::Get().GetD3DDevice()->CreateTexture2D(&desc, initData.get(), &ppResource);
		//	if (FAILED(hr)) {
		//		return false;
		//	}

		//	shaderResourceView_.Create(ppResource);

		//	return true;
		//}


		//GPUResourceLoader::GPUResourceLoader()
		//	: requestPath_(nullptr)
		//	, state_(State::Reqesuted)
		//{
		//	resource_ = new GPUResource();
		//}

		//GPUResourceLoader::~GPUResourceLoader()
		//{
		//	delete resource_;
		//	resource_ = nullptr;
		//}

		//void GPUResourceLoader::Update()
		//{
		//	switch (state_)
		//	{
		//		case State::Invalid:
		//		{
		//			break;
		//		}
		//		case State::Reqesuted:
		//		{
		//			// ���������^�C�~���O�œǂݍ��ݒ��ɂȂ�
		//			state_ = State::Loading;
		//			/** FALL THROUGH */
		//		}
		//		case State::Loading:
		//		{
		//			if (!resource_->Initialize(requestPath_)) {
		//				// ���s
		//				EngineAssertMsg(false, "GPUResourceLoader / �ǂݍ��݂Ɏ��s���܂����B\n");
		//				state_ = State::Invalid;
		//				break;
		//			}
		//			state_ = State::Completed;
		//			/** FALL THROUGH */
		//		}
		//		case State::Completed:
		//		{
		//			// �ǂݍ��݊���
		//			break;
		//		}
		//	}
		//}

		//void GPUResourceLoader::Request(const char* path)
		//{
		//	requestPath_ = path;
		//}




		/*******************************************/

		
		ResourceManager* ResourceManager::instance_ = nullptr;


		ResourceManager::ResourceManager()
		{
			loaders_.clear();
			bankMap_.clear();
		}


		ResourceManager::~ResourceManager()
		{
			ClearLoaders();
			ClearBank();
		}


		void ResourceManager::Update()
		{
			// �ǂݍ��ݏ���
			for (auto* loader : loaders_) {
				loader->Update();
			}
			// 1�t���[���Ŋ�������͂��Ȃ̂Ŗ��t���[��������
			ClearLoaders();
		}


		void ResourceManager::ClearLoaders()
		{
			if (loaders_.size() == 0) {
				return;
			}

			for (auto* loader : loaders_) {
				delete loader;
				loader = nullptr;
			}
			loaders_.clear();
		}


		void ResourceManager::ClearBank()
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
	}
}