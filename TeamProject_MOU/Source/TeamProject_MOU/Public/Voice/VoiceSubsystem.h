// MOU 음성 - 음성 시스템의 진입점.
//
// [팀원이 알아야 할 것 - 요약]
//   음성을 쓰려면 이 서브시스템만 알면 된다. 스레드나 오디오 버퍼는 볼 필요 없다.
//   콘솔에서:  MOU.Voice.Loopback 1   -> 내 목소리가 내 헤드폰으로 돌아온다
//              MOU.Voice.Stat         -> 프레임/버퍼/드랍 수
//              MOU.Voice.FakeNoise 1500  -> 마이크 없이 NPC 소음만 발생
//              MOU.Voice.Mute 1       -> 마이크 음소거 (C 키와 동일)
//              MOU.Voice.ShowUI       -> 상태 표시 위젯 띄우기 (VoiceStatusWidget.h)
//
// [이 파일이 시스템 어디에 있나]
//
//     FVoiceCaptureSource (게임 스레드에서 직접 폴링. VoiceCaptureSource.h 참고)
//       ↓ Poll()
//   ★ UVoiceSubsystem (게임 스레드)  ← 이 파일. Tick 에서 폴링해 재생 버퍼로 넘김
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
#include "Voice/VoiceCaptureSource.h"
#include "VoiceSubsystem.generated.h"

class FVoiceCaptureSource;
class UVoiceSynthComponent;

/**
 * 마이크 캡처와 재생을 소유한다.
 *
 * 역할:
 *   1. 마이크 캡처 객체(FVoiceCaptureSource)의 생성과 파괴
 *   2. 게임 스레드 Tick 에서 마이크를 폴링해 재생 버퍼로 넘김
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

	/**
	 * 마이크 음소거를 켜고 끈다. `C` 키에 연결된다(VOICE_INTEGRATION.md 7-1절).
	 *
	 * 켜면 **캡처 자체를 중단한다**(마이크를 놓는다. VAD 도 안 돈다) - 설계 문서의
	 * 표현 그대로다. "볼륨을 낮추는" 것이 아니라 "말하지 않는" 상태로 만드는 것이다.
	 * 이렇게 해야 나중에 네트워크 전송(V3)이 붙었을 때도 음소거 중엔 아무것도
	 * 나가지 않는다는 게 자연스럽게 보장된다 - 별도로 "전송 안 함" 분기를 안 둬도 된다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Voice")
	void SetMuted(bool bInMuted);

	UFUNCTION(BlueprintCallable, Category = "MOU|Voice")
	void ToggleMute() { SetMuted(!bMuted); }

	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	bool IsMuted() const { return bMuted; }

	/**
	 * 발화 모드(속삭임/보통/외침)를 바꾼다.
	 *
	 * 이 값이 들리는 거리와 NPC 가 듣는 거리를 동시에 결정한다.
	 * 실제 숫자는 MOUVoice::GetHearRadius / GetNoiseRange 한 곳에만 있다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Voice")
	void SetVoiceMode(EVoiceMode NewMode);

	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	EVoiceMode GetVoiceMode() const { return VoiceMode; }

	/**
	 * 말할 때 내 주위에 소리 도달 범위를 링으로 그린다(디버그 전용).
	 *
	 * 초록 = 사람이 듣는 거리, 빨강 = NPC 가 듣는 거리.
	 * 두 값 모두 V8 에서 실제 소음 이벤트가 쓸 값과 **같은 함수**에서 나오므로,
	 * 여기 보이는 원이 곧 실제 판정 범위다(어긋날 수 없다).
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Voice")
	void SetShowRadiusDebug(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	bool IsShowingRadiusDebug() const { return bShowRadiusDebug; }

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
	/** 게임 스레드 틱. 마이크를 폴링해 재생 버퍼로 넘긴다. */
	bool Tick(float DeltaTime);

	/** 재생용 신스 컴포넌트를 만든다(루프백을 처음 켤 때). */
	void EnsurePlaybackComponent();

	/** 말하는 동안 소리 도달 범위를 링으로 그린다. 디버그 빌드에서만 의미가 있다. */
	void DrawRadiusDebug();

	/**
	 * 마이크 캡처.
	 *
	 * 워커 스레드가 아니라 **게임 스레드에서 직접 폴링한다.**
	 * 엔진의 IVoiceCapture 구현이 게임 스레드 티커로 자기 버퍼를 채우는데
	 * 그 버퍼에 뮤텍스가 없기 때문이다(FVoiceCaptureSource.h 상단 주석 참고).
	 */
	TUniquePtr<FVoiceCaptureSource> CaptureSource;

	/** 매 틱 재사용하는 프레임 버퍼. 틱마다 할당하지 않으려고 멤버로 둔다. */
	TArray<FMOUVoiceFrame> PolledFrames;

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
	bool bMuted           = false;
	bool bShowRadiusDebug = false;

	/** 지금 발화 모드. V9 에서 키/UI 로 바꾸게 되고, 지금은 콘솔로만 바꾼다. */
	EVoiceMode VoiceMode = EVoiceMode::Normal;

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
