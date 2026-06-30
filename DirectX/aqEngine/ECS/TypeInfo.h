#pragma once
#include <type_traits>
#include <utility>
#include <string_view>


#define TYPE_INFO(T)									\
public:													\
	static constexpr std::string_view GetTypeName()		\
	{													\
		return #T;										\
	}													\
	static constexpr size_t GetTypeHash()				\
	{													\
		auto offsetBasis = 14695981039346656037ULL;		\
		constexpr auto prime = 1099511628211ULL;		\
		for (auto idx : #T)								\
		{												\
			offsetBasis ^= static_cast<size_t>(idx);	\
			offsetBasis *= prime;						\
		}												\
		return offsetBasis;								\
	}													\


namespace aq
{
	namespace ecs
	{

		namespace type
		{
			/**
			 * 指定した型がType情報を持っているか
			 */
			template<typename T>
			struct TypeHash
			{
			private:
				template<typename U>
				static auto Test(int) -> decltype(U::GetTypeHash(), std::true_type());
				template <typename U>
				static auto Test(...) -> decltype(std::false_type());


			public:
				using Type = decltype(Test<T>(0));
			};


			template<typename T>
			constexpr bool GetTypeName = TypeHash<T>::Type::value;
		}


		/**
		 * Type情報
		 */
		class TypeInfo
		{
		public:
			using DestructorFn = void(*)(void*);
			using MoveFn = void(*)(void* dst, void* src);
			using ConstructFn = void(*)(void*);

		private:
			size_t typeHash_;
			size_t size_;
			size_t align_;
			DestructorFn destructor_;
			MoveFn mover_;
			ConstructFn constructor_;


		private:
			constexpr explicit TypeInfo(const size_t typeHash, const size_t size, const size_t align, DestructorFn destructor, MoveFn mover, ConstructFn constructor)
				: typeHash_(typeHash), size_(size), align_(align), destructor_(destructor), mover_(mover), constructor_(constructor) {}


		public:
			constexpr TypeInfo() : typeHash_(-1), size_(0), align_(1), destructor_(nullptr), mover_(nullptr), constructor_(nullptr)
			{
			}

			constexpr bool operator==(const TypeInfo& other) const
			{
				return typeHash_ == other.typeHash_;
			}

			constexpr bool operator!=(const TypeInfo& other) const
			{
				return !(*this == other);
			}

			[[nodiscard]] constexpr size_t GetHash() const
			{ 
				return typeHash_;
			}

			[[nodiscard]] constexpr size_t GetSize() const
			{
				return size_;
			}

			[[nodiscard]] constexpr DestructorFn GetDestructor() const
			{
				return destructor_;
			}

			[[nodiscard]] constexpr MoveFn GetMover() const
			{
				return mover_;
			}

			[[nodiscard]] constexpr ConstructFn GetConstructor() const
			{
				return constructor_;
			}

			// 実行時 Archetype 構築経路で生メモリにコンポーネントを placement-new する。
			// ConstructFn は全型で設定されるため、trivial 型でもゼロ初期化されオブジェクト寿命が確定する。
			void Construct(void* p) const
			{
				if (constructor_) constructor_(p);
			}

			[[nodiscard]] constexpr size_t GetAlign() const
			{
				return align_;
			}

			template <class T, typename = std::enable_if_t<type::GetTypeName<T>>>
			static constexpr TypeInfo Create()
			{
				return TypeInfo(
					T::GetTypeHash(),
					sizeof(T),
					alignof(T),
					std::is_trivially_destructible_v<T> ? nullptr : &DestroyImpl<T>,
					std::is_trivially_copyable_v<T>     ? nullptr : &MoveImpl<T>,
					&ConstructImpl<T>   // 構築は常に必須（Chunk は生メモリ、trivial 型も構築して未初期化を防ぐ）
				);
			}

			static constexpr TypeInfo Create(const size_t hash, size_t size)
			{
				return TypeInfo(hash, size, 1, nullptr, nullptr, nullptr);
			}

		private:
			template <typename T>
			static void DestroyImpl(void* p) { static_cast<T*>(p)->~T(); }

			// 既定構築の placement-new（実行時 TypeInfo から構築するため）
			template <typename T>
			static void ConstructImpl(void* p) { new(p) T(); }

			// 非 trivially-copyable な型の move-construct（src は moved-from 状態になる）
			template <typename T>
			static void MoveImpl(void* dst, void* src)
			{
				new(dst) T(std::move(*reinterpret_cast<T*>(src)));
			}
		};
	}
}
