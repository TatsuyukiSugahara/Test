#include "aq.h"
#include "AudioEventEmitterComponent.h"
#include "Sound/Authoring/AudioDirector.h"


namespace aq
{
	namespace audio
	{
		AudioEventEmitterComponent::AudioEventEmitterComponent(AudioEventEmitterComponent&& other) noexcept
		{
			MoveFrom(std::move(other));
		}


		AudioEventEmitterComponent& AudioEventEmitterComponent::operator=(AudioEventEmitterComponent&& other) noexcept
		{
			if (this != &other) {
				ReleaseGameObject();
				MoveFrom(std::move(other));
			}
			return *this;
		}


		AudioEventEmitterComponent::~AudioEventEmitterComponent()
		{
			ReleaseGameObject();
		}


		void AudioEventEmitterComponent::MoveFrom(AudioEventEmitterComponent&& other) noexcept
		{
			autoPlayEventId  = other.autoPlayEventId;
			autoPlay         = other.autoPlay;
			goId             = other.goId;
			started          = other.started;
			previousPosition = other.previousPosition;
			hasPrevious      = other.hasPrevious;

			// moved-from は GameObject を握っていないことにする（dtor で解除させない）。
			other.goId    = 0;
			other.started = false;
		}


		void AudioEventEmitterComponent::ReleaseGameObject() noexcept
		{
			if (goId != 0 && AudioDirector::IsAvailable()) {
				AudioDirector::Get().UnregisterGameObject(goId);
			}
			goId = 0;
		}
	}
}
