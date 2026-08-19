// MOU 음성 - 음성 시스템 전체가 공유하는 타입과 상수.
//
// [이 파일이 시스템 어디에 있나]
//   Voice/ 폴더의 모든 파일이 이 헤더를 include 한다. 여기에는 로직이 없다.
//   설계 문서: 저장소 루트의 VOICE_INTEGRATION.md
//
// [현재 구현 단계]
//   V0 (FakeNoise 콘솔 명령) + V1 (로컬 루프백) + V2 (Opus 코덱) 까지.
//   네트워크 전송(V3)은 아직 없다. 그래서 EVoiceRoute 같은 라우팅 관련 타입은
//   아직 만들지 않았다 - 쓰는 곳이 없는 타입을 미리 만들면 설계가 바뀔 때
//   같이 바꿔야 하는 짐만 된다.
//
// [수정 시 같이 고쳐야 하는 파일]
//   SampleRate/FrameMs 를 바꾸면 VoiceCaptureSource.cpp 의 프레이밍과
//   VoiceSynthComponent 의 링버퍼 크기가 같이 영향을 받는다.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "VoiceTypes.generated.h"

/** 음성 시스템 전용 로그 카테고리. 에디터 출력 로그에서 'LogMOUVoice' 로 필터링한다. */
DECLARE_LOG_CATEGORY_EXTERN(LogMOUVoice, Log, All);

/**
 * 발화 모드. 목소리 크기에 따라 들리는 거리와 NPC 가 듣는 거리가 달라진다.
 * VOICE_INTEGRATION.md 13절의 표가 이 enum 에 대응한다.
 */
UENUM(BlueprintType)
enum class EVoiceMode : uint8
{
	Whisper UMETA(DisplayName = "속삭임"),
	Normal  UMETA(DisplayName = "보통"),
	Shout   UMETA(DisplayName = "외침"),

	/** 범위 검사용. 실제 모드가 아니다. 항상 마지막에 둘 것. */
	MAX UMETA(Hidden),
};

/**
 * 이 목소리가 어느 경로로 전달되는가.
 *
 * ★ 이 값의 **최종 결정권은 서버에 있다.** 클라가 보내는 값은 "요청" 이고,
 *   서버가 라우팅하며 확정한 값이 재생에 쓰인다(7-3절의 라우트 우선순위).
 *   근접과 무전 양쪽 조건에 다 걸리는 사람에게 둘 다 보내면 같은 목소리가
 *   두 번 겹쳐 들려 에코가 되기 때문이다.
 */
UENUM(BlueprintType)
enum class EVoiceRoute : uint8
{
	/** 거리 기반. 발신자 폰 위치에서 3D 로 난다. */
	Proximity UMETA(DisplayName = "근접"),

	/** 거리 무관. 무전기 액터 위치에서 난다 (V6). */
	Radio     UMETA(DisplayName = "무전"),

	MAX UMETA(Hidden),
};

/**
 * 음성 파이프라인의 고정 상수.
 *
 * 지금은 컴파일 타임 상수다. VOICE_INTEGRATION.md 13절은 최종적으로 이 값들을
 * UDeveloperSettings 로 빼는 것을 계획하고 있지만, V1 에서는 파이프라인이
 * 도는 것을 먼저 확인하는 게 목적이라 상수로 둔다.
 */
namespace MOUVoice
{
	/**
	 * 16kHz 모노.
	 *
	 * 사람 목소리 명료도는 8kHz 대역이면 충분하고(전화가 그렇다), 무전기 톤은
	 * 오히려 대역이 좁을수록 그럴듯하다. 48kHz 스테레오로 올리면 대역폭만 6배가 된다.
	 *
	 * [확인됨] 엔진의 Windows 캡처 구현은 8000~48000 Hz 만 받는다
	 * (VoiceCaptureWindows.cpp 의 CreateCaptureBuffer). 16000 은 안전한 범위 안이다.
	 */
	inline constexpr int32 SampleRate  = 16000;
	inline constexpr int32 NumChannels = 1;

