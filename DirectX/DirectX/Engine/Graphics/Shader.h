#pragma once
#include "IShader.h"


namespace engine
{
	namespace graphics
	{
		/**
		 * シェーダー (D3D11 実装)
		 */
		class Shader : public IShader
		{
		public:
			/** 既存コードの後方互換 alias */
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

			/** シェーダーオブジェクト (vs/ps/cs) を void* で返す */
			void*  GetNativeHandle() const override { return shader_; }
			/** 頂点シェーダーの InputLayout を void* で返す (VS 以外は nullptr) */
			void*  GetInputLayout()  const override { return static_cast<void*>(inputLayout_); }
			void*  GetByteCode()     const override { return blob_->GetBufferPointer(); }
			size_t GetByteCodeSize() const override { return blob_->GetBufferSize(); }
		};
	}
}
