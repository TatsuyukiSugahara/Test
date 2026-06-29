#pragma once
#include <memory>


namespace aq
{
	namespace sound
	{
		class SoundClip;
		class SoundStream;
		class ISoundVoice;
		class ISoundBackend;

		// 共有参照で寿命管理する常駐クリップ（§2.4 / High）。
		// ResourceManager::Load<SoundClip>() の戻り値をそのまま渡せる。
		using RefSoundClip = std::shared_ptr<const SoundClip>;
	}
}
