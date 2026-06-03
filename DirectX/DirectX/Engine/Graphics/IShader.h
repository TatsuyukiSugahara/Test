#pragma once
#include <cstdint>


namespace engine
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
			/** シェーダーオブジェクト本体 (D3D11: ID3D11VertexShader* 等) を void* で返す */
			virtual void*  GetNativeHandle() const = 0;
			/** 頂点シェーダーの InputLayout (VS 以外は nullptr) を void* で返す */
			virtual void*  GetInputLayout() const = 0;
			virtual void*  GetByteCode() const = 0;
			virtual size_t GetByteCodeSize() const = 0;
		};
	}
}