	/**
	 * 프레임 하나의 길이(ms)와 그에 해당하는 샘플 수.
	 *
	 * 20ms 인 이유: Opus 가 지원하는 프레임 길이(2.5/5/10/20/40/60ms) 중
	 * 지연과 오버헤드의 균형점이다.
	 *
	 * ★★ [V2 에서 확인됨] 20ms 는 우연이 아니라 **엔진이 강제하는 값이다.**
	 *
	 *   엔진의 Opus 래퍼는 프레임 길이를 인자로 받지 않는다. 하드코딩돼 있다:
	 *
	 *       // VoiceCodecOpus.cpp
	 *       #define NUM_OPUS_FRAMES_PER_SEC 50
	 *       FrameSize = SampleRate / NUM_OPUS_FRAMES_PER_SEC;   // 16000/50 = 320
	 *
	 *   즉 인코더는 **무조건 320샘플 단위로만** 인코딩한다. 우리 프레이밍이
	 *   마침 같아서 1프레임 = 1 Opus 프레임으로 딱 떨어진다.
	 *
	 *   **FrameMs 를 40 으로 바꾸면 "40ms Opus 프레임"이 되는 게 아니라
	 *   "한 패킷에 20ms 프레임 2개"가 된다.** 엔진 Encode() 가 들어온 바이트를
	 *   320샘플씩 쪼개 여러 번 opus_encode 를 부르고 헤더 하나에 묶기 때문이다.
	 *   대역폭이 줄어드는 것은 맞지만(헤더를 공유하므로) 이유가 13절 설명과 다르다.
	 *   그리고 320 의 배수가 아닌 값으로 바꾸면 나머지가 인코딩되지 않고 버려진다.
	 */
	inline constexpr int32 FrameMs          = 20;
	inline constexpr int32 SamplesPerFrame  = SampleRate * FrameMs / 1000;  // 320
	inline constexpr int32 BytesPerFrame    = SamplesPerFrame * sizeof(int16); // 640

	// -----------------------------------------------------------------------
	// Opus 코덱 (V2)
	//
	// 엔진의 IVoiceEncoder / IVoiceDecoder 를 그대로 쓴다. 직접 Opus 를 붙이지 않는다.
	// 래퍼는 VoiceCodec.h 이고, 여기 있는 것은 그 래퍼가 쓰는 크기 상수뿐이다.
	// -----------------------------------------------------------------------

	/**
	 * 인코딩 비트레이트(bits/sec).
	 *
	 * 엔진은 Init() 에서 비트레이트를 명시하지 않아 Opus 자동값에 맡긴다.
	 * 우리는 명시적으로 박는다 - 12절의 대역폭 계산(프레임당 ~60바이트)이
	 * 이 숫자를 전제로 하기 때문이다. 자동값에 맡기면 계산의 근거가 사라진다.
	 */
	inline constexpr int32 OpusBitrate = 24000;

	/**
	 * 인코딩 결과를 받을 작업 버퍼 크기(바이트).
	 *
	 * 넉넉하게 잡는다. 이 크기가 곧 인코딩 결과 크기가 되는 것이 아니라
	 * **상한**일 뿐이다(실제 크기는 비트레이트가 정한다). 작게 잡아서 인코더가
	 * 공간 부족으로 실패하는 것이 훨씬 나쁘다.
	 */
	inline constexpr int32 EncodeScratchBytes = 1024;

	/**
	 * ★ 프레임 하나가 이 크기를 넘으면 V3 의 Unreliable RPC 가 조용히 버린다.
	 *
	 * 15절: "Unreliable RPC 는 한 패킷에 들어가야 한다. 페이로드가 커지면
	 * 조용히 버려진다." V3 에서 터지면 원인 찾기가 매우 어려운 종류의 버그라,
	 * **V2 단계에서 미리 감시해서 넘으면 경고를 남긴다.**
	 * 지금 넘는다면 비트레이트나 프레임 길이 설정이 잘못된 것이다.
	 */
	inline constexpr int32 MaxEncodedFrameBytes = 128;

	/**
	 * ★★ 디코딩 결과를 받을 버퍼 크기(샘플 수). **이 값을 줄이면 소리가 안 난다.**
	 *
	 * 20ms 프레임 하나를 디코딩하는데 왜 6프레임짜리 버퍼가 필요한가:
	 * 엔진 디코더가 프레임마다 아래 검사를 하기 때문이다.
	 *
	 *     // VoiceCodecOpus.cpp, FVoiceDecoderOpus::Decode()
	 *     #define MAX_OPUS_FRAMES 6
	 *     if (UncompressedBufferAvail >= (MAX_OPUS_FRAMES * BytesPerFrame))  // 6*640 = 3840
	 *     { ...디코딩... }
	 *     else
	 *     { UE_LOG(..., "Decompression buffer too small to decode voice"); break; }
	 *
	 * **남은 공간이 3840바이트 미만이면 디코딩을 통째로 건너뛴다.**
	 * 320샘플(640바이트)짜리 버퍼를 주면 - 즉 "한 프레임 넣었으니 한 프레임 나오겠지"
	 * 라고 생각한 크기를 주면 - 반환되는 샘플 수가 0 이고, 에러도 아니다.
	 * "인코딩은 되는데 소리만 안 난다" 로 보여서 원인을 엉뚱한 데서 찾게 된다.
	 */
	inline constexpr int32 DecodeScratchSamples = 6 * SamplesPerFrame;  // 1920

