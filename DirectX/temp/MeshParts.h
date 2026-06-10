/// <summary>
/// ���b�V���p�[�c�N���X�B
/// </summary>

#pragma once

#include "tkFile/TkmFile.h"
#include "StructuredBuffer.h"
#include "Material.h"

namespace nsK2EngineLow {

	class RenderContext;
	class Skeleton;
	class Material;
	class IShaderResource;
	struct MaterialReInitData;


	const int MAX_MODEL_EXPAND_SRV = 32;	//�g��SRV�̍ő吔�B

	/// <summary>
	/// ���b�V��
	/// </summary>
	struct SMesh {
		VertexBuffer m_vertexBuffer;						//���_�o�b�t�@�B
		std::vector< IndexBuffer* >		m_indexBufferArray;	//�C���f�b�N�X�o�b�t�@�B
		std::vector< Material* >		m_materials;			//�}�e���A���B
		std::vector<int>				skinFlags;				//�X�L���������Ă��邩�ǂ����̃t���O�B
	};

	/// <summary>
	/// ���b�V���p�[�c�B
	/// </summary>
	class MeshParts : public Noncopyable {
	public:
		/// <summary>
		/// �f�X�g���N�^�B
		/// </summary>
		~MeshParts();
		/// <summary>
		/// tkm�t�@�C�����珉����
		/// </summary>
		/// <param name="tkmFile">tkm�t�@�C���B</param>
		/// /// <param name="fxFilePath">fx�t�@�C���̃t�@�C���p�X</param>
		/// <param name="vsEntryPointFunc">���_�V�F�[�_�[�̃G���g���[�|�C���g�̊֐���</param>
		/// <param name="vsSkinEntryPointFunc">�X�L������}�e���A���p�̒��_�V�F�[�_�[�̃G���g���[�|�C���g�̊֐���</param>
		/// <param name="psEntryPointFunc">�s�N�Z���V�F�[�_�[�̃G���g���[�|�C���g�̊֐���</param>
		/// <param name="colorBufferFormat">���̃��f���������_�����O����J���[�o�b�t�@�̃t�H�[�}�b�g</param>
		void InitFromTkmFile(
			const TkmFile& tkmFile,
			const char* fxFilePath,
			const char* vsEntryPointFunc,
			const char* vsSkinEntryPointFunc,
			const char* psEntryPointFunc,
			void* expandData,
			int expandDataSize,
			const std::array<IShaderResource*, MAX_MODEL_EXPAND_SRV>& expandShaderResourceView,
			const std::array<DXGI_FORMAT, MAX_RENDERING_TARGET>& colorBufferFormat,
			AlphaBlendMode alphaBlendMode,
			bool isDepthWrite,
			bool isDepthTest,
			D3D12_CULL_MODE cullMode
		);
		/// <summary>
		/// �`��B
		/// </summary>
		/// <param name="rc">�����_�����O�R���e�L�X�g</param>
		/// <param name="mWorld">���[���h�s��</param>
		/// <param name="mView">�r���[�s��</param>
		/// <param name="mProj">�v���W�F�N�V�����s��</param>
		/// <param name="numInstance">�C���X�^���X�̐�</param>
		void Draw(
			RenderContext& rc,
			const Matrix& mWorld,
			const Matrix& mView,
			const Matrix& mProj,
			int numInstance);
		/// <summary>
		/// �X�P���g�����֘A�t����B
		/// </summary>
		/// <param name="skeleton">�X�P���g��</param>
		void BindSkeleton(Skeleton& skeleton);
		/// <summary>
		/// ���b�V���ɑ΂��Ė₢���킹���s���B
		/// </summary>
		/// <param name="queryFunc">�N�G���֐�</param>
		void QueryMeshs(std::function<void(const SMesh& mesh)> queryFunc)
		{
			for (const auto& mesh : m_meshs) {
				queryFunc(*mesh);
			}
		}
		void QueryMeshAndDescriptorHeap(std::function<void(const SMesh& mesh, const DescriptorHeap& ds)> queryFunc)
		{
			for (int i = 0; i < m_meshs.size(); i++) {
				queryFunc(*m_meshs[i], m_descriptorHeap);
			}
		}
		/// <summary>
		/// ���f���̏�Z�J���[���ݒ肷��B
		/// </summary>
		/// <param name="mulColor">��Z�J���[</param>
		void SetMulColor(const Vector4& mulColor)
		{
			m_mulColor = mulColor;
		}
		/// <summary>
		/// �f�B�X�N���v�^�q�[�v���쐬�B
		/// </summary>
		void CreateDescriptorHeaps();
		/// <summary>
		/// �}�e���A�����ď������B
		/// </summary>
		void ReInitMaterials(const MaterialReInitData& reInitData);
	private:
		/// <summary>
		/// tkm���b�V�����烁�b�V�����쐬�B
		/// </summary>
		/// <param name="mesh">���b�V��</param>
		/// <param name="meshNo">���b�V���ԍ�</param>
		/// <param name="fxFilePath">fx�t�@�C���̃t�@�C���p�X</param>
		/// <param name="vsEntryPointFunc">���_�V�F�[�_�[�̃G���g���[�|�C���g�̊֐���</param>
		/// <param name="vsSkinEntryPointFunc">�X�L������}�e���A���p�̒��_�V�F�[�_�[�̃G���g���[�|�C���g�̊֐���</param>
		/// <param name="psEntryPointFunc">�s�N�Z���V�F�[�_�[�̃G���g���[�|�C���g�̊֐���</param>
		/// <param name="colorBufferFormat">���̃��f���������_�����O����J���[�o�b�t�@�̃t�H�[�}�b�g</param>
		void CreateMeshFromTkmMesh(
			const TkmFile::SMesh& mesh,
			int meshNo,
			int& materialNum,
			const char* fxFilePath,
			const char* vsEntryPointFunc,
			const char* vsSkinEntryPointFunc,
			const char* psEntryPointFunc,
			const std::array<DXGI_FORMAT, MAX_RENDERING_TARGET>& colorBufferFormat,
			AlphaBlendMode alphaBlendMode,
			bool isDepthWrite,
			bool isDepthTest,
			D3D12_CULL_MODE cullMode
		);


