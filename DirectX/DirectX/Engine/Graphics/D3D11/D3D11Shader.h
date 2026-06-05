#pragma once
#include "../IShader.h"

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

			void*  GetNativeHandle() const override { return shader_; }
			void*  GetInputLayout()  const override { return static_cast<void*>(inputLayout_); }
			void*  GetByteCode()     const override { return blob_->GetBufferPointer(); }
			size_t GetByteCodeSize() const override { return blob_->GetBufferSize(); }
		};
	}
}
