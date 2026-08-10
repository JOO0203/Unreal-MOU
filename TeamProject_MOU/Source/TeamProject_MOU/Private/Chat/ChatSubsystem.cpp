// MOU 채팅 - 서브시스템 구현.
//
// 이 파일이 하는 일은 결국 3가지다.
//   1. 워커 스레드 수명 관리 (생성 / 안전한 파괴)
//   2. 블루프린트가 부른 함수를 패킷 바이트로 조립해서 워커에게 넘기기
//   3. 워커가 큐에 넣어둔 결과를 게임 스레드에서 꺼내 델리게이트로 뿌리기
//
// 패킷 구조체(MOU::LoginReqBody 등)를 직접 다루는 곳은 여기와 ChatClientRunnable.cpp 뿐이다.
// UI 나 게임플레이 코드는 이 파일 위쪽(블루프린트 API)만 쓴다.

#include "Chat/ChatSubsystem.h"

#include "Chat/ChatClientRunnable.h"
#include "Chat/ChatFraming.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/RunnableThread.h"

DEFINE_LOG_CATEGORY(LogMOUChat);

namespace
{
	/**
	 * 로그 표시용 채널 이름.
	 * 서버 ChatServer.cpp 의 ChannelName() 과 같은 문자열을 쓴다.
	 * 서버 콘솔 로그와 에디터 로그를 나란히 놓고 대조할 때 편하다.
	 */
	const TCHAR* ToChannelName(EChatChannelBP Channel)
	{
		switch (Channel)
		{
		case EChatChannelBP::All:     return TEXT("전체");
		case EChatChannelBP::Team:    return TEXT("팀");
		case EChatChannelBP::Dead:    return TEXT("사망");
		case EChatChannelBP::Whisper: return TEXT("귓속말");
		case EChatChannelBP::System:  return TEXT("시스템");
		default:                      return TEXT("알수없음");
		}
	}
}

// ---------------------------------------------------------------------------
// 수명 관리
// ---------------------------------------------------------------------------

void UChatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 여기서 자동 접속하지 않는다.
	// 접속 시점(타이틀 화면인지, 인게임 진입 후인지)은 게임 흐름에 따라 달라야 하고,
	// PIE 로 잠깐 레벨만 확인할 때 매번 서버에 붙는 것도 원치 않기 때문이다.
	// 접속은 UI 나 GameMode 가 ConnectToChatServer() 를 호출해서 시작한다.

	// 게임 스레드 틱 등록. 워커가 쌓아둔 큐를 여기서 비운다.
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UChatSubsystem::Tick));

	UE_LOG(LogMOUChat, Log, TEXT("채팅 서브시스템 초기화 완료. 접속하려면 ConnectToChatServer 를 호출한다."));
}

void UChatSubsystem::Deinitialize()
{
	// 순서가 중요하다.
	// 틱을 먼저 끊어야 워커를 정리하는 도중에 Tick 이 죽은 큐를 읽는 일이 없다.
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}

	ShutdownClient();

	Super::Deinitialize();
}

void UChatSubsystem::ShutdownClient()
{
	if (ChatThread != nullptr)
	{
		// 1) 종료 요청 플래그를 세운다
		if (ChatClient != nullptr)
		{
			ChatClient->Stop();
		}

		// 2) 워커가 Run() 을 빠져나올 때까지 반드시 기다린다.
		//    bShouldWait 를 false 로 두면 아직 살아있는 스레드가 이미 해제된 큐를 건드려서
		//    "PIE 를 껐다 켜면 에디터가 통째로 죽는" 증상이 난다. 언리얼에서 가장 흔한 실수다.
		ChatThread->Kill(/*bShouldWait=*/true);

		delete ChatThread;
		ChatThread = nullptr;
	}

	// 3) 스레드가 완전히 끝난 뒤에 러너블을 해제한다.
	//    순서를 뒤집으면 아직 Run() 안에 있는 워커가 해제된 메모리를 만진다.
	delete ChatClient;
	ChatClient = nullptr;

	if (ConnectionState != EChatConnectionState::Disconnected)
	{
		SetConnectionState(EChatConnectionState::Disconnected, TEXT("클라이언트 종료"));
	}
	LoginResult = FChatLoginResult();
}

// ---------------------------------------------------------------------------
// 블루프린트 API
// ---------------------------------------------------------------------------

