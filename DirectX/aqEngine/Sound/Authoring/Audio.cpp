#include "aq.h"
#include "Audio.h"
#include "AudioDirector.h"
#include "Util/CRC32.h"


namespace aq
{
	namespace audio
	{
		namespace
		{
			NameId Hash(const char* name)
			{
				return name ? static_cast<NameId>(aq::util::ComputeCrc32(name)) : 0;
			}
		}


		void LoadBank(const char* path)
		{
			if (AudioDirector::IsAvailable()) {
				AudioDirector::Get().LoadBank(path);
			}
		}


		PlayingId PostEvent(const char* eventName, uint64_t gameObject)
		{
			if (!AudioDirector::IsAvailable()) {
				return 0;
			}
			return AudioDirector::Get().PostEvent(Hash(eventName), gameObject);
		}


		void StopPlaying(PlayingId id, float fadeSeconds)
		{
			if (AudioDirector::IsAvailable()) {
				AudioDirector::Get().StopPlaying(id, fadeSeconds);
			}
		}


		void StopAllByKind(const char* kindName, float fadeSeconds)
		{
			if (AudioDirector::IsAvailable()) {
				AudioDirector::Get().StopAllByKind(Hash(kindName), fadeSeconds);
			}
		}


		void SetSwitch(const char* groupName, const char* valueName, uint64_t gameObject)
		{
			if (AudioDirector::IsAvailable()) {
				AudioDirector::Get().SetSwitch(Hash(groupName), Hash(valueName), gameObject);
			}
		}


		void SetRTPC(const char* paramName, float value, uint64_t gameObject)
		{
			if (AudioDirector::IsAvailable()) {
				AudioDirector::Get().SetRTPC(Hash(paramName), value, gameObject);
			}
		}


		void SetState(const char* groupName, const char* valueName)
		{
			if (AudioDirector::IsAvailable()) {
				AudioDirector::Get().SetState(Hash(groupName), Hash(valueName));
			}
		}


		void RegisterGameObject(uint64_t gameObject)
		{
			if (AudioDirector::IsAvailable()) {
				AudioDirector::Get().RegisterGameObject(gameObject);
			}
		}


		void UnregisterGameObject(uint64_t gameObject)
		{
			if (AudioDirector::IsAvailable()) {
				AudioDirector::Get().UnregisterGameObject(gameObject);
			}
		}


		void SetGameObjectTransform(uint64_t gameObject,
		                            const math::Vector3& position, const math::Vector3& forward,
		                            const math::Vector3& up, const math::Vector3& velocity)
		{
			if (AudioDirector::IsAvailable()) {
				AudioDirector::Get().SetGameObjectTransform(gameObject, position, forward, up, velocity);
			}
		}


		void SetListener(const math::Vector3& position, const math::Vector3& forward,
		                 const math::Vector3& up, const math::Vector3& velocity)
		{
			if (AudioDirector::IsAvailable()) {
				AudioDirector::Get().SetListener(position, forward, up, velocity);
			}
		}
	}
}
