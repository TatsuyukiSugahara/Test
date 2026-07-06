#include "aq.h"
#include "AudioSourceComponent.h"
#include "Sound/SoundEngine.h"
#include "Sound/SoundClip.h"


namespace aq
{
	namespace sound
	{
		void AudioSourceComponent::OnDeserialized()
		{
			if (!clipPath.empty())
				clip = aq::res::ResourceManager::Get().Load<aq::sound::SoundClip>(clipPath.c_str());
		}


		AudioSourceComponent::AudioSourceComponent(AudioSourceComponent&& other) noexcept
		{
			MoveFrom(std::move(other));
		}


		AudioSourceComponent& AudioSourceComponent::operator=(AudioSourceComponent&& other) noexcept
		{
			if (this != &other) {
				DestroyOwned();
				MoveFrom(std::move(other));
			}
			return *this;
		}


		AudioSourceComponent::~AudioSourceComponent()
		{
			DestroyOwned();
		}


		void AudioSourceComponent::MoveFrom(AudioSourceComponent&& other) noexcept
		{
			clip          = std::move(other.clip);
			bus           = other.bus;
			attenuation   = other.attenuation;
			minDistance   = other.minDistance;
			maxDistance   = other.maxDistance;
			dopplerFactor = other.dopplerFactor;
			pitch         = other.pitch;
			volume        = other.volume;
			loop          = other.loop;
			autoPlay      = other.autoPlay;

			handle           = other.handle;
			initialized      = other.initialized;
			previousPosition = other.previousPosition;
			hasPrevious      = other.hasPrevious;

			// moved-from は実体を握っていないことにする（dtor で破棄させない）。
			other.handle      = SoundSourceHandle{};
			other.initialized = false;
		}


		void AudioSourceComponent::DestroyOwned() noexcept
		{
			if (handle.IsValid() && SoundEngine::IsAvailable()) {
				SoundEngine::Get().DestroySource(handle);
			}
			handle      = SoundSourceHandle{};
			initialized = false;
		}
	}
}
