#pragma once
#include <cstdint>
#include <string>


namespace editor
{
	class PlayModeController;




	/**
	 * Play/Stop 自己テスト。
	 * 再生 → 120 フレーム → 停止 → 復元の完了を待つ → 指紋を取る、を 5 回繰り返し、毎回の指紋を再生前の指紋と比べて
	 * 結果を StartupLog(startup_timing.log)へ出す。進行は EditorLayer::OnBeginFrame から毎フレーム 1 回 Tick で行う
	 */
	class PlayStopSelfTest
	{
	public:
		/** 繰り返す回数 */
		static constexpr int CYCLE_COUNT = 5;


	private:
		/** 進行の段階 */
		enum class Phase : uint8_t
		{
			Idle,
			Starting,       // 開始を受け付けた。次の Tick で Edit のワールドを準備する
			WaitBaseline,   // 準備した Edit のワールドの生成を待って基準の指紋を取る
			StartCycle,     // 1 回分を始める(再生を要求する)
			WaitPlay,       // 再生を要求した。Play になるのを待つ
			Playing,        // 再生中。決まったフレーム数を待って停止を要求する
			WaitStop,       // 停止を要求した。Edit になるのを待つ
			WaitRestore,    // 停止した。復元の完了を待って指紋を比べる
		};


		/** ワールドの指紋。項目ごとに比べる */
		struct Fingerprint
		{
			std::string editModel;    // 編集モデルの JSON
			std::string entities;     // エンティティの木(名前に頼らない正規化文字列)
			std::string levels;       // プレビュー以外のルート Level の出どころとプレビューの有無
			int         uiStackSize   = 0;
			uint32_t    activeVoices  = 0;
		};


	private:
		/** 進行の状態 */
		Phase    phase_        = Phase::Idle;
		int      cycle_        = 0;        // 今の回(0 始まり)
		uint32_t frameCount_   = 0;        // 今の段階に入ってからのフレーム数
		int      settleFrames_ = -1;       // 生成がそろってからのフレーム数(-1 はまだそろっていない)
		bool     passed_       = true;
		bool     exitOnFinish_ = false;    // 起動引数で始めた(終わったら終了コードを返して終了する)

		/** 再生前の指紋 */
		Fingerprint baseline_;


	public:
		/**
		 * 自己テストを始める。準備は次の Tick(フレームの先頭)で行うので、UI の構築中に呼んでよい。
		 * そのとき Edit でなければ失敗として記録して終わる。
		 * @param exitOnFinish 終わったらアプリを終了する(起動引数 -editor-selftest で始めたとき)
		 */
		void Start(const bool exitOnFinish);

		/** 実行中か */
		inline bool IsRunning() const { return phase_ != Phase::Idle; }

		/** 今の回(表示用。1 始まり) */
		inline int GetDisplayCycle() const { return cycle_ + 1; }

		/**
		 * 1 フレーム進める。EditorLayer::OnBeginFrame で、PlayModeController::ApplyPendingRequest の前に呼ぶ
		 * (ECS の反復の外なので、ここで指紋を取ってよい)
		 */
		void Tick(PlayModeController& controller);


	private:
		/** 基準の指紋を取る前の準備(Playground の Level とプレビュー)。失敗したら false */
		bool PrepareWorld();

		/** 生成がそろってから 2 フレーム経ったか(呼ぶたびに数える) */
		bool AdvanceSettle(const bool spawned);

		/** 1 回分の比較の結果を記録し、次の回か終わりへ進める */
		void FinishCycle(const bool matched, const std::string& diffItems);

		/** 全体の結果を出して終える */
		void Finish();

		/** 失敗として終える(準備の失敗など) */
		void Abort(const char* reason);


	private:
		/** Edit のワールド(全ルート Level とプレビュー)が生成済みか */
		static bool IsEditWorldSpawned();

		/** 今のワールドの指紋 */
		static Fingerprint TakeFingerprint();

		/** 違った項目の名前を空白区切りで返す(一致なら空)。違った項目は長さとハッシュをログに出す */
		static std::string Compare(const Fingerprint& expected, const Fingerprint& actual);
	};
}
