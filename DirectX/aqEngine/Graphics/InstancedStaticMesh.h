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
			// per-instance データ(slot1 頂点ストリーム。InstancedSimple.fx の I_WORLD0..3 / I_COLOR と一致)。
			// world は「転置済み」で格納する(float4x4(行) が CB 経路と同姿勢になるため)。
			// color はそのまま格納する(RGBA)。
			struct InstanceData
			{
				math::Matrix4x4 world;
				math::Vector4   color;
			};
			static_assert(sizeof(InstanceData) == 80, "InstanceData は 80B(float4x4 + float4)であること");

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

			/** 共有ジオメトリのローカル AABB(明示指定時のみ。per-instance カリングに使う) */
			math::AABB                     localBounds_;
			bool                           hasLocalBounds_ = false;

			/** 風揺れ(草など。有効なときだけ描画で b2 の WindCB を積む) */
			bool                           windEnabled_    = false;
			float                          windStrength_   = 0.0f;                            // 先端の揺れ幅 [m]
			float                          windFrequency_  = 0.0f;                            // 揺れの周波数
			math::Vector3                  windDirection_  = math::Vector3(1.0f, 0.0f, 0.0f); // 風向(正規化済み)


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

			/** gather: この mesh のインスタンスを1件積む(world は非転置で渡す。内部で転置格納)。色は白。 */
			void AddInstance(const math::Matrix4x4& world);

			/** gather: 色付きでインスタンスを1件積む(world は非転置で渡す。内部で転置格納)。 */
			void AddInstance(const math::Matrix4x4& world, const math::Vector4& color);

			/** gather: ベイク済み InstanceData をまとめて追加する(行列計算・転置は呼び出し側で済ませておく)。 */
			void AddInstances(const InstanceData* data, const uint32_t count);

			/**
			 * 共有ジオメトリの明示ローカル AABB を設定する。
			 * 空間は per-instance のワールド行列を掛ける前のメッシュローカル空間
			 * (StaticMesh::SetLocalBounds と同一契約)。設定すると per-instance
			 * フラスタムカリングの対象になる。
			 */
			inline void SetLocalBounds(const math::AABB& aabb) { localBounds_ = aabb; hasLocalBounds_ = true; }

			/** 明示ローカル AABB。未設定なら既定値(潰れた AABB)。 */
			inline const math::AABB& GetLocalBounds() const { return localBounds_; }

			/** 明示ローカル AABB が設定済みか */
			inline bool HasLocalBounds() const { return hasLocalBounds_; }

			/**
			 * 風揺れパラメータを設定する(設定した時点で風揺れが有効になる)。
			 * @param strength  先端の揺れ幅 [m]
			 * @param frequency 揺れの周波数
			 * @param direction 風向(正規化できないときは既定の +X を使う)
			 */
			void SetWindParams(const float strength, const float frequency, const math::Vector3& direction);

			/** 風揺れパラメータを持つか */
			inline bool HasWind() const { return windEnabled_; }

			/** 毎フレーム必ず呼ぶ。pending を動的VBへ書込み instanceCount_ を確定し、retired を計時する。 */
			void FlushInstances();

			/** 描画1件分を out に詰める。count==0 / 未準備なら false。time はゲームスレッドで採った経過時間 [s]。 */
			bool FillInstancedRenderItem(rendering::InstancedRenderItem& out, const float time) const;


			// ── 静的レジストリ(全メッシュ一括処理) ──
			/** shared_ptr で生成し、レジストリ(weak_ptr)へ登録する。 */
			static std::shared_ptr<InstancedStaticMesh> Create();
			/** 登録済み全メッシュを毎フレーム Flush する(gather の最後に呼ぶ)。 */
			static void FlushAllRegistered();
			/** 登録済み全メッシュのうち count>0 の描画アイテムを out へ追加する。time はゲームスレッドで採った経過時間 [s]。 */
			static void CollectRenderItems(std::vector<rendering::InstancedRenderItem>& out, const float time);

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
