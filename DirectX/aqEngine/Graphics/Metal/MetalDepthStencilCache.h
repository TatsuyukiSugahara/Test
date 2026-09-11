#pragma once
// Metal の深度ステンシルステートの作り置き(設計書/MetalBackend設計.md §4.3)。
//
// **本ヘッダは Objective-C++ 専用**で、Graphics/Metal/ 配下の .mm からのみ include する
// (MetalCommon.h が __OBJC__ を検査している)。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalCommon.h"


namespace aq
{
	namespace graphics
	{
		/**
		 * DepthMode -> MTLDepthStencilState の作り置き
		 *
		 * Metal では深度/ステンシルは PSO ではなく MTLDepthStencilState という別オブジェクトで、
		 * エンコーダへ独立に設定する。DepthMode は 3 値しかないので、
		 * **起動時に 3 つ作って持つだけ**でよい(遅延生成もキャッシュ検索も要らない)。
		 *
		 * 参照カウントは MRR(ARC ではない)。newDepthStencilStateWithDescriptor: で
		 * +1 されたものを保持し、デストラクタで release する。
		 */
		class MetalDepthStencilCache
		{
		private:
			/** DepthMode の値の数(ReadWrite / ReadOnly / Disabled) */
			static constexpr uint32_t DEPTH_MODE_COUNT = 3;

			/** Metal オブジェクト */
			id<MTLDevice>            device_;
			id<MTLDepthStencilState> states_[DEPTH_MODE_COUNT];


		public:
			explicit MetalDepthStencilCache(id<MTLDevice> device);
			~MetalDepthStencilCache();

			MetalDepthStencilCache(const MetalDepthStencilCache&)            = delete;
			MetalDepthStencilCache& operator=(const MetalDepthStencilCache&) = delete;


		public:
			/** DepthMode に対応する MTLDepthStencilState(生成に失敗していれば nil) */
			id<MTLDepthStencilState> Get(DepthMode mode);
		};
	}
}
#endif // ENGINE_GRAPHICS_METAL
