#include "aq.h"
#include "PassResources.h"


namespace aq
{
	namespace rendering
	{
		void PassResources::Set(const PassResourceKey key, const RenderTargetHandle handle)
		{
			for (Entry& entry : entries_) {
				if (entry.key == key) {
					entry.handle = handle;
					return;
				}
			}
			entries_.push_back({ key, handle });
		}


		RenderTargetHandle PassResources::Get(const PassResourceKey key) const
		{
			for (const Entry& entry : entries_) {
				if (entry.key == key) {
					return entry.handle;
				}
			}
			return RenderTargetHandle{};
		}


		bool PassResources::Has(const PassResourceKey key) const
		{
			for (const Entry& entry : entries_) {
				if (entry.key == key) {
					return true;
				}
			}
			return false;
		}
	}
}
