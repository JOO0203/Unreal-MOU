// MOU 채팅 서버 (1~3단계: 프로토콜 + 세션 + 프레이밍)
//
// 리슨서버와 별개의 프로세스로 상시 가동된다.
// 호스트가 게임을 종료해도 이 프로세스는 살아있으므로 채팅 로그가 유지된다.
//
// 사용법: ChatServer <port> [db경로]

#include "ChatLog.h"
#include "Session.h"
#include "Framing.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

using namespace MOU;

namespace
{
	SessionManager GSessions;
	std::atomic<bool> GRunning{ true };

	// Ctrl+C 로 서버를 내릴 때 큐에 남은 채팅 로그를 마저 쓰고 나간다.
	// 이게 없으면 accept() 에서 블록된 채 프로세스가 즉사해서
	// 아직 커밋 안 된 로그가 통째로 사라진다.
	//
	// 윈도우 CRT 는 SIGINT 핸들러를 별도 스레드에서 호출하므로
	// 여기서 ChatLog::Stop() 이 라이터 스레드를 join 해도 데드락이 나지 않는다.
	void OnInterrupt(int)
	{
		GRunning = false;
		ChatLog::Stop();
		std::_Exit(0);   // 소켓과 메모리 회수는 OS 에 맡긴다
	}

	const char* ChannelName(EChatChannel Channel)
	{
		switch (Channel)
		{
		case EChatChannel::All:     return "전체";
		case EChatChannel::Team:    return "팀";
		case EChatChannel::Dead:    return "사망";
		case EChatChannel::Whisper: return "귓속말";
		case EChatChannel::System:  return "시스템";
		default:                    return "알수없음";
		}
	}

	// ------------------------------------------------------------------
	// 라우팅
	//
	// 기존 실습의 SendMSG() 는 무조건 전원에게 보냈다.
	// 여기서는 채널에 따라 수신자를 고른다.
	// 귓속말은 9단계에서 case 하나만 추가하면 된다.
	// ------------------------------------------------------------------
	void RouteChat(const SessionPtr& Sender, EChatChannel Channel,
	               const char* Text, uint16_t TextLen)
	{
		// 발화 자격 검증. 클라이언트가 보낸 채널 값을 그대로 믿지 않는다.
		if (Channel == EChatChannel::Dead && !Sender->bDead)
		{
			std::printf("[거부] %s(%llu) 가 살아있는 상태로 사망 채널에 발화 시도\n",
			            Sender->Name.c_str(), static_cast<unsigned long long>(Sender->UserId));
			return;
		}
		if (Channel == EChatChannel::System)
		{
			return;   // 시스템 메시지는 서버만 만든다
		}

		ChatBroadcastBody Out{};
		Out.SenderUserId = Sender->UserId;                        // 서버 보관값
		Out.Timestamp    = static_cast<int64_t>(std::time(nullptr));
		Out.TextLen      = TextLen;
		Out.Channel      = static_cast<uint8_t>(Channel);
		CopyFixedString(Out.SenderName, kMaxNameLen, Sender->Name);   // 서버 보관값

		int DeliverCount = 0;
		GSessions.ForEach([&](const SessionPtr& Target)
		{
			if (!Target->bAuthed)
			{
				return;
			}

			bool bDeliver = false;
			switch (Channel)
			{
			case EChatChannel::All:
				bDeliver = true;
				break;
			case EChatChannel::Team:
				bDeliver = (Target->TeamId == Sender->TeamId);
				break;
			case EChatChannel::Dead:
				bDeliver = Target->bDead;          // 죽은 사람에게만
				break;
			case EChatChannel::Whisper:
				bDeliver = false;                  // 9단계
				break;
			default:
				break;
			}

			if (bDeliver)
			{
				SendPacket2(Target->Sock, EOpcode::ChatBroadcast,
				            &Out, sizeof(Out), Text, TextLen);
				++DeliverCount;
			}
		});

		std::printf("[%s] %s: %.*s  (수신 %d명)\n",
		            ChannelName(Channel), Sender->Name.c_str(),
		            static_cast<int>(TextLen), Text, DeliverCount);

		// 수신자가 0명이어도 기록은 남긴다.
		// "아무도 못 들었지만 말한 것은 사실" 이므로 나중에 신고/관전 조회 때 필요하다.
		// Enqueue 는 큐에 넣기만 하고 바로 돌아오므로 여기서 디스크를 기다리지 않는다.
		ChatLog::Enqueue(Out.Timestamp, Sender->UserId, Sender->Name,
		                 static_cast<uint8_t>(Channel), Sender->TeamId, Text, TextLen);
	}

