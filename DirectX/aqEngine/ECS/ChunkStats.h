#pragma once
#include <cstdint>
#include <vector>

// ECS の Chunk 使用状況の統計(設計書/アロケーター改良設計.md §0.3 P0-1)。
//
// AQ_ECS_CHUNK_STATS が定義されたときだけ計測する(CMake オプション AQ_ECS_CHUNK_STATS、既定 OFF)。
// Debug 専用にはしない。時間比較は最適化ビルド(Release)でも行うため。
// 未定義のときは公開関数が空の inline 関数になり、計測コードは残らない。
//
// 計測区間内ではログを出さない。CaptureChunkStats はメモリに溜めるだけで、
// 出力は DumpChunkStats の 1 回にまとめる。
#if defined(AQ_ECS_CHUNK_STATS)
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include "Platform/Common/DebugOutput.h"
#endif


namespace aq
{
	namespace ecs
	{
		/**
		 * 1 アーキタイプ(= 1 Chunk)分のスナップショット行
		 */
		struct ChunkStatsRow
		{
			/** chunkList_ の添字 */
			uint32_t chunkIndex = 0;
			/** アーキタイプのコンポーネント数 */
			uint32_t componentCount = 0;
			/** 体数(size) */
			uint32_t size = 0;
			/** 容量(capacity) */
			uint32_t capacity = 0;
			/** 1 体のバイト数(Archetype::GetArchetypeMemorySize) */
			uint64_t bytesPerEntity = 0;
			/** entityIDs_ の capacity と size */
			uint64_t entityIDCapacity = 0;
			uint64_t entityIDSize = 0;
			/** EntityID 1 個のバイト数 */
			uint64_t entityIDBytes = 0;
		};




		/**
		 * 再確保 1 回分の計測値(Chunk::ResetMemory が渡す)
		 */
		struct ChunkResizeSample
		{
			/** 拡張前後の容量 */
			uint32_t oldCapacity = 0;
			uint32_t newCapacity = 0;
			/** 1 体のバイト数 */
			uint64_t bytesPerEntity = 0;
			/** 移動した体数・コンポーネント要素数・バイト数 */
			uint64_t movedEntities = 0;
			uint64_t movedElements = 0;
			uint64_t movedBytes = 0;
			/** 非 trivial 型の move / destructor 呼び出し回数 */
			uint64_t moverCalls = 0;
			uint64_t destructorCalls = 0;
			/** 所要時間(ms)。確保・移動・旧バッファ解放を含む */
			double elapsedMs = 0.0;
			/** 内訳(ms)。新バッファの確保 / コンポーネントの移動 / 旧バッファの解放 */
			double allocMs = 0.0;
			double moveMs = 0.0;
			double freeMs = 0.0;
			/** アーキタイプの識別用(各コンポーネントのバイト数。move を持てば m、destructor を持てば d を付ける) */
			std::string components;
		};


#if defined(AQ_ECS_CHUNK_STATS)




		/**
		 * Chunk 統計の集計先(プロセスに 1 つ)
		 *
		 * スレッド安全性: 自前の軽量ミューテックス mutex_ で守る。
		 * RecordResize は EntityManager の iterationMutex_(ライトロック)保持中に呼ばれるので、
		 * ロック順は「iterationMutex_ → mutex_」に固定する。逆順を作らないため、
		 * スナップショット収集(collector_ が iterationMutex_ のリードロックを取る)は mutex_ を離してから呼ぶ。
		 */
		class ChunkStats
		{
		public:
			/** スナップショット収集関数(EntityManager が登録する) */
			using CollectFn = std::function<void(std::vector<ChunkStatsRow>&)>;

		private:
			/**
			 * スナップショット 1 回分
			 */
			struct Snapshot
			{
				std::string label;
				uint64_t frame = 0;
				std::vector<ChunkStatsRow> rows;
			};

