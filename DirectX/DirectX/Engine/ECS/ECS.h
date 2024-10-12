#pragma once
#include "EntityManager.h"
#include "System.h"


/**
 * éQçlÅFhttps://qiita.com/harayuu10/items/e15b02e3b0f3081d729b
 */

namespace engine
{
	namespace ecs
	{
		namespace _internal
		{
			template<typename Func, class... Args>
			static void Foreach(Chunk* chunk, Func&& func, Args ... args)
			{
				for (uint32_t i = 0; i < chunk->GetSize(); ++i) {
					func(&args[i]...);
				}
			}
		}


		template<class T1, typename Func>
		void Foreach(Func&& func)
		{
			auto chunkList = EntityManager::Get().GetChunkList<T1>();
			for (auto&& chunk : chunkList) {
				auto arg1 = chunk->template GetComponentArray<T1>();
				_internal::Foreach(chunk, func, arg1);
			}
		}


		template<class T1, class T2, typename Func>
		void Foreach(Func&& func)
		{
			auto chunkList = EntityManager::Get().GetChunkList<T1, T2>();
			for (auto&& chunk : chunkList) {
				auto arg1 = chunk->template GetComponentArray<T1>();
				auto arg2 = chunk->template GetComponentArray<T2>();
				_internal::Foreach(chunk, func, arg1, arg2);
			}
		}


		template<class T1, class T2, class T3, typename Func>
		void Foreach(Func&& func)
		{
			auto chunkList = EntityManager::Get().GetChunkList<T1, T2, T3>();
			for (auto&& chunk : chunkList) {
				auto arg1 = chunk->template GetComponentArray<T1>();
				auto arg2 = chunk->template GetComponentArray<T2>();
				auto arg3 = chunk->template GetComponentArray<T3>();
				_internal::Foreach(chunk, func, arg1, arg2, arg3);
			}
		}


		template<class T1, class T2, class T3, class T4, typename Func>
		void Foreach(Func&& func)
		{
			auto chunkList = EntityManager::Get().GetChunkList<T1, T2, T3, T4>();
			for (auto&& chunk : chunkList) {
				auto arg1 = chunk->template GetComponentArray<T1>();
				auto arg2 = chunk->template GetComponentArray<T2>();
				auto arg3 = chunk->template GetComponentArray<T3>();
				auto arg4 = chunk->template GetComponentArray<T4>();
				_internal::Foreach(chunk, func, arg1, arg2, arg3, arg4);
			}
		}
	}
}