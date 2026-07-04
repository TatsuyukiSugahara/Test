#include "aq.h"
#include "Level/LevelRegistry.h"
#include "Level/LevelSerializer.h"


namespace aq
{
	namespace level
	{
		LevelRegistry& LevelRegistry::Get()
		{
			static LevelRegistry instance;
			return instance;
		}


		std::string LevelRegistry::Normalize(std::string_view pathOrId)
		{
			std::string s(pathOrId);
			for (char& c : s) if (c == '\\') c = '/';
			return s;
		}


		std::shared_ptr<const LevelData> LevelRegistry::Load(std::string_view pathOrId)
		{
			const std::string key = Normalize(pathOrId);

			auto it = cache_.find(key);
			if (it != cache_.end()) return it->second;

			std::shared_ptr<const LevelData> data = LevelSerializer::Load(key.c_str());
			if (data) cache_[key] = data;
			return data;
		}


		std::shared_ptr<const LevelData> LevelRegistry::Register(std::string_view key, std::shared_ptr<const LevelData> data)
		{
			const std::string k = Normalize(key);

			auto it = cache_.find(k);
			if (it != cache_.end()) return it->second;

			cache_[k] = data;
			return data;
		}


		void LevelRegistry::Clear()
		{
			cache_.clear();
		}
	}
}
