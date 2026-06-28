/**
 * 階層スコープ プロファイラ
 *
 * 使い方:
 *   void Foo()
 *   {
 *       AQ_PROFILE_FUNCTION();          // 関数名でスコープ計測
 *       {
 *           AQ_PROFILE_SCOPE("SubTask"); // 任意名でスコープ計測
 *           // ...
 *       }
 *   }
 *
 * - スコープの入れ子は「親 → 子」の階層として記録される。
 * - スレッドごとに独立して記録され、ImGui パネルでスレッド別に表示できる。
 * - 計測は AQ_DEBUG_IMGUI 定義時のみ有効 (リリースでは完全に消える)。
 *
 * フレーム境界とスレッド:
 *   - 各スレッドは自身の samples に書き込み、フレーム境界で display へ publish する。
 *   - メインスレッドは PublishThisThread() で自身を、PublishWorkers() で
 *     アイドル状態のワーカースレッドをまとめて publish する。
 *   - レンダースレッドは selfPublishing として自分で PublishThisThread() を呼ぶ。
 */
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <memory>
#include <atomic>

// 計測マクロの有効/無効。デバッグ ImGui ビルドでのみ計測する。
#if defined(AQ_DEBUG_IMGUI)
#define AQ_PROFILE_ENABLED 1
#endif

namespace aq
{
	namespace profile
	{
		/** 1 スコープ分の計測結果 */
		struct Sample
		{
			const char* name        = "";
			int         parentIndex = -1;  // 同一スレッド samples 内の親インデックス (-1 = ルート)
			int         depth        = 0;
			double      durationMs   = 0.0;
			int64_t     startTick     = 0;  // 内部用 (QueryPerformanceCounter 値)
		};


		/** UI 表示用の 1 スレッド分スナップショット */
		struct ThreadFrame
		{
			uint64_t            threadId   = 0;
			int                 orderIndex = 0;  // 登録順 (表示順の安定化用)
			std::string         name;
			std::vector<Sample> samples;
		};


		/**
		 * プロファイラ本体 (Meyers シングルトン)。
		 * スレッドセーフ。スコープ計測はロックフリー (各スレッド自身の領域のみ操作)。
		 */
		class Profiler
		{
		public:
			static Profiler& Get();

			/** 呼び出しスレッドの表示名を設定する */
			void SetThreadName(const char* name);

			/** 呼び出しスレッドを「自己 publish 型」にする (レンダースレッド等) */
			void MarkSelfPublishing();

			// --- 計測 (マクロ経由で呼ぶ) ---
			void PushScope(const char* name);
			void PopScope();

			// --- フレーム境界 ---
			/** 呼び出しスレッドの samples を display へ publish しクリアする */
			void PublishThisThread();
			/** メインスレッドが、自己 publish 型でない他スレッド (ワーカー) を publish する */
			void PublishWorkers();

			// --- UI 取得 ---
			/** 全スレッドの display を out へコピーする */
			void CaptureSnapshot(std::vector<ThreadFrame>& out);

			/** QueryPerformanceCounter の 1 tick あたりのミリ秒。startTick の差分換算に使う。 */
			double MsPerTick() const { return ticksToMs_; }

			/**
			 * 直近 1 フレームの実時間 (ms)。PublishWorkers() 呼び出し間隔 = メインスレッドの
			 * フレーム周期で計測する（FlushRender の待ちも含む実フレーム時間）。
			 */
			double FrameMs() const { return frameMs_; }

		private:
			struct ThreadData
			{
				uint64_t            threadId       = 0;
				int                 orderIndex     = 0;
				std::string         name;
				std::vector<Sample> samples;          // 所有スレッドのみが書き込む
				std::vector<int>    stack;            // 開いているスコープの index スタック
				std::mutex          publishMutex;     // display / history 保護
				std::vector<Sample> display;          // publish 済み 最新 (UI が読む)
				// selfPublishing スレッド (Render) のみ: 直近数フレームの publish 履歴。
				// 非同期パイプラインでは Render の最新フレームが Main の表示フレームと別周期に
				// なり得るため、CaptureSnapshot で「同じ周期に走っていたフレーム」を選ぶのに使う。
				std::vector<std::vector<Sample>> history;
				bool                selfPublishing = false;
			};

			ThreadData& Local();

			std::mutex                               registryMutex_;
			std::vector<std::shared_ptr<ThreadData>> threads_;
			std::atomic<int>                         nextOrderIndex_ { 0 };
			double                                   ticksToMs_ = 0.0;  // QPC frequency 由来

			// フレーム時間計測 (メインスレッドのみが PublishWorkers() で更新・読み取り)
			int64_t                                  lastFramePublishTick_ = 0;
			double                                   frameMs_              = 0.0;

			Profiler();
		};


		/** RAII スコープ計測。コンストラクタで Push、デストラクタで Pop。 */
		class ScopedTimer
		{
		public:
			explicit ScopedTimer(const char* name) { Profiler::Get().PushScope(name); }
			~ScopedTimer()                          { Profiler::Get().PopScope(); }

			ScopedTimer(const ScopedTimer&)            = delete;
			ScopedTimer& operator=(const ScopedTimer&) = delete;
		};
	}
}


// --- 計測マクロ ---
#define AQ_PROFILE_CONCAT_INNER(a, b) a##b
#define AQ_PROFILE_CONCAT(a, b)       AQ_PROFILE_CONCAT_INNER(a, b)

#if defined(AQ_PROFILE_ENABLED)
	#define AQ_PROFILE_SCOPE(name) \
		aq::profile::ScopedTimer AQ_PROFILE_CONCAT(aqProfScope_, __LINE__)(name)
	#define AQ_PROFILE_FUNCTION() AQ_PROFILE_SCOPE(__FUNCTION__)
#else
	#define AQ_PROFILE_SCOPE(name) ((void)0)
	#define AQ_PROFILE_FUNCTION()  ((void)0)
#endif
