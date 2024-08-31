#pragma once
#include "../Resource/Resource.h"


namespace engine
{
	namespace graphics
	{
		/**
		 * TODO:マテリアルとか作るようにしたい
		 */

		class StaticMesh
		{
		public:
			/** 使用するシェーダーの種類 */
			enum class ShaderType
			{
				NormalModel,
			};

		private:
			VertexBuffer vertexBuffer_;
			IndexBuffer indexBuffer_;
			Shader vsShader_;
			Shader psShader_;
			engine::graphics::SamplerState samplerState_;
			graphics::ConstantBuffer constantBuffer_;
			math::Matrix4x4 worldMatrix_;

			engine::res::RefMeshResource meshResource_;
			engine::res::RefGPUResource gpuResource_;


		public:
			StaticMesh();
			~StaticMesh();

			bool Initialize(engine::res::RefMeshResource meshResource, engine::res::RefGPUResource gpuResource, const ShaderType shaderType);
			void Update(const math::Vector3& translation, const math::Quaternion& rotation, const math::Vector3& scale);
			void Render(RenderContext& context, const math::Matrix4x4& view, const math::Matrix4x4& projection);
		};
	}
}