#pragma once
#include "TypeInfo.h"
#include <new>
#include <utility>

namespace aq
{
	namespace ecs
	{
		// 型消去したコンポーネント実体を 1 個保持するストレージ（Prefab エディタ用・設計 §6.2）。
		// Chunk::AllocBuffer と同方式で `operator new(bytes, align_val_t(align))` によりアライメントを保証する。
		// move-only（バイトコピー禁止）: ヒープ上の実体ポインタを move で移譲する。
		// 構築/破棄は TypeInfo の ConstructFn / DestructorFn を使うため、非 trivial 型も安全に扱える。
		class AlignedStorage
		{
		public:
			AlignedStorage() = default;

			explicit AlignedStorage(TypeInfo type) : type_(type)
			{
				if (type.GetSize() == 0) return;   // 既定 TypeInfo（未解決）は空のまま
				ptr_ = ::operator new(type.GetSize(), std::align_val_t(type.GetAlign()));
				type.Construct(ptr_);              // ConstructFn は全型必須（trivial 型も初期化される）
			}

			~AlignedStorage() { Reset(); }

			AlignedStorage(AlignedStorage&& other) noexcept
				: ptr_(other.ptr_), type_(other.type_)
			{
				other.ptr_ = nullptr;
			}

			AlignedStorage& operator=(AlignedStorage&& other) noexcept
			{
				if (this != &other)
				{
					Reset();
					ptr_        = other.ptr_;
					type_       = other.type_;
					other.ptr_  = nullptr;
				}
				return *this;
			}

			AlignedStorage(const AlignedStorage&)            = delete;   // CopyFn 未定義のためコピー禁止
			AlignedStorage& operator=(const AlignedStorage&) = delete;

			void*           Get()     const { return ptr_; }
			const TypeInfo& Type()    const { return type_; }
			bool            IsValid() const { return ptr_ != nullptr; }

		private:
			void Reset()
			{
				if (!ptr_) return;
				if (TypeInfo::DestructorFn dtor = type_.GetDestructor()) dtor(ptr_);
				::operator delete(ptr_, std::align_val_t(type_.GetAlign()));
				ptr_ = nullptr;
			}

			void*    ptr_ = nullptr;
			TypeInfo type_;
		};
	}
}