UChatSubsystem* UChatSubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr)
	{
		return nullptr;
	}

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UChatSubsystem>() : nullptr;
}

void UChatSubsystem::ConnectToChatServer(const FString& InHost, int32 InPort)
{
	if (ChatThread != nullptr)
	{
		// 이미 워커가 돌고 있다. 워커 자체가 재연결 루프를 갖고 있으므로
		// 끊긴 상태여도 새로 만들 필요가 없다.
		UE_LOG(LogMOUChat, Log, TEXT("이미 채팅 클라이언트가 동작 중이다. 접속 요청을 무시한다."));
		return;
	}

	ChatClient = new FChatClientRunnable(InHost, InPort);

	// 스레드 이름에 접속 대상을 넣어두면 PIE 창을 여러 개 띄웠을 때
	// 디버거의 스레드 목록에서 구분하기 쉽다.
	ChatThread = FRunnableThread::Create(
		ChatClient,
		*FString::Printf(TEXT("MOUChatClient_%s_%d"), *InHost, InPort),
		0,
		TPri_BelowNormal);   // 채팅은 게임 프레임보다 우선순위가 낮아도 된다

	if (ChatThread == nullptr)
	{
		UE_LOG(LogMOUChat, Error, TEXT("채팅 워커 스레드 생성 실패"));
		delete ChatClient;
		ChatClient = nullptr;
		return;
	}

	SetConnectionState(EChatConnectionState::Connecting, FString::Printf(TEXT("%s:%d"), *InHost, InPort));
}

FString UChatSubsystem::GetLoginResultText(EChatLoginResultBP Result)
{
	switch (Result)
	{
	case EChatLoginResultBP::Success:         return TEXT("성공");
	case EChatLoginResultBP::VersionMismatch: return TEXT("서버와 버전이 다릅니다. 양쪽을 다시 빌드해야 합니다.");
	case EChatLoginResultBP::InvalidRequest:  return TEXT("잘못된 요청입니다.");
	case EChatLoginResultBP::AccountNotFound: return TEXT("존재하지 않는 아이디입니다.");
	case EChatLoginResultBP::WrongPassword:   return TEXT("비밀번호가 올바르지 않습니다.");
	case EChatLoginResultBP::DuplicateId:     return TEXT("이미 사용 중인 아이디입니다.");
	case EChatLoginResultBP::InvalidFormat:   return TEXT("아이디 또는 비밀번호 형식이 올바르지 않습니다.");
	case EChatLoginResultBP::ServerError:     return TEXT("서버 오류입니다. 잠시 후 다시 시도해 주세요.");
	default:                                  return TEXT("알 수 없는 오류입니다.");
	}
}

bool UChatSubsystem::ValidateCredentials(const FString& LoginId, const FString& Password, FString& OutReason)
{
	// 서버와 같은 규칙을 쓴다. 길이는 UTF-8 바이트 기준이라 한글 아이디면 글자 수보다 커진다.
	const int32 IdBytes = MOUChat::GetUtf8Length(LoginId);
	const int32 PwBytes = MOUChat::GetUtf8Length(Password);

	if (IdBytes < static_cast<int32>(MOU::kMinLoginIdLen))
	{
		OutReason = FString::Printf(TEXT("아이디는 %d자 이상이어야 합니다."), MOU::kMinLoginIdLen);
		return false;
	}
	if (IdBytes >= static_cast<int32>(MOU::kMaxLoginIdLen))
	{
		OutReason = FString::Printf(TEXT("아이디가 너무 깁니다. (%d바이트 미만)"), MOU::kMaxLoginIdLen);
		return false;
	}
	if (PwBytes < static_cast<int32>(MOU::kMinPasswordLen))
	{
		OutReason = FString::Printf(TEXT("비밀번호는 %d자 이상이어야 합니다."), MOU::kMinPasswordLen);
		return false;
	}
	if (PwBytes >= static_cast<int32>(MOU::kMaxPasswordLen))
	{
		OutReason = FString::Printf(TEXT("비밀번호가 너무 깁니다. (%d바이트 미만)"), MOU::kMaxPasswordLen);
		return false;
	}

	OutReason.Empty();
	return true;
}

