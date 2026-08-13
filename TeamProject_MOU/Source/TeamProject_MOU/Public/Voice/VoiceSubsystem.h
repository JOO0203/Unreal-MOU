// MOU 음성 - 음성 시스템의 진입점.
//
// [팀원이 알아야 할 것 - 요약]
//   음성을 쓰려면 이 서브시스템만 알면 된다. 스레드나 오디오 버퍼는 볼 필요 없다.
//   콘솔에서:  MOU.Voice.Loopback 1   -> 내 목소리가 내 헤드폰으로 돌아온다
//              MOU.Voice.Stat         -> 프레임/버퍼/드랍 수
//              MOU.Voice.FakeNoise 1500  -> 마이크 없이 NPC 소음만 발생
//
// [이 파일이 시스템 어디에 있나]
//
//     FVoiceCaptureRunnable (워커 스레드)
//       ↓ TQueue (SPSC)
//   ★ UVoiceSubsystem (게임 스레드)  ← 이 파일. Tick 으로 큐를 비운다
//       ↓ PushSamples
//     UVoiceSynthComponent (오디오 렌더 스레드)
//
// [왜 LocalPlayerSubsystem 인가]
//   마이크는 로컬 플레이어당 하나다. 채팅이 쓰는 GameInstanceSubsystem 으로 하면
//   PIE 다중 창에서 창마다 마이크를 열려고 해서 장치 경합이 난다.
//   (VOICE_INTEGRATION.md 6절 참고)
//
// [현재 구현 단계 - V1]
//   로컬 루프백까지. 네트워크 전송도, Opus 도, 무전기도 아직 없다.
//   이 단계의 목적은 "엔진 API 와 스레드 경계가 설계대로 도는가" 를 확인하는 것이다.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "Voice/VoiceTypes.h"
#include "VoiceSubsystem.generated.h"

class FVoiceCaptureRunnable;
class FRunnableThread;
class UVoiceSynthComponent;

/**
 * 마이크 캡처와 재생을 소유한다.
 *
 * 역할:
 *   1. 캡처 워커 스레드의 생성과 파괴
 *   2. 게임 스레드 Tick 에서 워커의 큐를 비우고 재생 버퍼로 넘김
 *   3. 콘솔 명령 / 블루프린트가 쓸 API 제공
 */
UCLASS()
class TEAMPROJECT_MOU_API UVoiceSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	// --- ULocalPlayerSubsystem --------------------------------------------
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 아무 데서나 서브시스템을 얻는 헬퍼. WorldContextObject 는 위젯/액터의 self. */
	UFUNCTION(BlueprintPure, Category = "MOU|Voice", meta = (WorldContext = "WorldContextObject"))
	static UVoiceSubsystem* Get(const UObject* WorldContextObject);

	/**
	 * 로컬 루프백을 켜고 끈다. 내 목소리가 내 헤드폰으로 즉시 돌아온다.
	 *
	 * V1 검증 전용이다. 네트워크 전송(V3)이 붙으면 이 경로는 디버깅용으로만 남는다.
	 * **헤드폰 없이 켜면 하울링이 난다.**
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Voice")
	void SetLoopbackEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	bool IsLoopbackEnabled() const { return bLoopbackEnabled; }

	/** 마이크가 실제로 열렸는지. 없어도 게임은 정상 진행된다. */
	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	bool IsCaptureReady() const;

	/**
	 * 지금 말하고 있는지 (VAD 판정). NPC 소음 발생 조건이자 UI 표시용이다.
	 * V8 에서 소음 이벤트의 입력이 된다.
	 */
	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	bool IsSpeaking() const { return bIsSpeaking; }

	/**
	 * 마지막 프레임의 음량(0~1).
	 * 옵션 화면의 입력 게이지가 이 값을 그린다(V9).
	 */
	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	float GetCurrentLoudness() const;

	/**
	 * 마이크 감도(VAD 임계값)를 바꾼다. 낮출수록 작은 소리도 발화로 친다.
	 * 마이크 환경이 사람마다 달라 반드시 조절 수단이 필요하다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Voice")
	void SetMicSensitivity(float InThreshold);

	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	float GetMicSensitivity() const;

	/** 진단용 통계를 한 줄 문자열로. MOU.Voice.Stat 이 쓴다. */
	FString GetStatsString() const;

private:
	/** 게임 스레드 틱. 워커 큐를 비우고 재생 버퍼로 넘긴다. */
	bool Tick(float DeltaTime);

	/** 워커 스레드를 Stop -> 종료 대기 -> 파괴 순서로 정리한다. */
	void ShutdownCapture();

	/** 재생용 신스 컴포넌트를 만든다(루프백을 처음 켤 때). */
	void EnsurePlaybackComponent();

	/**
	 * 캡처 워커. 게임 스레드에서 생성/파괴하고 그 사이엔 워커 스레드가 쓴다.
	 *
	 * 원시 포인터인 이유는 기존 채팅의 FChatClientRunnable 과 같다:
	 * 수명이 "스레드가 끝났는가" 에만 달려 있고 그 판단은 ShutdownCapture 하나에서만
	 * 내린다. 참조 카운트로 관리하면 스레드가 살아있는데 객체가 먼저 사라질 여지가 생긴다.
	 * **반드시 ShutdownCapture 를 거쳐서만 해제할 것. delete 를 직접 부르지 말 것.**
	 */
	FVoiceCaptureRunnable* CaptureRunnable = nullptr;
	FRunnableThread*       CaptureThread   = nullptr;

	/**
	 * 재생 컴포넌트.
	 *
	 * UPROPERTY 로 잡아두지 않으면 GC 가 수거해간다 - 이 서브시스템만 참조를
	 * 들고 있기 때문이다.
	 */
	UPROPERTY()
	TObjectPtr<UVoiceSynthComponent> PlaybackComponent;

	FTSTicker::FDelegateHandle TickHandle;

	bool bLoopbackEnabled = false;
	bool bIsSpeaking      = false;

	/**
	 * 마지막으로 프레임을 받은 시각.
	 *
	 * 프레임이 끊겼을 때 발화 상태를 내리는 데 쓴다. 엔진 캡처가 무음 구간에
	 * 데이터를 주지 않으면 bIsSpeaking 이 true 로 붙박이는 것을 막는다.
	 */
	double LastFrameTime = 0.0;

	/** 진단 카운터. 게임 스레드 전용이라 원자적일 필요가 없다. */
	int32 FramesReceived = 0;
	int32 FramesDropped  = 0;
};