	/**
	 * VAD(음성 감지) 임계값. RMS 를 0~1 로 정규화한 값과 비교한다.
	 *
	 * 이 값 아래가 HangoverMs 동안 지속되면 "말이 끝났다" 로 본다.
	 * 마이크 환경에 따라 크게 다르므로 V9 에서 옵션 화면으로 뺄 예정이고,
	 * 그 전까지는 MOU.Voice.Sensitivity 콘솔 명령으로 바꾼다.
	 */
	inline constexpr float DefaultVadThreshold = 0.02f;

	/**
	 * 말끝이 잘리지 않도록 무음이 이만큼 지속돼야 발화 종료로 친다.
	 * 이게 없으면 단어 사이의 짧은 공백마다 발화가 끊겼다 이어진다.
	 */
	inline constexpr float VadHangoverSeconds = 0.2f;

	/**
	 * 재생 링버퍼 용량(프레임 수).
	 *
	 * 게임 스레드가 push 하고 오디오 렌더 스레드가 pop 한다. 두 스레드의 주기가
	 * 다르므로 여유가 필요하다. 너무 작으면 push 가 잘리고(=끊김), 너무 크면
	 * 지연이 쌓인다. 8프레임 = 160ms 는 루프백 확인에 충분한 여유다.
	 */
	inline constexpr int32 PlaybackBufferFrames = 8;

	/**
	 * 재생 버퍼가 이만큼 쌓이면 오래된 것부터 버린다.
	 *
	 * 루프백에서 마이크 입력이 재생 소비보다 빠르면 지연이 무한히 누적된다.
	 * "지금 말한 것이 3초 뒤에 들리는" 상태를 막는 안전장치다.
	 */
	inline constexpr int32 PlaybackDropThresholdFrames = 6;

	// -----------------------------------------------------------------------
	// 발화 모드별 거리 — ★ 단일 진실 공급원 (single source of truth)
	//
	// VOICE_INTEGRATION.md 13절 표의 숫자를 여기 한 곳에만 둔다.
	//
	// **이 값을 읽는 곳이 앞으로 세 군데가 된다:**
	//   1. 디버그 시각화 (MOU.Voice.ShowRadius)          ← 지금
	//   2. 서버 근접 라우팅: 이 반경 * 1.2 안의 사람에게만 전송  (V3)
	//   3. NPC 소음: UAISense_Hearing::ReportNoiseEvent 의 MaxRange (V8)
	//
	// ★ 셋이 반드시 같은 함수를 불러야 한다.
	//   각자 숫자를 따로 들고 있으면 "화면에 보이는 원"과 "NPC 가 실제로 듣는 거리"가
	//   조용히 어긋난다. 그러면 디버그 표시가 거짓말을 하게 되고, 밸런싱이 불가능해진다.
	//   숫자를 바꿀 일이 있으면 **반드시 여기만 고친다.**
	// -----------------------------------------------------------------------

	/**
	 * 사람이 들을 수 있는 총 거리(cm).
	 *
	 * 이것은 감쇠 에셋의 `Radius + FalloffDistance` **합계**다.
	 * 에셋에 이 숫자를 그대로 넣으면 사거리가 두 배가 된다 - 7-2절 참고.
	 */
	inline constexpr float GetHearRadius(EVoiceMode Mode)
	{
		switch (Mode)
		{
		case EVoiceMode::Whisper: return 500.f;   //  5m
		case EVoiceMode::Shout:   return 3000.f;  // 30m
		default:                  return 1500.f;  // 15m (보통)
		}
	}

