/**
 * ���_�V�F�[�_�[�̓���
 */
struct VSInput
{
	float3 position : POSITION;
	float4 color    : COLOR0;
};
/**
 * �s�N�Z���V�F�[�_�[�̓���
 */
struct PSInput
{
	float4 position 	: POSITION;
	float4 color		: COLOR0;
};