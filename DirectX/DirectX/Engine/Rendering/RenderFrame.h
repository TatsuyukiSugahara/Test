#pragma once
#include "../Math/Matrix.h"
#include "../Math/Vector.h"
#include <vector>


class IShaderResourceView;

namespace engine
{
	namespace graphics
	{
		class IVertexBuffer;
		class IIndexBuffer;
		class ISamplerState;
		class IConstantBuffer;
		class IShader;
		class IShaderResourceView;
	}

	namespace rendering
	{
		/** カメラのスナップショット（ゲームスレッドでコピー） */
		struct CameraData
		{
			math::Matrix4x4 viewMatrix;
			math::Matrix4x4 projectionMatrix;
			math::Vector3   position;
		};


		/**
		 * 1 ドローコールに必要な情報のスナップショット。
		 * GPU リソースへの生ポインタはフレーム中に StaticMesh が生きている前提で有効。
		 * RenderThread 化する際はリソースハンドル方式へ置き換える。
		 */
		struct RenderItem
		{
			graphics::IVertexBuffer*        vertexBuffer   = nullptr;
			graphics::IIndexBuffer*         indexBuffer    = nullptr;
			graphics::ISamplerState*        samplerState   = nullptr;
			graphics::IConstantBuffer*      constantBuffer = nullptr;
			graphics::IShader*              vs             = nullptr;
			graphics::IShader*              ps             = nullptr;
			graphics::IShaderResourceView*  texture        = nullptr;
			uint32_t                        indexCount     = 0;
			math::Matrix4x4                 worldMatrix;
			uint32_t                        layer          = 0;
		};


		/**
		 * 1 フレームの描画スナップショット。
		 * ゲームスレッドが構築し、Renderer が消費する。
		 */
		struct RenderFrame
		{
			CameraData              camera;
			std::vector<RenderItem> items;

			void Clear() { items.clear(); }
		};
	}
}
