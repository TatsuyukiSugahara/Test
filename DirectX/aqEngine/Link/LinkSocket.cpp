#include "aq.h"
#include "LinkSocket.h"

#if defined(AQ_PLATFORM_WIN32)
// winsock.h(v1)は PCH の windows.h が先に取り込んでいる。winsock2.h を混ぜると再定義で壊れるので
// v1 の API(IPv4 のループバックだけなら足りる)だけを使い、リンクはここで指定する。
// aq.h の方針(依存ライブラリは AdditionalDependencies で指定)の例外。aqEngine.lib を使う全ての
// 実行ファイル(Game の各構成・CMake の Sample / Tools)へ足して回らずに済むよう、aq.cpp の dbghelp と同じく
// 使う側の翻訳単位に閉じる。
#pragma comment(lib, "ws2_32.lib")
#endif


namespace aq
{
	namespace link
	{
#if defined(AQ_PLATFORM_WIN32)
		namespace
		{
			/** 1 回の recv で読む最大バイト数 */
			static constexpr size_t RECV_CHUNK_SIZE = 64u * 1024u;
			/** 1 回の send に渡す最大バイト数 */
			static constexpr size_t SEND_CHUNK_SIZE = 1024u * 1024u;
			/** 送信済みの先頭をこのバイト数を超えたら詰める */
			static constexpr size_t SEND_COMPACT_SIZE = 1024u * 1024u;

			/** 2 つ目の接続へ送る行(通信スレッドは JSON を組み立てないので固定文字列で持つ) */
			static constexpr char BUSY_LINE[] =
				"{\"type\":\"event\",\"event\":\"error\",\"data\":{\"code\":\"busy\",\"message\":\"another client is already connected\"}}\n";

			/** shutdown の送信側(SD_SEND は winsock2.h にしか無い) */
			static constexpr int SHUTDOWN_SEND = 1;


			bool SetNonBlocking(const SOCKET s)
			{
				u_long mode = 1;
				return ::ioctlsocket(s, FIONBIO, &mode) == 0;
			}


			/** 未送信データを捨てて閉じる(相手が受信していなくても待たない) */
			void CloseAbortive(const SOCKET s)
			{
				linger lingerOption = {};
				lingerOption.l_onoff  = 1;
				lingerOption.l_linger = 0;
				::setsockopt(s, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&lingerOption), sizeof(lingerOption));
				::closesocket(s);
			}
		}
#endif


		LinkSocket::LinkSocket()
			: stopRequested_(false)
			, started_(false)
			, listenSocket_(0)
			, wsaStarted_(false)
			, currentGeneration_(0)
			, lastAssignedGeneration_(0)
			, inboxLineCount_(0)
			, inboxSize_(0)
			, outboxSize_(0)
			, sendPendingSize_(0)
			, overflowGeneration_(0)
			, droppedStaleOutbox_(0)
		{
		}


		LinkSocket::~LinkSocket()
		{
			Stop();
		}


		bool LinkSocket::Start(const uint16_t port)
		{
#if defined(AQ_PLATFORM_WIN32)
			if (started_ || port == 0) {
				return false;
			}

			WSADATA wsaData = {};
			if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
				aq::StartupMarkf("  [link] WSAStartup failed");
				return false;
			}
			wsaStarted_ = true;

			const SOCKET listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (listenSocket == INVALID_SOCKET) {
				aq::StartupMarkf("  [link] socket failed (error %d)", WSAGetLastError());
				Stop();
				return false;
			}

			// 外部ネットワークには公開しない(127.0.0.1 だけに bind する)
			sockaddr_in address = {};
			address.sin_family      = AF_INET;
			address.sin_port        = ::htons(port);
			address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
			if (::bind(listenSocket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR
				|| ::listen(listenSocket, SOMAXCONN) == SOCKET_ERROR
				|| !SetNonBlocking(listenSocket)) {
				aq::StartupMarkf("  [link] listen on 127.0.0.1:%u failed (error %d)", static_cast<uint32_t>(port), WSAGetLastError());
				::closesocket(listenSocket);
				Stop();
				return false;
			}

			listenSocket_ = static_cast<uintptr_t>(listenSocket);
			stopRequested_.store(false);
			thread_  = std::thread(&LinkSocket::ThreadMain, this);
			started_ = true;
			aq::StartupMarkf("  [link] listening on 127.0.0.1:%u", static_cast<uint32_t>(port));
			return true;
#else
			(void)port;
			return false;
#endif
		}


