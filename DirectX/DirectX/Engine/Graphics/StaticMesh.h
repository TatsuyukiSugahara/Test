#pragma once
#include <memory>
#include "../Resource/Resource.h"
#include "../Math/Matrix.h"
#include "IBuffer.h"
#include "IShader.h"
#include "ISamplerState.h"
#include "RenderContext.h"
#include "../Rendering/RenderFrame.h"


namespace engine
{
	namespace graphics
	{
		class StaticMesh
		{
		public:
			enum class ShaderType
			{
				NormalModel,
				SimpleBox,
			};

		private:
			std::shared_ptr<IVertexBuffer>   vertexBuffer_;
			std::shared_ptr<IIndexBuffer>    indexBuffer_;
			uint32_t                         indicesSize_;
			std::shared_ptr<ISamplerState>   samplerState_;
			math::Matrix4x4                  worldMatrix_;
			math::Matrix4x4                  localMatrix_;
			bool                             isInitialized_;

			engine::res::RefMeshResource meshResource_;
			engine::res::RefGPUResource  gpuResource_;
			engine::res::RefShaderResource vsShaderResource_;
			engine::res::RefShaderResource psShaderResource_;


		public:
			StaticMesh();
			~StaticMesh();

			void Initialize(engine::res::RefMeshResource meshResource, engine::res::RefGPUResource gpuResource, const ShaderType shaderType);
			void Initialize(const void* vertexBuffer, const uint32_t vertexNum, const void* indexBuffer, const uint32_t indexNum, const ShaderType shaderType);
			void SetLocalMatrix(const math::Matrix4x4& localMatrix);
			void Update(const math::Vector3& translation, const math::Quaternion& rotation, const math::Vector3& scale);

			/**
			 * 描画に必要な情報を RenderItem へコピーする。
			 * ロードが完了していない場合は false を返す。
			 */
			bool FillRenderItem(rendering::RenderItem& item) const;


		private:
			void Initialize(const ShaderType shaderType);
		};
	}
}
