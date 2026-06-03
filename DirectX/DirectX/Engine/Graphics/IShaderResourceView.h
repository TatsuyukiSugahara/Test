#pragma once


namespace engine
{
	namespace graphics
	{
		/** シェーダーリソースビュー インターフェース */
		class IShaderResourceView
		{
		public:
			virtual ~IShaderResourceView() = default;
			virtual void  Release() = 0;
			/** D3D11: ID3D11ShaderResourceView* を void* で返す */
			virtual void* GetNativeHandle() const = 0;
		};
	}
}