		void LinkSocket::Stop()
		{
			// 通信スレッドは select の復帰(遅くとも SELECT_TIMEOUT_MS 後)で stop に気付き、
			// 未送信データを捨ててソケットを閉じてから抜ける。ブロックする呼び出しが無いので join は必ず返る。
			if (started_) {
				stopRequested_.store(true);
				if (thread_.joinable()) {
					thread_.join();
				}
				started_      = false;
				listenSocket_ = 0;
			}

#if defined(AQ_PLATFORM_WIN32)
			if (wsaStarted_) {
				WSACleanup();
				wsaStarted_ = false;
			}
#endif

			currentGeneration_.store(0);
			{
				std::lock_guard<std::mutex> lock(inboxMutex_);
				inbox_.clear();
				inboxLineCount_ = 0;
				inboxSize_      = 0;
			}
			{
				std::lock_guard<std::mutex> lock(outboxMutex_);
				outbox_.clear();
				outboxSize_ = 0;
			}
			sendPendingSize_.store(0);
			overflowGeneration_.store(0);
		}


		bool LinkSocket::PopInbox(LinkInboxItem& item)
		{
			std::lock_guard<std::mutex> lock(inboxMutex_);
			if (inbox_.empty()) {
				return false;
			}

			item = std::move(inbox_.front());
			inbox_.pop_front();
			if (item.kind == LinkInboxKind::Line) {
				--inboxLineCount_;
				inboxSize_ -= item.text.size();
			}
			return true;
		}


		void LinkSocket::PushOutbox(const uint64_t connectionId, std::string text)
		{
			// 世代は戻らないので、積む時点で現在の世代でなければ二度と送られない。通信スレッドで捨てるのと
			// 同じ扱いにして数える(切断済みの相手宛てのデータで、次の接続の上限を食わないため)
			if (connectionId == 0 || connectionId != currentGeneration_.load()) {
				droppedStaleOutbox_.fetch_add(1);
				return;
			}

			const size_t lineSize = text.size() + 1;   // 終端の改行の分
			std::lock_guard<std::mutex> lock(outboxMutex_);
			if (outboxSize_ + sendPendingSize_.load() + lineSize > MAX_PENDING_SEND_SIZE) {
				// 応答を読まないクライアントとみなす。この要素は捨て、切断は通信スレッドに任せる
				overflowGeneration_.store(connectionId);
				return;
			}
			outboxSize_ += lineSize;
			outbox_.push_back(LinkOutboxItem{ connectionId, std::move(text) });
		}


		void LinkSocket::PushInboxControl(const uint64_t connectionId, const LinkInboxKind kind)
		{
			std::lock_guard<std::mutex> lock(inboxMutex_);
			inbox_.push_back(LinkInboxItem{ connectionId, kind, std::string() });
		}


		bool LinkSocket::TryPushInboxLine(const uint64_t connectionId, std::string text)
		{
			std::lock_guard<std::mutex> lock(inboxMutex_);
			if (inboxLineCount_ >= MAX_INBOX_LINE_COUNT || inboxSize_ + text.size() > MAX_INBOX_SIZE) {
				return false;
			}

			inboxSize_ += text.size();
			++inboxLineCount_;
			inbox_.push_back(LinkInboxItem{ connectionId, LinkInboxKind::Line, std::move(text) });
			return true;
		}


		bool LinkSocket::IsInboxBelowResumeThreshold() const
		{
			std::lock_guard<std::mutex> lock(inboxMutex_);
			return inboxLineCount_ <= MAX_INBOX_LINE_COUNT / 2 && inboxSize_ <= MAX_INBOX_SIZE / 2;
		}