	/**
	 * NPC 가 들을 수 있는 거리(cm). `ReportNoiseEvent` 의 MaxRange 로 그대로 들어간다.
	 *
	 * ★ 사람이 듣는 거리보다 **일부러 넓다.**
	 *   "나한텐 안 들렸는데 NPC 는 들었다" 가 있어야 긴장이 생긴다.
	 *   같게 만들면 안전 거리를 학습하기가 너무 쉬워진다(13절).
	 */
	inline constexpr float GetNoiseRange(EVoiceMode Mode)
	{
		switch (Mode)
		{
		case EVoiceMode::Whisper: return 700.f;
		case EVoiceMode::Shout:   return 4000.f;
		default:                  return 1800.f;
		}
	}

	/** 소음 이벤트의 Loudness 배율. 측정된 RMS 에 곱해서 쓴다. */
	inline constexpr float GetLoudnessScale(EVoiceMode Mode)
	{
		switch (Mode)
		{
		case EVoiceMode::Whisper: return 0.35f;
		case EVoiceMode::Shout:   return 1.6f;
		default:                  return 1.0f;
		}
	}

	/** 로그/UI 표시용 이름. */
	inline const TCHAR* GetVoiceModeName(EVoiceMode Mode)
	{
		switch (Mode)
		{
		case EVoiceMode::Whisper: return TEXT("속삭임");
		case EVoiceMode::Shout:   return TEXT("외침");
		default:                  return TEXT("보통");
		}
	}

	inline const TCHAR* GetVoiceRouteName(EVoiceRoute Route)
	{
		return Route == EVoiceRoute::Radio ? TEXT("무전") : TEXT("근접");
	}

	// -----------------------------------------------------------------------
	// 감쇠 분해 — ★ 하나의 거리를 두 숫자로 나눠 넣어야 한다 (7-2절)
	//
	//         │←─ Radius ─→│←──── Falloff ────→│
	//    발화자 ●━━━━━━━━━━━━┿━━━━━━━━━━━━━━━━━━━┫  그 너머 = 무음
	//         │   음량 100%  │  100% -> 0% 감소   │
	//         │←──────── GetHearRadius() ───────→│
	//
	// 언리얼 감쇠는 "100% 음량 구간(Radius)" 과 "감쇠 구간(FalloffDistance)" 을
	// 따로 받는다. **총 가청 거리는 그 합이다.**
	// GetHearRadius() 값을 Radius 에 그대로 넣으면 사거리가 두 배가 된다.
	// -----------------------------------------------------------------------

	/** 감쇠가 시작되기 전, 음량 100% 를 유지하는 거리(cm). */
	inline constexpr float GetAttenuationRadius(EVoiceMode Mode)
	{
		switch (Mode)
		{
		case EVoiceMode::Whisper: return 150.f;
		case EVoiceMode::Shout:   return 500.f;
		default:                  return 300.f;
		}
	}

	/** Radius 바깥으로 음량이 100% -> 0% 로 줄어드는 구간의 길이(cm). */
	inline constexpr float GetAttenuationFalloff(EVoiceMode Mode)
	{
		switch (Mode)
		{
		case EVoiceMode::Whisper: return 350.f;
		case EVoiceMode::Shout:   return 2500.f;
		default:                  return 1200.f;
		}
	}

	// ★★ 분해한 두 값의 합이 총 가청 거리와 어긋나지 않도록 **컴파일 타임에** 못박는다.
	//
	//   이게 어긋나면 증상이 고약하다: 서버는 GetHearRadius 기준으로 전송을 끊는데
	//   클라 감쇠는 다른 거리에서 0 이 되므로, **경계에서 목소리가 뚝 끊기거나**
	//   (감쇠가 더 길 때) **들려야 할 거리인데 소리가 없다**(감쇠가 더 짧을 때).
	//   둘 다 "가끔 이상하다" 로만 보여서 원인을 찾기 매우 어렵다.
	//
	//   숫자를 고칠 일이 생기면 세 함수를 같이 고쳐야 하고, 안 그러면 여기서 빌드가 깨진다.
	static_assert(GetAttenuationRadius(EVoiceMode::Whisper) + GetAttenuationFalloff(EVoiceMode::Whisper)
		== GetHearRadius(EVoiceMode::Whisper), "속삭임: Radius + Falloff 가 총 가청 거리와 다르다");
	static_assert(GetAttenuationRadius(EVoiceMode::Normal) + GetAttenuationFalloff(EVoiceMode::Normal)
		== GetHearRadius(EVoiceMode::Normal), "보통: Radius + Falloff 가 총 가청 거리와 다르다");
	static_assert(GetAttenuationRadius(EVoiceMode::Shout) + GetAttenuationFalloff(EVoiceMode::Shout)
		== GetHearRadius(EVoiceMode::Shout), "외침: Radius + Falloff 가 총 가청 거리와 다르다");

