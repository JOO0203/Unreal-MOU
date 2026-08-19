// MOU 음성 - 음성 재생 출구 구현.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 7-2절, 11절
//
// [★ OnGenerateAudio 는 오디오 렌더 스레드다. 헤더 주석의 금지 목록을 반드시 지킬 것.]

#include "Voice/VoiceSynthComponent.h"

#include "Components/AudioComponent.h"
#include "Engine/Attenuation.h"
#include "Sound/SoundAttenuation.h"

UVoiceSynthComponent::UVoiceSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 모노. 음성은 스테레오로 만들 이유가 없고, 공간화는 오디오 엔진이 한다.
	NumChannels = MOUVoice::NumChannels;

	// 기본은 2D 다. 루프백(내 목소리를 내가 듣기)은 월드 위치가 없으므로 그게 맞다.
	// 근접 재생은 SetProximityMode() 가 켠다.
	bAllowSpatialization = false;

	// 이 컴포넌트는 액터에 붙은 채로 계속 살아있고 필요할 때만 소리를 낸다.
	// 자동 활성화해두지 않으면 PushSamples 를 해도 아무 소리가 안 난다.
	bAutoActivate = true;
}

namespace
{
	/**
	 * 발화 모드에 맞는 근접 감쇠 설정을 만든다.
	 *
	 * 숫자는 전부 MOUVoice:: 함수에서 온다. 여기에 상수를 직접 적으면 안 된다 -
	 * 서버의 라우팅 반경, 디버그 링, V8 의 NPC 소음 반경이 같은 함수를 보고 있어서
	 * 여기만 다른 값을 쓰면 **화면에 보이는 원과 실제로 들리는 거리가 조용히 어긋난다.**
	 */
	FSoundAttenuationSettings MakeProximityAttenuation(EVoiceMode Mode)
	{
		FSoundAttenuationSettings Settings;

		Settings.bAttenuate  = true;
		Settings.bSpatialize = true;

		// 구형 감쇠. AttenuationShapeExtents.X 가 "음량 100% 를 유지하는 반경" 이고,
		// FalloffDistance 는 **그 바깥으로 추가되는** 감쇠 구간이다.
		// 총 가청 거리는 둘의 합이며, VoiceTypes.h 의 static_assert 가 이를 보증한다.
		Settings.AttenuationShape = EAttenuationShape::Sphere;
		Settings.AttenuationShapeExtents = FVector(MOUVoice::GetAttenuationRadius(Mode), 0.f, 0.f);
		Settings.FalloffDistance = MOUVoice::GetAttenuationFalloff(Mode);

		// dB 기반이라 사람 귀에 가장 자연스럽다(7-2절).
		Settings.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;

		// ★★ 이 한 줄이 없으면 "경계에서 목소리가 뚝 끊긴다".
		//
		//   기본값은 Continues 다 - 엔진 주석 그대로 "가청 거리를 넘어가도 계속
		//   감쇠할 뿐 완전히 꺼지지는 않는다". 그런데 **서버는 반경 밖 사람에게
		//   프레임을 아예 안 보낸다.** 그러면 클라는 아직 들리려 하는데 데이터가
		//   끊겨서, 감쇠로 서서히 사라지는 대신 그 지점에서 잘려나간다.
		//
		//   Silent 로 두어야 클라 감쇠가 0 이 되는 거리와 서버가 끊는 거리가 맞는다.
		//   (서버의 1.2배 여유는 이 정렬을 위한 안전 마진이다 - 7-2절)
		Settings.FalloffMode = ENaturalSoundFalloffMode::Silent;

		// 공기 흡수. 멀수록 고음이 깎여 먹먹해진다 - 거리감이 확 산다. 싸다.
		Settings.bAttenuateWithLPF = true;

		// 벽 뒤 목소리가 막힌다. 숨바꼭질 게임에는 사실상 필수다.
		//
		// ★ 비용 주의: **동시 발화자 수 = 매 프레임 트레이스 수**다.
		//   프로파일링에서 걸리면 여기를 먼저 끈다(무전기 스피커부터, 7-2절).
		Settings.bEnableOcclusion = true;

		return Settings;
	}
}

void UVoiceSynthComponent::SetProximityMode(EVoiceMode Mode)
{
	// 매 프레임 불려도 되도록 조기 반환한다.
	// 감쇠 갱신은 오디오 스레드로 명령을 보내는 일이라 공짜가 아니다.
	if (bSpatialConfigured && CurrentMode == Mode)
	{
		return;
	}

	const FSoundAttenuationSettings Settings = MakeProximityAttenuation(Mode);

	// ★ bAllowSpatialization 은 **사운드를 만들 때** 읽힌다.
	//   Start() 뒤에 켜도 그 사운드는 끝까지 2D 로 난다. 그래서 첫 호출은
	//   반드시 Start() 전이어야 한다(헤더 주석).
	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	AttenuationOverrides = Settings;

	// 이미 소리가 나고 있으면 살아있는 사운드에 갈아끼운다.
	//
	// 위에서 멤버만 바꾸면 **이미 재생 중인 사운드에는 반영되지 않는다** -
	// 다음에 Start() 를 다시 부를 때까지 옛 반경이 유지된다. 말하는 도중
	// 속삭임에서 외침으로 바꾸면 반경이 안 늘어나는 버그가 된다.
	// Stop/Start 로 다시 만들면 반영되지만 그 순간 소리가 끊겨 딸깍거린다.
	if (UAudioComponent* AudioComp = GetAudioComponent())
	{
		AudioComp->AdjustAttenuation(Settings);
	}

	CurrentMode = Mode;
	bSpatialConfigured = true;
}

