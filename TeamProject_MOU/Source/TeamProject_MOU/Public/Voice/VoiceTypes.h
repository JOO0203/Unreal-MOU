// MOU 음성 - 음성 시스템 전체가 공유하는 타입과 상수.
//
// [이 파일이 시스템 어디에 있나]
//   Voice/ 폴더의 모든 파일이 이 헤더를 include 한다. 여기에는 로직이 없다.
//   설계 문서: 저장소 루트의 VOICE_INTEGRATION.md
//
// [현재 구현 단계]
//   V0 (FakeNoise 콘솔 명령) + V1 (로컬 루프백) 까지.
//   네트워크 전송(V3)과 Opus 코덱(V2)은 아직 없다. 그래서 EVoiceRoute 같은
//   라우팅 관련 타입은 아직 만들지 않았다 - 쓰는 곳이 없는 타입을 미리 만들면
//   설계가 바뀔 때 같이 바꿔야 하는 짐만 된다.
//
// [수정 시 같이 고쳐야 하는 파일]
//   SampleRate/FrameMs 를 바꾸면 VoiceCaptureRunnable.cpp 의 프레이밍과
//   VoiceSynthComponent 의 링버퍼 크기가 같이 영향을 받는다.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/** 음성 시스템 전용 로그 카테고리. 에디터 출력 로그에서 'LogMOUVoice' 로 필터링한다. */
DECLARE_LOG_CATEGORY_EXTERN(LogMOUVoice, Log, All);

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
	 * 지연과 오버헤드의 균형점이다. V2 에서 Opus 를 넣을 때 이 값을 그대로 쓴다.
	 */
	inline constexpr int32 FrameMs          = 20;
	inline constexpr int32 SamplesPerFrame  = SampleRate * FrameMs / 1000;  // 320
	inline constexpr int32 BytesPerFrame    = SamplesPerFrame * sizeof(int16); // 640

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
}