	// -----------------------------------------------------------------------
	// 서버 라우팅 (V3)
	// -----------------------------------------------------------------------

	/**
	 * 근접 라우팅 반경에 곱하는 여유값.
	 *
	 * 서버는 `GetHearRadius(Mode) * 이 값` 안에 있는 사람에게만 프레임을 보낸다.
	 * 1.0 이 아니라 1.2 인 이유: 경계에 서 있는 사람은 **서버가 전송을 끊는 지점과
	 * 클라 감쇠가 0 이 되는 지점이 정확히 겹친다.** 거기서 조금만 움직여도
	 * 소리가 붙었다 끊겼다 해서 지직거린다. 감쇠로 이미 들리지 않는 구간까지
	 * 조금 더 보내두면 그 경계가 감쇠 안쪽에 묻힌다.
	 */
	inline constexpr float ProximityRoutingMargin = 1.2f;

	/**
	 * 한 플레이어가 초당 보낼 수 있는 최대 프레임 수(서버 강제).
	 *
	 * 정상값은 50(20ms x 50 = 1초)이다. 60 은 틱 흔들림을 감안한 여유.
	 * ★ 이것은 최적화가 아니라 **서버 보호**다. 개조 클라이언트가 초당 수천 개를
	 *   보내면 서버는 그걸 그대로 N명에게 복사한다 - 한 명이 방 전체를 마비시킬 수 있다.
	 *   V3 이 클라 데이터를 서버가 처음 받는 지점이라 여기서부터 막는다.
	 */
	inline constexpr int32 MaxFramesPerSecPerPlayer = 60;

	/**
	 * 이만큼 조용하면 그 발신자의 재생 스트림을 정리한다(초).
	 *
	 * 스트림 하나는 오디오 엔진의 **사운드 슬롯 하나를 계속 점유한다** -
	 * 무음을 재생하는 중이어도 마찬가지다. 방을 나간 사람의 스트림이 남아있으면
	 * 그 슬롯이 영영 안 돌아온다.
	 *
	 * 너무 짧으면 말 사이의 공백마다 스트림을 만들었다 부쉈다 한다. 3초는
	 * "대화가 끝났다" 로 볼 만하면서 문장 사이 호흡보다는 충분히 길다.
	 */
	inline constexpr double VoiceStreamIdleTimeoutSeconds = 3.0;

	/**
	 * 이만큼 프레임이 끊겼다가 다시 오면 **새 발화**로 보고 디코더를 리셋한다(초).
	 *
	 * 리셋하지 않으면 새 발화의 첫 프레임을 공백 이전 소리를 참고해 복원해서
	 * 말 첫머리가 짧게 지직거린다(V2 에서 같은 이유로 로컬 디코더도 리셋한다).
	 * VAD hangover(0.2초)보다 커야 한 문장 안에서 리셋되지 않는다.
	 */
	inline constexpr double VoiceUtteranceGapSeconds = 0.5;

	/** 음량(0~1)을 패킷에 실을 uint8 로 양자화한다. */
	inline uint8 QuantizeLoudness(float Loudness01)
	{
		return static_cast<uint8>(FMath::Clamp(Loudness01, 0.f, 1.f) * 255.f + 0.5f);
	}

	/** 양자화된 음량을 0~1 로 되돌린다. */
	inline float DequantizeLoudness(uint8 Quantized)
	{
		return static_cast<float>(Quantized) / 255.f;
	}

	/**
	 * 네트워크로 받은 enum 을 신뢰 가능한 범위로 자른다.
	 *
	 * ★ 개조 클라이언트는 enum 에 아무 값이나 넣을 수 있다. 범위 밖 값이
	 *   그대로 switch 에 들어가면 default 로 떨어져 당장은 안 죽지만,
	 *   나중에 이 값으로 배열을 인덱싱하는 코드가 생기면 그때 터진다.
	 *   **서버 경계에서 한 번 자르고 들어가는 것이 규칙이다.**
	 */
	inline EVoiceMode SanitizeMode(EVoiceMode Mode)
	{
		return (Mode < EVoiceMode::MAX) ? Mode : EVoiceMode::Normal;
	}