	private:
		//�g��SRV���ݒ肳��郌�W�X�^�̊J�n�ԍ��B
		const int EXPAND_SRV_REG__START_NO = 10;
		//�P�̃}�e���A���Ŏg�p�����SRV�̐��B
		const int NUM_SRV_ONE_MATERIAL = EXPAND_SRV_REG__START_NO + MAX_MODEL_EXPAND_SRV;
		//�P�̃}�e���A���Ŏg�p�����CBV�̐��B
		const int NUM_CBV_ONE_MATERIAL = 2;
		/// <summary>
		/// �萔�o�b�t�@�B
		/// </summary>
		/// <remarks>
		/// ���̍\���̂�ύX������ASimpleModel.fx��CB���ύX����悤�ɁB
		/// </remarks>
		struct SConstantBuffer {
			Matrix mWorld;		//���[���h�s��B
			Matrix mView;		//�r���[�s��B
			Matrix mProj;		//�v���W�F�N�V�����s��B
			Vector4 mulColor;	//��Z�J���[�B
		};
		ConstantBuffer m_commonConstantBuffer;					//���b�V�����ʂ̒萔�o�b�t�@�B
		ConstantBuffer m_expandConstantBuffer;					//���[�U�[�g���p�̒萔�o�b�t�@
		Vector4 m_mulColor = Vector4::One;						//���f���̏�Z�J���[(b1�ɃZ�b�g)�B
		std::array<IShaderResource*, MAX_MODEL_EXPAND_SRV> m_expandShaderResourceView = { nullptr };	//���[�U�[�g���V�F�[�_�[���\�[�X�r���[�B
		StructuredBuffer m_boneMatricesStructureBuffer;	//�{�[���s��̍\�����o�b�t�@�B
		std::vector< SMesh* > m_meshs;						//���b�V���B
		//std::vector< DescriptorHeap > m_descriptorHeap;	//�f�B�X�N���v�^�q�[�v�B
		DescriptorHeap m_descriptorHeap;					//�f�B�X�N���v�^�q�[�v�B
		Skeleton* m_skeleton = nullptr;						//�X�P���g���B
		void* m_expandData = nullptr;						//���[�U�[�g���f�[�^�B
	};
}