	// ------------------------------------------------------------------
	// 패킷 핸들러. false 를 반환하면 연결을 끊는다.
	// ------------------------------------------------------------------
	// 로그인 거부를 사유와 함께 알린다.
	// 그냥 연결을 끊어버리면 클라이언트는 원인을 모른 채 3초마다 재접속만 반복한다.
	void SendLoginFailure(const SessionPtr& Session, ELoginResult Reason)
	{
		LoginAckBody Ack{};
		Ack.bSuccess      = 0;
		Ack.Result        = static_cast<uint8_t>(Reason);
		Ack.ServerVersion = kProtocolVersion;
		SendPacket(Session->Sock, EOpcode::LoginAck, &Ack, sizeof(Ack));
	}

	bool HandleLoginReq(const SessionPtr& Session, const char* Body, uint32_t BodySize)
	{
		// Version 은 LoginReqBody 의 첫 필드다.
		// 구조체 크기가 안 맞더라도 이 2바이트만은 읽어서 정확한 사유를 돌려준다.
		if (BodySize < sizeof(uint16_t))
		{
			std::printf("[거부] LoginReq 가 너무 짧다 (%u바이트)\n", BodySize);
			SendLoginFailure(Session, ELoginResult::InvalidRequest);
			return false;
		}

		uint16_t ClientVersion = 0;
		std::memcpy(&ClientVersion, Body, sizeof(ClientVersion));

		if (ClientVersion != kProtocolVersion)
		{
			std::printf("[거부] 프로토콜 버전 불일치. 클라이언트=%u, 서버=%u"
			            " (양쪽을 같은 커밋으로 다시 빌드할 것)\n",
			            ClientVersion, kProtocolVersion);
			SendLoginFailure(Session, ELoginResult::VersionMismatch);
			return false;
		}

		if (BodySize < sizeof(LoginReqBody))
		{
			std::printf("[거부] LoginReq 크기 부족 (%u < %u)\n",
			            BodySize, static_cast<uint32_t>(sizeof(LoginReqBody)));
			SendLoginFailure(Session, ELoginResult::InvalidRequest);
			return false;
		}

		LoginReqBody Req{};
		std::memcpy(&Req, Body, sizeof(Req));

		Session->UserId  = GSessions.AssignUserId();
		Session->Name    = ReadFixedString(Req.Name, kMaxNameLen);
		Session->TeamId  = Req.TeamId;
		Session->bAuthed = true;

		if (Session->Name.empty())
		{
			Session->Name = "익명" + std::to_string(Session->UserId);
		}

		LoginAckBody Ack{};
		Ack.UserId        = Session->UserId;
		Ack.TeamId        = Session->TeamId;
		Ack.bSuccess      = 1;
		Ack.Result        = static_cast<uint8_t>(ELoginResult::Success);
		Ack.ServerVersion = kProtocolVersion;
		CopyFixedString(Ack.Name, kMaxNameLen, Session->Name);

		std::printf("[로그인] %s -> UserId=%llu, Team=%d\n",
		            Session->Name.c_str(),
		            static_cast<unsigned long long>(Session->UserId), Session->TeamId);

		return SendPacket(Session->Sock, EOpcode::LoginAck, &Ack, sizeof(Ack));
	}

