// MOU 음성 - 음성 재생 출구 (USynthComponent).
//
// [이 파일이 시스템 어디에 있나]
//
//     UVoiceSubsystem (게임 스레드)
//       │  PushSamples()   ← 게임 스레드가 넣는다
//       ▼ Audio::TCircularAudioBuffer<float>  (SPSC, 락 프리)
//   ★ UVoiceSynthComponent::OnGenerateAudio()  ← 오디오 렌더 스레드가 꺼낸다
//       ▼
//     스피커
//
// [★★ 최우선 주의사항 - OnGenerateAudio 는 게임 스레드가 아니다]
//   오디오 렌더 스레드에서 불린다. 이 함수 안에서 다음을 하면 오디오가 끊기고
//   결국 크래시한다:
//     - UObject 접근 (this 의 UPROPERTY 포함)
//     - 락 (FCriticalSection 등)
//     - 메모리 할당 (TArray::Add, NewObject 등)
//     - UE_LOG
//   허용되는 것은 링버퍼에서 pop 하기와 float 산술뿐이다.
//
//   디버깅이 필요하면 카운터를 올리고 게임 스레드에서 읽는다.
//   (기존 채팅의 "워커 스레드에서 UObject 금지" 와 정확히 같은 규칙이다)
//
// [대응하는 문서]
//   VOICE_INTEGRATION.md 7-2절 (재생/감쇠), 11절 (스레드 경계)
//
// [현재 구현 단계]
//   V1 (로컬 루프백). 아직 2D 로만 재생한다.
//   3D 공간화와 감쇠(AttenuationSettings)는 V3 에서 붙인다 - 지금은 소리를
//   낼 발신자 액터가 없기 때문이다(내 목소리를 내가 듣는 단계).

#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "DSP/Dsp.h"
#include "Voice/VoiceTypes.h"
#include "VoiceSynthComponent.generated.h"

/**
 * 링버퍼에 쌓인 PCM 을 오디오 엔진에 흘려보내는 컴포넌트.
 *
 * 이 클래스는 "어디서 온 소리인지" 를 모른다. 그냥 주는 대로 낸다.
 * 근접/무전 구분과 3D 부착은 이 컴포넌트를 쓰는 쪽(V3 이후)이 정한다.
 */
UCLASS(ClassGroup = (MOU), meta = (BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UVoiceSynthComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	// USynthComponent 는 FObjectInitializer 생성자만 제공한다(기본 생성자가 없다).
	UVoiceSynthComponent(const FObjectInitializer& ObjectInitializer);

	/**
	 * 재생할 PCM16 샘플을 밀어 넣는다. **게임 스레드에서만 호출한다.**
	 *
	 * 내부에서 float 로 변환해 링버퍼에 push 한다.
	 * 버퍼가 가득 차면 넘치는 만큼은 조용히 버려진다(음성의 정상 동작이다 -
	 * 오래된 소리를 억지로 재생하면 지연만 쌓인다).
	 */
	void PushSamples(const int16* Samples, int32 NumSamples);

	/** 버퍼에 쌓인 샘플 수. 지연 진단용. 게임 스레드에서 호출한다. */
	int32 GetBufferedSampleCount() const;

	/**
	 * 쌓인 소리를 버리라고 요청한다. **게임 스레드에서 호출한다.**
	 *
	 * ★ 여기서 직접 버퍼를 비우지 않는 이유:
	 *   TCircularAudioBuffer::SetCapacity() 는 내부 TArray 를 재할당한다.
	 *   오디오 렌더 스레드가 Pop() 안에서 그 메모리를 읽는 중이면
	 *   해제된 메모리를 읽게 된다(use-after-free).
	 *
	 *   그래서 플래그만 세우고, 실제 비우기는 **소비자인 오디오 렌더 스레드가**
	 *   OnGenerateAudio 안에서 한다. SPSC 버퍼는 소비자만 Pop 할 수 있다.
	 */
	void RequestFlush();

	/**
	 * 오디오 렌더 스레드가 버퍼를 비웠는데도 채워지지 않은 횟수(언더런).
	 * 소리가 끊긴다면 이 값이 오르는지 본다. 게임 스레드에서 읽는다.
	 */
	int32 GetUnderrunCount() const { return UnderrunCounter.GetValue(); }

	/** 버퍼가 넘쳐 버린 샘플 수. 지연이 쌓이는 중이라는 신호다. */
	int32 GetOverflowCount() const { return OverflowCounter.GetValue(); }

protected:
	// --- USynthComponent ---------------------------------------------------

	/** 오디오 엔진에 샘플레이트를 알린다. 게임 스레드에서 불린다. */
	virtual bool Init(int32& SampleRate) override;

	/**
	 * ★ 오디오 렌더 스레드에서 불린다. 위 주의사항을 반드시 지킬 것.
	 * @return 실제로 채운 샘플 수
	 */
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	/**
	 * 게임 스레드 -> 오디오 렌더 스레드 단방향 버퍼.
	 *
	 * Audio::TCircularAudioBuffer 는 SPSC 로 스레드 세이프하다고 엔진 주석에
	 * 명시돼 있다. 반대 방향으로 쓰거나 두 스레드가 같은 쪽을 만지면 안 된다.
	 */
	Audio::TCircularAudioBuffer<float> RingBuffer;

	/**
	 * 진단 카운터.
	 *
	 * OnGenerateAudio 안에서 UE_LOG 를 할 수 없으므로, 문제가 생기면 여기에
	 * 세어두고 게임 스레드가 MOU.Voice.Stat 으로 읽어간다.
	 */
	mutable FThreadSafeCounter UnderrunCounter;
	mutable FThreadSafeCounter OverflowCounter;

	/**
	 * 게임 스레드가 올리고 오디오 렌더 스레드가 확인하는 "비우기 요청" 카운터.
	 *
	 * bool 이 아니라 카운터인 이유: 렌더 스레드가 플래그를 내리는 사이에
	 * 게임 스레드가 다시 요청하면 그 요청이 사라진다. 값이 달라졌는지만 보면
	 * 요청을 놓치지 않는다.
	 */
	FThreadSafeCounter FlushRequestCounter;

	/** 렌더 스레드가 마지막으로 처리한 비우기 요청 번호. **렌더 스레드 전용.** */
	int32 LastHandledFlushRequest = 0;
};
