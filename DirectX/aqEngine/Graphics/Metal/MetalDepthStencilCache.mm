#include "aq.h"
// Metal の深度ステンシルステート。他構成では本体をガードして空 TU にする。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalDepthStencilCache.h"
#include <cstdio>

// 本 TU は手動参照カウント(MRR)前提で書いている。CMake は -fobjc-arc を渡していない。
#if __has_feature(objc_arc)
#error "MetalDepthStencilCache.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif


namespace aq
{
	namespace graphics
	{
		/**
		 * DepthMode -> MTLDepthStencilState の作り置き
		 */
		MetalDepthStencilCache::MetalDepthStencilCache(id<MTLDevice> device)
			: device_([device retain])
			, states_{ nil, nil, nil }
		{
			// 3 つとも起動時にまとめて作る。DepthMode の値と配列添字を一対一で対応させる。
			@autoreleasepool {
				MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];

				for (uint32_t i = 0; i < DEPTH_MODE_COUNT; ++i) {
					MTLCompareFunction compare     = MTLCompareFunctionLess;
					bool               writeEnable = true;
					metal::ToMTLDepthState(static_cast<DepthMode>(i), compare, writeEnable);

					desc.depthCompareFunction = compare;
					desc.depthWriteEnabled    = writeEnable ? YES : NO;

					states_[i] = [device_ newDepthStencilStateWithDescriptor:desc];
					if (states_[i] == nil) {
						char msg[256];
						std::snprintf(msg, sizeof(msg),
							"[MetalDepthStencilCache] MTLDepthStencilState の生成に失敗: DepthMode=%u", i);
						aq::StartupLog(msg);
						EngineAssertMsg(false, "Metal の深度ステートを作れませんでした");
					}
				}

				[desc release];
			}
		}


		MetalDepthStencilCache::~MetalDepthStencilCache()
		{
			for (uint32_t i = 0; i < DEPTH_MODE_COUNT; ++i) {
				[states_[i] release];
				states_[i] = nil;
			}
			[device_ release];
			device_ = nil;
		}


		id<MTLDepthStencilState> MetalDepthStencilCache::Get(const DepthMode mode)
		{
			const uint32_t index = static_cast<uint32_t>(mode);
			if (index >= DEPTH_MODE_COUNT) { return states_[static_cast<uint32_t>(DepthMode::ReadWrite)]; }
			return states_[index];
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL
