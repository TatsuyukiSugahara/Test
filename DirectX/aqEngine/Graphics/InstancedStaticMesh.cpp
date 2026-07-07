#include "aq.h"
#include "InstancedStaticMesh.h"


namespace aq
{
	namespace graphics
	{
		namespace
		{
			static constexpr uint32_t INITIAL_CAPACITY = 1024;   // 動的VBの初期容量(インスタンス数)
			static constexpr int      RETIRE_FRAMES     = 8;      // 旧VBを保持するフレーム数(in-flight ぶん)

			// 登録済みメッシュ(weak_ptr。寿命はコンポーネント側 or 名前レジストリの shared_ptr が持つ)。
			std::vector<std::weak_ptr<InstancedStaticMesh>> g_registry;

			// 名前レジストリ(shared_ptr 保持=アプリ寿命で生存)。キーは名前の 32bit ハッシュ。
			std::unordered_map<uint32_t, std::shared_ptr<InstancedStaticMesh>> g_named;

			const char* InstancedShaderPath(StaticMesh::ShaderType type)
			{
				return (type == StaticMesh::ShaderType::InstancedTextured)
					? "Assets/Shader/InstancedModelTex.fx"
					: "Assets/Shader/InstancedSimple.fx";
			}
		}


		void InstancedStaticMesh::Initialize(const void* vertexBuffer, uint32_t vertexNum, uint32_t vertexStride,
		                                     const void* indexBuffer,  uint32_t indexNum,
		                                     StaticMesh::ShaderType shaderType)
		{
			vertexBuffer_ = GraphicsDevice::Get().CreateVertexBuffer(vertexNum, vertexStride, vertexBuffer);
			indexBuffer_  = GraphicsDevice::Get().CreateIndexBuffer(indexNum, indexBuffer);
			indexCount_   = indexNum;

			const char* path = InstancedShaderPath(shaderType);
			vs_ = aq::res::ResourceManager::Get().LoadShader(path, "VSMain", IShader::ShaderType::VS);
			ps_ = aq::res::ResourceManager::Get().LoadShader(path, "PSMain", IShader::ShaderType::PS);
		}


		void InstancedStaticMesh::Initialize(aq::res::RefMeshResource meshResource, aq::res::RefGPUResource albedo,
		                                     StaticMesh::ShaderType shaderType)
		{
			// メッシュは非同期ロード。VB/IB は完了後 FlushInstances(BuildGeometryIfReady)で構築する。
			pendingMesh_    = meshResource;
			albedoResource_ = albedo;

			const char* path = InstancedShaderPath(shaderType);
			vs_ = aq::res::ResourceManager::Get().LoadShader(path, "VSMain", IShader::ShaderType::VS);
			ps_ = aq::res::ResourceManager::Get().LoadShader(path, "PSMain", IShader::ShaderType::PS);

			if (albedo) {
				SamplerDesc desc;
				desc.filter   = FilterMode::MinMagMipLinear;
				desc.addressU = AddressMode::Wrap;
				desc.addressV = AddressMode::Wrap;
				desc.addressW = AddressMode::Wrap;
				sampler_ = GraphicsDevice::Get().CreateSamplerState(desc);
			}
		}


		void InstancedStaticMesh::AddInstance(const math::Matrix4x4& world)
		{
			// float4x4(iWorld0..3) が CB 経路と同姿勢になるよう転置して格納する。
			InstanceData data;
			data.world = world;
			data.world.Transpose();
			pendingInstances_.push_back(data);
		}


		void InstancedStaticMesh::FlushInstances()
		{
			// 遅延ロード(FBX 等)のジオメトリが完了したら VB/IB を構築する(未完なら描画されない)。
			if (pendingMesh_ && !vertexBuffer_ && pendingMesh_->IsCompleted())
			{
				if (pendingMesh_->GetVerticsSize() > 0 && pendingMesh_->GetIndicesSize() > 0) {
					vertexBuffer_ = GraphicsDevice::Get().CreateVertexBuffer(
						pendingMesh_->GetVerticsSize(), sizeof(VertexData), pendingMesh_->GetVertics()->data());
					indexBuffer_  = GraphicsDevice::Get().CreateIndexBuffer(
						pendingMesh_->GetIndicesSize(), pendingMesh_->GetIndices()->data());
					indexCount_   = pendingMesh_->GetIndicesSize();

					// アルベド未指定なら FBX 等のマテリアルからテクスチャを自動取得する。
					if (!albedoResource_ && pendingMesh_->HasTexturePath()) {
						albedoResource_ = aq::res::ResourceManager::Get().Load<aq::res::GPUResource>(
							pendingMesh_->GetTexturePath().c_str());
					}
					// テクスチャがあるのにサンプラー未作成なら作る。
					if (albedoResource_ && !sampler_) {
						SamplerDesc desc;
						desc.filter   = FilterMode::MinMagMipLinear;
						desc.addressU = AddressMode::Wrap;
						desc.addressV = AddressMode::Wrap;
						desc.addressW = AddressMode::Wrap;
						sampler_ = GraphicsDevice::Get().CreateSamplerState(desc);
					}
				}
			}

			// 遅延破棄待ちの旧VBを計時(0 で解放)。
			for (size_t i = 0; i < retiredBuffers_.size(); )
			{
				if (--retiredBuffers_[i].framesLeft <= 0) {
					retiredBuffers_[i] = retiredBuffers_.back();
					retiredBuffers_.pop_back();
				} else {
					++i;
				}
			}

			// 毎フレーム必ず count を確定する(スキップすると過去フレームの行列で描かれるため)。
			const uint32_t count = static_cast<uint32_t>(pendingInstances_.size());
			instanceCount_ = count;
			if (count == 0) { return; }

			// 容量不足なら2倍成長・縮小なし。旧VBは in-flight 参照中なので遅延破棄へ。
			if (count > instanceCapacity_)
			{
				uint32_t newCapacity = (instanceCapacity_ == 0) ? INITIAL_CAPACITY : instanceCapacity_;
				while (newCapacity < count) { newCapacity *= 2; }

				if (instanceBuffer_) {
					retiredBuffers_.push_back({ instanceBuffer_, RETIRE_FRAMES });
				}
				instanceBuffer_   = GraphicsDevice::Get().CreateDynamicVertexBuffer(
					newCapacity, sizeof(InstanceData), nullptr);
				instanceCapacity_ = newCapacity;
			}

			if (instanceBuffer_) {
				instanceBuffer_->Update(pendingInstances_.data(), count * sizeof(InstanceData));
			}
			pendingInstances_.clear();   // capacity は保持
		}


