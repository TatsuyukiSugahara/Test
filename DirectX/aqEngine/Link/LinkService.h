#pragma once
#include "Link/LinkSocket.h"
#include "Util/SimpleJson.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>


namespace aq
{
	namespace link
	{
		/** 通信プロトコルのバージョン(hello / runtime.info で返す) */
		static constexpr uint32_t LINK_PROTOCOL_VERSION = 1;




		/**
		 * 命令を実行している要求の情報
		 */
		struct LinkRequestContext
		{
			/** 要求を送ってきた接続の世代 ID(応答やイベントの宛先) */
			uint64_t connectionId = 0;
			/** 命令名 */
			const char* command = "";
		};




		/**
		 * 命令ハンドラの戻り値。成功なら result、失敗ならエラーコードとメッセージを持つ
		 */
		struct LinkResult
		{
			bool            ok = true;
			util::JsonValue result;
			/** bad_request / unknown_command / invalid_params / not_found / failed */
			std::string     errorCode;
			std::string     errorMessage;


			/** 成功(result を省略したら空のオブジェクト) */
			static LinkResult Ok(util::JsonValue value = util::JsonValue::MakeObject());

			/** 失敗 */
			static LinkResult Error(const char* code, std::string message);
		};




		/**
		 * エディタ連携の命令受付(TCP + 改行区切り JSON)。
		 *
		 * 通信は LinkSocket の通信スレッドが行い、命令の解釈と実行はメインスレッドの Tick() で行う。
		 * ハンドラはメインスレッドで動くので、ECS / Level / Resource に触れてよい。
		 * `-editor-port` を指定したときだけ AppHost が Initialize する。モジュールが命令を足すときは
		 * IsAvailable() を確かめてから RegisterCommand する。
		 */
		class LinkService
		{
		public:
			/**
			 * 命令ハンドラ
			 * @param params 要求の params(省略時は空のオブジェクト)
			 * @param context 要求の接続世代など
			 */
			using CommandHandler = std::function<LinkResult(const util::JsonValue& params, const LinkRequestContext& context)>;

			/** 1 フレームに処理する行数の上限 */
			static constexpr uint32_t TICK_MAX_LINE_COUNT = 256;
			/** 1 フレームの処理時間の予算(ms)。1 命令ごとに確認するので、重い命令 1 つで超えることはある */
			static constexpr double TICK_BUDGET_MS = 4.0;


		private:
			/** 通信スレッドとソケット */
			LinkSocket socket_;

			/** 命令の登録表 */
			std::unordered_map<std::string, CommandHandler> commands_;

			/** hello / runtime.info で返すモジュール名 */
			std::string appName_;

			/** 統計(メインスレッドだけが数える) */
			uint64_t executedCount_;
			uint64_t discardedBeforeExecCount_;
			uint64_t holdTimeoutCount_;


		public:
			LinkService();
			~LinkService();

			LinkService(const LinkService&)            = delete;
			LinkService& operator=(const LinkService&) = delete;


		public:
			/**
			 * 命令を登録する。同じ名前は上書きする
			 * @param name 命令名("level.load" など)
			 * @param handler メインスレッドで呼ばれるハンドラ
			 */
			void RegisterCommand(const std::string& name, CommandHandler handler);

			/** 命令の登録を外す */
			void UnregisterCommand(const std::string& name);

			/** 登録済みの命令名の一覧(名前順) */
			std::vector<std::string> GetCommandNames() const;

			/**
			 * inbox を処理予算の範囲で処理する(メインスレッドのフレームの安全点で呼ぶ)
			 */
			void Tick();

			/**
			 * イベントを送る。宛先の世代は必ず明示する(特定の接続に由来しない通知は、
			 * 積む時点の GetCurrentConnectionId() を読んで渡す)
			 * @param connectionId 宛先の接続世代 ID
			 * @param name イベント名
			 * @param data イベントの data
			 */
			void SendEvent(const uint64_t connectionId, const char* name, const util::JsonValue& data);


			/**
			 * 状態・統計
			 */
		public:
			/** 現在の接続世代(0 は接続なし。通信スレッドが更新する) */
			inline uint64_t GetCurrentConnectionId() const { return socket_.GetCurrentGeneration(); }
			/** 最後に採番した接続世代(減らない) */
			inline uint64_t GetLastConnectionId() const { return socket_.GetLastAssignedGeneration(); }
			/** モジュール名 */
			inline const std::string& GetAppName() const { return appName_; }

			/** 実行した命令の数 */
			inline uint64_t GetExecutedCount() const { return executedCount_; }
			/** 実行前に切断を検出して捨てた要求の数 */
			inline uint64_t GetDiscardedBeforeExecCount() const { return discardedBeforeExecCount_; }
			/** 世代違いで送らずに捨てた送信データの数 */
			inline uint64_t GetDroppedStaleOutboxCount() const { return socket_.GetDroppedStaleOutboxCount(); }
			/** debug の hold 命令がタイムアウトした数 */
			inline uint64_t GetHoldTimeoutCount() const { return holdTimeoutCount_; }
			/** debug の hold 命令のタイムアウトを数える */
			inline void AddHoldTimeout() { ++holdTimeoutCount_; }


		private:
			/** 1 行の要求を解釈して実行し、応答を積む */
			void ExecuteLine(const LinkInboxItem& item);

			/** 応答を積む */
			void SendResponse(const uint64_t connectionId, const util::JsonValue& id, const LinkResult& result);

			/** 待ち受けを始める */
			bool Start(const uint16_t port, const char* appName);


		private:
			static LinkService* sInstance_;

		public:
			/**
			 * 待ち受けを始める。Win32 以外では「未対応」とログを出して false
			 * @param port 待ち受けるポート(127.0.0.1 のみ)
			 * @param appName hello / runtime.info で返すモジュール名
			 * @return 待ち受けを始めたら true(以後 IsAvailable() が true)
			 */
			static bool Initialize(const uint16_t port, const char* appName);

			/** 通信スレッドを止めて破棄する(未初期化なら何もしない) */
			static void Finalize();

			/** 有効か(Initialize に成功していれば true) */
			static bool IsAvailable() { return sInstance_ != nullptr; }

			/** インスタンス(IsAvailable() が true のときだけ呼ぶ) */
			static LinkService& Get();
		};
	}
}