void UChatSubsystem::Login(const FString& LoginId, const FString& Password, int32 TeamId)
{
	// 요청은 항상 보관해둔다.
	// 연결이 끊겼다가 자동 재접속했을 때 이 값으로 다시 로그인해야 하기 때문이다.
	PendingLoginId   = LoginId;
	PendingPassword  = Password;
	PendingTeamId    = TeamId;
	bHasPendingLogin = true;

	if (ConnectionState == EChatConnectionState::Connected
		|| ConnectionState == EChatConnectionState::LoggedIn)
	{
		SendPendingLogin();
	}
	else
	{
		// 아직 TCP 가 안 붙었다. 붙는 순간 Tick 의 Connected 처리에서 자동으로 보낸다.
		// 비밀번호는 절대 로그에 남기지 않는다.
		UE_LOG(LogMOUChat, Log, TEXT("연결 전이라 로그인 요청을 보관한다: %s (팀 %d)"), *LoginId, TeamId);
	}
}

void UChatSubsystem::RegisterAccount(const FString& LoginId, const FString& Password, const FString& Nickname)
{
	// 연결 전에 불릴 수 있으므로 일단 보관한다.
	// 지금 바로 EnqueuePacket 하면, 연결이 성사되는 순간 워커가 송신 큐를 비우면서
	// 이 패킷까지 같이 버린다(그 비우기는 낡은 패킷이 LoginReq 를 앞지르는 것을 막는 장치다).
	PendingRegisterId       = LoginId;
	PendingRegisterPassword = Password;
	PendingRegisterNickname = Nickname;
	bHasPendingRegister     = true;

	if (ConnectionState == EChatConnectionState::Connected
		|| ConnectionState == EChatConnectionState::LoggedIn)
	{
		SendPendingRegister();
	}
	else
	{
		UE_LOG(LogMOUChat, Log, TEXT("연결 전이라 가입 요청을 보관한다: %s"), *LoginId);
	}
}

void UChatSubsystem::SendPendingRegister()
{
	if (ChatClient == nullptr || !bHasPendingRegister)
	{
		return;
	}

	MOU::RegisterReqBody Request{};
	Request.Version = MOU::kProtocolVersion;
	MOUChat::CopyFixedString(Request.LoginId,  static_cast<int32>(MOU::kMaxLoginIdLen),  PendingRegisterId);
	MOUChat::CopyFixedString(Request.Password, static_cast<int32>(MOU::kMaxPasswordLen), PendingRegisterPassword);
	MOUChat::CopyFixedString(Request.Nickname, static_cast<int32>(MOU::kMaxNameLen),     PendingRegisterNickname);

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::RegisterReq, &Request, sizeof(Request)))
	{
		ChatClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUChat, Log, TEXT("RegisterReq 전송: %s (닉네임 %s)"),
			*PendingRegisterId, *PendingRegisterNickname);
	}
}

void UChatSubsystem::SendPendingLogin()
{
	if (ChatClient == nullptr || !bHasPendingLogin)
	{
		return;
	}

	MOU::LoginReqBody Request{};
	Request.Version = MOU::kProtocolVersion;   // 서버가 이 값을 검사하고 다르면 거부한다
	MOUChat::CopyFixedString(Request.LoginId,  static_cast<int32>(MOU::kMaxLoginIdLen),  PendingLoginId);
	MOUChat::CopyFixedString(Request.Password, static_cast<int32>(MOU::kMaxPasswordLen), PendingPassword);
	Request.TeamId = PendingTeamId;

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::LoginReq, &Request, sizeof(Request)))
	{
		ChatClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUChat, Log, TEXT("LoginReq 전송: %s (팀 %d)"), *PendingLoginId, PendingTeamId);
	}
}

