// MOU 음성 - 수신한 목소리를 재생하는 컴포넌트.
//
// [이 파일이 시스템 어디에 있나]
//
//     서버 (VoiceRouter)
//       │ ClientReceiveVoiceFrame  ← Client, Unreliable RPC
//       ▼
//     UVoiceComponent (내 PlayerController)
//       ▼ HandleFrame()
//   ★ UVoicePlaybackComponent  ← 이 파일. 발신자마다 스트림 하나
//       │  스트림 = { Opus 디코더 + UVoiceSynthComponent }
//       ▼
//     발신자의 폰에 붙은 UVoiceSynthComponent (3D)
//
// [왜 발신자마다 따로 관리하는가 - 두 가지 이유가 다르다]
//
//   1. **디코더**: Opus 는 직전 프레임을 참고해 다음을 복원한다(상태를 가진다).
//      여러 사람 목소리를 디코더 하나로 돌려 쓰면 서로 상태를 오염시켜 지직거린다.
//
//   2. **사운드**: 소리가 나야 하는 **위치가 사람마다 다르다.** 근접 음성의
//      핵심이 "그 사람 위치에서 들린다" 이므로 발신자 폰마다 사운드가 필요하다.
//
//   둘 다 "발신자마다 하나" 라서 한 스트림 구조체로 묶었다.
//
// [스트림 키가 (발신자, 라우트) 쌍인 이유]
//   V6 에서 한 사람이 근접과 무전 **양쪽으로 동시에** 들릴 수 있다 - 육성은
//   가까운 사람에게, 무전은 무전기에서. 두 소리는 위치도 음색도 다르므로
//   각각 별도 스트림이어야 한다. 지금은 근접뿐이지만 키를 미리 쌍으로 두면
//   V6 에서 이 파일을 다시 안 건드린다.
//
// [현재 구현 단계 - V3]
//   지터버퍼가 아직 없다(V4). 받은 프레임을 바로 디코딩해 링버퍼로 밀어넣는다.
//   UVoiceSynthComponent 의 링버퍼(8프레임 = 160ms)가 약간의 흔들림은 흡수한다.
//
// [대응하는 문서]
//   VOICE_INTEGRATION.md 6절(클래스), 7-2절(근접 재생), 14절 V3

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Voice/VoiceTypes.h"
#include "VoicePlaybackComponent.generated.h"

class APawn;
class APlayerController;
class FMOUVoiceDecoder;
class UVoiceSynthComponent;

/**
 * 스트림 하나를 가리키는 키. (발신자, 라우트) 쌍이다.
 *
 * USTRUCT 가 아닌 이유: 네트워크로 나가지도, 에디터에 보이지도 않는
 * 순수 내부 자료구조다. 리플렉션을 붙이면 얻는 것 없이 제약만 는다.
 */
struct FVoiceStreamKey
{
	int32       SpeakerId = 0;
	EVoiceRoute Route     = EVoiceRoute::Proximity;

	bool operator==(const FVoiceStreamKey& Other) const
	{
		return SpeakerId == Other.SpeakerId && Route == Other.Route;
	}

	friend uint32 GetTypeHash(const FVoiceStreamKey& Key)
	{
		return HashCombine(::GetTypeHash(Key.SpeakerId), ::GetTypeHash(static_cast<uint8>(Key.Route)));
	}
};

/**
 * 발신자 한 명(정확히는 한 라우트)의 재생 상태.
 *
 * ★ Synth 를 TWeakObjectPtr 로 들고 있고 UPROPERTY 를 걸지 않은 이유:
 *   RegisterComponent() 를 부르는 순간 그 컴포넌트는 **소유 액터(발신자 폰)의
 *   컴포넌트 목록에 들어가서** 액터가 GC 로부터 지켜준다. 우리가 또 붙들면
 *   폰이 죽었는데도 컴포넌트만 살아남는 상황을 만들 수 있다.
 *
 *   폰이 파괴되면(사망/리스폰) 컴포넌트도 같이 사라지고 이 약참조가 풀린다.
 *   그러면 다음 프레임에서 새 폰에 다시 만든다 - **리스폰 처리가 공짜로 된다.**
 */
