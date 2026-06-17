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
		};
	}
}