			/** 再確保 1 回ごとの記録(原因調査用。出力は [resize_events]) */
			struct ResizeEvent
			{
				uint64_t frame = 0;
				ChunkResizeSample sample;
			};

			static constexpr size_t LINE_BUFFER_SIZE = 512;
			static constexpr size_t MAX_RESIZE_EVENTS = 1024;


		private:
			mutable std::mutex mutex_;

			/** 収集元 */
			CollectFn collector_;

			/** 溜めたスナップショット(DumpChunkStats で出力して空にする) */
			std::vector<Snapshot> snapshots_;

			/** フレーム番号(AdvanceChunkStatsFrame で進める。0 は初回の呼び出しより前。呼ばれなければ 0 のまま) */
			uint64_t frame_ = 0;
			bool frameStarted_ = false;
			std::chrono::steady_clock::time_point frameStart_;

			/** 再確保 1 回ごとの記録 */
			std::vector<ResizeEvent> resizeEvents_;

			/** 再確保の累計 */
			uint64_t resizeCount_ = 0;
			uint64_t peakTransientBytes_ = 0;
			double totalResizeMs_ = 0.0;
			uint64_t movedEntities_ = 0;
			uint64_t movedElements_ = 0;
			uint64_t movedBytes_ = 0;
			uint64_t moverCalls_ = 0;
			uint64_t destructorCalls_ = 0;

			/** 再確保時間の最大値と、その時点の情報 */
			double maxResizeMs_ = 0.0;
			uint64_t maxResizeFrame_ = 0;
			/** 最大値を記録したフレーム全体の時間(ms)。フレームが閉じるまでは負値(不明) */
			double maxResizeFrameMs_ = -1.0;
			uint32_t maxResizeOldCapacity_ = 0;
			uint32_t maxResizeNewCapacity_ = 0;
			uint64_t maxResizeBytesPerEntity_ = 0;


		private:
			ChunkStats() {}
			~ChunkStats() {}


		public:
			/** 収集元を登録する */
			inline void SetCollector(CollectFn collector)
			{
				std::lock_guard<std::mutex> lock(mutex_);
				collector_ = std::move(collector);
			}

			/** 収集元を外す */
			inline void ClearCollector()
			{
				std::lock_guard<std::mutex> lock(mutex_);
				collector_ = nullptr;
			}


		public:
			/**
			 * 再確保 1 回分を加算する(ログは出さない)
			 * @param sample Chunk::ResetMemory の計測値
			 */
			void RecordResize(const ChunkResizeSample& sample)
			{
				// 旧バッファと新バッファが共存する一時ピーク(1 Chunk 内)
				const uint64_t transientBytes =
					(static_cast<uint64_t>(sample.oldCapacity) + sample.newCapacity) * sample.bytesPerEntity;

				std::lock_guard<std::mutex> lock(mutex_);
				if (resizeEvents_.size() < MAX_RESIZE_EVENTS) {
					ResizeEvent event;
					event.frame  = frame_;
					event.sample = sample;
					resizeEvents_.push_back(std::move(event));
				}
				++resizeCount_;
				totalResizeMs_   += sample.elapsedMs;
				movedEntities_   += sample.movedEntities;
				movedElements_   += sample.movedElements;
				movedBytes_      += sample.movedBytes;
				moverCalls_      += sample.moverCalls;
				destructorCalls_ += sample.destructorCalls;
				if (transientBytes > peakTransientBytes_) {
					peakTransientBytes_ = transientBytes;
				}
				if (sample.elapsedMs > maxResizeMs_) {
					maxResizeMs_             = sample.elapsedMs;
					maxResizeFrame_          = frame_;
					maxResizeFrameMs_        = -1.0;
					maxResizeOldCapacity_    = sample.oldCapacity;
					maxResizeNewCapacity_    = sample.newCapacity;
					maxResizeBytesPerEntity_ = sample.bytesPerEntity;
				}
			}