		bool InstancedStaticMesh::FillInstancedRenderItem(rendering::InstancedRenderItem& out) const
		{
			if (instanceCount_ == 0)                              { return false; }
			if (!vertexBuffer_ || !indexBuffer_ || !instanceBuffer_) { return false; }
			if (!vs_ || !ps_ || !vs_->IsCompleted() || !ps_->IsCompleted()) { return false; }

			IShader* vs = vs_->GetShader();
			IShader* ps = ps_->GetShader();
			if (!vs || !ps) { return false; }

			out.vertexBuffer   = vertexBuffer_;
			out.instanceBuffer = instanceBuffer_;
			out.indexBuffer    = indexBuffer_;
			out.vs = std::shared_ptr<IShader>(vs_, vs);   // リソースを alias して寿命を繋ぐ
			out.ps = std::shared_ptr<IShader>(ps_, ps);

			// アルベドテクスチャ(任意)。完了後に SRV を alias して渡す。
			if (albedoResource_ && albedoResource_->IsCompleted()) {
				IShaderResourceView* srv = albedoResource_->GetShaderResourceView();
				if (srv) { out.albedo = std::shared_ptr<IShaderResourceView>(albedoResource_, srv); }
			}
			out.sampler = sampler_;

			out.indexCount     = indexCount_;
			out.instanceCount  = instanceCount_;
			return true;
		}


		std::shared_ptr<InstancedStaticMesh> InstancedStaticMesh::Create()
		{
			auto mesh = std::make_shared<InstancedStaticMesh>();
			g_registry.push_back(mesh);
			return mesh;
		}


		void InstancedStaticMesh::FlushAllRegistered()
		{
			for (size_t i = 0; i < g_registry.size(); )
			{
				if (auto mesh = g_registry[i].lock()) {
					mesh->FlushInstances();
					++i;
				} else {
					g_registry[i] = g_registry.back();
					g_registry.pop_back();
				}
			}
		}


		void InstancedStaticMesh::CollectRenderItems(std::vector<rendering::InstancedRenderItem>& out)
		{
			for (auto& weak : g_registry)
			{
				if (auto mesh = weak.lock()) {
					rendering::InstancedRenderItem item;
					if (mesh->FillInstancedRenderItem(item)) {
						out.push_back(std::move(item));
					}
				}
			}
		}


		void InstancedStaticMesh::RegisterNamed(const char* name, std::shared_ptr<InstancedStaticMesh> mesh)
		{
			if (!name || !mesh) { return; }
			g_named[aqHash32(name)] = std::move(mesh);
		}


		InstancedStaticMesh* InstancedStaticMesh::GetByName(const char* name)
		{
			if (!name) { return nullptr; }
			auto it = g_named.find(aqHash32(name));
			return (it != g_named.end()) ? it->second.get() : nullptr;
		}


		InstancedStaticMesh* InstancedStaticMesh::RegisterFromModel(const char* name, const char* modelPath,
		                                                            const char* texturePath, StaticMesh::ShaderType shaderType)
		{
			if (!name || !modelPath) { return nullptr; }
			if (auto* existing = GetByName(name)) { return existing; }   // 重複登録しない

			auto mesh = Create();
			auto meshResource = aq::res::ResourceManager::Get().Load<aq::res::MeshResource>(modelPath);
			aq::res::RefGPUResource albedo;
			if (texturePath) {
				albedo = aq::res::ResourceManager::Get().Load<aq::res::GPUResource>(texturePath);
			}
			mesh->Initialize(meshResource, albedo, shaderType);
			RegisterNamed(name, mesh);
			return mesh.get();
		}


		InstancedStaticMesh* InstancedStaticMesh::RegisterFromData(const char* name,
		                                                           const void* vertexBuffer, uint32_t vertexNum, uint32_t vertexStride,
		                                                           const void* indexBuffer,  uint32_t indexNum,
		                                                           StaticMesh::ShaderType shaderType)
		{
			if (!name) { return nullptr; }
			if (auto* existing = GetByName(name)) { return existing; }

			auto mesh = Create();
			mesh->Initialize(vertexBuffer, vertexNum, vertexStride, indexBuffer, indexNum, shaderType);
			RegisterNamed(name, mesh);
			return mesh.get();
		}
	}
}
