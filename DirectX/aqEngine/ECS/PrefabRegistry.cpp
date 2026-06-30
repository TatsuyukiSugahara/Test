#include "aq.h"
#include "PrefabRegistry.h"
#include "PrefabSerializer.h"

namespace aq
{
	namespace ecs
	{
		PrefabRegistry& PrefabRegistry::Get()
		{
			static PrefabRegistry instance;
			return instance;
		}


		std::string PrefabRegistry::Normalize(std::string_view pathOrGuid)
		{
			std::string s(pathOrGuid);
			for (char& c : s) {
				if (c == '\\') c = '/';
			}
			return s;
		}


		uint64_t PrefabRegistry::HashKey(const std::string& normalized)
		{
			uint64_t hash = 14695981039346656037ULL;          // FNV-1a offset basis
			constexpr uint64_t prime = 1099511628211ULL;
			for (unsigned char c : normalized) {
				hash ^= static_cast<uint64_t>(c);
				hash *= prime;
			}
			if (hash == 0) hash = 1;                           // 0 は無効値に予約
			return hash;
		}


		uint64_t PrefabRegistry::AssignKey(const std::string& normalized)
		{
			uint64_t key = HashKey(normalized);
			// 既に別の文字列へ割り当て済みなら衝突。線形プローブで別キーを割り当てる。
			while (true) {
				auto it = idToKey_.find(key);
				if (it == idToKey_.end()) break;          // 空き
				if (it->second == normalized) break;      // 自分自身（再解決）
				EngineAssertMsg(false,
					"PrefabRegistry: uint64 cache key collision detected; assigning an alternate key");
				++key;
				if (key == 0) key = 1;
			}
			return key;
		}


		PrefabId PrefabRegistry::Resolve(std::string_view pathOrGuid)
		{
			const std::string norm = Normalize(pathOrGuid);

			auto cached = keyToId_.find(norm);
			if (cached != keyToId_.end()) return cached->second;

			Prefab prefab = PrefabSerializer::Load(norm.c_str());
			if (!prefab.IsValid()) {
				EngineAssertMsg(false, "PrefabRegistry::Resolve: failed to load prefab (missing/invalid file)");
				return PrefabId{ 0 };
			}

			const uint64_t key = AssignKey(norm);
			const PrefabId id{ key };
			keyToId_[norm]  = id;
			idToKey_[key]   = norm;
			idToData_[key]  = prefab.Data();
			return id;
		}


		PrefabId PrefabRegistry::Register(std::string_view key, const Prefab& prefab)
		{
			const std::string norm = Normalize(key);

			auto cached = keyToId_.find(norm);
			if (cached != keyToId_.end()) return cached->second;

			if (!prefab.IsValid()) {
				EngineAssertMsg(false, "PrefabRegistry::Register: prefab is invalid");
				return PrefabId{ 0 };
			}

			const uint64_t k = AssignKey(norm);
			const PrefabId id{ k };
			keyToId_[norm] = id;
			idToKey_[k]    = norm;
			idToData_[k]   = prefab.Data();
			return id;
		}


		std::shared_ptr<const PrefabData> PrefabRegistry::Find(PrefabId id) const
		{
			if (!id.IsValid()) return nullptr;
			auto it = idToData_.find(id.value);
			return (it != idToData_.end()) ? it->second : nullptr;
		}


		void PrefabRegistry::Clear()
		{
			keyToId_.clear();
			idToKey_.clear();
			idToData_.clear();
		}
	}
}