	inline EVoiceRoute SanitizeRoute(EVoiceRoute Route)
	{
		return (Route < EVoiceRoute::MAX) ? Route : EVoiceRoute::Proximity;
	}
}

// ---------------------------------------------------------------------------
// 패킷 (V3)
//
// VOICE_INTEGRATION.md 10절. 두 구조체가 **따로 있는 이유**가 이 설계의 핵심이다:
//
//   FVoiceFrame     클라가 서버에 보내는 것. **주장(claim)** 이다. 못 믿는다.
//   FVoiceFrameOut  서버가 클라에 보내는 것. **사실(fact)** 이다. 서버가 확정했다.
//
// 하나로 합치면 "이 필드를 믿어도 되는가" 가 코드에서 사라진다. 특히 SpeakerId 를
// 클라가 보내게 두면 **남을 사칭할 수 있다.** 그래서 SpeakerId 는 서버가 채우는
// FVoiceFrameOut 에만 있고, FVoiceFrame 에는 아예 자리가 없다.
// ---------------------------------------------------------------------------

/**
 * 클라 -> 서버. 20ms 한 조각.
 *
 * **발신자가 누구인지는 들어있지 않다.** 서버가 RPC 를 받은 컴포넌트의 소유자로
 * 알아내기 때문이다(그게 위조 불가능한 유일한 출처다).
 */
USTRUCT()
struct FVoiceFrame
{
	GENERATED_BODY()

	/** 지터버퍼 정렬과 유실 감지용. 순환(65535 -> 0)해도 무방하다. */
	UPROPERTY() uint16 Seq = 0;

	/** 클라가 **요청하는** 라우트. 최종 결정은 서버가 한다(EVoiceRoute 주석). */
	UPROPERTY() EVoiceRoute Route = EVoiceRoute::Proximity;

	/**
	 * 발화 모드. 이 값이 **서버의 라우팅 반경을 결정한다.**
	 *
	 * ★ 알려진 신뢰 문제: 개조 클라이언트가 항상 Shout 를 보내면 30m 밖까지
	 *   목소리가 전달된다. 지금은 서버가 범위 검사만 한다.
	 *   제대로 막으려면 발화 모드를 PlayerState 에 리플리케이트해서 서버가
	 *   자기가 아는 값을 쓰면 된다 - 키 입력이 이미 서버를 거치기 때문이다.
	 *   V9(옵션/키 바인딩)에서 같이 정리한다.
	 */
	UPROPERTY() EVoiceMode Mode = EVoiceMode::Normal;

	/** RMS 를 0~255 로 양자화한 값. UI 게이지와 V8 의 소음 크기에 쓴다. */
	UPROPERTY() uint8 Loudness = 0;

	/** Opus 압축 바이트. 정상값은 50~70바이트. 상한은 MOUVoice::MaxEncodedFrameBytes. */
	UPROPERTY() TArray<uint8> Opus;
};

/**
 * 서버 -> 클라. **여기 있는 값은 전부 서버가 확정한 것이다.**
 */
USTRUCT()
struct FVoiceFrameOut
{
	GENERATED_BODY()

	/**
	 * 누가 말했는가. `APlayerState::GetPlayerId()`.
	 *
	 * 받는 쪽은 이 값으로 발신자의 폰을 찾아 거기서 소리를 낸다.
	 * **서버만 채운다** - 클라가 보내는 FVoiceFrame 에는 이 필드가 없다.
	 */
	UPROPERTY() int32 SpeakerId = 0;

	UPROPERTY() uint16 Seq = 0;

	/** 서버가 우선순위까지 적용해 확정한 라우트. 재생 방식이 이 값으로 갈린다. */
	UPROPERTY() EVoiceRoute Route = EVoiceRoute::Proximity;

	/**
	 * 서버가 확정한 발화 모드.
	 *
	 * 받는 쪽이 **감쇠 반경을 이 값으로 정한다.** 서버의 라우팅 반경과 같은
	 * 값에서 나와야 "서버가 끊는 거리" 와 "클라에서 안 들리는 거리" 가 맞는다.
	 * 그래서 클라가 스스로 추측하지 않고 서버가 실어 보낸다.
	 */
	UPROPERTY() EVoiceMode Mode = EVoiceMode::Normal;

	/** 말하는 중 UI 표시(아이콘/게이지)에 쓴다. */
	UPROPERTY() uint8 Loudness = 0;

	UPROPERTY() TArray<uint8> Opus;
};
