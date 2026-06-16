#pragma once
#include "Graphics/IShader.h"

namespace engine
{
	namespace graphics
	{
		class Shader : public IShader
		{
		public:
			using ShaderType = IShader::ShaderType;

		private:
			ShaderType         shaderType_;
			void*              shader_;
			ID3D11InputLayout* inputLayout_;
			ID3DBlob*          blob_;

		public:
			Shader();
			~Shader();

			void   Release() override;
			bool   Load(const char* filePath, const char* entryFuncName, ShaderType shaderType) override;

			void*  GetByteCode()     const override { return blob_->GetBufferPointer(); }
			size_t GetByteCodeSize() const override { return blob_->GetBufferSize(); }

			/** D3D11 固有: シェーダーオブジェクトを指定型で取得 */
			template<typename T>
			T* GetShaderAs() const { return static_cast<T*>(shader_); }

			/** D3D11 固有: 頂点シェーダーの InputLayout を取得 */
			ID3D11InputLayout* GetInputLayoutD3D11() const { return inputLayout_; }
		};
	}
}
