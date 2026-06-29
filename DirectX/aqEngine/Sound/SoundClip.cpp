#include "aq.h"
#include "SoundClip.h"
#include "Decoder/WavDecoder.h"
#include "Decoder/MFDecoder.h"
#include <algorithm>
#include <string>


namespace aq
{
	namespace sound
	{
		namespace
		{
			bool HasExtension(const std::string& lowerPath, const char* ext)
			{
				const std::string e = ext;
				return lowerPath.size() >= e.size()
					&& lowerPath.compare(lowerPath.size() - e.size(), e.size(), e) == 0;
			}
		}


		bool SoundClipLoader::Loading()
		{
			SoundClipData* dst = static_cast<SoundClip*>(resource_.get())->GetMutable();
			if (dst == nullptr) {
				return false;
			}

			std::string lower = requestPath_;
			std::transform(lower.begin(), lower.end(), lower.begin(),
			               [](unsigned char c) { return static_cast<char>(::tolower(c)); });

			// 拡張子で全展開デコーダを選ぶ（§5.1）。
			bool ok = false;
			if (HasExtension(lower, ".wav")) {
				ok = WavDecoder::DecodeFile(requestPath_.c_str(), dst->format, dst->pcm);
			}
			else {
				// mp3/aac/wma/m4a 等は Media Foundation（Windows）。
				ok = MFDecoder::DecodeFileFully(requestPath_.c_str(), dst->format, dst->pcm);
			}
			if (!ok) {
				return false;
			}

			dst->defaultLoop = LoopRegion{};
			return true;
		}
	}
}
