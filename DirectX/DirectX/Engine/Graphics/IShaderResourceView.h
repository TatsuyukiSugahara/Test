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
			virtual void Release() = 0;
		};
	}
}