	bool HandleChatSend(const SessionPtr& Session, const char* Body, uint32_t BodySize)
	{
		if (!Session->bAuthed)
		{
			return true;   // 로그인 전 채팅은 무시하되 연결은 유지
		}
		if (BodySize < sizeof(ChatSendBody))
		{
			return false;
		}

		ChatSendBody Req{};
		std::memcpy(&Req, Body, sizeof(Req));

		// 선언한 TextLen 과 실제 도착한 바이트 수가 맞는지 확인한다.
		// 이 검사가 없으면 TextLen 을 크게 속여 버퍼 밖을 읽게 만들 수 있다.
		if (Req.TextLen > kMaxTextLen)
		{
			return false;
		}
		if (sizeof(ChatSendBody) + Req.TextLen != BodySize)
		{
			return false;
		}

		const char* Text = Body + sizeof(ChatSendBody);
		RouteChat(Session, static_cast<EChatChannel>(Req.Channel), Text, Req.TextLen);
		return true;
	}

	bool HandleSetDead(const SessionPtr& Session, const char* Body, uint32_t BodySize)
	{
		if (BodySize < sizeof(SetDeadBody))
		{
			return false;
		}

		SetDeadBody Req{};
		std::memcpy(&Req, Body, sizeof(Req));

		// 지금은 테스트 편의를 위해 클라이언트가 자기 상태를 바꾸도록 열어둔다.
		// 8단계에서 리슨서버 전용 인증 연결에서만 받도록 잠근다.
		Session->bDead = (Req.bDead != 0);
		std::printf("[상태] %s -> %s\n", Session->Name.c_str(),
		            Session->bDead ? "사망" : "생존");
		return true;
	}

	bool HandlePacket(const SessionPtr& Session, const PacketHeader& Header,
	                  const std::vector<char>& Body)
	{
		const char* Data = Body.empty() ? nullptr : Body.data();
		const uint32_t Size = static_cast<uint32_t>(Body.size());

		switch (static_cast<EOpcode>(Header.Opcode))
		{
		case EOpcode::LoginReq:  return HandleLoginReq(Session, Data, Size);
		case EOpcode::ChatSend:  return HandleChatSend(Session, Data, Size);
		case EOpcode::SetDead:   return HandleSetDead(Session, Data, Size);
		case EOpcode::Heartbeat: return true;
		default:
			std::printf("[경고] 알 수 없는 오피코드 %u\n", Header.Opcode);
			return false;
		}
	}

	// ------------------------------------------------------------------
	// 클라이언트 스레드
	// ------------------------------------------------------------------
	void ClientThread(SessionPtr Session)
	{
		char Temp[1024];
		PacketHeader Header{};
		std::vector<char> Body;

		for (;;)
		{
			const int Received = ::recv(Session->Sock, Temp, sizeof(Temp), 0);

			// 0 이면 정상 종료, 음수면 에러. 기존 코드는 != 0 만 봐서
			// 에러(-1) 일 때 send(sock, buf, -1, 0) 이 호출됐다.
			if (Received <= 0)
			{
				break;
			}

			Session->RecvBuf.insert(Session->RecvBuf.end(), Temp, Temp + Received);

			// 한 번의 recv 에 여러 패킷이 붙어 왔을 수 있으므로 다 꺼낼 때까지 돈다.
			bool bDisconnect = false;
			for (;;)
			{
				const EFrameResult Result = TryExtractPacket(Session->RecvBuf, Header, Body);

				if (Result == EFrameResult::NeedMore)
				{
					break;
				}
				if (Result == EFrameResult::Malformed)
				{
					std::printf("[차단] 비정상 패킷 크기. 연결을 끊는다. (UserId=%llu)\n",
					            static_cast<unsigned long long>(Session->UserId));
					bDisconnect = true;
					break;
				}
				if (!HandlePacket(Session, Header, Body))
				{
					bDisconnect = true;
					break;
				}
			}

			if (bDisconnect)
			{
				break;
			}
		}

		std::printf("[종료] %s (UserId=%llu) 연결 해제\n",
		            Session->Name.empty() ? "(미로그인)" : Session->Name.c_str(),
		            static_cast<unsigned long long>(Session->UserId));

		GSessions.Remove(Session);
		std::printf("       현재 접속자 %zu명\n", GSessions.Count());
	}
}