bool UVoiceSynthComponent::Init(int32& SampleRate)
{
	// 오디오 엔진에게 "나는 16kHz 로 낸다" 고 알린다.
	// 엔진 출력이 48kHz 여도 오디오 믹서가 리샘플링해준다.
	SampleRate = MOUVoice::SampleRate;

	// 링버퍼를 미리 잡는다. OnGenerateAudio 안에서는 할당을 할 수 없으므로
	// 반드시 여기(게임 스레드)에서 최종 크기를 확보해야 한다.
	RingBuffer.SetCapacity(MOUVoice::SamplesPerFrame * MOUVoice::PlaybackBufferFrames);

	return true;
}

void UVoiceSynthComponent::PushSamples(const int16* Samples, int32 NumSamples)
{
	if (!Samples || NumSamples <= 0)
	{
		return;
	}

	// PCM16 -> float(-1~1). 오디오 엔진은 float 로 다룬다.
	//
	// 스택 배열을 쓰는 이유: 매 프레임 TArray 를 할당하면 20ms 마다 힙을 때려
	// GC 압력과 프레임 스파이크가 생긴다.
	//
	// ★ 320샘플씩 끊어서 도는 이유 - 예전에는 `Min(NumSamples, 320)` 으로
	//   **넘치는 만큼을 그냥 버렸다.** 지금은 항상 320개만 들어오니 티가 안 나지만,
	//   V4 의 지터버퍼가 여러 프레임을 한 번에 밀어넣거나 디코더가 한 패킷에서
	//   여러 프레임을 풀어내면 **뒷부분이 조용히 사라진다.** 소리가 미묘하게
	//   끊기는데 카운터에도 안 잡혀서 원인을 찾을 실마리가 없는 종류의 버그다.
	//   버퍼 크기는 스택 상한일 뿐 처리량 상한이어서는 안 된다.
	float Converted[MOUVoice::SamplesPerFrame];

	const int16* Cursor = Samples;
	int32 Remaining = NumSamples;

	while (Remaining > 0)
	{
		const int32 ChunkSize = FMath::Min(Remaining, MOUVoice::SamplesPerFrame);

		for (int32 Index = 0; Index < ChunkSize; ++Index)
		{
			Converted[Index] = static_cast<float>(Cursor[Index]) / 32768.f;
		}

		const int32 Pushed = RingBuffer.Push(Converted, static_cast<uint32>(ChunkSize));

		// 넘쳐서 못 넣은 만큼을 센다. 이 값이 계속 오르면 입력이 재생보다
		// 빨라 지연이 쌓이고 있다는 뜻이다(= 버퍼를 비워야 한다).
		if (Pushed < ChunkSize)
		{
			// 한 번 가득 찼으면 남은 것도 못 들어간다. 세어두고 그만둔다.
			OverflowCounter.Add(Remaining - Pushed);
			return;
		}

		Cursor    += ChunkSize;
		Remaining -= ChunkSize;
	}
}

int32 UVoiceSynthComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	// ★★ 여기는 오디오 렌더 스레드다.
	//    UObject 접근 / 락 / 메모리 할당 / UE_LOG 전부 금지.
	//    아래 코드는 링버퍼 pop 과 float 산술만 한다.

	// 게임 스레드가 "비워달라" 고 했으면 여기서 비운다.
	// Pop 은 읽기 커서만 앞으로 미는 것이라 소비자 스레드에서 안전하다.
	// (게임 스레드가 직접 비우면 버퍼 메모리를 재할당하게 되어 위험하다)
	const int32 FlushRequest = FlushRequestCounter.GetValue();
	if (FlushRequest != LastHandledFlushRequest)
	{
		LastHandledFlushRequest = FlushRequest;
		RingBuffer.Pop(RingBuffer.Num());
	}

	const int32 Popped = RingBuffer.Pop(OutAudio, static_cast<uint32>(NumSamples));

	if (Popped < NumSamples)
	{
		// 데이터가 모자란 만큼은 무음으로 채운다.
		// 채우지 않으면 이전 버퍼 내용이 그대로 남아 지직거린다.
		FMemory::Memzero(OutAudio + Popped, (NumSamples - Popped) * sizeof(float));

		// 언더런 카운트. 말하고 있지 않을 때는 정상적으로 계속 오르므로,
		// "발화 중인데 오르는지" 로 판단해야 한다.
		UnderrunCounter.Increment();
	}

	// 모자란 부분도 무음으로 채웠으므로 요청받은 만큼 전부 채운 것이다.
	return NumSamples;
}

int32 UVoiceSynthComponent::GetBufferedSampleCount() const
{
	return static_cast<int32>(RingBuffer.Num());
}

void UVoiceSynthComponent::RequestFlush()
{
	// ★ 여기서 RingBuffer 를 직접 건드리면 안 된다.
	//   SetCapacity() 는 내부 TArray 를 재할당하는데, 그 순간 오디오 렌더
	//   스레드가 Pop() 안에서 옛 메모리를 읽고 있으면 use-after-free 다.
	//   플래그만 올리고 실제 비우기는 소비자(렌더 스레드)에게 맡긴다.
	FlushRequestCounter.Increment();

	// 진단 카운터는 게임 스레드에서 지워도 안전하다(원자적이고, 버퍼와 무관하다).
	UnderrunCounter.Reset();
	OverflowCounter.Reset();
}
