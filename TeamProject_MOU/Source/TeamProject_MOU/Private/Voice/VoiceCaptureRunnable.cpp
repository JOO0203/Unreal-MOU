// MOU 음성 - 마이크 캡처 워커 스레드 구현.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 7-1절 (캡처), 11절 (스레드 경계)
//
// [★ 이 파일의 모든 코드는 게임 스레드가 아니다]
//   Run() 안에서 UObject 를 만지면 재현이 어려운 랜덤 크래시가 난다.
//   유일한 예외는 DequeueFrame() / Get*() 로, 이들은 게임 스레드에서 불린다.

#include "Voice/VoiceCaptureRunnable.h"

#include "Interfaces/VoiceCapture.h"
#include "VoiceModule.h"

DEFINE_LOG_CATEGORY(LogMOUVoice);

FVoiceCaptureRunnable::FVoiceCaptureRunnable()
{
	VadThresholdBits.Set(FloatToBits(MOUVoice::DefaultVadThreshold));
	CurrentLoudnessBits.Set(FloatToBits(0.f));
}

FVoiceCaptureRunnable::~FVoiceCaptureRunnable()
{
	// 여기서 캡처를 정리하지 않는다. Exit() 이 이미 했다.
	// 소멸자에서 또 만지면 이미 해제된 것을 다시 만지게 된다.
}

int32 FVoiceCaptureRunnable::FloatToBits(float Value)
{
	// float 를 원자적으로 주고받기 위한 비트 재해석.
	// 언리얼에 float 원자 타입이 없어서 int32 로 옮겨 담는다.
	int32 Bits = 0;
	FMemory::Memcpy(&Bits, &Value, sizeof(int32));
	return Bits;
}

float FVoiceCaptureRunnable::BitsToFloat(int32 Bits)
{
	float Value = 0.f;
	FMemory::Memcpy(&Value, &Bits, sizeof(float));
	return Value;
}

void FVoiceCaptureRunnable::SetVadThreshold(float InThreshold)
{
	VadThresholdBits.Set(FloatToBits(FMath::Clamp(InThreshold, 0.f, 1.f)));
}

float FVoiceCaptureRunnable::GetVadThreshold() const
{
	return BitsToFloat(VadThresholdBits.GetValue());
}

float FVoiceCaptureRunnable::GetCurrentLoudness() const
{
	return BitsToFloat(CurrentLoudnessBits.GetValue());
}

bool FVoiceCaptureRunnable::Init()
{
	// [중요] DefaultEngine.ini 에 [Voice] bEnabled=true 가 없으면 여기서 걸린다.
	// 모듈은 링크되지만 IsVoiceEnabled() 가 false 라 캡처 생성이 전부 null 이 된다.
	if (!FVoiceModule::IsAvailable() || !FVoiceModule::Get().IsVoiceEnabled())
	{
		UE_LOG(LogMOUVoice, Warning,
			TEXT("엔진 Voice 모듈이 꺼져 있다. DefaultEngine.ini 에 [Voice] bEnabled=true 가 있는지 확인할 것. ")
			TEXT("음성 캡처 없이 계속 진행한다(재생은 정상 동작)."));
		bCaptureReady = false;
		return true; // 스레드는 정상 시작시킨다. 캡처만 안 할 뿐이다.
	}

	// DeviceName 을 비우면 시스템 기본 마이크를 쓴다.
	VoiceCapture = FVoiceModule::Get().CreateVoiceCapture(
		FString(), MOUVoice::SampleRate, MOUVoice::NumChannels);

	if (!VoiceCapture.IsValid())
	{
		// 마이크가 없거나 Windows 마이크 권한이 꺼져 있으면 여기로 온다.
		// 시스템 전체를 죽이지 않는다 - 마이크 없는 팀원도 게임은 해야 한다.
		UE_LOG(LogMOUVoice, Warning,
			TEXT("마이크를 열지 못했다. 장치가 없거나 Windows 마이크 권한이 꺼져 있을 수 있다. ")
			TEXT("음성 캡처 없이 계속 진행한다."));
		bCaptureReady = false;
		return true;
	}

	if (!VoiceCapture->Start())
	{
		UE_LOG(LogMOUVoice, Warning, TEXT("마이크는 열렸지만 캡처 시작에 실패했다."));
		VoiceCapture->Shutdown();
		VoiceCapture.Reset();
		bCaptureReady = false;
		return true;
	}

	bCaptureReady = true;
	UE_LOG(LogMOUVoice, Log, TEXT("마이크 캡처 시작 (%d Hz, %d채널, %dms 프레임)."),
		MOUVoice::SampleRate, MOUVoice::NumChannels, MOUVoice::FrameMs);
	return true;
}