void UChatSubsystem::SendChat(EChatChannelBP Channel, const FString& Text)
{
	if (ChatClient == nullptr)
	{
		UE_LOG(LogMOUChat, Warning, TEXT("채팅 클라이언트가 없다. ConnectToChatServer 를 먼저 호출한다."));
		return;
	}

	// 서버는 로그인 전 채팅을 조용히 버린다(연결은 유지). 사용자가 원인을 알 수 없으므로 여기서 알린다.
	if (ConnectionState != EChatConnectionState::LoggedIn)
	{
		UE_LOG(LogMOUChat, Warning, TEXT("아직 로그인 전이라 채팅을 보낼 수 없다. (현재 상태 %d)"),
			static_cast<int32>(ConnectionState));
		return;
	}

	// System 채널은 서버만 만든다. 보내봐야 서버가 버리므로 여기서 막는다.
	if (Channel == EChatChannelBP::System)
	{
		UE_LOG(LogMOUChat, Warning, TEXT("System 채널은 클라이언트가 보낼 수 없다."));
		return;
	}

	// 프로토콜의 TextLen 은 글자 수가 아니라 UTF-8 바이트 수다.
	// 상한(512바이트)을 넘겨 보내면 서버가 Malformed 로 판단해 연결을 끊어버리므로
	// 반드시 여기서 잘라서 보낸다.
	TArray<uint8> TextBytes;
	const int32 OriginalLength = MOUChat::GetUtf8Length(Text);
	const int32 SentLength     = MOUChat::EncodeUtf8Clamped(Text, static_cast<int32>(MOU::kMaxTextLen), TextBytes);

	if (SentLength <= 0)
	{
		return;   // 빈 메시지는 보내지 않는다
	}
	if (SentLength < OriginalLength)
	{
		UE_LOG(LogMOUChat, Warning, TEXT("메시지가 상한(%u바이트)을 넘어 잘렸다. %d -> %d 바이트"),
			MOU::kMaxTextLen, OriginalLength, SentLength);
	}

	MOU::ChatSendBody Body{};
	Body.TargetUserId = 0;                                   // 귓속말(9단계) 전용 필드. 그 외에는 0
	Body.TextLen      = static_cast<uint16>(SentLength);
	Body.Channel      = static_cast<uint8>(Channel);

	// 바디가 "고정부 + 가변 텍스트" 두 조각이라 BuildPacket 의 2조각 버전을 쓴다.
	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::ChatSend,
			&Body, sizeof(Body),
			TextBytes.GetData(), static_cast<uint32>(SentLength)))
	{
		ChatClient->EnqueuePacket(MoveTemp(Packet));
	}
}

void UChatSubsystem::SetDeadForTest(bool bDead)
{
	if (ChatClient == nullptr || ConnectionState != EChatConnectionState::LoggedIn)
	{
		UE_LOG(LogMOUChat, Warning, TEXT("로그인 후에만 생사 상태를 바꿀 수 있다."));
		return;
	}

	MOU::SetDeadBody Body{};
	Body.UserId = static_cast<uint64>(LoginResult.UserId);   // 서버는 세션 값을 쓰므로 참고용
	Body.bDead  = bDead ? 1 : 0;

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::SetDead, &Body, sizeof(Body)))
	{
		ChatClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUChat, Log, TEXT("SetDead 전송: %s"), bDead ? TEXT("사망") : TEXT("생존"));
	}
}

void UChatSubsystem::Disconnect()
{
	bHasPendingLogin = false;   // 사용자가 의도적으로 끊은 것이므로 자동 재로그인하지 않는다
	PendingPassword.Empty();    // 비밀번호를 필요 이상으로 메모리에 두지 않는다
	ShutdownClient();
}

// ---------------------------------------------------------------------------
// 게임 스레드 틱 - 워커 -> UI 방향의 유일한 통로
// ---------------------------------------------------------------------------