struct FVoiceStream
{
	/** 이 발신자 전용 Opus 디코더. */
	TUniquePtr<FMOUVoiceDecoder> Decoder;

	/** 발신자 폰에 붙인 사운드. 위 ★ 참고. */
	TWeakObjectPtr<UVoiceSynthComponent> Synth;

	/** 지금 Synth 가 붙어 있는 폰. 리스폰 감지에 쓴다. */
	TWeakObjectPtr<APawn> AttachedPawn;

	/** 디코딩 결과를 받는 버퍼. 스트림마다 들고 있어 매 프레임 할당하지 않는다. */
	TArray<int16> DecodedScratch;

	/** 마지막으로 받은 Seq. 유실 감지용(V3 은 통계만, V4 에서 지터버퍼가 쓴다). */
	uint16 LastSeq = 0;
	bool   bHasSeq = false;

	/** 마지막으로 프레임이 온 시각. 오래 조용하면 스트림을 정리한다. */
	double LastFrameTime = 0.0;

	/** 진단용. */
	int32 FramesPlayed = 0;
	int32 FramesLost   = 0;
};

/**
 * 남의 목소리를 재생한다. **내 PlayerController 에만 있으면 된다.**
 *
 * UVoiceComponent 가 RPC 를 받아 이리로 넘긴다. 이 컴포넌트는 네트워크를
 * 전혀 모른다 - "누가, 어떤 소리를 냈다" 만 받아서 들려주는 것이 전부다.
 */
UCLASS(ClassGroup = (MOU), meta = (BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UVoicePlaybackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoicePlaybackComponent();

	/**
	 * 주어진 PlayerController 의 재생 컴포넌트를 얻는다. 없으면 만든다.
	 *
	 * **로컬 컨트롤러가 아니면 null 을 돌려준다** - 남의 컨트롤러에 재생
	 * 컴포넌트를 만들면 서버에서 들리지도 않는 소리를 위해 디코딩을 하게 된다.
	 */
	static UVoicePlaybackComponent* FindOrCreate(APlayerController* OwnerPC);

	/** 서버가 보낸 프레임 하나를 재생한다. 게임 스레드에서만 부른다. */
	void HandleFrame(const FVoiceFrameOut& Frame);

	/** 전부 정리한다(레벨 이동 등). */
	void ResetAllStreams();

	/** 지금 살아있는 스트림 수. */
	int32 GetStreamCount() const { return Streams.Num(); }

	/** 진단용 한 줄 요약. MOU.Voice.Stat 이 쓴다. */
	FString GetStatsString() const;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** SpeakerId 로 발신자의 폰을 찾는다. 못 찾으면 null(아직 리플리케이트 전일 수 있다). */
	APawn* FindSpeakerPawn(int32 SpeakerId) const;

	/**
	 * 스트림의 사운드가 올바른 폰에 붙어 있도록 보장한다.
	 * 없거나 폰이 바뀌었으면(리스폰) 새로 만든다.
	 *
	 * @return 쓸 수 있는 사운드. 폰을 못 찾으면 null.
	 */
	UVoiceSynthComponent* EnsureSynthForStream(FVoiceStream& Stream, int32 SpeakerId, EVoiceMode Mode);

	/** 오래 조용한 스트림을 정리한다. */
	void CleanupIdleStreams();

	/** 발신자(+라우트)마다 하나. */
	TMap<FVoiceStreamKey, FVoiceStream> Streams;

	/** 진단 카운터. */
	int32 TotalFramesReceived = 0;
	int32 TotalFramesDropped  = 0;
	int32 TotalPawnMisses     = 0;
};
