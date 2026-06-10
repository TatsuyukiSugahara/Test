#pragma once

#include "tkFile/TkmFile.h"

namespace nsK2EngineLow {
	/// <summary>
	/// �}�e���A���B
	/// </summary>
	class Material : public Noncopyable {
	public:
		/// <summary>
		/// tkm�t�@�C���̃}�e���A����񂩂珉��������B
		/// </summary>
		/// <param name="tkmMat">tkm�}�e���A��</param>
		void InitFromTkmMaterila(
			const TkmFile::SMaterial& tkmMat,
			const char* fxFilePath,
			const char* vsEntryPointFunc,
			const char* vsSkinEntriyPointFunc,
			const char* psEntryPointFunc,
			const std::array<DXGI_FORMAT, MAX_RENDERING_TARGET>& colorBufferFormat,
			int numSrv,
			int numCbv,
			UINT offsetInDescriptorsFromTableStartCB,
			UINT offsetInDescriptorsFromTableStartSRV,
			AlphaBlendMode alphaBlendMode,
			bool isDepthWrite,
			bool isDepthTest,
			D3D12_CULL_MODE cullMode
		);
		/// <summary>
		/// �����_�����O���J�n����Ƃ��ɌĂяo���֐��B
		/// </summary>
		/// <param name="rc">�����_�����O�R���e�L�X�g</param>
		/// <param name="hasSkin">�X�L�������邩�ǂ����̃t���O</param>
		void BeginRender(RenderContext& rc, int hasSkin);

		/// <summary>
		/// �A���x�h�}�b�v���擾�B
		/// </summary>
		/// <returns></returns>
		Texture& GetAlbedoMap()
		{
			return *m_albedoMap;
		}
		/// <summary>
		/// �@���}�b�v���擾�B
		/// </summary>
		/// <returns></returns>
		Texture& GetNormalMap()
		{
			return *m_normalMap;
		}
		/// <summary>
		/// �X�y�L�����}�b�v���擾�B
		/// </summary>
		/// <returns></returns>
		Texture& GetSpecularMap()
		{
			return *m_specularMap;
		}
		/// <summary>
		/// ���˃}�b�v���擾�B
		/// </summary>
		/// <returns></returns>
		Texture& GetReflectionMap()
		{
			return *m_reflectionMap;
		}
		/// <summary>
		/// ���܃}�b�v���擾�B
		/// </summary>
		/// <returns></returns>
		Texture& GetRefractionMap()
		{
			return *m_refractionMap;
		}
		/// <summary>
		/// �萔�o�b�t�@���擾�B
		/// </summary>
		/// <returns></returns>
		ConstantBuffer& GetConstantBuffer()
		{
			return m_constantBuffer;
		}
		
	private:
		/// <summary>
		/// �p�C�v���C���X�e�[�g�̏������B
		/// </summary>
		void InitPipelineState(
			const std::array<DXGI_FORMAT, MAX_RENDERING_TARGET>& colorBufferFormat,
			AlphaBlendMode alphaBlendMode,
			bool isDepthWrite,
			bool isDepthTest,
			D3D12_CULL_MODE cullMode
		);
		/// <summary>
		/// �V�F�[�_�[�̏������B
		/// </summary>
		/// <param name="fxFilePath">fx�t�@�C���̃t�@�C���p�X</param>
		/// <param name="vsEntryPointFunc">���_�V�F�[�_�[�̃G���g���[�|�C���g�̊֐���</param>
		/// <param name="vsEntryPointFunc">�X�L������}�e���A���p�̒��_�V�F�[�_�[�̃G���g���[�|�C���g�̊֐���</param>
		/// <param name="psEntryPointFunc">�s�N�Z���V�F�[�_�[�̃G���g���[�|�C���g�̊֐���</param>
		void InitShaders(
			const char* fxFilePath,
			const char* vsEntryPointFunc,
			const char* vsSkinEntriyPointFunc,
			const char* psEntryPointFunc);
		/// <summary>
		/// �e�N�X�`�����������B
		/// </summary>
		/// <param name="tkmMat"></param>
		void InitTexture(const TkmFile::SMaterial& tkmMat);
	public:
		/// <summary>
		/// �}�e���A���̏�Z�J���[���ݒ肷��B
		/// </summary>
		/// <param name="mulColor">�Z�J���[(RGBA, 1.0f=�ύX�Ȃ�)</param>
		void SetMulColor(const Vector4& mulColor)
		{
			m_materialParam.mulColor = mulColor;
			m_constantBuffer.CopyToVRAM(m_materialParam);
		}

	private:
		/// <summary>
		/// �}�e���A���p�����[�^�B
		/// </summary>
		struct SMaterialParam {
			int hasNormalMap;	//�@���}�b�v��ێ����Ă��邩�ǂ����̃t���O�B
			int hasSpecMap;		//�X�y�L�����}�b�v��ێ����Ă��邩�ǂ����̃t���O�B
			int pad0;			// �A���C�������g�p�B
			int pad1;			// �A���C�������g�p�B
			Vector4 mulColor;	// ��Z�J���[(RGBA)�B
		};
		SMaterialParam m_materialParam;					//�}�e���A���p�����[�^�iCopyToVRAM�p�ɕۑ�j�B
		Texture* m_albedoMap;						//�A���x�h�}�b�v�B
		Texture* m_normalMap;						//�@���}�b�v�B
		Texture* m_specularMap;						//�X�y�L�����}�b�v�B
		Texture* m_reflectionMap;					//���t���N�V�����}�b�v�B
		Texture* m_refractionMap;					//���܃}�b�v�B

		ConstantBuffer m_constantBuffer;				//�萔�o�b�t�@�B
		RootSignature m_rootSignature;					//���[�g�V�O�l�`���B
		PipelineState m_nonSkinModelPipelineState;		//�X�L���Ȃ����f���p�̃p�C�v���C���X�e�[�g�B
		PipelineState m_skinModelPipelineState;			//�X�L�����胂�f���p�̃p�C�v���C���X�e�[�g�B
		PipelineState m_transSkinModelPipelineState;	//�X�L�����胂�f���p�̃p�C�v���C���X�e�[�g(�������}�e���A��)�B
		PipelineState m_transNonSkinModelPipelineState;	//�X�L���Ȃ����f���p�̃p�C�v���C���X�e�[�g(�������}�e���A��)�B
		Shader* m_vsNonSkinModel = nullptr;				//�X�L���Ȃ����f���p�̒��_�V�F�[�_�[�B
		Shader* m_vsSkinModel = nullptr;				//�X�L�����胂�f���p�̒��_�V�F�[�_�[�B
		Shader* m_psModel = nullptr;					//���f���p�̃s�N�Z���V�F�[�_�[�B
	};
}

