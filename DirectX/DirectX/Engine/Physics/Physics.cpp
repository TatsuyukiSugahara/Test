#include "Physics.h"


namespace engine
{
	namespace physics
	{
		template <typename PhysicsWorkd> 
		Physics<PhysicsWorkd>* Physics<PhysicsWorkd>::sInstance_ = nullptr;
	}
}