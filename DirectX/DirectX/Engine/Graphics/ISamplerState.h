#pragma once
#include "GraphicsTypes.h"


namespace engine
{
	namespace graphics
	{
		/** サンプラーステート インターフェース */
		class ISamplerState
		{
		public:
			virtual ~ISamplerState() = default;
			virtual bool  Create(const SamplerDesc& desc) = 0;
			virtual void  Release() = 0;
			/** D3D11: ID3D11SamplerState* を void* で返す */
			virtual void* GetNativeHandle() const = 0;
		};
	}
}
