#include "aq.h"
#include "AppModuleRegistry.h"


namespace aq
{
	void AppModuleRegistry::Register(const char* name, Factory factory)
	{
		if (name == nullptr || name[0] == '\0' || !factory) {
			aq::StartupMarkf("  [module] ignored invalid registration");
			return;
		}

		for (const Entry& entry : entries_) {
			if (entry.name == name) {
				aq::StartupMarkf("  [module] ignored duplicate registration: %s", name);
				return;
			}
		}

		entries_.push_back(Entry{ name, std::move(factory) });
	}


	std::unique_ptr<IAppModule> AppModuleRegistry::Create(const std::string& name) const
	{
		if (entries_.empty()) {
			return nullptr;
		}

		// 名前の省略は登録順の先頭
		if (name.empty()) {
			return entries_.front().factory();
		}

		for (const Entry& entry : entries_) {
			if (entry.name == name) {
				return entry.factory();
			}
		}
		return nullptr;
	}


	std::vector<std::string> AppModuleRegistry::GetNames() const
	{
		std::vector<std::string> names;
		names.reserve(entries_.size());
		for (const Entry& entry : entries_) {
			names.push_back(entry.name);
		}
		return names;
	}
}
