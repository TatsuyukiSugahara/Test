#pragma once
#ifdef AQ_DEBUG_IMGUI

#include <vector>
#include <cstdint>
#include <memory>
#include "Math/Vector.h"
#include "Core/IDebugRenderable.h"
#include "Graphics/IShaderResourceView.h"

namespace aq
{
	class Camera;

	namespace terrain
	{
		class HeightmapChunk;

		/**
		 * ハイトマップペイントエディタ。
		 *
		 * 使い方:
		 *   painter_.Attach(chunk, terrainWorldOffset);
		 *   DebugUI::Get().Register(&painter_);   // BattleScene::Initialize()
		 *   DebugUI::Get().Unregister(&painter_); // BattleScene::Finalize()
		 *
		 * DebugRender() が毎フレーム呼ばれ、ImGui パネルと 3D ビューペイントを処理する。
		 * Ctrl+Z でアンドゥ、[Export PNG] でファイル保存。
		 */
		class HeightmapPainter : public IDebugRenderable
		{
		public:
			enum class BrushMode : uint8_t { Raise, Lower, Smooth, Flatten };

			HeightmapPainter()  = default;
			~HeightmapPainter() override;

			/** chunk: HeightmapChunk の参照 (ライフタイムは外部管理)
			 *  worldOffset: TransformComponent.localPosition (地形メッシュのワールドオフセット)
			 *  offsetPtr:   TC の localPosition へのポインタ。SetTerrainSize 時に自動更新される */
			void Attach(HeightmapChunk* chunk, math::Vector3 worldOffset,
			            math::Vector3* offsetPtr = nullptr);
			void Detach();

			// IDebugRenderable
			void DebugRenderMenu() override;
			void DebugRender() override;
			const char* GetDebugLabel() const override { return "Heightmap Painter"; }

		public:
			BrushMode brushMode_     = BrushMode::Raise;
			float     brushRadius_   = 10.0f;    // world units
			float     brushStrength_ = 0.5f;
			float     flattenTarget_ = 0.0f;     // [0,1] 正規化高さ
			bool      enabled_        = false;    // ツール全体の有効フラグ
			bool      paint3DEnabled_ = true;    // 3D ビュー直接ペイント有効フラグ
			bool      show_           = true;    // ウィンドウ表示フラグ

		private:
			HeightmapChunk* chunk_       = nullptr;
			math::Vector3   worldOffset_ = {};
			math::Vector3*  offsetPtr_   = nullptr;  // BattleScene TC の localPosition

			// preview texture: API 非依存の IShaderResourceView
			// RGBA8 フォーマットでグレースケールプレビューを保持する
			std::unique_ptr<graphics::IShaderResourceView> previewSrv_;
			std::vector<uint8_t> previewPixels_;   // CPU 側 RGBA8 バッファ (w*h*4)
			uint32_t previewW_ = 0;
			uint32_t previewH_ = 0;
			void CreatePreviewTexture(uint32_t w, uint32_t h);
			void ReleasePreviewTexture();
			void UpdatePreviewTexture();

			// 3D brush hit state (DrawBrushOverlay 用)
			math::Vector3 lastHitWorld_ = {};
			bool          hitLastFrame_  = false;

			// 2D キャンバス上での前フレームマウス状態
			bool          wasMouseDownOnCanvas_ = false;

			// dirty: RebuildFromHeights() を次フレームで呼ぶ
			bool dirty_        = false;
			bool windowLocked_ = false;

			// アンドゥスタック (最大 32 スナップショット)
			static constexpr int kMaxUndo = 32;
			std::vector<std::vector<float>> undoStack_;
			void PushUndo();
			void PopUndo();

			// ブラシ適用
			void ApplyBrushAtUV(float u, float v, float dt);
			std::vector<float> smoothTmp_;   // Smooth ブラシ用一時バッファ

			// スクリーン → レイ変換
			bool ScreenToRay(float mx, float my, float sw, float sh,
			                 const Camera& cam,
			                 math::Vector3& outOrigin, math::Vector3& outDir) const;

			// レイ vs 高さフィールド交差 (ステップマーチング)
			bool RaycastHeightmap(const math::Vector3& origin, const math::Vector3& dir,
			                      float& outU, float& outV, math::Vector3& outHit) const;

			// 3D ブラシ円オーバーレイ (ForegroundDrawList に投影)
			void DrawBrushOverlay(const Camera& cam, float sw, float sh) const;

			// PNG エクスポート
			void ExportPNG(const char* path) const;
		};
	}
}
#endif // AQ_DEBUG_IMGUI
