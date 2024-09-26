#pragma once
#include "../Engine/Application.h"
#include "../Engine/Resource/Resource.h"
#include "../Engine/Graphics/Camera.h"

// @todo for test
#include "../Engine/HID/Input.h"

namespace app
{
	class Hoge
	{
	private:

		struct VSConstantBuffer
		{
			engine::math::Matrix4x4 world;
			engine::math::Matrix4x4 view;
			engine::math::Matrix4x4 proj;
		};

	private:
		engine::graphics::VertexBuffer vertexBuffer_;
		engine::graphics::IndexBuffer indexBuffer_;
		engine::graphics::ConstantBuffer constantBuffer_;
		engine::graphics::Shader vsShader_;
		engine::graphics::Shader psShader_;

		engine::Camera camera_;
		engine::math::Vector3 camearPosistion_;

		engine::math::Vector3 position_;

		engine::res::RefMeshResource meshResouce_;
		engine::res::RefGPUResource gpuResource_;
		//engine::res::FbxLoader fbxLoader_;
		//engine::res::GPUResourceLoader gpuResourceLoader_;
		engine::graphics::SamplerState samplerState_;

		uint32_t drawIndex_;

	public:
		Hoge()
		{
			position_.Set(engine::math::Vector3(0,0,0));

			//engine::graphics::VertexData vertices[] = {
			//	{ engine::math::Vector3(-0.5f,  0.5f, 0.0f), engine::math::Vector3(1.0f, 0.0f, 1.0f), engine::math::Vector2(1.0f, 1.0f) },
			//	{ engine::math::Vector3(0.5f, -0.5f, 0.0f), engine::math::Vector3(1.0f, 0.0f, 1.0f), engine::math::Vector2(1.0f, 1.0f) },
			//	{ engine::math::Vector3(-0.5f, -0.5f, 0.0f), engine::math::Vector3(1.0f, 0.0f, 1.0f), engine::math::Vector2(1.0f, 1.0f) },
			//	{ engine::math::Vector3(0.5f,  0.5f, 0.0f), engine::math::Vector3(1.0f, 0.0f, 1.0f), engine::math::Vector2(1.0f, 1.0f) }
			//};
			//vertexBuffer_.Create(4, sizeof(engine::graphics::VertexData), vertices);

			//uint32_t indexs[] = {
			//	0, 1, 2,
			//	0, 3, 1,
			//};
			//indexBuffer_.Create(ArraySize(indexs), indexs);
			//drawIndex_ = ArraySize(indexs);

			camearPosistion_.Set(engine::math::Vector3(0.0f, 0.0f, 1.0f));
			camera_.SetPosition(camearPosistion_);
			camera_.SetTarget(engine::math::Vector3(0.0f, 0.0f, 0.0f));
			camera_.SetNear(0.01f);
			camera_.Update();

			constantBuffer_.Create(nullptr, sizeof(VSConstantBuffer));

			// @todo for test
			//fbxLoader_.Request("Assets/Character/LittleCat.fbx");
			//fbxLoader_.Update();

			
			//meshResouce_ = engine::res::ResourceManager::Get().Load<engine::res::RefMeshResource, engine::res::FbxLoader>("Assets/Character/LittleCat.fbx");
			//gpuResource_ = engine::res::ResourceManager::Get().Load<engine::res::RefGPUResource, engine::res::TextureLoader>("Assets/Character/Cat_Body_Crying.png");
			//meshResouce_ = engine::res::ResourceManager::Get().Load<engine::res::RefMeshResource, engine::res::FbxLoader>("Assets/Character/SmallFish.fbx");
			//gpuResource_ = engine::res::ResourceManager::Get().Load<engine::res::RefGPUResource, engine::res::TextureLoader>("Assets/Character/SmallFish.png");
			meshResouce_ = engine::res::ResourceManager::Get().Load<engine::res::RefMeshResource, engine::res::FbxLoader>("Assets/Character/SmallFish.fbx");
			gpuResource_ = engine::res::ResourceManager::Get().Load<engine::res::RefGPUResource, engine::res::TextureLoader>("Assets/Character/SmallFish.png");

			//engine::res::ResourceManager::Get().Load<engine::res::RefPMDResource, engine::res::PMDLoader>("Assets/Character/Piamon/Model.pmx");

			engine::res::ResourceManager::Get().Update();

			//gpuResourceLoader_.Request("Assets/Character/Cat_Body_Crying.png");
			//gpuResourceLoader_.Update();

			D3D11_SAMPLER_DESC samplerDesc;
			engine::memory::Clear(&samplerDesc, sizeof(D3D11_SAMPLER_DESC));
			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerState_.Create(samplerDesc);

			vertexBuffer_.Create(meshResouce_->GetVerticsSize(), sizeof(engine::graphics::VertexData), meshResouce_->GetVertics()->data());
			drawIndex_ = meshResouce_->GetIndicesSize();
			indexBuffer_.Create(drawIndex_, meshResouce_->GetIndices()->data());

			vsShader_.Load("Assets/Shader/Model.fx", "VSMain", engine::graphics::Shader::ShaderType::VS);
			psShader_.Load("Assets/Shader/Model.fx", "PSMain", engine::graphics::Shader::ShaderType::PS);
		}