int main(int argc, char** argv)
{
#ifdef _WIN32
	::SetConsoleOutputCP(CP_UTF8);
#endif
	// 출력을 파일로 리다이렉트하면 stdout 이 전체 버퍼링으로 바뀌어
	// 프로세스가 끝날 때까지 로그가 하나도 보이지 않는다.
	// MSVC 는 _IOLBF(줄 버퍼링)를 _IOFBF 와 동일하게 처리하므로 무버퍼로 둔다.
	::setvbuf(stdout, nullptr, _IONBF, 0);

	if (argc < 2 || argc > 3)
	{
		std::printf("사용법: %s <port> [db경로]\n", argv[0]);
		std::printf("  db경로를 생략하면 현재 디렉터리의 chat_log.db 를 쓴다.\n");
		return 1;
	}

	if (!NetInit())
	{
		std::printf("NetInit() 실패\n");
		return 1;
	}

	const SocketHandle ListenSock = ::socket(PF_INET, SOCK_STREAM, 0);
	if (ListenSock == kInvalidSocket)
	{
		std::printf("socket() 실패: %d\n", LastNetError());
		return 1;
	}

	sockaddr_in ServerAddr{};
	ServerAddr.sin_family      = AF_INET;
	ServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	ServerAddr.sin_port        = htons(static_cast<uint16_t>(std::atoi(argv[1])));

	if (::bind(ListenSock, reinterpret_cast<sockaddr*>(&ServerAddr), sizeof(ServerAddr)) != 0)
	{
		std::printf("bind() 실패: %d\n", LastNetError());
		return 1;
	}
	if (::listen(ListenSock, SOMAXCONN) != 0)
	{
		std::printf("listen() 실패: %d\n", LastNetError());
		return 1;
	}

	std::signal(SIGINT,  OnInterrupt);
	std::signal(SIGTERM, OnInterrupt);

	// 채팅 로그 DB. 두 번째 인자로 경로를 바꿀 수 있다 (테스트용으로 분리할 때 편하다).
	// 열기에 실패해도 서버는 계속 돈다. 로그가 안 남는 것보다 채팅이 끊기는 게 나쁘다.
	const char* DbPath = (argc >= 3) ? argv[2] : "chat_log.db";
	ChatLog::Start(DbPath);

	std::printf("=== MOU 채팅 서버 시작 (port %s) ===\n", argv[1]);

	while (GRunning)
	{
		sockaddr_in ClientAddr{};
		int AddrSize = sizeof(ClientAddr);

		const SocketHandle ClientSock =
			::accept(ListenSock, reinterpret_cast<sockaddr*>(&ClientAddr),
#ifdef _WIN32
			         &AddrSize);
#else
			         reinterpret_cast<socklen_t*>(&AddrSize));
#endif
		if (ClientSock == kInvalidSocket)
		{
			std::printf("accept() 실패: %d\n", LastNetError());
			continue;
		}

		char AddrText[INET_ADDRSTRLEN] = {};
		::inet_ntop(AF_INET, &ClientAddr.sin_addr, AddrText, sizeof(AddrText));
		std::printf("[접속] %s\n", AddrText);

		// 고정 배열이 아니므로 접속자 수 상한이 없다.
		SessionPtr Session = GSessions.Add(ClientSock);
		std::thread(ClientThread, Session).detach();
	}

	ChatLog::Stop();
	CloseSocket(ListenSock);
	NetShutdown();
	return 0;
}
