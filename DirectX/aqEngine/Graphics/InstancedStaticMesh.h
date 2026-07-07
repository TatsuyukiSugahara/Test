#pragma once
#include <memory>
#include <vector>
#include "IBuffer.h"
#include "IShader.h"
#include "StaticMesh.h"                 // ShaderType
#include "Resource/Resource.h"
#include "Math/Matrix.h"
#include "Rendering/RenderFrame.h"


namespace aq
{
	namespace graphics
	{
		/**
		 * 同一ジオメトリを大量に描くための共有メッシュ。共有 VB/IB と per-instance の
		 * ワールド行列(動的VB・フレームリング)を持ち、instanceCount 個を1ドローで描く。
		 *
		 * 使い方(1フレーム):
		 *   1. gather 中に各エンティティが AddInstance(world) を積む(単一スレッド区間)。
		 *   2. FlushInstances() を毎フレーム必ず呼ぶ(count==0 でも instanceCount_ を確定)。
		 *   3. FillInstancedRenderItem() で描画1件分を得る(count==0 なら false)。
		 * 複数メッシュは静的レジストリ(weak_ptr)で管理し、FlushAllRegistered /
		 * CollectRenderItems で一括処理する(C-1 の共有パターンと同型)。
		 */
		class InstancedStaticMesh
		{
		public:
			// per-instance データ(slot1 頂点ストリーム。InstancedSimple.fx の I_WORLD0..3 と一致)。
			// world は「転置済み」で格納する(float4x4(行) が CB 経路と同姿勢になるため)。
			struct InstanceData
			{
				math::Matrix4x4 world;
			};
			static_assert(sizeof(InstanceData) == 64, "InstanceData は 64B(float4x4)であること");

		private:
			// 再確保した旧インスタンスVBを in-flight ぶん保持してから解放する(局所遅延破棄)。
			struct RetiredBuffer
			{
				std::shared_ptr<IVertexBuffer> buffer;
				int                            framesLeft;
			};

			/** 共有ジオメトリ + シェーダ */
			std::shared_ptr<IVertexBuffer> vertexBuffer_;
			std::shared_ptr<IIndexBuffer>  indexBuffer_;
			uint32_t                       indexCount_ = 0;
			aq::res::RefShaderResource     vs_;
			aq::res::RefShaderResource     ps_;

			/** FBX/TKM 等の遅延ロード元(完了後に VB/IB を構築する)。生データ初期化時は空。 */
			aq::res::RefMeshResource       pendingMesh_;
			/** アルベドテクスチャ(任意)+ サンプラー。テクスチャ付きシェーダで使う。 */
			aq::res::RefGPUResource        albedoResource_;
			std::shared_ptr<ISamplerState> sampler_;

			/** per-instance(動的・フレームリング) */
			std::shared_ptr<IVertexBuffer> instanceBuffer_;
			uint32_t                       instanceCapacity_ = 0;   // 動的VBの容量(インスタンス数)
			uint32_t                       instanceCount_    = 0;   // 直近 Flush で確定した数
			std::vector<InstanceData>      pendingInstances_;       // gather 中の積み先(capacity 維持)
			std::vector<RetiredBuffer>     retiredBuffers_;         // 遅延破棄待ちの旧VB


		public:
			InstancedStaticMesh()  = default;
			~InstancedStaticMesh() = default;

			/** 自作頂点(Box 等)で初期化する(即時)。 */
			void Initialize(const void* vertexBuffer, uint32_t vertexNum, uint32_t vertexStride,
			                const void* indexBuffer,  uint32_t indexNum,
			                StaticMesh::ShaderType shaderType);

			/** ロード済みメッシュ(FBX/TKM 等)で初期化する。メッシュは非同期ロードのため、
			 *  VB/IB の構築は完了後(FlushInstances 内)に行う。albedo は任意(テクスチャ付きシェーダ用)。 */
			void Initialize(aq::res::RefMeshResource meshResource, aq::res::RefGPUResource albedo,
			                StaticMesh::ShaderType shaderType);

			/** gather: この mesh のインスタンスを1件積む(world は非転置で渡す。内部で転置格納)。 */
			void AddInstance(const math::Matrix4x4& world);

			/** 毎フレーム必ず呼ぶ。pending を動的VBへ書込み instanceCount_ を確定し、retired を計時する。 */
			void FlushInstances();

			/** 描画1件分を out に詰める。count==0 / 未準備なら false。 */
			bool FillInstancedRenderItem(rendering::InstancedRenderItem& out) const;


			// ── 静的レジストリ(全メッシュ一括処理) ──
			/** shared_ptr で生成し、レジストリ(weak_ptr)へ登録する。 */
			static std::shared_ptr<InstancedStaticMesh> Create();
			/** 登録済み全メッシュを毎フレーム Flush する(gather の最後に呼ぶ)。 */
			static void FlushAllRegistered();
			/** 登録済み全メッシュのうち count>0 の描画アイテムを out へ追加する。 */
			static void CollectRenderItems(std::vector<rendering::InstancedRenderItem>& out);

			// ── 名前レジストリ(JSON/コンポーネントからの共有メッシュ参照) ──
			/** 名前で共有メッシュを登録する(shared_ptr を保持=アプリ寿命で生存)。 */
			static void RegisterNamed(const char* name, std::shared_ptr<InstancedStaticMesh> mesh);
			/** 名前で共有メッシュを引く。未登録なら nullptr。 */
			static InstancedStaticMesh* GetByName(const char* name);

			/**
			 * モデルパス(FBX/TKM 等)から名前付きインスタンスメッシュを登録する薄いヘルパ。
			 * texturePath は任意(nullptr 可)。既に同名が登録済みならそれを返す(重複登録しない)。
			 * デバイス準備後・ForEach 外の安全点で呼ぶこと。戻り値は登録された(or 既存の)メッシュ。
			 */
			static InstancedStaticMesh* RegisterFromModel(const char* name, const char* modelPath,
			                                              const char* texturePath = nullptr,
			                                              StaticMesh::ShaderType shaderType = StaticMesh::ShaderType::InstancedTextured);

			/**
			 * 自作頂点データ(Box 等)から名前付きインスタンスメッシュを登録する薄いヘルパ。
			 * 既に同名が登録済みならそれを返す。デバイス準備後・ForEach 外の安全点で呼ぶこと。
			 */
			static InstancedStaticMesh* RegisterFromData(const char* name,
			                                             const void* vertexBuffer, uint32_t vertexNum, uint32_t vertexStride,
			                                             const void* indexBuffer,  uint32_t indexNum,
			                                             StaticMesh::ShaderType shaderType = StaticMesh::ShaderType::InstancedSimple);
		};
	}
}
