#pragma once
// Metal のシェーダ(設計書/MetalBackend設計.md §9)。
//
// **本ヘッダは Objective-C++ 専用**で、Graphics/Metal/ 配下の .mm からのみ include する。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalCommon.h"
#include "Graphics/IShader.h"
#include <string>


namespace aq
{
	namespace graphics
	{
		/**
		 * Metal シェーダ (P0: パス / エントリ名 / 種別を憶えるだけのスタブ)
		 *
		 * 本実装(P0.5)ではビルド時に生成した .metal(MSL)を読み、
		 * newLibraryWithSource: で実行時コンパイルして newFunctionWithName: で
		 * MTLFunction を取り出す(設計書 §9.2)。エントリ名は spirv-cross が
		 * "main0" に固定するため、HLSL 側のエントリ名は照合に使わない。
		 * xcrun metal が使えないため、.metallib の事前ビルドは採らない(設計書 §0.2)。
		 */
		class MetalShader : public IShader
		{
		private:
			/** Metal オブジェクト */
			id<MTLDevice>   device_;
			id<MTLLibrary>  library_;
			id<MTLFunction> function_;

			/** ロード情報 */
			std::string filePath_;
			std::string entryFuncName_;
			ShaderType  type_;


		public:
			explicit MetalShader(id<MTLDevice> device);
			~MetalShader() override;


		public:
			bool   Load(const char* filePath, const char* entryFuncName, ShaderType shaderType) override;
			void   Release() override;

			/** Metal はバイトコードを外へ出さない(PSO 生成は MTLFunction 経由) */
			inline void*  GetByteCode() const override     { return nullptr; }
			inline size_t GetByteCodeSize() const override { return 0; }


			/**
			 * PSO 生成用 (P0.5 以降)
			 */
		public:
			/** PSO へ渡す MTLFunction。P0 では常に nil */
			inline id<MTLFunction> GetFunction() const { return function_; }

			inline ShaderType  GetType() const          { return type_; }
			inline const char* GetFilePath() const      { return filePath_.c_str(); }
			inline const char* GetEntryFuncName() const { return entryFuncName_.c_str(); }
		};
	}
}
#endif // ENGINE_GRAPHICS_METAL