		void LinkSocket::ThreadMain()
		{
#if defined(AQ_PLATFORM_WIN32)
			const SOCKET listenSocket = static_cast<SOCKET>(listenSocket_);

			// 接続中のクライアント
			SOCKET   client           = INVALID_SOCKET;
			uint64_t clientGeneration = 0;

			// 受信: 行に分けきれていないバイト列と、改行を探し終えた位置
			std::string recvBuffer;
			size_t      scanPos    = 0;
			bool        readPaused = false;
			std::unique_ptr<char[]> recvChunk(new char[RECV_CHUNK_SIZE]);

			// 送信: 未送信のバイト列と送信済みオフセット
			std::string sendBuffer;
			size_t      sendOffset = 0;

			const auto updateSendPending = [&]()
				{
					sendPendingSize_.store(sendBuffer.size() - sendOffset);
				};

			const auto disconnect = [&](const char* reason, const bool abortive)
				{
					if (client == INVALID_SOCKET) {
						return;
					}

					// 前の接続宛ての応答を次の接続へ届けないため、Disconnected を積む前に現在の世代を 0 にする
					currentGeneration_.store(0);
					PushInboxControl(clientGeneration, LinkInboxKind::Disconnected);
					if (abortive) {
						CloseAbortive(client);
					} else {
						::closesocket(client);
					}
					aq::StartupMarkf("  [link] disconnected (connection %llu): %s",
					                 static_cast<unsigned long long>(clientGeneration), reason);

					client           = INVALID_SOCKET;
					clientGeneration = 0;
					recvBuffer.clear();
					scanPos    = 0;
					readPaused = false;
					sendBuffer.clear();
					sendOffset = 0;
					updateSendPending();
				};

			// 受信済みのバイト列から行を取り出して inbox に積む。inbox が上限なら残りは recvBuffer に置いて受信を止める。
			// 1 行の上限を超えたら false
			const auto extractLines = [&]() -> bool
				{
					size_t lineStart = 0;
					bool   tooLong   = false;
					for (;;) {
						const size_t newline = recvBuffer.find('\n', scanPos);
						if (newline == std::string::npos) {
							scanPos = recvBuffer.size();
							tooLong = (recvBuffer.size() - lineStart) > MAX_LINE_SIZE;
							break;
						}

						size_t lineEnd = newline;
						if (lineEnd > lineStart && recvBuffer[lineEnd - 1] == '\r') {
							--lineEnd;
						}
						if (lineEnd - lineStart > MAX_LINE_SIZE) {
							tooLong = true;
							break;
						}
						// 空行(手入力のクライアントなど)は捨てる
						if (lineEnd > lineStart) {
							if (!TryPushInboxLine(clientGeneration, recvBuffer.substr(lineStart, lineEnd - lineStart))) {
								if (!readPaused) {
									aq::StartupMarkf("  [link] inbox full, receive paused");
								}
								readPaused = true;
								scanPos    = lineStart;
								break;
							}
						}
						lineStart = newline + 1;
						scanPos   = lineStart;
					}
					recvBuffer.erase(0, lineStart);
					scanPos -= lineStart;
					return !tooLong;
				};

			// 送信バッファを書けるだけ書く。エラーなら false
			const auto flushSend = [&]() -> bool
				{
					while (sendOffset < sendBuffer.size()) {
						const size_t remain = std::min(sendBuffer.size() - sendOffset, SEND_CHUNK_SIZE);
						const int sent = ::send(client, sendBuffer.data() + sendOffset, static_cast<int>(remain), 0);
						if (sent == SOCKET_ERROR) {
							if (WSAGetLastError() == WSAEWOULDBLOCK) {
								break;   // 残りは書き込み可能になってから送る
							}
							return false;
						}
						sendOffset += static_cast<size_t>(sent);
					}

					if (sendOffset == sendBuffer.size()) {
						sendBuffer.clear();
						sendOffset = 0;
					} else if (sendOffset > SEND_COMPACT_SIZE && sendOffset > sendBuffer.size() / 2) {
						sendBuffer.erase(0, sendOffset);
						sendOffset = 0;
					}
					updateSendPending();
					return true;
				};

			// outbox を送信バッファへ移す。宛先が今の接続でない要素は捨てる
			const auto drainOutbox = [&]()
				{
					std::deque<LinkOutboxItem> items;
					{
						std::lock_guard<std::mutex> lock(outboxMutex_);
						if (outbox_.empty()) {
							return;
						}
						items.swap(outbox_);
						// メインスレッドの上限判定から一瞬でも消えないよう、ロック中に送信側へ付け替える
						sendPendingSize_.fetch_add(outboxSize_);
						outboxSize_ = 0;
					}

					for (LinkOutboxItem& item : items) {
						if (client != INVALID_SOCKET && item.connectionId == clientGeneration) {
							sendBuffer += item.text;
							sendBuffer += '\n';
						} else {
							droppedStaleOutbox_.fetch_add(1);
						}
					}
					updateSendPending();
				};

			const auto acceptClients = [&]()
				{
					for (;;) {
						sockaddr_in address = {};
						int addressSize = sizeof(address);
						const SOCKET accepted = ::accept(listenSocket, reinterpret_cast<sockaddr*>(&address), &addressSize);
						if (accepted == INVALID_SOCKET) {
							break;   // WSAEWOULDBLOCK(待ちが無い)を含む
						}
						// 待ち受け側の設定を引き継ぐかに頼らず、明示的に非ブロッキングにする
						SetNonBlocking(accepted);

						// 同時に接続できるのは 1 つ。2 つ目には error を 1 行送ってから閉じる
						if (client != INVALID_SOCKET) {
							::send(accepted, BUSY_LINE, static_cast<int>(sizeof(BUSY_LINE) - 1), 0);
							::shutdown(accepted, SHUTDOWN_SEND);
							::closesocket(accepted);
							aq::StartupMarkf("  [link] rejected a second client (busy)");
							continue;
						}

						// 1 行ずつの小さな応答を Nagle で溜めない(往復遅延のため)
						BOOL noDelay = TRUE;
						::setsockopt(accepted, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

						client           = accepted;
						clientGeneration = lastAssignedGeneration_.load() + 1;
						lastAssignedGeneration_.store(clientGeneration);
						currentGeneration_.store(clientGeneration);
						PushInboxControl(clientGeneration, LinkInboxKind::Connected);
						aq::StartupMarkf("  [link] connected (connection %llu)",
						                 static_cast<unsigned long long>(clientGeneration));
					}
				};

			const auto receive = [&]()
				{
					const int received = ::recv(client, recvChunk.get(), static_cast<int>(RECV_CHUNK_SIZE), 0);
					if (received > 0) {
						recvBuffer.append(recvChunk.get(), static_cast<size_t>(received));
						if (!extractLines()) {
							disconnect("line too long", true);
						}
					} else if (received == 0) {
						disconnect("closed by peer", false);
					} else if (WSAGetLastError() != WSAEWOULDBLOCK) {
						disconnect("recv error", true);
					}
				};

			while (!stopRequested_.load()) {
				// 上限超過による切断要求(メインスレッドが立てる)
				{
					const uint64_t overflow = overflowGeneration_.exchange(0);
					if (overflow != 0 && client != INVALID_SOCKET && overflow == clientGeneration) {
						disconnect("send buffer limit exceeded (client is not reading)", true);
					}
				}

				// 応答を送信バッファへ移し、書ける分は select を待たずに書く
				drainOutbox();
				if (client != INVALID_SOCKET && sendOffset < sendBuffer.size()) {
					if (!flushSend()) {
						disconnect("send error", true);
					}
				}

				// inbox が半分まで減ったら受信を再開する
				if (client != INVALID_SOCKET && readPaused && IsInboxBelowResumeThreshold()) {
					readPaused = false;
					aq::StartupMarkf("  [link] receive resumed");
					if (!extractLines()) {
						disconnect("line too long", true);
					}
				}

				fd_set readSet;
				fd_set writeSet;
				FD_ZERO(&readSet);
				FD_ZERO(&writeSet);
				FD_SET(listenSocket, &readSet);
				if (client != INVALID_SOCKET) {
					if (!readPaused) {
						FD_SET(client, &readSet);
					}
					if (sendOffset < sendBuffer.size()) {
						FD_SET(client, &writeSet);
					}
				}

				timeval timeout = {};
				timeout.tv_sec  = 0;
				timeout.tv_usec = static_cast<long>(SELECT_TIMEOUT_MS * 1000);
				const int ready = ::select(0, &readSet, &writeSet, nullptr, &timeout);
				if (stopRequested_.load()) {
					break;
				}
				if (ready == SOCKET_ERROR) {
					// 想定外。空回りしないよう select と同じだけ待ってからやり直す
					std::this_thread::sleep_for(std::chrono::milliseconds(SELECT_TIMEOUT_MS));
					continue;
				}
				if (ready == 0) {
					continue;
				}

				// 今の接続を先に処理する。切断と再接続が同じ回に来たとき、切断を検出してから accept するので、
				// 再接続したクライアントを busy で拒否せずに済む
				if (client != INVALID_SOCKET && FD_ISSET(client, &readSet)) {
					receive();
				}
				if (client != INVALID_SOCKET && FD_ISSET(client, &writeSet)) {
					if (!flushSend()) {
						disconnect("send error", true);
					}
				}
				if (FD_ISSET(listenSocket, &readSet)) {
					acceptClients();
				}
			}

			// 終了: 未送信データは破棄してソケットを閉じる
			currentGeneration_.store(0);
			if (client != INVALID_SOCKET) {
				CloseAbortive(client);
			}
			::closesocket(listenSocket);
#endif
		}
	}
}
