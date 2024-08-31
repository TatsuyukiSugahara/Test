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
		 * FBXリソース読み込み
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
				// 失敗
				EngineAssertMsg(false, "FbxLoader / 読み込みに失敗しました。\n");
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
		//	// マネージャー初期化
		//	fbxsdk::FbxManager* fbxManager = fbxsdk::FbxManager::Create();
		//	// インポーター初期化
		//	FbxImporter* importer = FbxImporter::Create(fbxManager, "");
		//	if (!importer->Initialize(requestPath_, -1, fbxManager->GetIOSettings())) {
		//		return false;
		//	}

		//	// シーン作成
		//	FbxScene* scene = FbxScene::Create(fbxManager, "");
		//	importer->Import(scene);
		//	importer->Destroy();

		//	// 三角ポリゴンにする
		//	FbxGeometryConverter geometryConverter(fbxManager);
		//	if (!geometryConverter.Triangulate(scene, true)) {
		//		return false;
		//	}

		//	// メッシュ取得
		//	FbxMesh* mesh = scene->GetSrcObject<FbxMesh>();
		//	if (!mesh) {
		//		return false;
		//	}

		//	MeshData* meshData = static_cast<MeshData*>(resource_->data_);

		//	// UVセット名の取得
		//	FbxStringList uvSetNameList;
		//	mesh->GetUVSetNames(uvSetNameList);
		//	const char* uvSetName = uvSetNameList.GetStringAt(0);

		//	// 頂点座標リスト
		//	std::vector<VertexWork> vertexWorkList;
		//	for (int32_t i = 0; i < mesh->GetControlPointsCount(); ++i) {
		//		fbxsdk::FbxVector4 point = mesh->GetControlPointAt(i);
		//		VertexWork vertexWork;
		//		vertexWork.position.Set(static_cast<float>(point[0]), static_cast<float>(point[1]), static_cast<float>(point[2]));
		//		vertexWorkList.push_back(vertexWork);
		//	}
		//	// 頂点毎の情報取得
		//	std::vector<math::Vector2Int32> workIndexPairList;
		//	for (int32_t polygonIndex = 0; polygonIndex < mesh->GetPolygonCount(); ++polygonIndex) {
		//		for (int32_t vertexIndex = 0; vertexIndex < mesh->GetPolygonSize(polygonIndex); ++vertexIndex) {
		//			// インデックス
		//			int32_t index = mesh->GetPolygonVertex(polygonIndex, vertexIndex);
		//			// 頂点座標
		//			VertexWork& vertexWork = vertexWorkList[index];
		//			// 法線座標
		//			fbxsdk::FbxVector4 normal;
		//			mesh->GetPolygonVertexNormal(polygonIndex, vertexIndex, normal);
		//			// UV座標
		//			fbxsdk::FbxVector2 uv;
		//			bool isUnMapped;
		//			mesh->GetPolygonVertexUV(polygonIndex, vertexIndex, uvSetName, uv, isUnMapped);
		//			// 法線とUV設定されていなければ設定
		//			if (!vertexWork.isSetting) {
		//				vertexWork.normal.Set(static_cast<float>(normal[0]), static_cast<float>(normal[1]), static_cast<float>(normal[2]));
		//				vertexWork.uv.Set(static_cast<float>(uv[0]), static_cast<float>(uv[1]));
		//				vertexWork.isSetting = true;
		//			}
		//			else if (!IsSetNormalUV(vertexWork, normal, uv)) {
		//				// 同一頂点インデックスだとしても法線かUVが異なる場合は新たなインデックスとする
		//				index = CreateVertexIndex(vertexWork, normal, uv, vertexWorkList, index, workIndexPairList);
		//			}
		//			meshData->indices.push_back(index);
		//		}
		//	}
		//	// 頂点情報生成
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
		//	// すでに生成されている場合は、そのインデックスを返す
		//	for (int32_t i = 0; i < indexPairList.size(); ++i) {
		//		const int32_t newIndex = indexPairList[i].y;
		//		if (oldIndex != indexPairList[i].x) continue;
		//		if (!IsSetNormalUV(vertexWorkList[newIndex], normal, uv)) continue;
		//		return newIndex;
		//	}
		//	// 新たなインデックスを作成して返す
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
				// 失敗
				EngineAssertMsg(false, "FbxLoader / 読み込みに失敗しました。\n");
				resource_->state_ = ResourceBase::ResourceState::Invalid;
				return;
			}
			resource_->state_ = ResourceBase::ResourceState::Completed;
		}


		bool PMDLoader::Loading()
		{
			// ファイルを開く
			FILE* fp = nullptr;
			fopen_s(&fp, requestPath_, "rb");

			if (fp == nullptr) {
				return false;
			}

			// ファイルサイズ取得
			fpos_t fileSize;
			fseek(fp, 0, SEEK_END);
			fgetpos(fp, &fileSize);
			fseek(fp, 0, SEEK_SET);
			// ファイルデータ取得
			uint8_t* bin = new uint8_t[fileSize];
			fileSize = fread_s(bin, sizeof(uint8_t)*fileSize, sizeof(uint8_t), static_cast<size_t>(fileSize), fp);
			fclose(fp);

			// ヘッダー
			PMDData::Header* header = reinterpret_cast<PMDData::Header*>(bin);
			bin += sizeof(PMDData::Header);

			//PMDData::Information* information = reinterpret_cast<PMDData::Information*>(bin);
			//bin += 183;//sizeof(PMDData::Information);

			// 頂点情報
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
		 * FBXリソース読み込み
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
				// 失敗
				EngineAssertMsg(false, "TextureLoader / 読み込みに失敗しました。\n");
				resource_->state_ = ResourceBase::ResourceState::Invalid;
				return;
			}
			resource_->state_ = ResourceBase::ResourceState::Completed;
		}


		bool TextureLoader::Loading()
		{
			// マルチバイト文字列からワイド文字列へ変換
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
			// ミップマップ
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
		//	// マルチバイト文字列からワイド文字列へ変換
		//	wchar_t filePath[256];
		//	size_t ret;
		//	mbstowcs_s(&ret, filePath, path, ArraySize(filePath));
		//	
		//	DirectX::TexMetadata info;
		//	std::unique_ptr<DirectX::ScratchImage> image = std::make_unique<DirectX::ScratchImage>();

		//	// 画像読み込み
		//	HRESULT hr = DirectX::LoadFromWICFile(filePath, DirectX::WIC_FLAGS_NONE, &info, *image);
		//	if (FAILED(hr)) {
		//		info = {};
		//		return false;
		//	}
		//	// ミップマップ
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
		//			// 処理きたタイミングで読み込み中になる
		//			state_ = State::Loading;
		//			/** FALL THROUGH */
		//		}
		//		case State::Loading:
		//		{
		//			if (!resource_->Initialize(requestPath_)) {
		//				// 失敗
		//				EngineAssertMsg(false, "GPUResourceLoader / 読み込みに失敗しました。\n");
		//				state_ = State::Invalid;
		//				break;
		//			}
		//			state_ = State::Completed;
		//			/** FALL THROUGH */
		//		}
		//		case State::Completed:
		//		{
		//			// 読み込み完了
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
			// 読み込み処理
			for (auto* loader : loaders_) {
				loader->Update();
			}
			// 1フレームで完了するはずなので毎フレーム初期化
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