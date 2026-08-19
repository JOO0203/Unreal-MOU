// MOU 음성 - 수신 재생 구현.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 7-2절, 14절 V3
//
// [스레드] 전부 게임 스레드다. 오디오 렌더 스레드로 넘어가는 경계는
//          UVoiceSynthComponent 의 링버퍼 하나뿐이다(11절).

#include "Voice/VoicePlaybackComponent.h"

#include "Voice/VoiceCodec.h"
#include "Voice/VoiceSynthComponent.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

UVoicePlaybackComponent::UVoicePlaybackComponent()
{
	// 유휴 스트림 정리에만 쓰는 틱이라 매 프레임 돌 이유가 없다.
	// 0.5초 간격이면 정리가 늦어봐야 반 박자이고 비용은 사실상 0 이다.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.5f;
}

UVoicePlaybackComponent* UVoicePlaybackComponent::FindOrCreate(APlayerController* OwnerPC)
{
	if (!IsValid(OwnerPC))
	{
		return nullptr;
	}

	// ★ 로컬 컨트롤러가 아니면 만들지 않는다.
	//
	//   리슨서버에서 이걸 빼먹으면 호스트가 **접속한 모든 플레이어의 컨트롤러마다**
	//   재생 컴포넌트를 만들게 된다. 아무도 못 듣는 소리를 위해 사람 수만큼
	//   디코딩과 사운드를 돌리는 셈이라, 인원이 늘수록 호스트만 느려진다.
	if (!OwnerPC->IsLocalController())
	{
		return nullptr;
	}

	if (UVoicePlaybackComponent* Existing = OwnerPC->FindComponentByClass<UVoicePlaybackComponent>())
	{
		return Existing;
	}

	UVoicePlaybackComponent* Created = NewObject<UVoicePlaybackComponent>(OwnerPC, TEXT("MOUVoicePlaybackRemote"));
	Created->RegisterComponent();

	UE_LOG(LogMOUVoice, Log, TEXT("수신 재생 컴포넌트 생성 완료."));
	return Created;
}

void UVoicePlaybackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetAllStreams();
	Super::EndPlay(EndPlayReason);
}

void UVoicePlaybackComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	CleanupIdleStreams();
}

// ---------------------------------------------------------------------------
// 프레임 처리
// ---------------------------------------------------------------------------