			/**
			 * フレームを 1 つ進める。
			 * 前回の呼び出しからの経過時間をそのフレーム全体の時間とみなし、
			 * 再確保時間の最大値を記録したフレームならその時間を残す。
			 */
			void AdvanceFrame()
			{
				const auto now = std::chrono::steady_clock::now();
				std::lock_guard<std::mutex> lock(mutex_);
				if (frameStarted_ && resizeCount_ > 0 && maxResizeFrame_ == frame_) {
					maxResizeFrameMs_ = std::chrono::duration<double, std::milli>(now - frameStart_).count();
				}
				// フレーム 0 は「初回の呼び出しより前」。始点が無いので時間は取らない
				++frame_;
				frameStarted_ = true;
				frameStart_   = now;
			}


			/**
			 * 現時点のスナップショットを取り、メモリに溜める(ログは出さない)
			 * ECS の反復中・FlushCommands のコールバック内からは呼ばないこと
			 * (収集時に iterationMutex_ のリードロックを取るため、同じスレッドが保持中だとデッドロックする)。
			 * @param label 任意のラベル(「大量追加後」など)。nullptr は空文字列扱い
			 */
			void Capture(const char* label)
			{
				CollectFn collector;
				uint64_t frame = 0;
				{
					std::lock_guard<std::mutex> lock(mutex_);
					collector = collector_;
					frame     = frame_;
				}

				Snapshot snapshot;
				snapshot.label = (label != nullptr) ? label : "";
				snapshot.frame = frame;
				if (collector) {
					collector(snapshot.rows);
				}

				std::lock_guard<std::mutex> lock(mutex_);
				snapshots_.push_back(std::move(snapshot));
			}


			/**
			 * スナップショットを 1 つ取ってから、溜めた全スナップショットと再確保の累計を出力する。
			 * 出力したスナップショットは捨てる(再確保の累計は Reset まで残す)。
			 * @param label 今回取るスナップショットのラベル
			 */
			void Dump(const char* label)
			{
				Capture(label);

				std::vector<std::string> lines;
				{
					std::lock_guard<std::mutex> lock(mutex_);
					BuildReport(lines);
					snapshots_.clear();
				}

				// 出力はロックの外で行う(出力先の待ちで ECS の構造変更を止めない)
				for (const std::string& line : lines) {
					aq::debug::OutputString(line.c_str());
				}
			}


			/**
			 * 再確保の累計と溜めたスナップショットを捨てる(計測区間の開始に使う)。
			 * フレーム番号と収集元は残す。
			 */
			void Reset()
			{
				std::lock_guard<std::mutex> lock(mutex_);
				snapshots_.clear();
				resizeEvents_.clear();
				resizeCount_             = 0;
				peakTransientBytes_      = 0;
				totalResizeMs_           = 0.0;
				movedEntities_           = 0;
				movedElements_           = 0;
				movedBytes_              = 0;
				moverCalls_              = 0;
				destructorCalls_         = 0;
				maxResizeMs_             = 0.0;
				maxResizeFrame_          = 0;
				maxResizeFrameMs_        = -1.0;
				maxResizeOldCapacity_    = 0;
				maxResizeNewCapacity_    = 0;
				maxResizeBytesPerEntity_ = 0;
			}