uint32 FVoiceCaptureRunnable::Run()
{
	// 엔진이 한 번에 주는 양이 들쭉날쭉하므로 넉넉히 잡는다.
	// 1초치면 어떤 지연에도 넘치지 않는다.
	TArray<uint8> ReadBuffer;
	ReadBuffer.SetNumUninitialized(MOUVoice::SampleRate * sizeof(int16));

	while (!bStopRequested)
	{
		if (!bCaptureReady || !VoiceCapture.IsValid())
		{
			// 마이크가 없는 경우. 스레드는 살려두되 아무 일도 하지 않는다.
			// (Stop() 이 올 때까지 기다린다)
			FPlatformProcess::Sleep(0.1f);
			continue;
		}

		uint32 AvailableBytes = 0;
		const EVoiceCaptureState::Type State = VoiceCapture->GetCaptureState(AvailableBytes);

		if (State == EVoiceCaptureState::Ok && AvailableBytes > 0)
		{
			uint32 ReadBytes = 0;
			const EVoiceCaptureState::Type ReadState = VoiceCapture->GetVoiceData(
				ReadBuffer.GetData(), static_cast<uint32>(ReadBuffer.Num()), ReadBytes);

			if (ReadState == EVoiceCaptureState::Ok && ReadBytes > 0)
			{
				PendingBytes.Append(ReadBuffer.GetData(), static_cast<int32>(ReadBytes));
				DrainPendingSamples();
			}
		}

		// 프레임 절반 주기로 돈다. 프레임 주기와 같게 두면 매번 아슬아슬하게
		// 늦어 프레임이 하나씩 밀린다.
		FPlatformProcess::Sleep(MOUVoice::FrameMs * 0.5f / 1000.f);
	}

	return 0;
}

void FVoiceCaptureRunnable::DrainPendingSamples()
{
	// 20ms(=640바이트) 단위로 정확히 잘라낸다. 남는 꼬리는 다음 회차로 넘긴다.
	while (PendingBytes.Num() >= MOUVoice::BytesPerFrame)
	{
		FMOUVoiceFrame Frame;
		Frame.Samples.SetNumUninitialized(MOUVoice::SamplesPerFrame);
		FMemory::Memcpy(Frame.Samples.GetData(), PendingBytes.GetData(), MOUVoice::BytesPerFrame);

		Frame.Loudness = ComputeRms(Frame.Samples.GetData(), MOUVoice::SamplesPerFrame);
		CurrentLoudnessBits.Set(FloatToBits(Frame.Loudness));

		// --- VAD: 임계값 + hangover -----------------------------------------
		// hangover 가 없으면 단어 사이 공백마다 발화가 끊겼다 이어져서
		// 말끝이 잘리고 NPC 소음 이벤트도 잘게 쪼개진다.
		const float Threshold = GetVadThreshold();
		const double Now = FPlatformTime::Seconds();

		if (Frame.Loudness >= Threshold)
		{
			bSpeaking = true;
			SilenceStartedAt = 0.0;
		}
		else if (bSpeaking)
		{
			if (SilenceStartedAt == 0.0)
			{
				SilenceStartedAt = Now;
			}
			else if (Now - SilenceStartedAt >= MOUVoice::VadHangoverSeconds)
			{
				bSpeaking = false;
				SilenceStartedAt = 0.0;
			}
		}

		Frame.bIsSpeaking = bSpeaking;
		CapturedFrames.Enqueue(MoveTemp(Frame));

		PendingBytes.RemoveAt(0, MOUVoice::BytesPerFrame, EAllowShrinking::No);
	}
}

float FVoiceCaptureRunnable::ComputeRms(const int16* Samples, int32 NumSamples)
{
	if (!Samples || NumSamples <= 0)
	{
		return 0.f;
	}

	// double 로 누적한다. float 로 하면 320개를 더하는 동안 정밀도가 눈에 띄게 깎인다.
	double SumOfSquares = 0.0;
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		const double Normalized = static_cast<double>(Samples[Index]) / 32768.0;
		SumOfSquares += Normalized * Normalized;
	}

	return static_cast<float>(FMath::Sqrt(SumOfSquares / static_cast<double>(NumSamples)));
}

bool FVoiceCaptureRunnable::DequeueFrame(FMOUVoiceFrame& OutFrame)
{
	return CapturedFrames.Dequeue(OutFrame);
}

void FVoiceCaptureRunnable::Stop()
{
	bStopRequested = true;
}

void FVoiceCaptureRunnable::Exit()
{
	// 워커 스레드가 Run() 을 빠져나온 직후 이 스레드에서 불린다.
	// 캡처를 만든 스레드에서 정리하는 것이 가장 안전하다.
	if (VoiceCapture.IsValid())
	{
		VoiceCapture->Stop();
		VoiceCapture->Shutdown();
		VoiceCapture.Reset();
	}
	bCaptureReady = false;
	UE_LOG(LogMOUVoice, Log, TEXT("마이크 캡처 종료."));
}
