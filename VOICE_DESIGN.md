# MOU 음성 시스템 설계 (무전기 / 근접 음성 / NPC 청각)

작성: 2026-08-12
상태: **설계만. 코드 없음.**

이 문서는 `SERVER_INTEGRATION.md` 와 같은 규칙을 따른다 — 무엇을 하는지보다 **왜 그렇게 했는지**를 남긴다.

---

## 목차

1. [무엇을 만드는가](#1-무엇을-만드는가)
2. [큰 그림](#2-큰-그림)
3. [왜 UE 내장 VoIP 를 안 쓰는가](#3-왜-ue-내장-voip-를-안-쓰는가)
4. [왜 채팅서버가 아니라 리슨서버로 보내는가](#4-왜-채팅서버가-아니라-리슨서버로-보내는가)
5. [클래스 설계](#5-클래스-설계)
6. [파이프라인 상세](#6-파이프라인-상세)
7. [패킷 / 데이터 구조](#7-패킷--데이터-구조)
8. [스레드 경계](#8-스레드-경계)
9. [대역폭 계산](#9-대역폭-계산)
10. [튜닝 파라미터](#10-튜닝-파라미터)
11. [구현 순서 (마일스톤)](#11-구현-순서-마일스톤)
12. [알려진 함정](#12-알려진-함정)
13. [착수 전에 확인할 것](#13-착수-전에-확인할-것)

---

## 1. 무엇을 만드는가

요구사항 세 가지다. **셋은 같은 파이프라인의 서로 다른 출구**이지, 별개 시스템이 아니다.

| # | 기능 | 한 줄 정의 |
|---|---|---|
| A | **근접 음성** | 내 목소리가 내 캐릭터 위치에서 3D 로 나온다. 가까운 사람만 듣는다 |
| B | **무전** | 내 목소리가 같은 주파수를 켠 사람에게 거리와 무관하게 간다. 2D + 무전기 필터 |
| C | **NPC 청각** | 말하면 그게 소음이 되어 적대 NPC 가 찾아온다 |

### 이 설계의 핵심 재미 포인트

C 를 A/B 와 엮으면 규칙 하나로 게임이 만들어진다.

```
말한다 → 소리가 난다 → NPC 가 온다
```

여기서 파생되는 것들:

- **근접으로 말하면 내 위치가 노출된다.** 속삭이면 반경이 줄고, 소리치면 늘어난다.
- **무전으로 말해도 입은 움직인다.** 송신자 위치에 작은 소음이 난다 (속삭임 수준).
- **★ 받는 쪽 무전기가 소리를 낸다.** 팀원이 무전을 치면 **내 무전기 스피커에서** 소리가 나고,
  그게 **내 위치의 소음**이 된다. 숨어 있는데 팀원이 무전을 치면 들킨다.
- **이어피스(이어폰)를 끼면** 위 소음이 사라진다. → 아이템 가치가 생긴다.
- **무전기를 켜 두면** 남의 송신이 내 위치에서 새어나온다. → 끄고 다닐 이유가 생긴다.

**이 문서는 위 규칙을 시스템으로 만드는 것이 목표다.** 무전기 아이템 자체(모델, 인벤토리,
줍기)는 다루지 않는다. 다만 시스템이 아이템에게 요구하는 인터페이스(`URadioComponent`)는 정의한다.

---

## 2. 큰 그림

```
 [ 내 클라이언트 ]
   마이크
     │ IVoiceCapture (16kHz mono PCM16)
     ▼
   VoiceCaptureRunnable  (워커 스레드)     ← UObject 금지. 기존 채팅 규칙과 동일
     │  20ms 프레이밍 + RMS(음량) 계산 + VAD(무음 컷)
     │  Opus 인코딩 (~40~80바이트/프레임)
     ▼ TQueue (SPSC)
   VoiceSubsystem (게임 스레드)
     │  PTT 상태, 발화 모드(속삭임/보통/외침), 무전 채널
     ▼
   VoiceComponent (PlayerController 에 부착)
     │  ServerSendVoiceFrame()  ← Server, Unreliable RPC
     ▼
╔══════════════════ 리슨서버 (권위) ══════════════════╗
║  VoiceRouter (WorldSubsystem)                        ║
║    · 발신자 신원을 서버가 확정 (클라 값 안 믿음)      ║
║    · 레이트 리밋 / 페이로드 크기 검사                 ║
║    · 라우팅 결정:                                     ║
║        근접 → 반경 안의 PlayerController 목록         ║
║        무전 → 같은 주파수 + 전원 켜짐 목록            ║
║    · 반이중(half-duplex) 채널 점유 중재               ║
║                                                       ║
║  NoiseReporter                                        ║
║    · 0.3초마다 UAISense_Hearing::ReportNoiseEvent    ║
║        - 송신자 입 위치 (근접/무전 각각 다른 반경)    ║
║        - ★ 수신자 무전기 스피커 위치 (이어피스면 생략)║
╚═══════════════════════════════════════════════════════╝
     │ ClientReceiveVoiceFrame()  ← Client, Unreliable RPC. 들을 자격이 있는 사람에게만
     ▼
 [ 듣는 클라이언트 ]
   VoicePlaybackComponent
     │  (발신자, 라우트) 쌍마다 지터버퍼 + Opus 디코더
     ▼ TCircularAudioBuffer<float> (SPSC, 락 프리)
   VoiceSynthComponent (USynthComponent)   ← OnGenerateAudio 는 오디오 렌더 스레드
     ├─ 근접 라우트: 발신자 폰에 부착. 3D + Attenuation + Occlusion
     └─ 무전 라우트: 내 무전기 액터에 부착. 3D(스피커 위치) + 무전 필터(EQ/디스토션/노이즈)
                     이어피스 착용 시에만 2D 로 전환
```

### 기존 시스템과의 관계

| | 채팅 / 로그인 / 로비 | 음성 |
|---|---|---|
| 경로 | `Server.exe` 로 가는 별도 TCP | **게임 리슨서버의 UE 리플리케이션** |
| 이유 | 호스트가 나가도 유지돼야 함 | **위치를 알아야 함.** 위치는 리슨서버에만 있다 |

**음성은 채팅서버를 타지 않는다.** 이유는 4절.

---

## 3. 왜 UE 내장 VoIP 를 안 쓰는가

UE 에는 이미 `OnlineSubsystem` + `UVOIPTalker` 조합의 VoIP 가 있다. 그걸 안 쓰는 이유:

| | 내장 VoIP (`UVOIPTalker` + OSS) | **직접 구현 (이 설계)** |
|---|---|---|
| 붙이는 비용 | 매우 낮음. 컴포넌트 붙이고 ini 설정 | 높음. 캡처/인코딩/지터버퍼 직접 |
| 3D 공간화 | 됨 (`UVOIPTalker` + Attenuation) | 됨 |
| **한 사람이 두 경로로 동시 송출** | **안 됨** — 근접과 무전을 동시에 다르게 라우팅 못 함 | 됨 |
| **수신자별 라우팅 결정** | 뮤트 리스트 수준. 거리 기반 컷 어려움 | 완전 제어 |
| **음량(RMS)을 게임 로직이 읽기** | 어려움 | 캡처 단계에서 바로 나옴 |
| **AI 청각 연동** | 별도로 다시 만들어야 함 | 파이프라인에 내장 |
| OSS 의존 | `OnlineSubsystemNull` 필요, 리슨서버 조합에서 함정 많음 | 없음 |

**결론: 직접 구현한다.**

결정적인 이유는 "한 명이 근접과 무전으로 **동시에**, **서로 다른 수신자에게**, **서로 다른 필터로**
나가야 한다" 는 요구다. 내장 VoIP 는 "플레이어 → 음성 스트림 1개" 모델이라 이걸 표현할 수 없다.
거기다 C(NPC 청각)를 위해 음량 값이 필요한데, 내장 경로에서는 그 값이 엔진 안에 갇혀 있다.

**단, 엔진의 `Voice` 모듈은 그대로 쓴다.** 마이크 캡처(`IVoiceCapture`)와 Opus 코덱
(`IVoiceEncoder`/`IVoiceDecoder`)은 엔진 것을 쓴다. 직접 만드는 것은 그 **위의 라우팅과 재생**이다.
Opus 를 직접 붙이거나 WASAPI 를 직접 만지는 일은 하지 않는다.

```csharp
// Build.cs 에 추가할 것
"Voice",             // IVoiceCapture / IVoiceEncoder / IVoiceDecoder
"AudioMixer",        // USynthComponent
"SignalProcessing",  // 무전 필터용 (선택)
"AudioExtensions",   // 소스 이펙트 (선택)
```

```ini
; DefaultEngine.ini
[Voice]
bEnabled=true
```

---

## 4. 왜 채팅서버가 아니라 리슨서버로 보내는가

기존 시스템이 채팅·로비를 별도 TCP 서버로 뺐으니 음성도 그리로 보내고 싶을 수 있다. **안 된다.**

| 근거 | 설명 |
|---|---|
| **TCP 는 음성에 부적합** | 패킷 하나가 늦으면 뒤의 것이 전부 대기한다(head-of-line blocking). 음성은 늦은 패킷을 **버리는 게** 맞다. 재전송된 200ms 전 목소리는 쓰레기다 |
| **위치가 채팅서버에 없다** | 근접 음성의 라우팅 조건은 "거리"다. 거리를 알려면 위치가 필요하고, 위치는 리슨서버에만 있다. 채팅서버에 위치를 계속 밀어 넣으면 그건 게임서버를 하나 더 만드는 것이다 |
| **AI 청각이 서버에 있다** | `UAIPerceptionSystem` 은 게임 월드(리슨서버)에서 돈다. 소음 이벤트를 만들려면 어차피 리슨서버가 발화 사실을 알아야 한다 |
| **채널 상태가 게임 상태다** | 무전기 전원/주파수는 아이템 상태 = 게임 상태 = 리슨서버가 권위 |

> **채팅서버가 유리한 점(호스트가 나가도 유지)은 음성에서 의미가 없다.**
> 호스트가 나가면 게임 세션 자체가 없어지므로(문서 11절) 근접 음성이 유지될 이유가 없다.

**전송은 UE 리플리케이션의 Unreliable RPC 로 한다.** Unreliable 이므로 잃어버린 프레임은 그냥
잃어버린다 — 지터버퍼가 PLC(packet loss concealment)로 때운다. 이게 음성의 정상 동작이다.

---

## 5. 클래스 설계

파일 배치는 기존 `Chat/` 과 같은 규칙을 따른다.

```
Source/TeamProject_MOU/
  Public/Voice/                     Private/Voice/
    VoiceTypes.h                      (헤더 전용)
    VoiceCaptureRunnable.h            VoiceCaptureRunnable.cpp
    VoiceSubsystem.h                  VoiceSubsystem.cpp
    VoiceComponent.h                  VoiceComponent.cpp
    VoiceRouter.h                     VoiceRouter.cpp
    VoicePlaybackComponent.h          VoicePlaybackComponent.cpp
    VoiceSynthComponent.h             VoiceSynthComponent.cpp
    RadioComponent.h                  RadioComponent.cpp
```

| 클래스 | 부모 | 어디 사는가 | 역할 |
|---|---|---|---|
| `FVoiceCaptureRunnable` | `FRunnable` | 클라 워커 스레드 | 마이크 폴링 → 20ms 프레이밍 → RMS/VAD → Opus 인코딩 → `TQueue` |
| `UVoiceSubsystem` | `ULocalPlayerSubsystem` | 클라 게임 스레드 | 워커 생명주기, PTT, 발화 모드, 입력 장치 선택, 내 음성 UI 상태 |
| `UVoiceComponent` | `UActorComponent` | `APlayerController` | RPC 창구. 올려보내기/받아오기. **양쪽에 존재** |
| `UVoiceRouter` | `UWorldSubsystem` | **서버 전용** | 라우팅 테이블, 수신자 결정, 반이중 중재, 레이트 리밋, 소음 이벤트 발행 |
| `UVoicePlaybackComponent` | `UActorComponent` | 클라 (내 PC) | 지터버퍼 + 디코더 풀. 스트림마다 Synth 를 만들고 붙인다 |
| `UVoiceSynthComponent` | `USynthComponent` | 클라 (폰/무전기에 부착) | 링버퍼에서 PCM 을 꺼내 오디오 렌더 스레드에 공급 |
| `URadioComponent` | `UActorComponent` | 서버+클라 | 무전기 상태(전원/주파수/이어피스/음량). **아이템 파트가 채워주는 인터페이스** |

### 왜 `UVoiceComponent` 를 폰이 아니라 `PlayerController` 에 두는가

- 폰이 죽어도 살아있다. 관전 중에도 무전은 되어야 한다.
- `PlayerController` 는 **자기 소유 클라이언트에게 항상 relevant** 하다. 폰에 붙이면
  "멀리 있어서 relevant 하지 않은 폰" 의 RPC 가 조용히 사라진다 — **무전은 거리와 무관해야 하므로 치명적이다.**
- 서버가 "누구에게 보낼지" 를 `PlayerController` 단위로 고른다. 컴포넌트가 거기 있으면 그대로 부르면 된다.

### 왜 `UVoiceSubsystem` 이 `LocalPlayerSubsystem` 인가

마이크는 **로컬 플레이어당 하나**다. `GameInstanceSubsystem`(채팅이 쓰는 것)으로 하면
PIE 다중 창에서 창마다 마이크를 열려고 해서 장치 경합이 난다.

> **PIE 다중 창 테스트 시 주의:** 창 두 개가 같은 물리 마이크를 열려고 한다.
> `VoiceSubsystem` 에 `bCaptureEnabled` 를 두고 **PIE 에서는 첫 창만 캡처**하도록 막는다.
> 나머지 창은 재생만 검증한다. 진짜 양방향 테스트는 프로세스 두 개로 한다.

---

## 6. 파이프라인 상세

### 6-1. 캡처 (클라이언트)

```
IVoiceCapture(16kHz, mono, PCM16)
  → GetVoiceData() 로 가변 길이 바이트 회수
  → 누적 버퍼에 붙이고 320샘플(=20ms=640바이트) 단위로 잘라냄
  → 프레임마다 RMS 계산 → 0~1 정규화 → Loudness
  → VAD: Loudness < 임계값이 200ms 지속되면 송신 중단(꼬리 자르기)
  → IVoiceEncoder::Encode → 40~80바이트
  → TQueue<FVoiceFrame> (SPSC)
```

**왜 16kHz mono 인가.** 사람 목소리 명료도는 8kHz 대역이면 충분하고(전화가 그렇다),
무전기 톤은 오히려 대역이 좁을수록 그럴듯하다. 48kHz 스테레오로 올리면 대역폭만 6배가 된다.

**왜 20ms 프레임인가.** Opus 가 지원하는 프레임 길이 중 지연/오버헤드 균형점이다.
대역폭이 문제가 되면 **40ms 로 올리는 노브를 둔다** — 패킷 수가 절반이 되고 지연은 20ms 늘어난다.

**VAD 는 게임 로직이기도 하다.** 무음을 안 보내면 대역폭이 줄어드는 것도 있지만,
**"지금 말하고 있는가" 가 곧 "지금 소리를 내고 있는가"** 라서 C(NPC 청각)의 입력이 된다.
그래서 VAD 결과를 워커에서 계산해 프레임에 실어 보낸다. 게임 스레드에서 다시 계산하지 않는다.

**PTT(Push To Talk) 기본, 오픈 마이크는 옵션.**
근접은 오픈 마이크가 자연스럽지만, 기본값은 PTT 로 둔다 — 오픈 마이크 + NPC 청각 조합은
"기침했더니 죽었다" 가 되어 팀 테스트에서 불만이 폭발한다. 옵션으로 열어두되 기본은 끈다.

**입력 바인딩(EnhancedInput):**

| 액션 | 기본 키 | 하는 일 |
|---|---|---|
| `IA_VoiceProximity` | `V` (홀드) | 근접 발화 |
| `IA_VoiceRadio` | `B` (홀드) | 무전 송신 |
| `IA_VoiceMode` | `Ctrl+V` (토글) | 속삭임 ↔ 보통 ↔ 외침 |

### 6-2. 송신과 라우팅 (서버 권위)

**클라 → 서버**

```cpp
UFUNCTION(Server, Unreliable, WithValidation)
void ServerSendVoiceFrame(const FVoiceFrame& Frame);
```

`WithValidation` 에서 거르는 것: 페이로드 크기 상한(128바이트), 채널 값 범위, Loudness 범위.
**넘으면 연결을 끊지 말고 프레임만 버린다** — 음성은 유실이 정상이라 끊을 이유가 없다.

**서버가 다시 쓰는 값 (클라 값을 믿지 않는다)**

기존 문서의 핵심 원칙 3번("신원은 서버가 확정한다")을 그대로 따른다.

| 필드 | 클라가 보냄 | 서버가 하는 일 |
|---|---|---|
| `SpeakerId` | ❌ 안 보냄 | 서버가 `PlayerState` 에서 채운다 |
| `Route` | 보냄 | 무전이면 **무전기가 실제로 켜져 있는지 서버가 확인**. 아니면 근접으로 강등하거나 폐기 |
| `Loudness` | 보냄 | `Clamp(0,1)` + 발화 모드 상한으로 자름 |
| `Seq` | 보냄 | 그대로 통과 (지터버퍼용). 위조해봐야 자기 목소리만 깨진다 |
| 위치 | ❌ 안 보냄 | 서버가 폰 위치를 쓴다 |

**레이트 리밋.** 플레이어당 초당 프레임 수 상한(60). 넘으면 버린다.
없으면 조작된 클라이언트가 초당 수천 프레임을 밀어 호스트를 죽일 수 있다.

**라우팅 결정 (프레임 하나마다)**

```
근접(Proximity):
  수신자 = 발신자 폰 위치에서 EffectiveRadius(발화모드) * 1.2 안에 있는 폰의 소유 PC
           (1.2 배는 여유. 경계에서 뚝 끊기는 것보다 감쇠로 사라지는 게 자연스럽다)
  + 발신자가 사망 상태면 → 사망자에게만 (기존 채팅의 Dead 채널 규칙과 통일)

무전(Radio):
  수신자 = RadioRegistry[발신자 주파수] 중
             · 전원 켜짐
             · 자기 자신 제외
           (거리 무관. 중계기/전파방해를 넣고 싶으면 여기에 조건을 추가한다)
```

**★ 절대 `Multicast` 로 뿌리지 않는다.**
`NetMulticast` 로 전체에 뿌리고 클라이언트가 거리를 보고 버리게 만들면,
**개조 클라이언트가 맵 반대편 대화를 전부 들을 수 있다.** 음성 월핵이다.
서버가 수신자 목록을 만들어 **`Client` RPC 로 콕 집어 보낸다.** 대역폭도 이쪽이 싸다.

```cpp
UFUNCTION(Client, Unreliable)
void ClientReceiveVoiceFrame(const FVoiceFrameOut& Frame);  // SpeakerId, Route, Seq, Loudness, Opus
```

**반이중 (half-duplex) — 무전 채널 점유**

무전 채널 하나에서 동시에 두 명이 말하면 실제 무전기는 뭉개진다. 그걸 그대로 구현한다.

```
서버가 주파수마다 "지금 송신 중인 사람" 을 1명만 허용.
  · 비어 있으면 → 점유, 송신 허용
  · 남이 점유 중이면 → 요청자에게 "채널 사용 중" 삑 소리 한 번, 프레임 폐기
  · 300ms 동안 프레임이 안 오면 점유 해제
```

**왜 이걸 넣는가.** 무전기다운 맛도 있지만 진짜 이유는 **대역폭**이다 (9절).
동시 발화자 수의 상한이 생겨야 호스트 업로드가 예측 가능해진다.
근접은 반이중을 적용하지 않는다 — 실제로 여러 명이 동시에 말할 수 있어야 한다.

### 6-3. 재생 (클라이언트)

**스트림 단위 = (SpeakerId, Route) 쌍.**
같은 사람이 근접과 무전으로 동시에 올 수 있으므로 키를 두 개로 잡아야 한다.

```
ClientReceiveVoiceFrame (게임 스레드)
  → 스트림 조회/생성
  → 지터버퍼에 Seq 순으로 삽입
  → 버퍼가 목표치(3프레임=60ms)를 넘으면 재생 시작
  → 매 틱: 버퍼에서 프레임 꺼내 Opus 디코딩 → float PCM
      · 빠진 Seq 는 디코더 PLC 로 메움 (없으면 무음)
      · 버퍼가 과하게 쌓이면(>8프레임) 가장 오래된 것부터 버림 (지연 누적 방지)
  → TCircularAudioBuffer<float> 에 push        ← 여기가 스레드 경계
                                     ↓
  VoiceSynthComponent::OnGenerateAudio()        ← 오디오 렌더 스레드가 pop
```

**★ `OnGenerateAudio` 는 게임 스레드가 아니다.**
여기서 `UObject` 를 만지거나 `TArray` 를 재할당하거나 락을 잡으면 **오디오가 끊기고 결국 크래시한다.**
반드시 락 프리 SPSC 링버퍼(`Audio::TCircularAudioBuffer<float>`)만 통과시킨다.
**기존 채팅의 "워커 스레드에서 UObject 금지" 규칙과 정확히 같은 규칙이다.** (8절 표 참고)

**라우트별 출력 경로**

| | 근접 | 무전 |
|---|---|---|
| Synth 부착 위치 | **발신자의 폰** | **수신자(나)의 무전기 액터** |
| 공간화 | 3D. Attenuation + Occlusion 켬 | 3D (내 무전기 위치). 이어피스 착용 시 2D |
| 감쇠 반경 | 발화 모드에 따라 (10절) | 무전기 스피커 반경 (짧게, ~10m) |
| 필터 | 없음 | **밴드패스 300~3000Hz + 소프트 클립 + 화이트노이즈 베드 + 시작/끝 스퀄치 삑** |

**무전 필터를 소스 이펙트 체인으로 만든다.**
`USoundEffectSourcePresetChain` 에 EQ → Distortion → (선택) Bitcrusher 를 걸고
`VoiceSynthComponent` 의 `SourceEffectChain` 에 지정한다. 코드로 샘플을 주무르지 않는다 —
디자이너가 에디터에서 톤을 굴릴 수 있어야 한다.

**노이즈 베드와 스퀄치는 별도 `UAudioComponent` 로 낸다.**
송신 시작/종료 시점에 재생. 이게 있고 없고가 "무전기 같다" 의 90%다.

**뮤트.** 클라이언트 로컬 뮤트 목록(플레이어별)은 재생 단계에서 프레임을 버린다.
서버까지 갈 필요 없다 — 괴롭힘 방지용 강제 뮤트가 필요해지면 그때 서버 라우팅에 조건을 추가한다.

### 6-4. NPC 청각 (서버 전용) ★ 이 시스템의 목적

**소음 이벤트는 서버에서만 만든다.** `UAIPerceptionSystem` 이 서버에서 돌기 때문이다.

```cpp
UAISense_Hearing::ReportNoiseEvent(
    World,
    NoiseLocation,   // 입 위치 또는 무전기 스피커 위치
    Loudness,        // 0~1 로 정규화된 음량 (반경 배율로 쓰인다)
    Instigator,      // 이 소리의 책임자. NPC 가 "누구를 찾을지" 판단하는 근거
    MaxRange,        // 발화 모드가 정하는 절대 상한
    Tag              // "Voice.Proximity" / "Voice.Radio" / "Voice.RadioSpeaker"
);
```

**★ 프레임마다 보고하면 안 된다.**
20ms 마다 소음 이벤트를 쏘면 초당 50회 × 인원수의 perception 갱신이 돌아 서버가 죽는다.

```
집계 창(NoiseWindow) = 0.3초
  창 안의 프레임 Loudness 중 최댓값을 취해 창이 끝날 때 한 번 보고
  → 초당 3.3회. 값은 "가장 크게 낸 소리" 기준이라 속삭임에 묻히지 않는다
```

**보고 지점 세 곳**

| 지점 | 위치 | Loudness | Tag | 조건 |
|---|---|---|---|---|
| 근접 발화 | 발신자 머리 | 프레임 최대 RMS × 발화모드 배율 | `Voice.Proximity` | 항상 |
| 무전 송신 | 발신자 머리 | **× 0.35 로 감쇠** | `Voice.Radio` | 항상. 무전이라도 입은 움직인다 |
| **무전 수신** | **수신자의 무전기 액터** | 무전기 볼륨 × 0.8 | `Voice.RadioSpeaker` | **이어피스 미착용 + 전원 켜짐일 때만** |

세 번째가 이 설계의 핵심이다. **팀원의 무전이 내 위치를 노출시킨다.**
"무전 켜고 다닐래, 끄고 정보 없이 다닐래" 라는 선택이 생기고, 이어피스가 진짜 아이템이 된다.

**NPC 쪽에서 할 일**

1. `AIPerceptionComponent` 에 `AISenseConfig_Hearing` 추가 (`HearingRange` 는 NPC 종류마다).
2. 이 프로젝트는 **StateTree** 를 쓰므로, 소음 자극을 받으면
   `LastHeardLocation` 을 블랙보드/StateTree 파라미터에 넣고 `Investigate` 상태로 전이.
3. **Tag 로 반응을 나눈다** — 이게 무료로 얻는 연출이다.

| Tag | NPC 반응 (예시) |
|---|---|
| `Voice.Proximity` | 정확히 그 지점으로 이동. 사람이 거기 있다 |
| `Voice.Radio` | 약하게 반응. 주변 배회 |
| `Voice.RadioSpeaker` | 그 지점으로 이동. **소리의 주인이 수신자이므로 `Instigator` 는 수신자다** |

> `Instigator` 를 잘못 넣으면 NPC 가 엉뚱한 사람을 쫓는다.
> **소음의 책임자 = 소리가 난 위치에 있는 사람** 이다. 무전 수신 소음의 책임자는 송신자가 아니라 **수신자**다.

**GAS 연동 (선택).**
발화 중에 `State.Voice.Speaking` 게임플레이 태그를 부여하면 다른 어빌리티가 조건으로 쓸 수 있다
(예: "은신 어빌리티는 발화 중에 효과 반감"). 태그 부여도 서버에서 한다.

---

## 7. 패킷 / 데이터 구조

```cpp
// VoiceTypes.h

UENUM(BlueprintType)
enum class EVoiceRoute : uint8
{
    Proximity  UMETA(DisplayName = "근접"),
    Radio      UMETA(DisplayName = "무전"),
};

UENUM(BlueprintType)
enum class EVoiceMode : uint8
{
    Whisper  UMETA(DisplayName = "속삭임"),
    Normal   UMETA(DisplayName = "보통"),
    Shout    UMETA(DisplayName = "외침"),
};

/** 클라 → 서버. 20ms 한 조각. */
USTRUCT()
struct FVoiceFrame
{
    GENERATED_BODY()

    UPROPERTY() uint16          Seq = 0;        // 지터버퍼 정렬용. 순환해도 무방
    UPROPERTY() EVoiceRoute     Route = EVoiceRoute::Proximity;
    UPROPERTY() EVoiceMode      Mode  = EVoiceMode::Normal;
    UPROPERTY() uint8           Loudness = 0;   // RMS 를 0~255 로 양자화. float 로 보낼 이유 없다
    UPROPERTY() TArray<uint8>   Opus;           // 40~80바이트. 상한 128
};

/** 서버 → 클라. SpeakerId 가 붙는다. 서버가 채운 값이다. */
USTRUCT()
struct FVoiceFrameOut
{
    GENERATED_BODY()

    UPROPERTY() int32           SpeakerId = 0;  // PlayerState::GetPlayerId()
    UPROPERTY() uint16          Seq = 0;
    UPROPERTY() EVoiceRoute     Route = EVoiceRoute::Proximity;
    UPROPERTY() uint8           Loudness = 0;   // 말하는 UI 표시(입 아이콘)에 쓴다
    UPROPERTY() TArray<uint8>   Opus;
};
```

**`Loudness` 를 uint8 로 보내는 이유.** float 는 4바이트, 페이로드가 40바이트인데 10%다.
음량은 UI 게이지와 소음 반경 배율에만 쓰이므로 256단계면 넘치게 충분하다.

**Opus 상한 128바이트.** 16kHz/24kbps/20ms 면 정상값이 60바이트 근처다. 128 을 넘으면 조작이다.

### `URadioComponent` — 아이템 파트가 채워줄 인터페이스

```cpp
UPROPERTY(ReplicatedUsing=OnRep_Power, BlueprintReadOnly) bool  bPoweredOn = false;
UPROPERTY(Replicated, BlueprintReadOnly)                  int32 Frequency = 1;      // 1~8 채널
UPROPERTY(Replicated, BlueprintReadOnly)                  bool  bEarpiece = false;  // ★ 소음 여부를 가른다
UPROPERTY(Replicated, BlueprintReadOnly)                  float SpeakerVolume = 1.f;

// 서버 전용
void SetPowered(bool bOn);        // VoiceRouter 의 주파수 레지스트리에 등록/해제
void SetFrequency(int32 NewFreq); // 레지스트리 이동
```

**등록/해제는 반드시 `URadioComponent` 에서 `UVoiceRouter` 로 통지한다.**
라우터가 매 프레임 월드의 모든 액터를 훑으면 안 된다 — 무전 프레임마다 O(N) 이 된다.
레지스트리(`TMap<int32 Freq, TArray<TWeakObjectPtr<URadioComponent>>>`)를 유지하면 O(수신자)다.

---

## 8. 스레드 경계

기존 문서 10절의 표와 같은 형식으로 정리한다. **규칙 위반 시 증상까지 적어둔다.**

| 하는 곳 | 해도 되는 것 | 하면 안 되는 것 | 어기면 |
|---|---|---|---|
| `FVoiceCaptureRunnable` (워커) | `IVoiceCapture`, 바이트 배열, Opus 인코딩, `TQueue` 넣기 | UObject, 델리게이트, 액터 접근 | 랜덤 크래시 |
| `UVoiceSubsystem::Tick` (게임) | 큐 비우기, RPC 호출, UI 갱신 | — | |
| `UVoiceRouter` (서버 게임 스레드) | 라우팅, 소음 이벤트, 레지스트리 | 블로킹 I/O | 호스트 프레임 드랍 |
| `UVoicePlaybackComponent` (게임) | 디코딩, 지터버퍼, 링버퍼에 **push** | 링버퍼에서 pop | 오디오 글리치 |
| `UVoiceSynthComponent::OnGenerateAudio` (**오디오 렌더**) | 링버퍼에서 **pop**, float 산술 | **UObject / 락 / 메모리 할당 / UE_LOG** | 오디오 끊김 → 크래시 |

**큐는 전부 SPSC 다.** 채팅과 같다. 각 큐를 반대편에서 쓰면 안 된다.

**종료 순서** (기존 `ShutdownClient` 와 같은 이유로 순서가 중요하다):

1. `Capture->Stop()` — 마이크 정지
2. `Runnable->Stop()` → `Thread->Kill(/*bShouldWait=*/**true**)` → `delete Runnable`
3. `SynthComponent->Stop()` 후에 링버퍼 해제 — **순서를 뒤집으면 오디오 렌더 스레드가 해제된 버퍼를 읽는다**

`bShouldWait=false` 로 두면 **PIE 를 껐다 켤 때 에디터가 죽는다.** 채팅에서 이미 겪은 문제다.

---

## 9. 대역폭 계산

가정: 16kHz mono, Opus 24kbps, 20ms 프레임 → 페이로드 60바이트 + UE 오버헤드 ~40바이트 = **100바이트/프레임**, 초당 50프레임 = **5 KB/s (스트림 1개)**

**8인 플레이 최악의 경우 (반이중 없음, 전원 동시 발화)**

```
8명 × 각자 7명이 들음 = 56 스트림 → 호스트 업로드 280 KB/s = 2.2 Mbps
```

가정용 업로드에서 위험하고, 게임 리플리케이션까지 같은 회선을 쓴다.

**반이중 + 근접 거리 컷 적용 시 (현실값)**

```
무전 동시 발화 = 주파수당 1명 (반이중 강제)
근접 동시 발화 = 실측 2~3명, 각자 평균 3명이 들음

무전 1 × 7 + 근접 3 × 3 = 16 스트림 → 호스트 업로드 80 KB/s = 640 kbps
클라이언트 수신 = 동시에 들리는 스트림 3~4개 → 20 KB/s
```

**LAN 시연(이 프로젝트의 확정 환경)에서는 여유롭다.**
인터넷으로 확장한다면 40ms 프레임(패킷 절반) + Opus 16kbps 로 내리면 절반이 된다.

**측정 방법.** `Stat Net`, `Net PktLag`, 그리고 `VoiceRouter` 에 자체 카운터
(`MOU.Voice.Stat` 콘솔 명령: 초당 프레임 수 / 스트림 수 / 드랍 수)를 둔다.
**추측하지 말고 숫자를 본다** — 기존 문서가 프레이밍 회귀 테스트를 둔 것과 같은 이유다.

---

## 10. 튜닝 파라미터

전부 `UDeveloperSettings`(`UVoiceSettings`)로 빼서 `DefaultGame.ini` 에서 만진다.
코드 재컴파일 없이 밸런싱하기 위해서다.

**오디오**

| 이름 | 기본값 | 설명 |
|---|---|---|
| `SampleRate` | 16000 | 바꾸면 인코더/디코더 양쪽 |
| `FrameMs` | 20 | 40 으로 올리면 대역폭 절반, 지연 +20ms |
| `TargetJitterFrames` | 3 (60ms) | 낮추면 반응 빠름 / 끊김 증가 |
| `MaxJitterFrames` | 8 | 넘으면 오래된 것부터 버림 |
| `VadThreshold` | 0.02 | RMS 임계. 환경마다 조정 필요 |
| `VadHangoverMs` | 200 | 말끝이 잘리지 않게 |

**발화 모드 — 밸런스의 핵심**

| 모드 | 근접 감쇠 반경 | 소음 `MaxRange` | Loudness 배율 |
|---|---|---|---|
| 속삭임 | 500 (5m) | 700 | 0.35 |
| 보통 | 1500 (15m) | 1800 | 1.0 |
| 외침 | 3000 (30m) | 4000 | 1.6 |

> **소음 반경이 들리는 반경보다 약간 넓다.** 의도된 것이다.
> "사람에겐 안 들리는데 NPC 는 들었다" 가 있어야 긴장이 생긴다. 같으면 학습이 너무 쉽다.

**무전**

| 이름 | 기본값 | 설명 |
|---|---|---|
| `RadioChannelHoldMs` | 300 | 반이중 점유 해제 시간 |
| `RadioSpeakerRadius` | 1000 (10m) | 무전기 스피커가 들리는 거리 |
| `RadioSpeakerNoiseScale` | 0.8 | **수신자 위치 소음 배율. ★ 밸런스 핵심** |
| `RadioMouthNoiseScale` | 0.35 | 무전 송신 시 송신자 입 소음 |
| `NumFrequencies` | 8 | 무전 채널 수 |

**AI**

| 이름 | 기본값 | 설명 |
|---|---|---|
| `NoiseWindowSec` | 0.3 | 소음 보고 집계 창. 낮추면 서버 부하 증가 |
| `MaxFramesPerSecPerPlayer` | 60 | 레이트 리밋 |

---

## 11. 구현 순서 (마일스톤)

**각 단계는 다음 단계 없이도 눈으로 확인 가능해야 한다.** 기존 문서의 "동작 확인" 방식을 따른다.

| 단계 | 내용 | 검증 방법 |
|---|---|---|
| **V1** | `Voice` 모듈 활성화, 캡처 → **로컬 루프백 재생** (네트워크 없음) | 내 목소리가 내 헤드폰에 0.1초 뒤 들린다 |
| **V2** | Opus 인코딩/디코딩 삽입 (여전히 로컬) | 음질이 전화 수준. 지연 증가 미미 |
| **V3** | `VoiceComponent` RPC + 서버 라우팅(근접만) + `SynthComponent` 3D 부착 | PIE 2창. 가까이 가면 들리고 멀어지면 사라진다 |
| **V4** | 지터버퍼 + PLC + 통계 콘솔 명령 | `Net PktLoss=10` 넣어도 알아듣는다 |
| **V5** | `RadioComponent` + 주파수 레지스트리 + 무전 라우트 + 반이중 | 맵 반대편에서 무전이 오간다. 동시 송신 시 "사용 중" |
| **V6** | 무전 필터(EQ/노이즈/스퀄치) + 무전기 액터 3D 출력 | 무전기 같은 소리가 무전기 위치에서 난다 |
| **V7** | **소음 이벤트 3종** + NPC StateTree 반응 | 말하면 NPC 가 온다. 속삭이면 안 온다 |
| **V8** | **이어피스** + 무전 수신 소음 | 이어피스 빼고 무전 받으면 NPC 가 온다 ← **목표 지점** |
| V9 | 발화 모드 UI, 말하는 사람 표시, 뮤트, 마이크 장치 선택 | 손맛 다듬기 |

**V3 과 V7 이 각각 위험 구간이다.** V3 은 스레드/오디오 파이프라인이 처음 붙는 곳,
V7 은 서버 부하가 처음 문제 되는 곳이다. 이 둘에 일정을 넉넉히 둔다.

**콘솔 명령 (기존 `MOU.Chat.*` 규칙과 통일)**

```
MOU.Voice.Loopback 1          로컬 루프백 켜기/끄기 (V1 검증용)
MOU.Voice.Stat                초당 프레임/스트림/드랍 수
MOU.Voice.Mode <0|1|2>        발화 모드 강제 (속삭임/보통/외침)
MOU.Voice.Radio.Freq <1-8>    주파수 변경
MOU.Voice.Radio.Power <0|1>   무전기 전원
MOU.Voice.Earpiece <0|1>      이어피스 착용
MOU.Voice.FakeNoise <반경>    마이크 없이 소음만 발생 ← ★ AI 팀이 마이크 없이 작업할 수 있게
```

> **`MOU.Voice.FakeNoise` 를 V1 에 먼저 만든다.** 이게 있으면 AI 담당자가
> 음성 파이프라인 완성을 기다리지 않고 NPC 반응을 병렬로 작업할 수 있다.
> 마이크 없는 팀원도 게임 테스트에 참여할 수 있다.

---

## 12. 알려진 함정

**오디오 렌더 스레드**

- `OnGenerateAudio` 안에서 `UE_LOG` 하나만 넣어도 오디오가 튄다. 디버깅은 카운터를 올리고 게임 스레드에서 읽는다.
- `SynthComponent` 를 매 발화마다 `NewObject` 하면 GC 압력이 생긴다. **스트림 풀을 만들어 재사용한다.**

**네트워크**

- Unreliable RPC 는 **한 패킷에 들어가야 한다.** 페이로드가 커지면 조용히 버려진다. 128바이트 상한은 이 때문이기도 하다.
- 폰이 relevant 하지 않으면 그 폰의 RPC 는 안 간다. **그래서 `PlayerController` 에 컴포넌트를 둔다.**(5절)
- 리슨서버의 **호스트 본인은 RPC 를 타지 않는다.** 서버=클라 동일 프로세스이므로 라우팅 결과를
  직접 재생 경로에 넣어야 한다. 여기를 빼먹으면 "호스트만 아무 소리도 안 들린다" 가 나온다. **자주 나는 버그다.**

**PIE / 장치**

- PIE 다중 창에서 마이크 장치 경합 → 첫 창만 캡처(5절).
- 마이크가 없거나 권한이 없으면 `CreateVoiceCapture` 가 null 을 준다. **전체 시스템이 죽지 않고 재생만 동작해야 한다.**
- Windows 마이크 권한을 끈 팀원이 반드시 한 명은 있다. 실패 시 UI 로 알린다.

**게임플레이**

- **사망자 처리를 정하지 않으면 유령이 말한다.** 기존 채팅의 Dead 채널 규칙과 통일할 것.
- 소음 이벤트를 프레임마다 쏘면 서버가 죽는다(6-4절). 집계 창 필수.
- `Instigator` 를 송신자로 잘못 넣으면 NPC 가 엉뚱한 사람을 쫓는다(6-4절).

**프라이버시 (팀 프로젝트라도 명시한다)**

- **음성을 디스크에 저장하지 않는다.** 녹음 기능을 만들지 않는다.
- 마이크가 열려 있는 동안 **화면에 항상 표시**한다 (PTT 든 오픈 마이크든).
- 채팅 비밀번호 규칙과 같은 급으로 다룬다 — 음성 데이터를 `UE_LOG` 에 덤프하지 않는다.

---

## 13. 착수 전에 확인할 것

아래는 **엔진 헤더에서 직접 확인하고 시작할 것.** 버전 간 시그니처가 바뀐 이력이 있는 영역이다.

| 확인할 것 | 어디서 |
|---|---|
| `FVoiceModule::Get().CreateVoiceCapture()` 인자 | `Engine/Source/Runtime/Online/Voice/Public/VoiceModule.h` |
| `IVoiceCapture::GetVoiceData()` 시그니처 | `.../Voice/Public/Interfaces/VoiceCapture.h` |
| `IVoiceEncoder`/`IVoiceDecoder` 의 프레임 크기 제약 | `.../Voice/Public/Interfaces/VoiceCodec.h` |
| Opus 가 허용하는 프레임 길이 (2.5/5/10/20/40/60ms 중 하나여야 함) | 위와 동일 |
| `UAISense_Hearing::ReportNoiseEvent()` 인자 순서 | `Engine/Source/Runtime/AIModule/Classes/Perception/AISense_Hearing.h` |
| `Audio::TCircularAudioBuffer<float>` 존재/이름 | `Engine/Source/Runtime/SignalProcessing/Public/DSP/BufferVectorOperations.h` 부근 |
| `USynthComponent::OnGenerateAudio` 시그니처 | `Engine/Source/Runtime/AudioMixer/Public/Components/SynthComponent.h` |

**먼저 확인해야 하는 이유:** V1(로컬 루프백)이 이 API 들 위에 통째로 올라간다.
여기서 어긋나면 뒤의 설계가 전부 흔들린다. **V1 을 하루 안에 붙여보고 나머지 일정을 잡는 것을 권한다.**
