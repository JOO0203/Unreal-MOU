// MOU 음성 - 음성 재생 출구 구현.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 7-2절, 11절
//
// [★ OnGenerateAudio 는 오디오 렌더 스레드다. 헤더 주석의 금지 목록을 반드시 지킬 것.]

#include "Voice/VoiceSynthComponent.h"

UVoiceSynthComponent::UVoiceSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 모노. 음성은 스테레오로 만들 이유가 없고, 공간화는 오디오 엔진이 한다.
	NumChannels = MOUVoice::NumChannels;

	// V1 은 루프백(내 목소리를 내가 듣기)이라 2D 로 낸다.
	// V3 에서 발신자 폰에 붙일 때 true 로 바꾸고 AttenuationSettings 를 건다.
	bAllowSpatialization = false;

	// 이 컴포넌트는 액터에 붙은 채로 계속 살아있고 필요할 때만 소리를 낸다.
	// 자동 활성화해두지 않으면 PushSamples 를 해도 아무 소리가 안 난다.
	bAutoActivate = true;
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
	// GC 압력과 프레임 스파이크가 생긴다. 한 프레임은 320샘플로 고정이므로
	// 상한이 명확해 스택으로 충분하다.
	float Converted[MOUVoice::SamplesPerFrame];
	const int32 CountToConvert = FMath::Min(NumSamples, MOUVoice::SamplesPerFrame);

	for (int32 Index = 0; Index < CountToConvert; ++Index)
	{
		Converted[Index] = static_cast<float>(Samples[Index]) / 32768.f;
	}

	const int32 Pushed = RingBuffer.Push(Converted, static_cast<uint32>(CountToConvert));

	// 넘쳐서 못 넣은 만큼을 센다. 이 값이 계속 오르면 마이크 입력이 재생보다
	// 빨라 지연이 쌓이고 있다는 뜻이다(= 버퍼를 비워야 한다).
	if (Pushed < CountToConvert)
	{
		OverflowCounter.Add(CountToConvert - Pushed);
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
