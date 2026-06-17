#pragma once
#include <cstdint>


namespace aq
{
	namespace graphics
	{
		/** シェーダー インターフェース */
		class IShader
		{
		public:
			enum class ShaderType : uint8_t
			{
				VS,
				PS,
				CS,
			};

			virtual ~IShader() = default;
			virtual bool   Load(const char* filePath, const char* entryFuncName, ShaderType shaderType) = 0;
			virtual void   Release() = 0;
			virtual void*  GetByteCode() const = 0;
			virtual size_t GetByteCodeSize() const = 0;
		};
	}
}
