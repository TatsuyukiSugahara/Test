#pragma once

namespace engine
{
	namespace graphics
	{
		/**
		 * �V�F�[�_�[
		 */
		class Shader
		{
		public:
			enum class ShaderType : uint8_t
			{
				VS,		// ���_�V�F�[�_�[
				PS,		// �s�N�Z���V�F�[�_�[
				CS,		// �R���s���[�g�V�F�[�_�[
			};


		private:
			ShaderType shaderType_;
			void* shader_;
			ID3D11InputLayout* inputLayout_;
			ID3DBlob* blob_;


		public:
			/** �R���X�g���N�^ */
			Shader();
			/** �f�X�g���N�^ */
			~Shader();

			/** ��� */
			void Release();
			/** �ǂݍ��� */
			bool Load(const char* filePath, const char* entryFuncName, ShaderType shaderType);
			/** �V�F�[�_�[�擾 */
			inline void* GetBody() { return shader_; }
			/** �C���v�b�g���C�A�E�g�擾 */
			inline ID3D11InputLayout* GetInputLayout() const { return inputLayout_; }
			inline void* GetByteCode() const { return blob_->GetBufferPointer(); }
			inline size_t GetByteCodeSize() const { return blob_->GetBufferSize(); }
		};
	}
}