void UVoicePlaybackComponent::HandleFrame(const FVoiceFrameOut& Frame)
{
	++TotalFramesReceived;

	if (Frame.Opus.Num() <= 0)
	{
		return;
	}

	// 서버가 확정해 보낸 값이지만 한 번 더 자른다.
	// 서버가 우리 편이라는 가정은 맞지만, 범위 밖 enum 이 흘러들어오면
	// 나중에 이 값으로 배열을 인덱싱하는 코드가 생겼을 때 터진다.
	const EVoiceRoute Route = MOUVoice::SanitizeRoute(Frame.Route);
	const EVoiceMode  Mode  = MOUVoice::SanitizeMode(Frame.Mode);

	const FVoiceStreamKey Key{ Frame.SpeakerId, Route };
	FVoiceStream& Stream = Streams.FindOrAdd(Key);

	const double Now = FPlatformTime::Seconds();

	// --- 디코더 준비 --------------------------------------------------------
	if (!Stream.Decoder.IsValid())
	{
		Stream.Decoder = MakeUnique<FMOUVoiceDecoder>();
		if (!Stream.Decoder->Initialize())
		{
			// 코덱을 못 만들면 이 발신자의 소리는 못 낸다. 스트림을 지워
			// 다음 프레임에서 다시 시도하게 둔다(일시적 문제일 수 있다).
			Streams.Remove(Key);
			return;
		}
	}
	// 오래 끊겼다 다시 오면 새 발화다. 디코더의 예측 상태를 비운다.
	else if (Stream.bHasSeq && (Now - Stream.LastFrameTime) > MOUVoice::VoiceUtteranceGapSeconds)
	{
		Stream.Decoder->Reset();
		Stream.bHasSeq = false;
	}

	// --- 순서 검사 ----------------------------------------------------------
	//
	// ★ uint16 순환(65535 -> 0)을 견뎌야 한다.
	//   Seq 를 그냥 크기 비교하면 순환하는 순간 "65535개를 잃었다" 가 되어
	//   통계가 망가지고, 정상 패킷을 오래된 것으로 오판해 버리게 된다.
	//   **차이를 uint16 으로 계산하면** 순환이 자동으로 처리된다.
	if (Stream.bHasSeq)
	{
		const uint16 Delta = static_cast<uint16>(Frame.Seq - Stream.LastSeq);

		if (Delta == 0)
		{
			// 같은 프레임이 두 번 왔다. 재생하면 소리가 겹친다.
			++TotalFramesDropped;
			return;
		}

		if (Delta > 32768)
		{
			// 차이를 부호 있는 값으로 보면 음수 - 즉 **뒤늦게 도착한 옛 패킷**이다.
			// 지터버퍼가 없는 V3 에서는 순서를 되돌릴 수 없으므로 버리는 것이 맞다.
			// 지금 재생하면 이미 지나간 소리가 끼어들어 오히려 더 어색해진다.
			// (V4 에서 지터버퍼가 생기면 여기서 버리지 않고 제자리에 꽂는다)
			++TotalFramesDropped;
			return;
		}

		if (Delta > 1)
		{
			// 중간이 비었다 = 유실. Opus 는 짧은 공백은 알아서 메운다.
			Stream.FramesLost += Delta - 1;
		}
	}

	Stream.LastSeq = Frame.Seq;
	Stream.bHasSeq = true;
	Stream.LastFrameTime = Now;

	// --- 디코딩 -------------------------------------------------------------
	if (!Stream.Decoder->Decode(Frame.Opus.GetData(), Frame.Opus.Num(), Stream.DecodedScratch))
	{
		++TotalFramesDropped;
		return;
	}

	// --- 사운드 준비 (발신자 폰에 부착) --------------------------------------
	UVoiceSynthComponent* Synth = EnsureSynthForStream(Stream, Frame.SpeakerId, Mode);
	if (!Synth)
	{
		// 발신자 폰이 아직 이 클라에 없다. 다음 프레임에 다시 시도한다.
		return;
	}

	// --- 지연 방지 ----------------------------------------------------------
	//
	// 네트워크 흔들림으로 프레임이 몰려 오면 버퍼가 계속 차서 "몇 초 전 목소리" 를
	// 듣게 된다. 음성은 오래된 것을 버리고 현재로 따라붙는 것이 맞다.
	const int32 BufferedFrames = Synth->GetBufferedSampleCount() / MOUVoice::SamplesPerFrame;
	if (BufferedFrames >= MOUVoice::PlaybackDropThresholdFrames)
	{
		Synth->RequestFlush();
		++TotalFramesDropped;
	}

	Synth->PushSamples(Stream.DecodedScratch.GetData(), Stream.DecodedScratch.Num());
	++Stream.FramesPlayed;
}

// ---------------------------------------------------------------------------
// 스트림 관리
// ---------------------------------------------------------------------------

APawn* UVoicePlaybackComponent::FindSpeakerPawn(int32 SpeakerId) const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;

	if (!GameState)
	{
		return nullptr;
	}

	// PlayerArray 는 리플리케이트되므로 클라에서도 채워져 있다.
	// 인원이 한 방에 몇 명뿐이라 선형 탐색으로 충분하다 - 20ms 마다 도는 것이
	// 부담이 되면 그때 TMap 캐시를 두면 되지만, 지금 넣으면 플레이어 입퇴장마다
	// 캐시를 무효화하는 코드가 더 늘어난다.
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (PlayerState && PlayerState->GetPlayerId() == SpeakerId)
		{
			return PlayerState->GetPawn();
		}
	}

	return nullptr;
}

