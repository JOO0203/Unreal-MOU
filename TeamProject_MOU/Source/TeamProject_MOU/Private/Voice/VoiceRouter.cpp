// MOU 음성 - 서버 라우팅 구현.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 7-2절(근접), 13절(반경), 14절 V3
//
// [스레드] 서버 게임 스레드 전용. 블로킹 I/O 를 하면 호스트 프레임이 떨어진다(11절).

#include "Voice/VoiceRouter.h"

#include "Voice/VoiceComponent.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

UVoiceRouter* UVoiceRouter::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UVoiceRouter>() : nullptr;
}

void UVoiceRouter::RouteFrame(APlayerController* SenderPC, const FVoiceFrame& Frame)
{
	UWorld* World = GetWorld();

	if (!World || !IsValid(SenderPC))
	{
		++FramesRejected;
		return;
	}

	// --- 1. 발신자 신원 확정 ------------------------------------------------
	//
	// ★ 클라가 보낸 값이 아니라 **RPC 를 받은 컨트롤러**에서 얻는다.
	//   이게 이 함수 전체에서 가장 중요한 줄이다 - 여기서 클라 값을 쓰면
	//   누구나 남을 사칭할 수 있게 된다.
	const APlayerState* SenderState = SenderPC->PlayerState;

	if (!SenderState)
	{
		// 접속 직후 PlayerState 가 아직 없는 순간. 곧 생긴다.
		++FramesRejected;
		return;
	}

	const int32 SpeakerId = SenderState->GetPlayerId();
	const double Now = FPlatformTime::Seconds();

	// --- 2. 레이트 리밋 -----------------------------------------------------
	//
	// 크기 검사보다 **먼저** 한다. 홍수를 막는 것이 목적이므로, 버릴 프레임이라도
	// 개수는 세어야 한다. 뒤에 두면 규격 위반 프레임을 초당 수천 개 보내는
	// 공격이 리밋을 통째로 우회한다.
	if (!CheckRateLimit(SpeakerId, Now))
	{
		++FramesRateLimited;
		return;
	}

	++FramesRouted;

	// --- 3. 페이로드 검증 ---------------------------------------------------
	//
	// 여기서 걸리는 것은 접속을 끊을 만한 일은 아니다(VoiceComponent 의
	// GHardMaxOpusBytes 주석 참고). 조용히 버린다.
	if (Frame.Opus.Num() <= 0 || Frame.Opus.Num() > MOUVoice::MaxEncodedFrameBytes)
	{
		++FramesRejected;
		return;
	}

	// --- 4. 발신자 위치 -----------------------------------------------------
	const APawn* SenderPawn = SenderPC->GetPawn();

	if (!SenderPawn)
	{
		// 폰이 없으면 근접 음성을 낼 위치가 없다(관전 중 등).
		//
		// >>> V5 에서 여기에 사망 검사가 들어간다:
		//       사망자가 보낸 프레임은 **라우팅 전에** 버린다. 클라 차단만
		//       두면 개조 클라이언트가 죽어서도 계속 말할 수 있다(8절 3중 방어).
		++FramesRejected;
		return;
	}

	const FVector SenderLocation = SenderPawn->GetActorLocation();

	// --- 5. 반경 결정 -------------------------------------------------------
	const EVoiceMode Mode = MOUVoice::SanitizeMode(Frame.Mode);

	// ★ 반경 숫자를 여기 적으면 안 된다. MOUVoice:: 함수 하나에서만 나와야
	//   디버그 링(MOU.Voice.ShowRadius), 클라 감쇠, V8 의 NPC 소음 반경이
	//   서로 어긋나지 않는다(VoiceTypes.h 의 단일 진실 공급원 주석).
	const float Radius = MOUVoice::GetHearRadius(Mode) * MOUVoice::ProximityRoutingMargin;
	const float RadiusSquared = Radius * Radius;

	// --- 6. 내보낼 프레임 구성 ----------------------------------------------
	//
	// 여기 담기는 값은 전부 **서버가 확정한 것**이다. 그래서 받는 쪽은 이 값을
	// 그대로 믿고 재생해도 된다.
	FVoiceFrameOut Out;
	Out.SpeakerId = SpeakerId;
	Out.Seq       = Frame.Seq;
	Out.Mode      = Mode;
	Out.Loudness  = Frame.Loudness;
	Out.Opus      = Frame.Opus;

	// V3 은 근접뿐이다. 클라가 무전을 요청해도 근접으로 내보낸다.
	//
	// >>> V6 에서 여기가 라우트 결정 로직이 된다:
	//       · 무전이면 무전기 소지/전원을 서버가 재확인
	//       · 수신자 한 명당 **한 라우트만** (근접 안이면 근접, 밖이면 무전).
	//         둘 다 보내면 같은 목소리가 겹쳐 들려 에코가 된다(7-3절).
	Out.Route = EVoiceRoute::Proximity;

	// --- 7. 수신자 결정 -----------------------------------------------------
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* ListenerPC = It->Get();

		if (!IsValid(ListenerPC) || ListenerPC == SenderPC)
		{
			// 자기 목소리는 자기에게 안 보낸다.
			// 자기 목소리를 듣고 싶으면 MOU.Voice.Loopback 을 쓴다(디버그 전용).
			continue;
		}

		const APawn* ListenerPawn = ListenerPC->GetPawn();

		if (!ListenerPawn)
		{
			continue;
		}

		// >>> V5: 여기에 "수신자가 사망자면 제외" 가 들어간다.
		//         이게 없으면 죽은 사람이 산 사람 대화를 엿듣는다(8절).

		// 제곱 거리로 비교한다. 매 프레임 x 인원수만큼 도는 자리라
		// 제곱근을 뽑을 이유가 없다.
		const float DistanceSquared =
			FVector::DistSquared(ListenerPawn->GetActorLocation(), SenderLocation);

		if (DistanceSquared > RadiusSquared)
		{
			continue;
		}

		if (UVoiceComponent* ListenerVoice = UVoiceComponent::Find(ListenerPC))
		{
			ListenerVoice->DeliverToOwner(Out);
			++FramesDelivered;
		}
	}

	// >>> V8: 여기에 소음 이벤트 집계가 들어간다.
	//         ★ 프레임마다 쏘면 안 된다 - 초당 50회 x 인원수의 perception 갱신이
	//         돌아 서버가 죽는다. 0.3초 창의 최대 음량을 모아 한 번 보고한다(7-5절).
}

