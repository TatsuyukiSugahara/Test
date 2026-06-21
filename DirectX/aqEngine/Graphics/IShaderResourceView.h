#pragma once


namespace aq
{
	namespace graphics
	{
		/** シェーダーリソースビュー インターフェース */
		class IShaderResourceView
		{
		public:
			virtual ~IShaderResourceView() = default;
			virtual void Release() = 0;
			/** API ネイティブハンドル (D3D11: ID3D11ShaderResourceView*, D3D12: etc.) を返す。
			 *  ImTextureID キャストや API 固有コードでのみ使用すること。 */
			virtual void* GetNativeHandle() const { return nullptr; }
		};
	}
}
