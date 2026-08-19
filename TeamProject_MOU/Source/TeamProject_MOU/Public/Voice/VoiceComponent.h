// MOU 음성 - 네트워크 창구.
//
// [이 파일이 시스템 어디에 있나]
//
//   [말하는 클라]                        [듣는 클라]
//     UVoiceSubsystem                      UVoicePlaybackComponent
//       │ SendVoiceFrame()                    ▲ HandleFrame()
//       ▼                                     │
//   ★ UVoiceComponent ──RPC──▶ 서버 ──RPC──▶ UVoiceComponent ★
//                          (UVoiceRouter 가
//                           수신자를 결정)
//
// **이 컴포넌트는 음성을 전혀 모른다.** Opus 바이트를 받아 보내고, 받아서
// 넘기는 것이 전부다. 그래서 라우팅 규칙이 바뀌어도(V6 무전) 이 파일은 안 바뀐다.
//
// [★ 왜 폰이 아니라 PlayerController 에 붙는가]
//
//   **PlayerController 는 자기 소유 클라이언트에게 항상 relevant 하다.**
//   폰에 붙이면 "멀어서 relevant 하지 않은 폰" 의 RPC 가 **조용히 사라진다.**
//   근접 음성만 보면 "멀면 안 들리는 게 맞잖아" 싶지만, V6 의 무전은
//   **거리와 무관해야 하므로 치명적이다** - 맵 반대편 무전이 안 가는데
//   에러도 안 나서 원인을 찾기 매우 어렵다.
//
//   폰이 죽어도 컨트롤러는 살아있다는 것도 중요하다(관전 중 무전 수신, V6).
//
// [★ 리슨서버 호스트는 RPC 를 "타지 않는다"]
//
//   호스트는 서버이자 클라라 같은 프로세스다. 그래서 언리얼이 RPC 를
//   네트워크로 보내지 않고 **함수를 그 자리에서 직접 부른다**(callspace = Local).
//   결과적으로 코드는 똑같이 동작하지만, 여기에 `if (IsLocalController()) return;`
//   같은 분기를 넣으면 **호스트만 아무 소리도 안 들리는** 버그가 된다.
//   이 파일에 그런 분기가 없는 것은 의도된 것이다(15절).
//
// [대응하는 문서]
//   VOICE_INTEGRATION.md 5절(왜 리슨서버로), 6절(클래스), 10절(패킷), 14절 V3

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Voice/VoiceTypes.h"
#include "VoiceComponent.generated.h"

class APlayerController;
class UVoicePlaybackComponent;

/**
 * 음성 프레임의 송수신 창구. **모든 PlayerController 에 하나씩 있어야 한다.**
 *
 * 양쪽 끝에 존재해야 RPC 가 목적지를 찾으므로, 런타임에 붙이지 않고
 * PlayerController 생성자에서 만든다(`CreateDefaultSubobject`).
 */
UCLASS(ClassGroup = (MOU), meta = (BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UVoiceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoiceComponent();

	/** 주어진 컨트롤러의 음성 컴포넌트를 찾는다. 없으면 null(생성자에서 안 만든 것이다). */
	static UVoiceComponent* Find(APlayerController* PC);

	// --- 보내는 쪽 (로컬 클라) ---------------------------------------------

	/**
	 * 압축된 20ms 조각 하나를 서버로 보낸다. **로컬 컨트롤러에서만 부른다.**
	 *
	 * Seq 는 이 컴포넌트가 붙인다 - 호출자(UVoiceSubsystem)는 "무슨 소리인지" 만
	 * 알면 되고 "몇 번째 조각인지" 는 전송 계층의 관심사다.
	 */
	void SendVoiceFrame(const TArray<uint8>& Opus, float Loudness01, EVoiceMode Mode, EVoiceRoute Route);

	// --- 받는 쪽 (서버가 호출) ---------------------------------------------

	/** 서버가 이 컨트롤러의 소유자에게 프레임을 내려보낸다. **서버에서만 부른다.** */
	void DeliverToOwner(const FVoiceFrameOut& Frame);

	// --- RPC ---------------------------------------------------------------

	/**
	 * 클라 -> 서버. 20ms 조각 하나.
	 *
	 * Unreliable 인 이유: 늦게 도착한 목소리는 **쓰레기다.** 재전송으로
	 * 200ms 전 소리를 받아봐야 이미 지나간 대화다. 잃어버린 프레임은
	 * 그냥 잃어버리는 것이 음성의 정상 동작이다(5절).
	 */
	UFUNCTION(Server, Unreliable, WithValidation)
	void ServerSendVoiceFrame(const FVoiceFrame& Frame);

	/** 서버 -> 클라. 들을 자격이 있다고 서버가 판단한 사람에게만 간다. */
	UFUNCTION(Client, Unreliable)
	void ClientReceiveVoiceFrame(const FVoiceFrameOut& Frame);

	// --- 진단 ---------------------------------------------------------------

	int32 GetFramesSent()     const { return FramesSent; }
	int32 GetFramesDelivered() const { return FramesDelivered; }
	int32 GetFramesRejected() const { return FramesRejected; }

	/** 이 컴포넌트의 소유 PlayerController. 아니면 null. */
	APlayerController* GetOwningPlayerController() const;

private:
	/**
	 * 다음에 보낼 프레임 번호.
	 *
	 * uint16 이라 약 22분(65536 x 20ms)마다 순환한다. 받는 쪽이 **차이를 uint16 으로
	 * 계산하면** 순환이 자동으로 처리되므로 문제되지 않는다
	 * (UVoicePlaybackComponent::HandleFrame 의 순서 검사 참고).
	 */
	uint16 NextSeq = 0;

	int32 FramesSent      = 0;
	int32 FramesDelivered = 0;
	int32 FramesRejected  = 0;
};