		private:
			/**
			 * 出力テキストを組み立てる(TSV。スプレッドシートへそのまま貼れる形)
			 * mutex_ を保持して呼ぶこと。
			 */
			void BuildReport(std::vector<std::string>& lines) const
			{
				AppendLine(lines, "=== aq::ecs chunk stats (AQ_ECS_CHUNK_STATS) ===\n");

				// 再確保の累計(key \t value)
				AppendLine(lines, "[resize]\n");
				AppendLine(lines, "resize_count\t%llu\n", ToULL(resizeCount_));
				AppendLine(lines, "peak_transient_bytes\t%llu\n", ToULL(peakTransientBytes_));
				AppendLine(lines, "total_resize_ms\t%.4f\n", totalResizeMs_);
				AppendLine(lines, "max_resize_ms\t%.4f\n", maxResizeMs_);
				AppendLine(lines, "max_resize_frame\t%llu\n", ToULL(maxResizeFrame_));
				if (maxResizeFrameMs_ >= 0.0) {
					AppendLine(lines, "max_resize_frame_ms\t%.4f\n", maxResizeFrameMs_);
				} else {
					AppendLine(lines, "max_resize_frame_ms\tn/a\n");
				}
				AppendLine(lines, "max_resize_old_capacity\t%u\n", maxResizeOldCapacity_);
				AppendLine(lines, "max_resize_new_capacity\t%u\n", maxResizeNewCapacity_);
				AppendLine(lines, "max_resize_bytes_per_entity\t%llu\n", ToULL(maxResizeBytesPerEntity_));
				AppendLine(lines, "moved_entities\t%llu\n", ToULL(movedEntities_));
				AppendLine(lines, "moved_elements\t%llu\n", ToULL(movedElements_));
				AppendLine(lines, "moved_bytes\t%llu\n", ToULL(movedBytes_));
				AppendLine(lines, "mover_calls\t%llu\n", ToULL(moverCalls_));
				AppendLine(lines, "destructor_calls\t%llu\n", ToULL(destructorCalls_));

				// 再確保 1 回ごと(原因調査用)
				AppendLine(lines, "[resize_events]\n");
				AppendLine(lines,
					"frame\told_cap\tnew_cap\tbytes_per_entity\tmoved_entities\ttotal_ms\talloc_ms\tmove_ms\tfree_ms"
					"\tmover_calls\tdtor_calls\tcomponents\n");
				for (const ResizeEvent& event : resizeEvents_) {
					const ChunkResizeSample& r = event.sample;
					AppendLine(lines, "%llu\t%u\t%u\t%llu\t%llu\t%.4f\t%.4f\t%.4f\t%.4f\t%llu\t%llu\t%s\n",
						ToULL(event.frame), r.oldCapacity, r.newCapacity, ToULL(r.bytesPerEntity),
						ToULL(r.movedEntities), r.elapsedMs, r.allocMs, r.moveMs, r.freeMs,
						ToULL(r.moverCalls), ToULL(r.destructorCalls), r.components.c_str());
				}

				// スナップショットごとの合計(1 スナップショット 1 行)
				AppendLine(lines, "[snapshot_summary]\n");
				AppendLine(lines,
					"label\tframe\tarchetypes\tempty_archetypes\tentities\talloc_bytes\tused_bytes\tusage_pct"
					"\tentity_id_capacity\tentity_id_size\tentity_id_alloc_bytes\n");
				for (const Snapshot& snapshot : snapshots_) {
					uint64_t emptyCount = 0;
					uint64_t entities = 0;
					uint64_t allocBytes = 0;
					uint64_t usedBytes = 0;
					uint64_t idCapacity = 0;
					uint64_t idSize = 0;
					uint64_t idAllocBytes = 0;
					for (const ChunkStatsRow& row : snapshot.rows) {
						if (row.size == 0) {
							++emptyCount;
						}
						entities     += row.size;
						allocBytes   += static_cast<uint64_t>(row.capacity) * row.bytesPerEntity;
						usedBytes    += static_cast<uint64_t>(row.size) * row.bytesPerEntity;
						idCapacity   += row.entityIDCapacity;
						idSize       += row.entityIDSize;
						idAllocBytes += row.entityIDCapacity * row.entityIDBytes;
					}
					const double usagePct = (allocBytes > 0)
						? 100.0 * static_cast<double>(usedBytes) / static_cast<double>(allocBytes)
						: 0.0;
					AppendLine(lines, "%s\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%.2f\t%llu\t%llu\t%llu\n",
						snapshot.label.c_str(), ToULL(snapshot.frame),
						ToULL(snapshot.rows.size()), ToULL(emptyCount), ToULL(entities),
						ToULL(allocBytes), ToULL(usedBytes), usagePct,
						ToULL(idCapacity), ToULL(idSize), ToULL(idAllocBytes));
				}

				// アーキタイプごとの分布(1 Chunk 1 行。全スナップショットを 1 つの表にする)
				AppendLine(lines, "[snapshot_rows]\n");
				AppendLine(lines,
					"label\tframe\tchunk\tcomponents\tsize\tcapacity\tbytes_per_entity\talloc_bytes\tused_bytes"
					"\tentity_id_capacity\tentity_id_size\n");
				for (const Snapshot& snapshot : snapshots_) {
					for (const ChunkStatsRow& row : snapshot.rows) {
						AppendLine(lines, "%s\t%llu\t%u\t%u\t%u\t%u\t%llu\t%llu\t%llu\t%llu\t%llu\n",
							snapshot.label.c_str(), ToULL(snapshot.frame),
							row.chunkIndex, row.componentCount, row.size, row.capacity,
							ToULL(row.bytesPerEntity),
							ToULL(static_cast<uint64_t>(row.capacity) * row.bytesPerEntity),
							ToULL(static_cast<uint64_t>(row.size) * row.bytesPerEntity),
							ToULL(row.entityIDCapacity), ToULL(row.entityIDSize));
					}
				}

				AppendLine(lines, "=== end chunk stats ===\n");
			}




