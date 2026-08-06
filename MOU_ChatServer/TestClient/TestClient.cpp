// MOU 채팅 서버 검증용 콘솔 클라이언트.
//
// 기존 실습 클라이언트와 달리, 이름을 문자열에 붙여 보내지 않는다.
// 로그인 패킷으로 이름을 알리고, 이후 서버가 부여한 UserId 로 식별된다.
//
// 사용법:
//   TestClient <ip> <port> <name> [mode]
//
//   mode 생략   : 대화형. /team /dead /alive /all 명령 사용 가능
//   mode split  : 패킷 하나를 1바이트씩 쪼개 전송   (분할 테스트)
//   mode merge  : 패킷 3개를 한 번의 send 로 전송   (합침 테스트)
//   mode bad    : BodySize 를 999999 로 위조해 전송 (방어 테스트)

#include "Framing.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace MOU;

namespace
{
	SocketHandle GSock = kInvalidSocket;
	std::atomic<bool> GRunning{ true };
	uint64_t GMyUserId = 0;

	const char* ChannelName(uint8_t Channel)
	{
		switch (static_cast<EChatChannel>(Channel))
		{
		case EChatChannel::All:     return "전체";
		case EChatChannel::Team:    return "팀";
		case EChatChannel::Dead:    return "사망";
		case EChatChannel::Whisper: return "귓속말";
		case EChatChannel::System:  return "시스템";
		default:                    return "?";
		}
	}

	// 채팅 패킷 하나를 완성된 바이트 배열로 만든다.
	// 테스트 모드에서 이 바이트열을 마음대로 쪼개거나 붙여서 보낸다.
	std::vector<char> BuildChatPacket(EChatChannel Channel, const std::string& Text)
	{
		const uint16_t TextLen = static_cast<uint16_t>(
			Text.size() > kMaxTextLen ? kMaxTextLen : Text.size());

		ChatSendBody Body{};
		Body.TargetUserId = 0;
		Body.TextLen      = TextLen;
		Body.Channel      = static_cast<uint8_t>(Channel);

		PacketHeader Header{};
		Header.BodySize = static_cast<uint32_t>(sizeof(Body) + TextLen);
		Header.Opcode   = static_cast<uint16_t>(EOpcode::ChatSend);

		std::vector<char> Packet(sizeof(Header) + Header.BodySize);
		std::memcpy(Packet.data(), &Header, sizeof(Header));
		std::memcpy(Packet.data() + sizeof(Header), &Body, sizeof(Body));
		std::memcpy(Packet.data() + sizeof(Header) + sizeof(Body), Text.data(), TextLen);
		return Packet;
	}

	// ------------------------------------------------------------------
	// 수신 스레드. 서버와 동일한 프레이밍 로직을 쓴다.
	// ------------------------------------------------------------------
	void RecvThread()
	{
		std::vector<char> RecvBuf;
		char Temp[1024];
		PacketHeader Header{};
		std::vector<char> Body;

		while (GRunning)
		{
			const int Received = ::recv(GSock, Temp, sizeof(Temp), 0);

			if (Received == 0)
			{
				// 서버가 정상적으로 연결을 닫았다.
				std::printf("[연결 종료] 서버가 연결을 닫았습니다.\n");
				GRunning = false;
				return;
			}
			if (Received < 0)
			{
				// 타임아웃은 정상이다. 종료 요청이 없으면 계속 기다린다.
				if (IsRecvTimeout(LastNetError()) && GRunning)
				{
					continue;
				}
				std::printf("[연결 끊김] 수신 오류 %d\n", LastNetError());
				GRunning = false;
				return;
			}

			RecvBuf.insert(RecvBuf.end(), Temp, Temp + Received);

			for (;;)
			{
				const EFrameResult Result = TryExtractPacket(RecvBuf, Header, Body);
				if (Result == EFrameResult::NeedMore)
				{
					break;
				}
				if (Result == EFrameResult::Malformed)
				{
					std::printf("[오류] 서버가 비정상 패킷을 보냈습니다.\n");
					GRunning = false;
					return;
				}

				switch (static_cast<EOpcode>(Header.Opcode))
				{
				case EOpcode::LoginAck:
					if (Body.size() >= sizeof(LoginAckBody))
					{
						LoginAckBody Ack{};
						std::memcpy(&Ack, Body.data(), sizeof(Ack));
						GMyUserId = Ack.UserId;
						std::printf("[로그인 성공] UserId=%llu, 이름=%s, 팀=%d\n",
						            static_cast<unsigned long long>(Ack.UserId),
						            ReadFixedString(Ack.Name, kMaxNameLen).c_str(),
						            Ack.TeamId);
					}
					break;

				case EOpcode::ChatBroadcast:
					if (Body.size() >= sizeof(ChatBroadcastBody))
					{
						ChatBroadcastBody Msg{};
						std::memcpy(&Msg, Body.data(), sizeof(Msg));
						const char* Text = Body.data() + sizeof(Msg);
						std::printf("[%s] %s: %.*s\n",
						            ChannelName(Msg.Channel),
						            ReadFixedString(Msg.SenderName, kMaxNameLen).c_str(),
						            static_cast<int>(Msg.TextLen), Text);
					}
					break;

				default:
					std::printf("[수신] 오피코드 %u (%zu바이트)\n",
					            Header.Opcode, Body.size());
					break;
				}
			}
		}
	}

