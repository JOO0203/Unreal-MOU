// MOU 음성 - 서버 라우팅.
//
// [이 파일이 시스템 어디에 있나]
//
//   여러 클라 ──ServerSendVoiceFrame──▶ ★ UVoiceRouter (서버 전용)
//                                          │  · 발신자 신원을 서버가 확정
//                                          │  · 레이트 리밋
//                                          │  · 수신자 결정 (근접 = 거리)
//                                          ▼
//                                       각 수신자의 UVoiceComponent
//                                          └─ ClientReceiveVoiceFrame
//
// [★ 이 클래스가 존재하는 이유 = "클라가 보낸 값을 하나도 안 믿기 위해서"]
//
//   클라가 보내는 FVoiceFrame 에는 **발신자가 누구인지 들어있지 않다.**
//   서버가 RPC 를 받은 컴포넌트의 소유자로 알아낸다 - 그게 위조할 수 없는
//   유일한 출처이기 때문이다. 클라가 SpeakerId 를 실어 보내게 두면
//   **남을 사칭할 수 있다.**
//
//   같은 이유로 "누가 들을 수 있는가" 도 전적으로 서버가 정한다. 클라에게
//   보내놓고 클라가 안 듣는 방식(음소거)으로 만들면, 개조 클라이언트가
//   **들으면 안 되는 소리를 다 듣는다**(7-3절의 무전기 전원과 같은 원칙).
//
// [왜 WorldSubsystem 인가]
//   라우팅은 월드(맵)에 종속된 상태다 - 위치, 플레이어 목록, V6 의 무전기
//   레지스트리 전부 월드가 바뀌면 갈아엎어야 한다. GameInstanceSubsystem 으로
//   두면 레벨 이동 때 옛 월드의 액터를 붙들고 있게 된다.
//
// [현재 구현 단계 - V3]
//   근접만. 아래가 아직 없고, 각각 자리를 주석으로 표시해 두었다:
//     V5  사망자 차단 (3중 방어의 서버 몫)
//     V6  무전 라우팅 + 무전기 레지스트리 + 반이중 중재
//     V8  소음 이벤트 발행
//
// [대응하는 문서]
//   VOICE_INTEGRATION.md 5절, 6절, 7-2절(근접 라우팅), 14절 V3

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Voice/VoiceTypes.h"
#include "VoiceRouter.generated.h"

class APlayerController;

/**
 * 음성 프레임을 누구에게 보낼지 정한다. **서버에서만 의미가 있다.**
 *
 * 클라에도 객체는 생기지만(월드 서브시스템이라 막을 방법이 마땅치 않다)
 * RouteFrame 이 클라에서 불릴 일이 없다 - Server RPC 안에서만 부르기 때문이다.
 */
UCLASS()
class TEAMPROJECT_MOU_API UVoiceRouter : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 월드에서 라우터를 얻는다. 없으면 null. */
	static UVoiceRouter* Get(const UWorld* World);

	/**
	 * 프레임 하나를 받아 들을 자격이 있는 사람들에게 보낸다.
	 *
	 * @param SenderPC  RPC 를 보낸 컨트롤러. **발신자 신원의 유일한 출처다.**
	 * @param Frame     클라가 보낸 값. 신원 외에는 전부 여기서 검증한다.
	 */
	void RouteFrame(APlayerController* SenderPC, const FVoiceFrame& Frame);

	/** 진단용 한 줄 요약. */
	FString GetStatsString() const;

private:
	/**
	 * 초당 프레임 수를 제한한다.
	 * @return 통과하면 true. false 면 이 프레임은 버린다.
	 */
	bool CheckRateLimit(int32 SpeakerId, double Now);

	/** 플레이어 한 명의 초당 전송량 창(window). */
	struct FRateWindow
	{
		double WindowStartTime = 0.0;
		int32  FrameCount      = 0;

		/** 이 창에서 이미 경고를 남겼는지. 매 프레임 로그하면 로그가 잠긴다. */
		bool bWarnedThisWindow = false;
	};

	/** 발신자별 레이트 리밋 상태. */
	TMap<int32, FRateWindow> RateWindows;

	// --- 진단 ---------------------------------------------------------------
	int32 FramesRouted      = 0;  // 서버가 처리한 프레임 수
	int32 FramesDelivered   = 0;  // 실제로 내보낸 수신 건수(사람 수만큼 는다)
	int32 FramesRateLimited = 0;
	int32 FramesRejected    = 0;  // 크기/신원/위치 문제로 버린 것
};
