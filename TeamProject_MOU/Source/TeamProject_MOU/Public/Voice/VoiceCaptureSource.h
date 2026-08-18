// MOU 음성 - 마이크 캡처 소스.
//
// [이 파일이 시스템 어디에 있나]
//
//     마이크
//       │ IVoiceCapture (엔진 Voice 모듈)
//       ▼
//   ★ FVoiceCaptureSource  ← 이 파일. 20ms 프레이밍 + RMS + VAD
//       ▼ (직접 호출)
//     UVoiceSubsystem (게임 스레드 티커가 Poll 한다)
//       ▼ 링버퍼 (SPSC)
//     UVoiceSynthComponent (오디오 렌더 스레드)
//
// [★★ 왜 워커 스레드가 아닌가 - 설계가 바뀐 이유]
//
//   처음에는 기존 채팅(FChatClientRunnable)처럼 워커 스레드로 만들었다가 되돌렸다.
//   엔진 구현을 열어보니 그렇게 하면 안 되는 구조였다:
//
//     class FVoiceCaptureWindows : public IVoiceCapture, public FTSTickerObjectBase
//                                                        ^^^^^^^^^^^^^^^^^^^^^^^^^
//   엔진의 캡처 객체는 **게임 스레드 티커**로 자기 내부 버퍼를 채운다.
//   그리고 그 버퍼를 지키는 **뮤텍스가 없다**.
//   워커 스레드에서 GetVoiceData() 를 부르면 게임 스레드의 Tick() 이 쓰는 중인
//   버퍼를 동시에 읽게 된다 - 데이터 레이스다.
//
//   그래서 캡처 폴링은 게임 스레드에서 한다. 비용도 문제되지 않는다:
//   GetVoiceData 는 memcpy 이고, 320샘플 RMS 는 산술 320번이다.
//
//   **진짜 스레드 경계는 여기가 아니라 재생 쪽**(게임 스레드 -> 오디오 렌더 스레드)
//   이고, 그건 VoiceSynthComponent 의 락 프리 링버퍼가 담당한다.
//
//   V2 에서 Opus 인코딩이 들어와도 16kbps 인코딩은 수십 마이크로초라 게임
//   스레드에서 충분하다. 프로파일링에서 걸리면 그때 **인코딩만** 워커로 뺀다
//   (캡처 폴링은 여전히 게임 스레드에 남아야 한다).
//
// [대응하는 문서]
//   VOICE_INTEGRATION.md 7-1절 (캡처), 11절 (스레드 경계)

#pragma once

#include "CoreMinimal.h"
#include "Voice/VoiceTypes.h"

class IVoiceCapture;

/**
 * 캡처된 20ms 조각 하나.
 *
 * V2 에서 Opus 가 들어오면 PCM 대신 압축 바이트가 되고,
 * V3 에서 네트워크로 나갈 때 Seq/Route 필드가 붙는다.
 */
struct FMOUVoiceFrame
{
	/** 16kHz 모노 PCM16. 항상 MOUVoice::SamplesPerFrame 개다. */
	TArray<int16> Samples;

	/** 이 프레임의 RMS 를 0~1 로 정규화한 값. NPC 소음 크기와 UI 게이지에 쓴다. */
	float Loudness = 0.f;

	/** VAD 판정 결과. false 면 무음 구간이다. */
	bool bIsSpeaking = false;
};

/**
 * 마이크를 열고 20ms 프레임으로 잘라주는 객체.
 *
 * **모든 멤버 함수는 게임 스레드에서만 호출한다.** (위 주석 참고)
 * 그래서 원자적 타입이나 큐가 필요 없다 - 평범한 멤버로 충분하다.
 */
class FVoiceCaptureSource
{
public:
	FVoiceCaptureSource();
	~FVoiceCaptureSource();

	/**
	 * 마이크를 연다.
	 *
	 * [중요] 이 함수를 부르기 전에 **Voice 모듈이 로드돼 있어야 한다.**
	 * 호출자(UVoiceSubsystem::Initialize)가 게임 스레드에서 보장한다.
	 *
	 * @return 마이크가 실제로 열렸으면 true. false 여도 게임은 정상 진행돼야 한다.
	 */
	bool Start();

	/** 마이크를 닫는다. 두 번 불러도 안전하다. */
	void Shutdown();

	/**
	 * 엔진에서 받은 데이터를 20ms 프레임으로 잘라 OutFrames 에 **덧붙인다.**
	 * 매 게임 스레드 틱마다 부른다.
	 */
	void Poll(TArray<FMOUVoiceFrame>& OutFrames);

	/** 마이크가 열려 있는지. */
	bool IsReady() const { return bReady; }

	/** VAD 임계값(마이크 감도). */
	void  SetVadThreshold(float InThreshold);
	float GetVadThreshold() const { return VadThreshold; }

	/** 마지막으로 계산한 음량. UI 게이지용. */
	float GetCurrentLoudness() const { return CurrentLoudness; }

private:
	/** 누적 버퍼에서 20ms 씩 잘라낸다. */
	void DrainPendingSamples(TArray<FMOUVoiceFrame>& OutFrames);

	/** 샘플 배열의 RMS 를 0~1 로 정규화해 돌려준다. */
	static float ComputeRms(const int16* Samples, int32 NumSamples);

	TSharedPtr<IVoiceCapture> VoiceCapture;

	/** 엔진이 임의 길이로 주는 바이트를 20ms 경계까지 모아두는 버퍼. */
	TArray<uint8> PendingBytes;

	/** GetVoiceData 로 받아올 임시 버퍼. 매번 할당하지 않으려고 멤버로 둔다. */
	TArray<uint8> ReadBuffer;

	bool  bReady          = false;
	float VadThreshold    = MOUVoice::DefaultVadThreshold;
	float CurrentLoudness = 0.f;

	/** VAD hangover 상태. */
	double SilenceStartedAt = 0.0;
	bool   bSpeaking        = false;
};