bool UChatSubsystem::Tick(float /*DeltaTime*/)
{
	if (ChatClient == nullptr)
	{
		return true;   // false 를 돌려주면 틱이 영구 해제된다. 항상 true
	}

	// 1) 상태 변화 처리
	FChatClientEvent Event;
	while (ChatClient->DequeueEvent(Event))
	{
		switch (Event.Type)
		{
		case EChatClientEventType::Connecting:
			SetConnectionState(EChatConnectionState::Connecting, Event.Detail);
			break;

		case EChatClientEventType::Connected:
			SetConnectionState(EChatConnectionState::Connected, Event.Detail);
			// 가입을 먼저 보낸다. 같은 TCP 스트림이라 서버가 이 순서대로 처리하므로,
			// "가입 후 곧바로 로그인" 이 한 번의 연결로 끝난다.
			SendPendingRegister();
			// 접속 전에 Login() 이 호출됐거나, 끊겼다 재접속한 경우 여기서 자동으로 로그인한다.
			SendPendingLogin();
			break;

		case EChatClientEventType::ConnectFailed:
			// 워커가 알아서 재시도하므로 여기서 할 일은 UI 에 알리는 것뿐이다.
			SetConnectionState(EChatConnectionState::Disconnected, Event.Detail);
			break;

		case EChatClientEventType::LoginAck:
			LoginResult = Event.Login;
			if (LoginResult.bSuccess)
			{
				SetConnectionState(EChatConnectionState::LoggedIn, LoginResult.Name);
				UE_LOG(LogMOUChat, Log, TEXT("로그인 완료. UserId=%lld, 이름=%s, 팀=%d"),
					LoginResult.UserId, *LoginResult.Name, LoginResult.TeamId);
			}
			else if (LoginResult.Result == EChatLoginResultBP::VersionMismatch)
			{
				// 재시도해도 계속 실패한다. 원인을 바로 알 수 있게 상세히 남긴다.
				UE_LOG(LogMOUChat, Error,
					TEXT("프로토콜 버전 불일치로 로그인이 거부됐다. 클라이언트=%d, 서버=%d. ")
					TEXT("ChatServer.exe 와 언리얼 프로젝트를 같은 커밋으로 다시 빌드할 것."),
					static_cast<int32>(MOU::kProtocolVersion), LoginResult.ServerVersion);
			}
			else
			{
				// 아이디/비밀번호 실수는 흔한 일이라 사용자가 고칠 수 있게 사유를 그대로 남긴다.
				UE_LOG(LogMOUChat, Warning, TEXT("서버가 로그인을 거부했다: %s"),
					*UChatSubsystem::GetLoginResultText(LoginResult.Result));

				// 인증 실패는 재시도해도 같은 결과다. 보관해둔 요청을 지워
				// 재접속 때마다 틀린 비밀번호를 자동 재전송하는 것을 막는다.
				if (LoginResult.Result == EChatLoginResultBP::AccountNotFound
					|| LoginResult.Result == EChatLoginResultBP::WrongPassword
					|| LoginResult.Result == EChatLoginResultBP::InvalidFormat)
				{
					bHasPendingLogin = false;
					PendingPassword.Empty();
				}
			}
			OnChatLoginCompleted.Broadcast(LoginResult);
			break;

		case EChatClientEventType::RegisterAck:
			// 응답을 받았으므로 보관본을 지운다.
			// 안 지우면 재접속할 때마다 가입을 다시 시도해 "이미 있는 아이디" 가 반복된다.
			bHasPendingRegister = false;
			PendingRegisterPassword.Empty();

			if (Event.Login.bSuccess)
			{
				UE_LOG(LogMOUChat, Log, TEXT("계정 생성 완료. 이어서 로그인하면 된다."));
			}
			else
			{
				UE_LOG(LogMOUChat, Warning, TEXT("계정 생성 실패: %s"),
					*UChatSubsystem::GetLoginResultText(Event.Login.Result));
			}
			OnChatRegisterCompleted.Broadcast(Event.Login.bSuccess, Event.Login.Result);
			break;

		case EChatClientEventType::Disconnected:
			LoginResult = FChatLoginResult();
			SetConnectionState(EChatConnectionState::Disconnected, Event.Detail);
			break;
		}
	}

	// 2) 수신한 채팅 처리
	FChatMessage Message;
	while (ChatClient->DequeueMessage(Message))
	{
		// UI 가 붙기 전(5단계 이전)에도 동작을 확인할 수 있도록 로그를 남긴다.
		UE_LOG(LogMOUChat, Log, TEXT("[%s] %s: %s"),
			ToChannelName(Message.Channel), *Message.SenderName, *Message.Text);

		// 여기가 워커 스레드 -> 게임 스레드 경계의 끝이다.
		// 게임 스레드에서 부르므로 이 델리게이트 안에서 UMG 위젯을 만들어도 안전하다.
		OnChatMessageReceived.Broadcast(Message);
	}

	return true;
}

void UChatSubsystem::SetConnectionState(EChatConnectionState NewState, const FString& Detail)
{
	if (ConnectionState == NewState)
	{
		return;   // 같은 상태를 반복 브로드캐스트하지 않는다 (재연결 시도 중 로그 폭주 방지)
	}

	ConnectionState = NewState;
	OnChatStateChanged.Broadcast(NewState, Detail);
}

