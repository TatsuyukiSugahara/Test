#pragma once
#include <string>
#include "Math/Vector.h"

namespace aq
{
	namespace ui
	{
		struct TextStyleOutline
		{
			bool          enabled = false;
			math::Vector4 color   = { 0.f, 0.f, 0.f, 1.f };
			float         width   = 0.08f;  // SDF threshold offset (0 ~ 0.5)
		};

		struct TextStyleShadow
		{
			bool          enabled  = false;
			math::Vector4 color    = { 0.f, 0.f, 0.f, 0.6f };
			math::Vector2 offset   = { 2.f, -2.f };  // canvas px
			float         softness = 0.05f;
		};

		struct TextStyleGradient
		{
			bool          enabled     = false;
			math::Vector4 topColor    = { 1.f, 1.f, 1.f, 1.f };
			math::Vector4 bottomColor = { 0.6f, 0.6f, 0.6f, 1.f };
		};

		struct TextStyle
		{
			std::string       name;
			std::string       fontPath;
			float             fontSize  = 24.f;
			math::Vector4     fillColor = { 1.f, 1.f, 1.f, 1.f };

			TextStyleOutline  outline;
			TextStyleShadow   shadow;
			TextStyleGradient gradient;

			bool LoadFromJson(const char* path);
			bool SaveToJson(const char* path) const;

			static TextStyle MakeDefault();
		};

	} // namespace ui
} // namespace aq
