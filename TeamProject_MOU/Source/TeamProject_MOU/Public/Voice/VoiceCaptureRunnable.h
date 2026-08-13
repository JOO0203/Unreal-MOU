// MOU 음성 - 마이크 캡처 워커 스레드.
//
// [이 파일이 시스템 어디에 있나]
//
//     마이크
//       │ IVoiceCapture (엔진 Voice 모듈)
//       ▼
//   ★ FVoiceCaptureRunnable (워커 스레드)  ← 이 파일
//       │  20ms 프레이밍 + RMS(음량) 계산 + VAD
//       ▼ TQueue (SPSC)
//     UVoiceSubsystem (게임 스레드)
//       ▼
//     UVoiceSynthComponent (오디오 렌더 스레드)
//
// [★ 스레드 규칙 - 어기면 랜덤 크래시]
//   이 클래스의 Run() 은 게임 스레드가 아니다.
//   여기서 UObject / 액터 / 델리게이트를 절대 건드리지 않는다.
//   허용: IVoiceCapture, 바이트 배열, 순수 구조체, TQueue 에 넣기, UE_LOG.
//
//   기존 채팅 시스템의 FChatClientRunnable 과 정확히 같은 규칙이다.
//   (SERVER_INTEGRATION.md 10절 "스레드 규칙" 참고)
//
// [대응하는 문서]
//   VOICE_INTEGRATION.md 7-1절 (캡처), 11절 (스레드 경계)

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Queue.h"
#include "Voice/VoiceTypes.h"

class IVoiceCapture;

/**
 * 캡처된 20ms 조각 하나.
 *
 * UObject 가 아닌 순수 데이터 구조체다. 워커 스레드가 만들어 큐에 넣고
 * 게임 스레드가 꺼낸다. 그래서 UObject 포인터를 절대 넣으면 안 된다.
 *
 * V2 에서 Opus 를 넣으면 PCM 대신 압축 바이트가 들어가고, V3 에서 네트워크로
 * 나갈 때 Seq/Route 필드가 붙는다. 지금은 루프백이라 PCM 그대로다.
 */
struct FMOUVoiceFrame
{
	/** 16kHz 모노 PCM16 샘플. 항상 MOUVoice::SamplesPerFrame 개다. */
	TArray<int16> Samples;

	/** 이 프레임의 RMS 를 0~1 로 정규화한 값. NPC 소음 크기와 UI 게이지에 쓴다. */
	float Loudness = 0.f;

	/** VAD 판정 결과. false 면 무음 구간이다. */
	bool bIsSpeaking = false;
};

/**
 * 마이크를 열고 20ms 프레임으로 잘라 큐에 넣는 워커.
 *
 * 수명: UVoiceSubsystem 이 게임 스레드에서 생성/파괴한다.
 *       그 사이에는 워커 스레드가 이 객체를 쓴다.
 *       반드시 UVoiceSubsystem::ShutdownCapture() 를 거쳐서만 해제할 것.
 *       (Stop -> Kill(true) -> delete 순서. 순서를 어기면 PIE 종료 시 에디터가 죽는다)
 */
class FVoiceCaptureRunnable : public FRunnable
{
public:
	FVoiceCaptureRunnable();
	virtual ~FVoiceCaptureRunnable() override;

	// --- FRunnable ---------------------------------------------------------
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

	/**
	 * 캡처된 프레임을 하나 꺼낸다. **게임 스레드에서만 호출한다.**
	 * @return 꺼낼 것이 있었으면 true
	 */
	bool DequeueFrame(FMOUVoiceFrame& OutFrame);

	/**
	 * 마이크가 실제로 열렸는지. 마이크가 없거나 권한이 없으면 false 다.
	 * 이 경우에도 게임은 정상 진행돼야 한다(재생만 동작).
	 */
	bool IsCaptureReady() const { return bCaptureReady; }

	/** VAD 임계값을 바꾼다. 워커 스레드가 읽으므로 원자적 타입에 담는다. */
	void SetVadThreshold(float InThreshold);
	float GetVadThreshold() const;

	/** 마지막으로 계산한 음량. UI 게이지용. 게임 스레드에서 읽어도 안전하다. */
	float GetCurrentLoudness() const;

private:
	/** 누적 버퍼에서 20ms 씩 잘라내 큐에 넣는다. 워커 스레드 전용. */
	void DrainPendingSamples();

	/** 샘플 배열의 RMS 를 0~1 로 정규화해 돌려준다. */
	static float ComputeRms(const int16* Samples, int32 NumSamples);

	/** 엔진 캡처 객체. 워커 스레드에서만 만지고, 게임 스레드는 건드리지 않는다. */
	TSharedPtr<IVoiceCapture> VoiceCapture;

	/**
	 * 워커 -> 게임 스레드 단방향 큐 (SPSC).
	 * 반대 방향으로 쓰면 안 된다. 기존 채팅 시스템과 같은 규칙이다.
	 */
	TQueue<FMOUVoiceFrame, EQueueMode::Spsc> CapturedFrames;

	/**
	 * 엔진에서 받은 바이트를 20ms 경계로 자르기 전까지 모아두는 버퍼.
	 * 엔진은 임의 길이로 주기 때문에 그대로는 프레임으로 못 쓴다.
	 */
	TArray<uint8> PendingBytes;

	/** Stop() 이 호출됐는지. 워커 루프의 종료 조건. */
	FThreadSafeBool bStopRequested;

	/** 마이크가 열렸는지. Init() 에서 확정된다. */
	FThreadSafeBool bCaptureReady;

	/** 무음이 시작된 시각. VAD hangover 계산용. 워커 스레드 전용. */
	double SilenceStartedAt = 0.0;

	/** 지금 발화 중인지. 워커 스레드 전용. */
	bool bSpeaking = false;

	/**
	 * VAD 임계값과 현재 음량.
	 *
	 * float 를 그대로 두면 스레드 간에 찢어진 값을 읽을 수 있다.
	 * TAtomic 이 아니라 int32 비트 재해석 + FPlatformAtomics 를 쓰는 이유는
	 * 언리얼이 float 원자 연산을 직접 제공하지 않기 때문이다.
	 */
	mutable FThreadSafeCounter VadThresholdBits;
	mutable FThreadSafeCounter CurrentLoudnessBits;

	static int32 FloatToBits(float Value);
	static float BitsToFloat(int32 Bits);
};