UVoiceSynthComponent* UVoicePlaybackComponent::EnsureSynthForStream(
	FVoiceStream& Stream, int32 SpeakerId, EVoiceMode Mode)
{
	APawn* SpeakerPawn = FindSpeakerPawn(SpeakerId);

	if (!SpeakerPawn)
	{
		// 발신자는 근접 반경 안에 있으므로 폰도 리플리케이트돼 있어야 정상이다.
		// 접속 직후처럼 아직 안 온 순간에만 잠깐 걸린다. 계속 오르면 문제다.
		++TotalPawnMisses;
		return nullptr;
	}

	UVoiceSynthComponent* Synth = Stream.Synth.Get();

	// 폰이 바뀌었으면(사망 후 리스폰) 옛 사운드는 죽은 폰에 붙어 있다.
	// 그대로 두면 새 위치가 아니라 **시체가 있던 자리에서 목소리가 난다.**
	if (Synth && Stream.AttachedPawn.Get() != SpeakerPawn)
	{
		Synth->Stop();
		Synth->DestroyComponent();
		Synth = nullptr;
		Stream.Synth.Reset();
	}

	if (!Synth)
	{
		USceneComponent* AttachTarget = SpeakerPawn->GetRootComponent();

		if (!AttachTarget)
		{
			// 붙일 곳이 없으면 만들지 않는다.
			// 그냥 만들면 컴포넌트가 월드 원점에 놓여 **목소리가 맵 한가운데서 난다** -
			// "소리는 나는데 엉뚱한 데서 들린다" 는 원인 찾기 고약한 증상이다.
			++TotalPawnMisses;
			return nullptr;
		}

		Synth = NewObject<UVoiceSynthComponent>(SpeakerPawn);

		// ★★ 순서가 중요하다. SetProximityMode 를 Start() **전에** 불러야 한다.
		//    공간화 여부는 사운드를 만드는 시점에 읽히기 때문에, 나중에 켜면
		//    그 사운드는 끝까지 2D 로 난다 - "소리는 나는데 방향이 없다" 가 된다.
		Synth->SetProximityMode(Mode);

		// 등록 전에는 SetupAttachment, 등록 후에는 AttachToComponent 다.
		// 여기서는 아직 등록 전이다.
		Synth->SetupAttachment(AttachTarget);
		Synth->RegisterComponent();
		Synth->Start();

		Stream.Synth = Synth;
		Stream.AttachedPawn = SpeakerPawn;
	}
	else
	{
		// 발화 모드가 바뀌었으면 감쇠만 갈아끼운다(모드가 그대로면 즉시 반환한다).
		Synth->SetProximityMode(Mode);
	}

	return Synth;
}

void UVoicePlaybackComponent::CleanupIdleStreams()
{
	const double Now = FPlatformTime::Seconds();

	for (auto It = Streams.CreateIterator(); It; ++It)
	{
		FVoiceStream& Stream = It.Value();

		if ((Now - Stream.LastFrameTime) <= MOUVoice::VoiceStreamIdleTimeoutSeconds)
		{
			continue;
		}

		if (UVoiceSynthComponent* Synth = Stream.Synth.Get())
		{
			Synth->Stop();
			Synth->DestroyComponent();
		}

		It.RemoveCurrent();
	}
}

void UVoicePlaybackComponent::ResetAllStreams()
{
	for (TPair<FVoiceStreamKey, FVoiceStream>& Pair : Streams)
	{
		if (UVoiceSynthComponent* Synth = Pair.Value.Synth.Get())
		{
			Synth->Stop();
			Synth->DestroyComponent();
		}
	}

	Streams.Empty();
}

FString UVoicePlaybackComponent::GetStatsString() const
{
	int32 Lost = 0;
	int32 Played = 0;

	for (const TPair<FVoiceStreamKey, FVoiceStream>& Pair : Streams)
	{
		Lost   += Pair.Value.FramesLost;
		Played += Pair.Value.FramesPlayed;
	}

	return FString::Printf(
		TEXT("스트림=%d 수신=%d 재생=%d 버림=%d 유실=%d 폰없음=%d"),
		Streams.Num(), TotalFramesReceived, Played, TotalFramesDropped, Lost, TotalPawnMisses);
}
