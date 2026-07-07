#pragma once
#include "ECS/ECS.h"
#include "Graphics/InstancedStaticMesh.h"
#include <string>


namespace aq
{
	namespace ecs
	{
		/**
		 * 共有 InstancedStaticMesh のインスタンスとして描画されるエンティティに付けるコンポーネント。
		 * mesh は「登録名」または「モデルファイルパス(FBX/TKM 等)」で指定する:
		 *   - 名前が名前レジストリに登録済みなら、それを共有して使う(コードで RegisterNamed した場合)。
		 *   - 未登録なら**ファイルパスとみなして自動ロード+登録**する(名前=パスをキーに共有)。
		 * これによりコードを書かずにインスペクターでモデル(FBX)を差し替えられる。texture は任意。
		 * 配置は InstancedPointListComponent(同一エンティティ)の座標に従う。
		 */
		class InstancedStaticMeshComponent : public aq::ecs::IComponent
		{
			ecsComponent(aq::ecs::InstancedStaticMeshComponent);

		private:
			std::string meshName_;   // 登録名 or モデルパス(fbx/tkm)。空なら描画なし
			std::string texturePath_; // アルベドテクスチャ(任意。パス指定ロード時に使う)


		public:
			InstancedStaticMeshComponent()  {}
			~InstancedStaticMeshComponent() {}


		public:
			inline void SetMesh(const char* nameOrPath)  { meshName_    = nameOrPath ? nameOrPath : ""; }
			inline void SetTexture(const char* path)      { texturePath_ = path ? path : ""; }
			inline const std::string& GetMeshName() const { return meshName_; }

			// 共有メッシュを解決する。登録名ならそれを、未登録ならファイルパスとみなして
			// 自動ロード+登録する(名前=パスをキーに共有するので同一メッシュは1つに集約=1ドロー)。
			inline aq::graphics::InstancedStaticMesh* GetMesh() const
			{
				if (meshName_.empty()) { return nullptr; }

				if (auto* found = aq::graphics::InstancedStaticMesh::GetByName(meshName_.c_str())) {
					return found;   // 登録済み(コード登録 or 既にロード済み)
				}
				// 未登録 → パスとみなしてロード+登録。モデルはテクスチャ付きシェーダで描く。
				// texture を明示しなければ FBX 等のマテリアルからアルベドを自動取得する。
				return aq::graphics::InstancedStaticMesh::RegisterFromModel(
					meshName_.c_str(), meshName_.c_str(),
					texturePath_.empty() ? nullptr : texturePath_.c_str(),
					aq::graphics::StaticMesh::ShaderType::InstancedTextured);
			}

			// 永続フィールド: メッシュ名/パス + テクスチャ(JSON・インスペクター)。文字列は FieldPath。
			template <typename V>
			void Reflect(V& visitor)
			{
				visitor.FieldPath("mesh",    meshName_,    "mesh (name or fbx/tkm path)");
				visitor.FieldPath("texture", texturePath_, "texture (albedo)");
			}
		};
	}
}
