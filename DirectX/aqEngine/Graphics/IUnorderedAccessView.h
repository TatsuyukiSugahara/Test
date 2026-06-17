#pragma once


namespace aq
{
	namespace graphics
	{
		/** アンオーダードアクセスビュー インターフェース */
		class IUnorderedAccessView
		{
		public:
			virtual ~IUnorderedAccessView() = default;
			virtual void Release() = 0;
		};
	}
}
