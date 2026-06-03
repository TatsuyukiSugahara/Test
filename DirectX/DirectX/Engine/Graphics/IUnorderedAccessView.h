#pragma once


namespace engine
{
	namespace graphics
	{
		/** アンオーダードアクセスビュー インターフェース */
		class IUnorderedAccessView
		{
		public:
			virtual ~IUnorderedAccessView() = default;
			virtual void  Release() = 0;
			/** D3D11: ID3D11UnorderedAccessView* を void* で返す */
			virtual void* GetNativeHandle() const = 0;
		};
	}
}
