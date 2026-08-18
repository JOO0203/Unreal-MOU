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
class URadioComponent;

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

	// --- 무전기 레지스트리 (V6) ---------------------------------------------

	/**
	 * 켜진 무전기를 목록에 넣는다. `URadioComponent::SetPowered(true)` 가 부른다.
	 *
	 * ★ 라우터가 매 프레임 월드의 모든 액터를 훑으면 안 된다 - 무전 프레임마다
	 *   O(월드 액터 수)가 된다. 무전기가 스스로 등록하면 O(켜진 무전기 수)다(10절).
	 */
	void RegisterRadio(URadioComponent* Radio);

	/** 목록에서 뺀다. 전원을 끄거나 무전기가 파괴될 때 부른다. */
	void UnregisterRadio(URadioComponent* Radio);

	/** 지금 켜져 있는 무전기 수. */
	int32 GetPoweredRadioCount() const { return PoweredRadios.Num(); }

	/**
	 * 이 플레이어가 지금 쓸 수 있는(들고 있고 켜져 있는) 무전기를 찾는다.
	 * 없으면 null - 무전 송신 자격이 없다는 뜻이다.
	 */
	URadioComponent* FindUsableRadioFor(const APlayerState* Holder) const;

	/** 진단용 한 줄 요약. */
	FString GetStatsString() const;

private:
	/**
	 * 근접 라우팅. 이미 보낸 사람을 OutRouted 에 기록한다.
	 *
	 * @return 실제로 보낸 사람 수
	 */
	int32 RouteProximity(APlayerController* SenderPC, const FVector& SenderLocation,
		EVoiceMode Mode, FVoiceFrameOut& Out, TSet<APlayerController*>& OutRouted);

	/**
	 * 무전 라우팅. **OutRouted 에 이미 있는 사람은 건너뛴다.**
	 *
	 * ★ 이 건너뛰기가 "라우트 우선순위: 근접 > 무전" 의 실체다(7-3절).
	 *   무전 송신 중에도 육성은 그대로 나가므로, 가까이 있는 사람은 근접과 무전
	 *   **양쪽 조건에 다 걸린다.** 둘 다 보내면 같은 목소리가 두 번 겹쳐 에코가 된다.
	 */
	int32 RouteRadio(APlayerController* SenderPC, FVoiceFrameOut& Out,
		const TSet<APlayerController*>& AlreadyRouted);

	/**
	 * 반이중 채널을 이 발신자가 쓸 수 있는지 확인하고 점유한다.
	 * @return 송신해도 되면 true.
	 */
	bool TryAcquireRadioChannel(int32 SpeakerId, double Now);

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

	/**
	 * 전원이 켜진 무전기 목록.
	 *
	 * ★ TWeakObjectPtr 인 이유: 무전기는 **파괴될 수 있는 아이템**이다.
	 *   강참조로 들면 부순 무전기가 목록 때문에 안 사라지고, 생포인터로 들면
	 *   죽은 메모리를 읽는다. 순회할 때 IsValid() 로 걸러내며 정리한다(15절).
	 */
	TArray<TWeakObjectPtr<URadioComponent>> PoweredRadios;

	// --- 반이중 채널 (동시에 한 명만 송신) -----------------------------------

	/** 지금 채널을 점유한 발신자. INDEX_NONE 이면 비어 있다. */
	int32 RadioChannelOwnerId = INDEX_NONE;

	/** 점유자가 마지막으로 프레임을 보낸 시각. 이후 일정 시간이 지나면 풀린다. */
	double RadioChannelLastFrameTime = 0.0;


	// --- 진단 ---------------------------------------------------------------
	int32 FramesRouted      = 0;  // 서버가 처리한 프레임 수
	int32 FramesDelivered   = 0;  // 실제로 내보낸 수신 건수(사람 수만큼 는다)
	int32 FramesRateLimited = 0;
	int32 FramesRejected    = 0;  // 크기/신원/위치 문제로 버린 것

	/**
	 * 사망자가 보내와서 버린 프레임 수.
	 *
	 * ★ 이 값이 오르는 것 자체는 정상이다 - 클라가 죽은 것을 아직 모르는
	 *   짧은 순간(복제 지연)에 몇 개는 온다. **계속 오르면** 그 클라의
	 *   1겹 차단이 안 먹고 있다는 뜻이고, 개조 클라이언트일 수도 있다.
	 */
	int32 FramesFromDead = 0;

	/** 무전으로 라우팅한 프레임 수. */
	int32 FramesRadio = 0;

	/** 반이중 채널이 이미 점유돼 있어 버린 프레임 수. */
	int32 FramesChannelBusy = 0;

	/** 무전기가 없거나 꺼져 있어서 무전 송신이 거부된 프레임 수. */
	int32 FramesNoRadio = 0;
};
