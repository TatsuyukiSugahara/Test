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


		const char* DescribePassKey(const PassResourceKey key)
		{
			if (key == PassResourceKeys::Scene)        { return "Scene"; }
			if (key == PassResourceKeys::Depth)        { return "Depth"; }
			if (key == PassResourceKeys::GBuffer0)     { return "GBuffer0"; }
			if (key == PassResourceKeys::GBuffer1)     { return "GBuffer1"; }
			if (key == PassResourceKeys::GBuffer2)     { return "GBuffer2"; }
			if (key == PassResourceKeys::GBuffer3)     { return "GBuffer3"; }
			if (key == PassResourceKeys::WorldPos)     { return "WorldPos"; }
			if (key == PassResourceKeys::HiZ)          { return "HiZ"; }
			if (key == PassResourceKeys::PostInput)    { return "PostInput"; }
			if (key == PassResourceKeys::PostOutput)   { return "PostOutput"; }
			if (key == PassResourceKeys::BloomTexture) { return "BloomTexture"; }
			if (key == PassResourceKeys::Output)       { return "Output"; }

			// ゲーム独自キー (MakePassKey("Game.Xxx") 等)。名前を持たないので 16 進表示にする。
			static char unknown[16];
			snprintf(unknown, sizeof(unknown), "0x%08x", key);
			return unknown;
		}
	}
}
