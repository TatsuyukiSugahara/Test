#pragma once
#include <type_traits>
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


namespace engine
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

		private:
			size_t typeHash_;
			size_t size_;
			DestructorFn destructor_;


		private:
			constexpr explicit TypeInfo(const size_t typeHash, const size_t size, DestructorFn destructor)
				: typeHash_(typeHash), size_(size), destructor_(destructor) {}


		public:
			constexpr TypeInfo() : typeHash_(-1), size_(0), destructor_(nullptr)
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

			template <class T, typename = std::enable_if_t<type::GetTypeName<T>>>
			static constexpr TypeInfo Create()
			{
				return TypeInfo(T::GetTypeHash(), sizeof(T),
					std::is_trivially_destructible_v<T> ? nullptr : &DestroyImpl<T>);
			}

			static constexpr TypeInfo Create(const size_t hash, size_t size)
			{
				return TypeInfo(hash, size, nullptr);
			}

		private:
			template <typename T>
			static void DestroyImpl(void* p) { static_cast<T*>(p)->~T(); }
		};
	}
}
