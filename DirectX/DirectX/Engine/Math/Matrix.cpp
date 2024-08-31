#include "../EnginePreCompile.h"
#include "Matrix.h"

namespace engine
{
	namespace math
	{
		const Matrix4x4 Matrix4x4::Identity(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		);
	}
}