#pragma once

namespace engine
{
	namespace graphics
	{
		/**
		 * シェーダー
		 */
		class Shader
		{
		public:
			enum class ShaderType : uint8_t
			{
				VS,		// 頂点シェーダー
				PS,		// ピクセルシェーダー
				CS,		// コンピュートシェーダー
			};


		private:
			ShaderType shaderType_;
			void* shader_;
			ID3D11InputLayout* inputLayout_;
			ID3DBlob* blob_;


		public:
			/** コンストラクタ */
			Shader();
			/** デストラクタ */
			~Shader();

			/** 解放 */
			void Release();
			/** 読み込み */
			bool Load(const char* filePath, const char* entryFuncName, ShaderType shaderType);
			/** シェーダー取得 */
			inline void* GetBody() { return shader_; }
			/** インプットレイアウト取得 */
			inline ID3D11InputLayout* GetInputLayout() const { return inputLayout_; }
			inline void* GetByteCode() const { return blob_->GetBufferPointer(); }
			inline size_t GetByteCodeSize() const { return blob_->GetBufferSize(); }
		};
	}
}