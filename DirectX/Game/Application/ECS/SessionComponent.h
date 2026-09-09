#pragma once
#include "Stage/StageData.h"


namespace app
{
	namespace ecs
	{
		/**
		 * 1 プレイセッションの共有状態。セッションエンティティ 1 体だけが持つ。
		 * 生成は GameFlow::Initialize、破棄は GameFlow::Finalize で、ステージ再入場では破棄しない。
		 * 取得は aq::ecs::EntityContext::GetSingletonComponent<SessionComponent>() で行う。
		 *
		 * スレッド規約: 書き込みはメインスレッドの GameFlow 状態クラスだけが行い、
		 * System はワーカースレッドから const で読み取るだけに留める。
		 * 状態機械の進行にしか使わないデータ (選択ステージ / プレイ結果 / ステージ一覧 /
		 * 生成物ハンドル) は共有せず GameFlow の私有メンバに置く。
		 */
		struct SessionComponent : public aq::ecs::IComponent
		{
			ecsComponent(app::ecs::SessionComponent);

			/** 進行 */
			bool gameplayPaused = false;   // true でゲーム System (走行/判定) を停止 (リザルト用)

			/** ステージ */
			std::shared_ptr<stage::StageData> activeStage;   // ロード済みステージ定義 (不変)

			/** ワールド */
			aq::ecs::EntityHandle playerHandle;          // プレイヤー
			aq::ecs::EntityHandle collectFxHandle;       // コイン取得エフェクトの常駐エミッタ
			aq::ecs::EntityHandle coinInstancesHandle;   // コインのインスタンス描画エンティティ

			/** ミニマップ (コース XZ 範囲 → 0-1 正規化のパラメータ) */
			aq::math::Vector2 minimapCenterXZ;            // コース範囲の中心 (XZ)
			float             minimapHalfExtent = 1.0f;   // 正方形マップに収める半径 [m]

			// 実行時専用。永続フィールドを持たないので Reflect は空。
			template <typename V>
			void Reflect(V&) {}
		};
	}
}
