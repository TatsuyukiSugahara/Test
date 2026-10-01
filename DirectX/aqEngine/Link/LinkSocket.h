#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>


namespace aq
{
	namespace link
	{
		/**
		 * inbox の要素の種類
		 */
		enum class LinkInboxKind : uint8_t
		{
			Connected,      // 接続を受け付けた(text は空)
			Line,           // 1 行受信した(text は改行を除いた行)
			Disconnected,   // 切断を検出した(text は空)
		};




		/**
		 * 通信スレッド → メインスレッドへ渡す要素
		 */
		struct LinkInboxItem
		{
			/** 接続世代 ID(1 から単調増加。0 は「接続なし」) */
			uint64_t      connectionId = 0;
			LinkInboxKind kind         = LinkInboxKind::Line;
			std::string   text;
		};




		/**
		 * メインスレッド → 通信スレッドへ渡す要素
		 */
		struct LinkOutboxItem
		{
			/** 宛先の接続世代 ID。現在の世代と一致しなければ送らずに捨てる */
			uint64_t    connectionId = 0;
			/** 送る 1 行(終端の改行は通信スレッドが付ける) */
			std::string text;
		};




		/**
		 * エディタ連携の通信スレッドと非ブロッキングソケット。
		 *
		 * 127.0.0.1 だけで待ち受け、同時に 1 クライアントだけ受け付ける。受信したバイト列を改行で
		 * 行に分け、接続世代 ID を付けて inbox に積む。outbox の行は宛先の世代が現在の世代と
		 * 一致するときだけ送る。JSON は解釈しない。
		 * ソケットに触れるのは通信スレッドだけで、メインスレッドは mutex 付きのキュー操作しか行わない。
		 * Win32 以外ではスタブ(Start が常に失敗する)。
		 */
		class LinkSocket
		{
		public:
			/** 1 行の上限。超えたら切断する */
			static constexpr size_t MAX_LINE_SIZE = 16u * 1024u * 1024u;
			/** inbox の上限(行の件数)。達したら受信を止める */
			static constexpr size_t MAX_INBOX_LINE_COUNT = 1024;
			/** inbox の上限(行の合計バイト数)。達したら受信を止める */
			static constexpr size_t MAX_INBOX_SIZE = 32u * 1024u * 1024u;
			/** outbox + 送信バッファ(未送信)の上限。超えたら切断する */
			static constexpr size_t MAX_PENDING_SEND_SIZE = 64u * 1024u * 1024u;
			/** select の待ち時間の上限(ms)。stop フラグに気付くまでの最大遅延でもある */
			static constexpr uint32_t SELECT_TIMEOUT_MS = 50;


		private:
			/** 通信スレッド */
			std::thread       thread_;
			std::atomic<bool> stopRequested_;
			bool              started_;

			/** 待ち受けソケット(Win32 の SOCKET。ヘッダに winsock の型を出さないため整数で持つ) */
			uintptr_t listenSocket_;
			/** WSAStartup 済みか(Stop で WSACleanup する) */
			bool      wsaStarted_;

			/** 接続世代 ID */
			std::atomic<uint64_t> currentGeneration_;      // 現在の接続。0 は接続なし
			std::atomic<uint64_t> lastAssignedGeneration_; // 最後に採番した世代(減らない)

			/** inbox(受信済み・未処理)。件数とバイト数は Line だけを数える */
			mutable std::mutex        inboxMutex_;
			std::deque<LinkInboxItem> inbox_;
			size_t                    inboxLineCount_;
			size_t                    inboxSize_;

			/** outbox(未送信)。outboxSize_ は積まれている行の合計バイト数 */
			std::mutex                 outboxMutex_;
			std::deque<LinkOutboxItem> outbox_;
			size_t                     outboxSize_;

			/** 通信スレッドの送信バッファに残っている未送信バイト数(上限の判定用) */
			std::atomic<size_t> sendPendingSize_;
			/** 上限超過で切断を要求された世代(0 なら要求なし) */
			std::atomic<uint64_t> overflowGeneration_;

			/** 統計: 世代違いで送らずに捨てた outbox の要素数 */
			std::atomic<uint64_t> droppedStaleOutbox_;


		public:
			LinkSocket();
			~LinkSocket();

			LinkSocket(const LinkSocket&)            = delete;
			LinkSocket& operator=(const LinkSocket&) = delete;


		public:
			/**
			 * 127.0.0.1:port で待ち受けを始め、通信スレッドを起動する
			 * @param port 待ち受けるポート(1-65535)
			 * @return 失敗(未対応プラットフォーム・bind 失敗など)なら false
			 */
			bool Start(const uint16_t port);

			/**
			 * 通信スレッドを止める。未送信データは破棄してソケットを閉じ、join してから WSACleanup する
			 */
			void Stop();

			/**
			 * inbox の先頭を 1 件取り出す(メインスレッド用)
			 * @return 空なら false
			 */
			bool PopInbox(LinkInboxItem& item);

			/**
			 * outbox に 1 行積む(メインスレッド用。待たされない)。
			 * 宛先がすでに現在の世代でなければ積まずに捨てる。上限を超える場合は捨てて、その世代の切断を要求する
			 * @param connectionId 宛先の接続世代 ID
			 * @param text 改行を含まない 1 行
			 */
			void PushOutbox(const uint64_t connectionId, std::string text);


		public:
			/** 現在の接続世代(0 は接続なし) */
			inline uint64_t GetCurrentGeneration() const { return currentGeneration_.load(); }
			/** 最後に採番した接続世代 */
			inline uint64_t GetLastAssignedGeneration() const { return lastAssignedGeneration_.load(); }
			/** 世代違いで捨てた outbox の要素数 */
			inline uint64_t GetDroppedStaleOutboxCount() const { return droppedStaleOutbox_.load(); }


		private:
			/** 通信スレッドの本体 */
			void ThreadMain();

			/** 制御要素(Connected / Disconnected)を inbox に積む。上限に数えず必ず積む */
			void PushInboxControl(const uint64_t connectionId, const LinkInboxKind kind);

			/**
			 * 1 行を inbox に積む
			 * @return inbox が上限に達していて積めなければ false
			 */
			bool TryPushInboxLine(const uint64_t connectionId, std::string text);

			/** inbox が上限の半分まで減ったか(受信の再開判定) */
			bool IsInboxBelowResumeThreshold() const;
		};
	}
}
