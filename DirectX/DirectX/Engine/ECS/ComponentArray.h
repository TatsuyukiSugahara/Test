#pragma once
#include <cstdint>


namespace engine
{
	namespace ecs
	{
		template <typename T>
		class ComponentArray
		{
		private:
			T* begin_;
			size_t size_;


		public:
			ComponentArray(T* begin, const size_t size)
				: begin_(begin)
				, size_(size)
			{
			}


			T& operator[](const int32_t index)
			{
				return begin_[index];
			}


			T* begin()
			{
				return begin_();
			}
			T* eng()
			{
				return begin_ + size_;
			}

		};
	}
}