// ---------------------------------------------------------------------------
// 레이트 리밋
// ---------------------------------------------------------------------------

bool UVoiceRouter::CheckRateLimit(int32 SpeakerId, double Now)
{
	FRateWindow& Window = RateWindows.FindOrAdd(SpeakerId);

	// 새 창 시작. 처음 만들어진 항목은 WindowStartTime 이 0 이라 여기서 초기화된다.
	if ((Now - Window.WindowStartTime) >= 1.0)
	{
		Window.WindowStartTime    = Now;
		Window.FrameCount         = 0;
		Window.bWarnedThisWindow  = false;
	}

	++Window.FrameCount;

	if (Window.FrameCount <= MOUVoice::MaxFramesPerSecPerPlayer)
	{
		return true;
	}

	// 창마다 한 번만 경고한다. 매 프레임 남기면 초당 수천 줄이 찍혀
	// **로그를 보느라 서버가 더 느려진다** - 공격을 도와주는 꼴이 된다.
	if (!Window.bWarnedThisWindow)
	{
		Window.bWarnedThisWindow = true;
		UE_LOG(LogMOUVoice, Warning,
			TEXT("플레이어 %d 의 음성 전송이 초당 %d 프레임을 넘었다(정상값 50). ")
			TEXT("초과분은 버린다."),
			SpeakerId, MOUVoice::MaxFramesPerSecPerPlayer);
	}

	return false;
}

FString UVoiceRouter::GetStatsString() const
{
	return FString::Printf(
		TEXT("라우팅=%d 전달=%d 리밋=%d 거부=%d 발신자=%d명"),
		FramesRouted, FramesDelivered, FramesRateLimited, FramesRejected, RateWindows.Num());
}
