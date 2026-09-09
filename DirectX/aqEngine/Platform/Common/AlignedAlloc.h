#pragma once
#include "Platform/Common/PlatformDefs.h"
#include <cstddef>

#if defined(AQ_PLATFORM_WINDOWS_FAMILY)
#include <malloc.h>
#else
#include <stdlib.h>
#endif


namespace aq
{
	namespace memory
	{
		/**
		 * アラインメント指定のメモリ確保
		 * @param size      確保するバイト数
		 * @param alignment アラインメント(2 の冪)。2 の冪でなければ nullptr を返す
		 * @return 確保した領域。失敗時は nullptr
		 */
		inline void* AlignedAlloc(size_t size, size_t alignment)
		{
#if defined(AQ_PLATFORM_WINDOWS_FAMILY)
			return _aligned_malloc(size, alignment);
#else
			// _aligned_malloc は「2 の冪」だけを要求する。posix_memalign は加えて
			// sizeof(void*) の倍数であることを要求するため、Windows 版の挙動に
			// 合わせて小さすぎるアラインメントは切り上げる。
			if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
				return nullptr;
			}
			if (alignment < sizeof(void*)) {
				alignment = sizeof(void*);
			}

			void* ptr = nullptr;
			if (posix_memalign(&ptr, alignment, size) != 0) {
				return nullptr;
			}
			return ptr;
#endif
		}

		/** AlignedAlloc で確保した領域の解放(nullptr 可) */
		inline void AlignedFree(void* ptr)
		{
#if defined(AQ_PLATFORM_WINDOWS_FAMILY)
			_aligned_free(ptr);
#else
			free(ptr);
#endif
		}
	}
}
