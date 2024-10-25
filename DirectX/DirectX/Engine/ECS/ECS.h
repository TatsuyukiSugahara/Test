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
				const uint32_t chunkIndex = EntityManager::Get().GetChunkIndex(chunk->GetArchetype());
				for (uint32_t i = 0; i < chunk->GetSize(); ++i) {
					Entity entity(chunkIndex, i);
					func(entity , &args[i]...);
				}
			}
		}


#define getArg(T) chunk->template GetComponentArray<T>()

		template<class T1, typename Func>
		void Foreach(Func&& func)
		{
			auto chunkList = EntityManager::Get().GetChunkList<T1>();
			for (auto&& chunk : chunkList) {
				_internal::Foreach(chunk, func, getArg(T1));
			}
		}


		template<class T1, class T2, typename Func>
		void Foreach(Func&& func)
		{
			auto chunkList = EntityManager::Get().GetChunkList<T1, T2>();
			for (auto&& chunk : chunkList) {
				_internal::Foreach(chunk, func, getArg(T1), getArg(T2));
			}
		}


		template<class T1, class T2, class T3, typename Func>
		void Foreach(Func&& func)
		{
			auto chunkList = EntityManager::Get().GetChunkList<T1, T2, T3>();
			for (auto&& chunk : chunkList) {
				_internal::Foreach(chunk, func, getArg(T1), getArg(T2), getArg(T3));
			}
		}


		template<class T1, class T2, class T3, class T4, typename Func>
		void Foreach(Func&& func)
		{
			auto chunkList = EntityManager::Get().GetChunkList<T1, T2, T3, T4>();
			for (auto&& chunk : chunkList) {
				_internal::Foreach(chunk, func, getArg(T1), getArg(T2), getArg(T3), getArg(T4));
			}
		}


		template<class T1, class T2, class T3, class T4, class T5, typename Func>
		void Foreach(Func&& func)
		{
			auto chunkList = EntityManager::Get().GetChunkList<T1, T2, T3, T4, T5>();
			for (auto&& chunk : chunkList) {
				_internal::Foreach(chunk, func, getArg(T1), getArg(T2), getArg(T3), getArg(T4), getArg(T5));
			}
		}


		template<class T1, class T2, class T3, class T4, class T5, class T6, typename Func>
		void Foreach(Func&& func)
		{
			auto chunkList = EntityManager::Get().GetChunkList<T1, T2, T3, T4, T5, T6>();
			for (auto&& chunk : chunkList) {
				_internal::Foreach(chunk, func, getArg(T1), getArg(T2), getArg(T3), getArg(T4), getArg(T5), getArg(T6));
			}
		}

#undef getArg
	}
}