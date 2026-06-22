#pragma once
#ifdef AQ_DEBUG_IMGUI

#include <cstdint>
#include <vector>
#include "Core/IDebugRenderable.h"
#include "Math/Vector.h"

namespace aq
{
	class Camera;

	namespace terrain
	{
		class HeightmapChunk;

		class SplatmapPainter : public IDebugRenderable
		{
		public:
			enum class BrushMode : uint8_t { Paint, Erase, Smooth };

			SplatmapPainter() = default;
			~SplatmapPainter() override = default;

			void Attach(HeightmapChunk* chunk, math::Vector3 worldOffset);
			void Detach();

			void DebugRender() override;
			const char* GetDebugLabel() const override { return "Splatmap Painter"; }

		public:
			BrushMode brushMode_ = BrushMode::Paint;
			uint32_t  activeLayer_ = 0;
			float     brushRadius_ = 10.0f;
			float     brushStrength_ = 0.3f;
			bool      enabled_        = true;
			bool      paint3DEnabled_ = true;

		private:
			HeightmapChunk* chunk_ = nullptr;
			math::Vector3 worldOffset_ = {};

			math::Vector3 lastHitWorld_ = {};
			bool hitLastFrame_ = false;
			bool wasMouseDownOnCanvas_ = false;
			bool dirty_ = false;
			bool windowLocked_ = false;

			float snowHeightStart_ = 4.0f;
			float snowHeightEnd_ = 8.0f;
			float rockSlopeStart_ = 0.18f;
			float rockSlopeEnd_ = 0.48f;
			float noiseScale_ = 7.0f;
			float noiseStrength_ = 0.8f;

			static constexpr int kMaxUndo = 32;
			std::vector<std::vector<math::Vector4>> undoStack_;
			std::vector<math::Vector4> smoothTmp_;

			void PushUndo();
			void PopUndo();
			void ApplyBrushAtUV(float u, float v, float dt);
			void AutoGenerate();
			void ExportPNG(const char* path) const;

			bool ScreenToRay(float mx, float my, float sw, float sh,
			                 const Camera& cam,
			                 math::Vector3& outOrigin, math::Vector3& outDir) const;
			bool RaycastHeightmap(const math::Vector3& origin, const math::Vector3& dir,
			                      float& outU, float& outV, math::Vector3& outHit) const;
			void DrawBrushOverlay(const Camera& cam, float sw, float sh) const;
		};
	}
}
#endif // AQ_DEBUG_IMGUI