		private:
			/** printf 書式で 1 行足す */
			static void AppendLine(std::vector<std::string>& lines, const char* format, ...)
			{
				char buffer[LINE_BUFFER_SIZE];
				va_list args;
				va_start(args, format);
				vsnprintf(buffer, sizeof(buffer), format, args);
				va_end(args);
				lines.emplace_back(buffer);
			}

			/** %llu 用の変換(uint64_t の実体型がプラットフォームで違うため) */
			static unsigned long long ToULL(const uint64_t value)
			{
				return static_cast<unsigned long long>(value);
			}


		public:
			/** 集計先を取得する */
			static ChunkStats& Get()
			{
				static ChunkStats instance;
				return instance;
			}
		};




		/**
		 * 現時点の全 Chunk のスナップショットを取り、メモリに溜める(ログは出さない)
		 * ECS の反復中・FlushCommands のコールバック内からは呼ばないこと。
		 * @param label 任意のラベル(例: "after_spawn" / "after_destroy")
		 */
		inline void CaptureChunkStats(const char* label)
		{
			ChunkStats::Get().Capture(label);
		}

		/**
		 * スナップショットを 1 つ取り、溜めた分と再確保の累計をまとめて出力する
		 * 出力先は aq::debug::OutputString(Win32/UWP = OutputDebugStringA、Apple = stderr、Android = logcat)。
		 * @param label 今回取るスナップショットのラベル
		 */
		inline void DumpChunkStats(const char* label = "dump")
		{
			ChunkStats::Get().Dump(label);
		}

		/** 統計のフレーム番号を 1 つ進める(フレームの区切りで 1 回呼ぶ) */
		inline void AdvanceChunkStatsFrame()
		{
			ChunkStats::Get().AdvanceFrame();
		}

		/** 再確保の累計と溜めたスナップショットを捨てる(計測区間の開始に呼ぶ) */
		inline void ResetChunkStats()
		{
			ChunkStats::Get().Reset();
		}


#else


		// 計測無効時は何もしない(呼び出し側に #if を書かせないための空実装)
		inline void CaptureChunkStats(const char* label) { (void)label; }
		inline void DumpChunkStats(const char* label = "dump") { (void)label; }
		inline void AdvanceChunkStatsFrame() {}
		inline void ResetChunkStats() {}


#endif
	}
}