	bool DoLogin(const std::string& Name, int32_t TeamId)
	{
		LoginReqBody Req{};
		CopyFixedString(Req.Name, kMaxNameLen, Name);
		Req.TeamId = TeamId;
		return SendPacket(GSock, EOpcode::LoginReq, &Req, sizeof(Req));
	}

	void SetDead(bool bDead)
	{
		SetDeadBody Body{};
		Body.UserId = GMyUserId;
		Body.bDead  = bDead ? 1 : 0;
		SendPacket(GSock, EOpcode::SetDead, &Body, sizeof(Body));
	}

	// ==================================================================
	// 테스트 1: 분할
	// 패킷 하나를 1바이트씩 나눠 보낸다.
	// 서버가 조각을 모아 하나의 메시지로 복원하면 통과.
	// ==================================================================
	void RunSplitTest()
	{
		std::printf("\n=== [분할 테스트] 패킷 1개를 1바이트씩 전송 ===\n");
		const std::vector<char> Packet =
			BuildChatPacket(EChatChannel::All, "분할전송테스트");

		std::printf("총 %zu바이트를 1바이트씩 보냅니다...\n", Packet.size());
		for (size_t i = 0; i < Packet.size(); ++i)
		{
			if (!SendAll(GSock, Packet.data() + i, 1))
			{
				std::printf("전송 실패 (%zu번째 바이트)\n", i);
				return;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}

		std::printf("전송 완료.\n");
		std::printf("기대 결과: 서버 콘솔에 '분할전송테스트' 가 정확히 1번 출력\n");
	}

	// ==================================================================
	// 테스트 2: 합침
	// 패킷 3개를 하나의 버퍼로 합쳐 한 번의 send 로 보낸다.
	// 서버가 3개로 분리해 처리하면 통과.
	// ==================================================================
	void RunMergeTest()
	{
		std::printf("\n=== [합침 테스트] 패킷 3개를 한 번에 전송 ===\n");

		std::vector<char> Combined;
		for (int i = 1; i <= 3; ++i)
		{
			const std::vector<char> Packet =
				BuildChatPacket(EChatChannel::All, "합침테스트" + std::to_string(i));
			Combined.insert(Combined.end(), Packet.begin(), Packet.end());
		}

		std::printf("총 %zu바이트를 한 번의 send 로 보냅니다...\n", Combined.size());
		if (!SendAll(GSock, Combined.data(), static_cast<int32_t>(Combined.size())))
		{
			std::printf("전송 실패\n");
			return;
		}

		std::printf("전송 완료.\n");
		std::printf("기대 결과: 서버 콘솔에 합침테스트1/2/3 이 각각 출력 (총 3줄)\n");
	}

	// ==================================================================
	// 테스트 3: 방어
	// BodySize 를 999999 로 위조해 보낸다.
	// 서버가 연결만 끊고 죽지 않으면 통과.
	// ==================================================================
	void RunBadTest()
	{
		std::printf("\n=== [방어 테스트] BodySize=999999 위조 패킷 전송 ===\n");

		PacketHeader Header{};
		Header.BodySize = 999999;
		Header.Opcode   = static_cast<uint16_t>(EOpcode::ChatSend);

		SendAll(GSock, reinterpret_cast<const char*>(&Header), sizeof(Header));
		std::printf("전송 완료.\n");
		std::printf("기대 결과: 서버가 '[차단]' 출력 후 이 연결만 끊음.\n");
		std::printf("          서버 프로세스는 계속 살아있어야 함.\n");
	}

	// 파일이나 파이프로 입력을 넣으면 맨 앞에 UTF-8 BOM 이 붙고,
	// 윈도우 줄바꿈(CRLF)이면 끝에 \r 이 남는다.
	// 그대로 두면 "/dead" 가 명령으로 인식되지 않고 채팅 메시지로 나간다.
	void SanitizeLine(std::string& Line)
	{
		if (Line.size() >= 3 &&
		    static_cast<unsigned char>(Line[0]) == 0xEF &&
		    static_cast<unsigned char>(Line[1]) == 0xBB &&
		    static_cast<unsigned char>(Line[2]) == 0xBF)
		{
			Line.erase(0, 3);
		}
		while (!Line.empty() && (Line.back() == '\r' || Line.back() == '\n'))
		{
			Line.pop_back();
		}
	}

	void RunInteractive()
	{
		std::printf("\n=== 대화형 모드 ===\n");
		std::printf("  /all      전체 채널로 전환\n");
		std::printf("  /team     팀 채널로 전환\n");
		std::printf("  /deadchan 사망 채널로 전환\n");
		std::printf("  /dead     내 상태를 사망으로\n");
		std::printf("  /alive    내 상태를 생존으로\n");
		std::printf("  /q        종료\n\n");

		EChatChannel Current = EChatChannel::All;
		std::string Line;

		while (GRunning && std::getline(std::cin, Line))
		{
			SanitizeLine(Line);

			if (Line.empty())          { continue; }
			if (Line == "/q")          { break; }
			if (Line == "/all")        { Current = EChatChannel::All;  std::printf("-> 전체 채널\n"); continue; }
			if (Line == "/team")       { Current = EChatChannel::Team; std::printf("-> 팀 채널\n");   continue; }
			if (Line == "/deadchan")   { Current = EChatChannel::Dead; std::printf("-> 사망 채널\n"); continue; }
			if (Line == "/dead")       { SetDead(true);  continue; }
			if (Line == "/alive")      { SetDead(false); continue; }

			const std::vector<char> Packet = BuildChatPacket(Current, Line);
			if (!SendAll(GSock, Packet.data(), static_cast<int32_t>(Packet.size())))
			{
				std::printf("[전송 실패]\n");
				break;
			}
		}
	}
}

int main(int argc, char** argv)
{
#ifdef _WIN32
	::SetConsoleOutputCP(CP_UTF8);
	::SetConsoleCP(CP_UTF8);
#endif

	if (argc < 4)
	{
		std::printf("사용법: %s <ip> <port> <name> [split|merge|bad]\n", argv[0]);
		return 1;
	}

	const std::string Mode = (argc >= 5) ? argv[4] : "";

	if (!NetInit())
	{
		std::printf("NetInit() 실패\n");
		return 1;
	}

	GSock = ::socket(PF_INET, SOCK_STREAM, 0);
	if (GSock == kInvalidSocket)
	{
		std::printf("socket() 실패: %d\n", LastNetError());
		return 1;
	}

	sockaddr_in ServerAddr{};
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port   = htons(static_cast<uint16_t>(std::atoi(argv[2])));
	if (::inet_pton(AF_INET, argv[1], &ServerAddr.sin_addr) != 1)
	{
		std::printf("잘못된 IP: %s\n", argv[1]);
		return 1;
	}

	if (::connect(GSock, reinterpret_cast<sockaddr*>(&ServerAddr), sizeof(ServerAddr)) != 0)
	{
		std::printf("connect() 실패: %d\n", LastNetError());
		return 1;
	}
	std::printf("서버에 연결되었습니다.\n");

	// 수신 스레드가 영원히 블록되지 않도록 타임아웃을 건다.
	SetRecvTimeout(GSock, 1000);

	std::thread Receiver(RecvThread);

	// 팀 ID 는 이름 길이의 홀짝으로 대충 나눈다. 팀 채널 테스트용.
	const int32_t TeamId = static_cast<int32_t>(std::strlen(argv[3]) % 2);
	DoLogin(argv[3], TeamId);

	// LoginAck 를 받을 시간을 준다.
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	if      (Mode == "split") { RunSplitTest(); std::this_thread::sleep_for(std::chrono::seconds(1)); }
	else if (Mode == "merge") { RunMergeTest(); std::this_thread::sleep_for(std::chrono::seconds(1)); }
	else if (Mode == "bad")   { RunBadTest();   std::this_thread::sleep_for(std::chrono::seconds(2)); }
	else                      { RunInteractive(); }

	// --- 종료 절차 ---
	// 1) FIN 을 보내 아직 큐에 남은 송신 데이터를 확실히 내보낸다.
	//    여기서 곧바로 CloseSocket 을 부르면 마지막으로 보낸 메시지가 유실된다.
	//    (recv 에 블록된 다른 스레드가 있는 소켓을 닫으면 송신 버퍼가 버려진다)
	ShutdownSend(GSock);

	// 2) 서버가 이쪽 FIN 을 보고 연결을 닫으면 수신 스레드가 recv==0 을 보고 끝난다.
	if (Receiver.joinable())
	{
		Receiver.join();
	}

	// 3) 양방향이 모두 정리된 뒤에 닫는다.
	GRunning = false;
	CloseSocket(GSock);
	GSock = kInvalidSocket;

	NetShutdown();
	return 0;
}