// ---------------------------------------------------------------------------
// 콘솔 명령 - UI 가 없는 4단계에서 동작을 검증하기 위한 것
//
// PIE 에서 ` 키를 눌러 콘솔을 열고 아래 명령을 입력한다.
//   MOU.Chat.Connect 127.0.0.1 9000
//   MOU.Chat.Register player1 secret123 홍길동
//   MOU.Chat.Login player1 secret123 0
//   MOU.Chat.Say 0 안녕하세요          (첫 인자가 채널: 0=전체 1=팀 2=사망)
//   MOU.Chat.Dead 1
//   MOU.Chat.Disconnect
//
// 5단계에서 UMG 가 붙어도 이 명령들은 디버깅용으로 남겨둔다.
// ---------------------------------------------------------------------------

namespace
{
	/**
	 * 콘솔 명령이 실행된 월드에서 채팅 서브시스템을 찾는다.
	 * PIE 창이 여러 개면 "지금 콘솔을 연 창" 의 것이 잡힌다.
	 */
	UChatSubsystem* FindChatSubsystem(UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UChatSubsystem>() : nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs GChatConnectCommand(
		TEXT("MOU.Chat.Connect"),
		TEXT("채팅 서버에 접속한다. 사용법: MOU.Chat.Connect [호스트] [포트]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UChatSubsystem* Chat = FindChatSubsystem(World))
				{
					const FString HostArg = Args.IsValidIndex(0) ? Args[0] : TEXT("127.0.0.1");
					const int32   PortArg = Args.IsValidIndex(1) ? FCString::Atoi(*Args[1]) : 9000;
					Chat->ConnectToChatServer(HostArg, PortArg);
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GChatLoginCommand(
		TEXT("MOU.Chat.Login"),
		TEXT("채팅 서버에 로그인한다. 사용법: MOU.Chat.Login <아이디> <비밀번호> [팀ID]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UChatSubsystem* Chat = FindChatSubsystem(World))
				{
					if (Args.Num() < 2)
					{
						UE_LOG(LogMOUChat, Warning,
							TEXT("사용법: MOU.Chat.Login <아이디> <비밀번호> [팀ID]"));
						return;
					}
					const int32 TeamArg = Args.IsValidIndex(2) ? FCString::Atoi(*Args[2]) : 0;
					Chat->Login(Args[0], Args[1], TeamArg);
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GChatRegisterCommand(
		TEXT("MOU.Chat.Register"),
		TEXT("계정을 만든다. 사용법: MOU.Chat.Register <아이디> <비밀번호> [닉네임]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UChatSubsystem* Chat = FindChatSubsystem(World))
				{
					if (Args.Num() < 2)
					{
						UE_LOG(LogMOUChat, Warning,
							TEXT("사용법: MOU.Chat.Register <아이디> <비밀번호> [닉네임]"));
						return;
					}
					const FString Nick = Args.IsValidIndex(2) ? Args[2] : Args[0];
					Chat->RegisterAccount(Args[0], Args[1], Nick);
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GChatSayCommand(
		TEXT("MOU.Chat.Say"),
		TEXT("채팅을 보낸다. 사용법: MOU.Chat.Say <채널 0~3> <메시지>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				UChatSubsystem* Chat = FindChatSubsystem(World);
				if (Chat == nullptr || Args.Num() < 2)
				{
					return;
				}

				const int32 ChannelValue = FCString::Atoi(*Args[0]);

				// 콘솔은 공백으로 인자를 쪼개므로 두 번째 인자부터 전부 이어 붙여 한 문장으로 만든다.
				TArray<FString> TextParts(Args.GetData() + 1, Args.Num() - 1);
				Chat->SendChat(static_cast<EChatChannelBP>(ChannelValue), FString::Join(TextParts, TEXT(" ")));
			}));

	FAutoConsoleCommandWithWorldAndArgs GChatDeadCommand(
		TEXT("MOU.Chat.Dead"),
		TEXT("생사 상태를 바꾼다(테스트용). 사용법: MOU.Chat.Dead <0|1>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UChatSubsystem* Chat = FindChatSubsystem(World))
				{
					Chat->SetDeadForTest(Args.IsValidIndex(0) ? (FCString::Atoi(*Args[0]) != 0) : true);
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GChatDisconnectCommand(
		TEXT("MOU.Chat.Disconnect"),
		TEXT("채팅 서버와의 연결을 끊는다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				if (UChatSubsystem* Chat = FindChatSubsystem(World))
				{
					Chat->Disconnect();
				}
			}));
}