		void Update()
		{
			engine::math::Vector3 trans(engine::math::Vector3::Zero);
			//if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
			//	trans.x += 0.001f;
			//}
			//if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
			//	trans.x -= 0.001f;
			//}

			if (GetAsyncKeyState(VK_UP) & 0x8000) {
				trans.z += 0.001f;
			}
			if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
				trans.z -= 0.001f;
			}

			if (engine::hid::InputManager::Get().GetKeyBoard().IsPressed(engine::hid::BUTTON_LEFT)) {
				trans.x -= 0.001f;
			}
			if (engine::hid::InputManager::Get().GetKeyBoard().IsPressed(engine::hid::BUTTON_RIGHT)) {
				trans.x += 0.001f;
			}


			//position_.Add(trans);
			camearPosistion_.Add(trans);
			camera_.SetPosition(camearPosistion_);
			camera_.Update();
		}

		void Render(engine::graphics::RenderContext& context)
		{
			float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			context.ClearRenderTargetView(0, clearColor);

			context.IASetVertexBuffer(vertexBuffer_);
			context.IASetIndexBuffer(indexBuffer_);
			context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			VSConstantBuffer cb;
			engine::math::Matrix4x4 world, trans, rot, scal;
			engine::math::Quaternion q;
			trans.MakeTranslation(position_);
			q.SetEuler(engine::math::Vector3(90.0f, 90.0f, 90.0f));
			//rot.MakeRotationAxis(engine::math::Vector3(), 90.0f);
			rot.MakeRotationFromQuaternion(q);
			scal.MakeScaling(engine::math::Vector3(1.0f));
			world.Mull(scal, rot);
			cb.world.Mull(world, trans);
			cb.view = camera_.GetViewMatrix();
			cb.proj = camera_.GetProjectionMatrix();
			context.UpdateSubresource(constantBuffer_, cb);
			context.VSSetConstantBuffer(0, constantBuffer_);
			context.PSSetConstantBuffer(0, constantBuffer_);

			context.PSSetShaderResource(0, *gpuResource_->GetShaderResourceView());
			context.PsSetSampler(0, samplerState_);

			context.VSSetShader(vsShader_);
			context.PSSetShader(psShader_);
			context.IASetInputLayout(vsShader_.GetInputLayout());

			context.DrawIndexed(drawIndex_);
		}
	};

	class Application : public engine::IApplication
	{
		// @todo for test
	private:
		Hoge* hoge_;

	public:
		Application();
		virtual ~Application();

		bool Initialize() override;
		void Finalize() override;
		void Update(engine::graphics::RenderContext& context) override;

		void Register() override;


	private:
		void Render(engine::graphics::RenderContext& context);
	};
}