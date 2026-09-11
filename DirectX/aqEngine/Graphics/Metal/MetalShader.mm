#include "aq.h"
// Metal のシェーダ。他構成では本体をガードして空 TU にする。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalShader.h"

// 本 TU は手動参照カウント(MRR)前提で書いている。CMake は -fobjc-arc を渡していない。
#if __has_feature(objc_arc)
#error "MetalShader.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif


namespace aq
{
	namespace graphics
	{
		/**
		 * Metal シェーダ
		 */
		MetalShader::MetalShader(id<MTLDevice> device)
			: device_([device retain])
			, library_(nil)
			, function_(nil)
			, filePath_()
			, entryFuncName_()
			, type_(ShaderType::VS)
		{
		}


		MetalShader::~MetalShader()
		{
			Release();
			[device_ release];
			device_ = nil;
		}


		bool MetalShader::Load(const char* filePath, const char* entryFuncName, const ShaderType shaderType)
		{
			// TODO(P0.5): .metal を読んで newLibraryWithSource: でコンパイルする(設計書 §9.2)。
			//             P0 はパス / エントリ名 / 種別を憶えるだけで成功扱いにする
			//             (false を返すとエンジンのリソース初期化がそこで止まるため)。
			filePath_      = filePath      ? filePath      : "";
			entryFuncName_ = entryFuncName ? entryFuncName : "";
			type_          = shaderType;
			return true;
		}


		void MetalShader::Release()
		{
			[function_ release];
			function_ = nil;
			[library_ release];
			library_  = nil;
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL
