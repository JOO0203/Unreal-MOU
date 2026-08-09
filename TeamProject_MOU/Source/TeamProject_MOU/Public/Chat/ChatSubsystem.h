// MOU 채팅 - 채팅 시스템의 진입점.
//
// [팀원이 알아야 할 것 - 요약]
//   채팅을 쓰려면 이 서브시스템만 알면 된다. 소켓이나 스레드는 볼 필요 없다.
//   블루프린트에서:  Get Chat Subsystem -> Connect To Chat Server -> Login -> Send Chat
//   메시지 수신은 OnChatMessageReceived 델리게이트에 바인딩한다.
//
// [왜 GameInstanceSubsystem 인가]
//   - 레벨을 이동해도(리슨서버 트래블 포함) 파괴되지 않아 연결이 유지된다.
//     PlayerController 에 붙이면 트래블 때마다 재접속해야 한다.
//   - PIE 에서 클라이언트 창을 N개 띄우면 GameInstance 도 N개 생긴다.
//     즉 이 서브시스템도 N개가 되고, 채팅 연결도 자동으로 N개가 된다.
//     별도 작업 없이 다중 클라이언트 채팅 테스트가 된다.
//   - GameMode 에 붙이면 서버에만 존재해서 클라이언트가 못 쓴다.
//
// [게임 로직과의 관계]
//   이 시스템은 게임의 리슨서버 리플리케이션과 완전히 분리되어 있다.
//   Server RPC / Multicast 를 쓰지 않고, ChatServer.exe 로 가는 별도 TCP 소켓을 쓴다.
//   따라서 호스트가 게임을 나가도 채팅은 끊기지 않는다.
//
//   현재 미해결(8단계 예정): TeamId 와 생사 여부의 권위자는 게임(리슨서버)인데
//   채팅 서버는 그 정보를 모른다. 지금은 클라이언트가 Login/SetDeadForTest 로
//   직접 알려주고 있어서 위조가 가능하다. 리슨서버가 채팅 서버에 미러링하도록 바꿔야 한다.

#pragma once

#include "CoreMinimal.h"
#include "Chat/ChatTypes.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ChatSubsystem.generated.h"

class FChatClientRunnable;
class FRunnableThread;

/** 채팅 한 줄을 받았을 때. UI 는 여기 바인딩해서 로그를 채운다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChatMessageReceived, const FChatMessage&, Message);

/** 연결 상태가 바뀌었을 때. Detail 에는 실패 사유 같은 부가 설명이 들어온다(없을 수도 있다). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChatStateChanged, EChatConnectionState, NewState, const FString&, Detail);

/** 로그인이 끝나 내 신원(UserId/이름/팀)이 확정됐을 때. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChatLoginCompleted, const FChatLoginResult&, Result);

/**
 * 채팅 서버 연결의 소유자.
 *
 * 역할:
 *   1. 워커 스레드(FChatClientRunnable)의 생성과 파괴
 *   2. 게임 스레드 Tick 에서 워커의 큐를 비우고 델리게이트로 전파
 *   3. 블루프린트/UMG 가 쓸 API 제공 (패킷 조립은 여기서 한다)
 */
UCLASS()
class TEAMPROJECT_MOU_API UChatSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// --- UGameInstanceSubsystem ------------------------------------------
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- 블루프린트 API ---------------------------------------------------

	/**
	 * 아무 데서나 서브시스템을 얻는 헬퍼. 블루프린트에서 노드 하나로 쓸 수 있다.
	 * WorldContextObject 는 위젯이나 액터의 self 를 넣으면 된다.
	 */
	UFUNCTION(BlueprintPure, Category = "MOU|Chat", meta = (WorldContext = "WorldContextObject"))
	static UChatSubsystem* Get(const UObject* WorldContextObject);

	/**
	 * 채팅 서버에 접속을 시작한다. 즉시 반환하고 실제 접속은 워커 스레드에서 진행된다.
	 * 접속 결과는 OnChatStateChanged 로 알려준다.
	 *
	 * 실패해도 자동으로 재시도하므로, 서버를 나중에 켜도 알아서 붙는다.
	 * 이미 연결 중이면 아무 것도 하지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Chat")
	void ConnectToChatServer(const FString& InHost = TEXT("127.0.0.1"), int32 InPort = 9000);

	/**
	 * 로그인 요청. 서버가 UserId 를 부여하고 LoginAck 로 돌려준다.
	 *
	 * 아직 연결 전이라면 요청을 보관했다가 연결되는 순간 자동으로 보낸다.
	 * 그래서 ConnectToChatServer 직후에 바로 불러도 된다.
	 *
	 * 주의: 여기 넣은 이름/팀은 서버가 검증하지 않는다 (4단계 한계).
	 *       인증은 8단계에서 리슨서버 티켓 방식으로 붙일 예정이다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Chat")
	void Login(const FString& DisplayName, int32 TeamId);

	/**
	 * 채팅을 보낸다. 로그인 전에 부르면 서버가 무시하므로 여기서 미리 막는다.
	 *
	 * 텍스트가 프로토콜 상한(UTF-8 기준 512바이트, 한글 약 170자)을 넘으면
	 * 문자 경계에 맞춰 자른다. 자르지 않고 보내면 서버가 연결을 끊어버린다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Chat")
	void SendChat(EChatChannelBP Channel, const FString& Text);

	/**
	 * 내 생사 상태를 서버에 알린다. 사망 채널 발화 자격 판정에 쓰인다.
	 *
	 * [임시] 이름 그대로 테스트용이다. 지금은 클라이언트가 자기 상태를 마음대로
	 * 바꿀 수 있어서 살아있는 사람이 사망 채널을 엿볼 수 있다.
	 * 8단계에서 리슨서버만 보낼 수 있도록 잠글 것이므로, 게임 로직에서 이 함수를
	 * 호출하는 코드를 늘리지 말 것.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Chat", meta = (DevelopmentOnly))
	void SetDeadForTest(bool bDead);

	/** 연결을 끊고 워커 스레드를 정리한다. 재접속하려면 ConnectToChatServer 를 다시 부른다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Chat")
	void Disconnect();

	UFUNCTION(BlueprintPure, Category = "MOU|Chat")
	EChatConnectionState GetConnectionState() const { return ConnectionState; }

	/** 서버가 확정한 내 신원. 로그인 전에는 bSuccess 가 false 다. */
	UFUNCTION(BlueprintPure, Category = "MOU|Chat")
	FChatLoginResult GetLoginResult() const { return LoginResult; }

	// --- 델리게이트 -------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "MOU|Chat")
	FOnChatMessageReceived OnChatMessageReceived;

	UPROPERTY(BlueprintAssignable, Category = "MOU|Chat")
	FOnChatStateChanged OnChatStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "MOU|Chat")
	FOnChatLoginCompleted OnChatLoginCompleted;

private:
	/** 게임 스레드 틱. 워커 큐를 비우고 델리게이트를 브로드캐스트한다. */
	bool Tick(float DeltaTime);

	/** 워커 스레드를 Stop -> 종료 대기 -> 파괴 순서로 정리한다. */
	void ShutdownClient();

	void SetConnectionState(EChatConnectionState NewState, const FString& Detail = FString());

	/** 보관해둔 로그인 요청을 실제로 전송한다. */
	void SendPendingLogin();

	UPROPERTY()
	EChatConnectionState ConnectionState = EChatConnectionState::Disconnected;

	UPROPERTY()
	FChatLoginResult LoginResult;

	/**
	 * 소켓 워커. 게임 스레드에서 생성/파괴하고, 그 사이에는 워커 스레드가 이 객체를 쓴다.
	 *
	 * 스마트 포인터를 쓰지 않고 원시 포인터로 두는 이유:
	 * 러너블의 수명은 "스레드가 끝났는가" 에만 달려 있고, 그 판단은 ShutdownClient 하나에서만
	 * 내린다. 참조 카운트로 관리하면 오히려 스레드가 살아있는데 객체가 먼저 사라질 여지가 생긴다.
	 * 반드시 ShutdownClient 를 거쳐서만 해제할 것. delete 를 직접 부르지 말 것.
	 */
	FChatClientRunnable* ChatClient = nullptr;
	FRunnableThread*     ChatThread = nullptr;

	FTSTicker::FDelegateHandle TickHandle;

	// 연결 전에 Login() 이 호출된 경우 여기 보관했다가 Connected 시점에 보낸다.
	bool    bHasPendingLogin = false;
	FString PendingName;
	int32   PendingTeamId = -1;
};
