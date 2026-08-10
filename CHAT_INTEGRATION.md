# MOU 채팅 시스템 — 통합 문서

담당: 서버/클라이언트 파트
최종 갱신: 2026-08-10

이 문서 하나만 보면 **새 PC 에서 처음부터 셋업하고, 빌드하고, 검증하고, 이어서 개발**할 수 있다.
채팅 관련해서 알아야 할 내용은 전부 여기에 있다.

---

## 목차

1. [지금까지 뭘 만들었나](#1-지금까지-뭘-만들었나)
2. [새 PC 셋업 (처음 한 번)](#2-새-pc-셋업-처음-한-번)
3. [빌드하기](#3-빌드하기)
4. [동작 확인](#4-동작-확인)
5. [구조](#5-구조)
6. [파일별 역할](#6-파일별-역할)
7. [API 레퍼런스](#7-api-레퍼런스)
8. [프로토콜 레퍼런스](#8-프로토콜-레퍼런스)
9. [팀 공용 파일 변경 내역](#9-팀-공용-파일-변경-내역)
10. [이 시스템의 코딩 규칙](#10-이-시스템의-코딩-규칙)
11. [알려진 한계](#11-알려진-한계)
12. [다음 단계](#12-다음-단계)
13. [문제 해결](#13-문제-해결)

---

## 1. 지금까지 뭘 만들었나

게임 본체는 **리슨서버** 방식이지만, 채팅만은 **별도 프로세스(데디케이트)** 로 분리했다.
호스트가 게임을 나가도 채팅 서버는 살아있으므로 대화가 끊기지 않고, 나중에 로그도 남길 수 있다.

| 단계 | 내용 | 상태 |
|---|---|---|
| 1 | 프로토콜 정의 (`ChatProtocol.h`) | 완료 |
| 2 | 세션 구조체 (`Session.h/.cpp`) | 완료 |
| 3 | 길이 프리픽스 프레이밍 (`Framing.h/.cpp`) | 완료 (split/merge/bad 3종 통과) |
| 4 | 로그인 — 서버가 신원 확정 | 기본 형태만 (DB·인증 없음) |
| 5 | 채널 라우팅 (All / Team / Dead) | 완료 |
| 6 | SQLite 적재 + 히스토리 조회 | **미착수** |
| 7 | 언리얼 클라이언트 (소켓/스레드/서브시스템) | 완료 |
| 7-UI | 언리얼 채팅 UI 위젯 | 완료 (WBP 불필요) |
| 8 | 리슨서버 → 채팅서버 신원 미러링 | **미착수** |
| 9 | 귓속말 | **미착수** |

> 단계 번호는 원래 `MOU_ChatServer/README.md` 의 로드맵을 따른다.

---

## 2. 새 PC 셋업 (처음 한 번)

### 2-1. 필요한 것

| 항목 | 버전 | 비고 |
|---|---|---|
| Unreal Engine | **5.8** | `.uproject` 의 `EngineAssociation` 이 5.8 |
| Visual Studio | 2022 또는 2026 | **둘 다 설치돼 있으면 아래 2-3 필수** |
| VS 워크로드 | C++ 게임 개발 / C++ 데스크톱 개발 | MSVC 툴셋 + Windows SDK |
| Git | 아무거나 | |

### 2-2. 저장소 받기

```bash
git clone <저장소 URL> Unreal-MOU
```

> **`TeamProject_MOU` 폴더만 따로 복사하면 빌드가 깨진다.**
> `Build.cs` 가 저장소 루트의 `MOU_ChatServer/Shared` 를 참조하기 때문이다. 반드시 전체를 받을 것.

받은 뒤 저장소 구조:

```
Unreal-MOU/
├─ CHAT_INTEGRATION.md          이 문서
├─ MOU_ChatServer/              채팅 서버 (엔진 없는 순수 C++)
│   ├─ CMakeLists.txt
│   ├─ README.md                서버 자체 설계 문서
│   ├─ Shared/                  서버·클라이언트 공용
│   │   ├─ ChatProtocol.h       ★ 언리얼도 이 파일을 그대로 include 한다
│   │   ├─ Net.h                소켓 API 래퍼 (언리얼에서 include 금지)
│   │   └─ Framing.h/.cpp       프레이밍 (언리얼에서 include 금지)
│   ├─ Server/
│   │   ├─ ChatServer.cpp       accept 루프 / 패킷 핸들러 / 채널 라우팅
│   │   └─ Session.h/.cpp       ClientSession + SessionManager
│   └─ TestClient/
│       └─ TestClient.cpp       검증용 콘솔 클라이언트
└─ TeamProject_MOU/             언리얼 프로젝트
    └─ Source/TeamProject_MOU/
        ├─ TeamProject_MOU.Build.cs
        ├─ Public/Chat/         채팅 헤더 4개
        └─ Private/Chat/        채팅 구현 4개
```

### 2-3. Visual Studio 버전 고정 ★ 중요

VS 2022 와 2026 이 **둘 다 설치돼 있으면**, UBT 가 자동으로 최신(2026)용 프로젝트 파일을 만든다.
그 상태에서 VS 2022 로 열면 솔루션 탐색기에 이렇게 뜬다:

> **TeamProject_MOU(호환되지 않음)** — 이 프로젝트는 현재 버전의 Visual Studio와 호환되지 않습니다.

VS 2022 는 2026 의 툴셋(`v145`, `ToolsVersion 18.0`)을 모르기 때문이다.

**해결**: `%APPDATA%\Unreal Engine\UnrealBuildTool\BuildConfiguration.xml` 을 아래처럼 만든다.
이 파일은 **사용자별 설정**이라 git 에 들어가지 않는다. PC 마다 각자 해야 한다.

```xml
<?xml version="1.0" encoding="utf-8" ?>
<Configuration xmlns="https://www.unrealengine.com/BuildConfiguration">
  <ProjectFileGenerator>
    <Format>VisualStudio2022</Format>
  </ProjectFileGenerator>
  <BuildConfiguration>
    <bAllowUBAExecutor>false</bAllowUBAExecutor>
  </BuildConfiguration>
</Configuration>
```

- `Format` — 쓰는 VS 에 맞춰 `VisualStudio2022` 또는 `VisualStudio2026`.
  이걸 넣어야 우클릭 → *Generate Visual Studio project files* 를 눌러도 계속 그 버전으로 나온다.
- `bAllowUBAExecutor` — 빌드 가속기(UBA) 끄기. **13번 문제 해결** 참고. 메모리가 넉넉하면 빼도 된다.

### 2-4. 프로젝트 파일 생성

`TeamProject_MOU.uproject` 우클릭 → **Generate Visual Studio project files**

또는 커맨드라인 (엔진 경로는 각자 다름):

```bash
"F:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" -projectfiles -project="C:\...\Unreal-MOU\TeamProject_MOU\TeamProject_MOU.uproject" -game -rocket -progress
```

> 새 `.cpp`/`.h` 를 추가하거나 `git pull` 로 받은 뒤에는 **반드시 다시 실행**해야 VS 에 파일이 보인다.

---

## 3. 빌드하기

### 3-1. 채팅 서버 (`ChatServer.exe`)

엔진과 무관한 순수 C++ 콘솔 프로그램이다. 두 가지 방법 중 아무거나.

**방법 A — CMake** (VS 에 번들된 것 사용 가능)

```bash
cmake -S MOU_ChatServer -B MOU_ChatServer/build
cmake --build MOU_ChatServer/build --config Debug
```

**방법 B — cl 직접 호출** (개발자 명령 프롬프트 / `vcvars64.bat` 실행 후)

```bash
cl /nologo /std:c++17 /EHsc /utf-8 /Fe:build\ChatServer.exe /Fo:build\obj\ /IShared /IServer Server\ChatServer.cpp Server\Session.cpp Shared\Framing.cpp
```

```bash
cl /nologo /std:c++17 /EHsc /utf-8 /Fe:build\TestClient.exe /Fo:build\tcobj\ /IShared TestClient\TestClient.cpp Shared\Framing.cpp
```

주의점 두 가지:

- **`/utf-8` 은 필수다.** 소스에 한글 문자열이 있다.
- `/Fo:` 에 지정한 폴더가 **미리 존재해야 한다.** 없으면 `C1083` 이 난다.
  그리고 두 실행 파일이 obj 폴더를 공유하면 안 된다(`Framing.obj` 충돌).

### 3-2. 언리얼 프로젝트

**VS 에서**: 구성을 `Development Editor` / `Win64` 로 두고 빌드 (Ctrl+Shift+B).
시작 프로젝트가 `UE5`(엔진)로 잡혀 있으면 `TeamProject_MOU` 로 바꾼다.

**커맨드라인에서**:

```bash
"F:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" TeamProject_MOUEditor Win64 Development -Project="C:\...\Unreal-MOU\TeamProject_MOU\TeamProject_MOU.uproject" -WaitMutex
```

`Variant_Platforming does not exist` 류의 경고가 잔뜩 뜨는데, 이건 원래 템플릿에 있던
`Build.cs` 의 남은 경로라서 **무시해도 된다.** 채팅과 무관하다.

---

## 4. 동작 확인

### 4-1. 기본 흐름

**1) 채팅 서버를 먼저 켠다** (별도 콘솔 창)

```bash
ChatServer.exe 9000
```

**2) 에디터에서 PIE 실행 → `` ` `` 키로 콘솔을 열고**

```bash
MOU.Chat.Connect 127.0.0.1 9000
```
```bash
MOU.Chat.Login 홍길동 0
```
```bash
MOU.Chat.ShowUI
```

**3) 화면 왼쪽 아래에 채팅창이 뜬다.** Enter 로 입력창을 열고, 치고, Enter 로 전송.

**4) 확인 지점**

| 어디 | 나와야 하는 것 |
|---|---|
| 서버 콘솔 | `[로그인] 홍길동 -> UserId=1, Team=0` |
| 서버 콘솔 | `[전체] 홍길동: 안녕하세요  (수신 1명)` |
| 채팅창 | `[전체] 홍길동: 안녕하세요` |
| 에디터 출력 로그 | 필터 `LogMOUChat` 로 같은 내용 |

**서버가 꺼져 있어도 게임은 정상 진행된다.** 3초 간격으로 자동 재접속하고,
재접속에 성공하면 **자동으로 다시 로그인**한다. 채팅 때문에 다른 파트 테스트가 막히지 않는다.

### 4-2. 다중 클라이언트 테스트

PIE 플레이어 수를 2~3 으로 올린다. 창마다 GameInstance 가 따로 생기므로
채팅 연결도 창마다 독립적으로 만들어진다. 각 창에서 다른 이름으로 로그인하면 된다.

`TestClient.exe` 를 섞어도 된다:

```bash
TestClient.exe 127.0.0.1 9000 테스터
```

### 4-3. 채팅창 조작

| 조작 | 동작 |
|---|---|
| `Enter` | 입력창 열기 / 전송 |
| `Esc` | 입력 취소 |
| `/a`, `/all` | 전체 채널로 전환 |
| `/t`, `/team` | 팀 채널로 전환 |
| `/d`, `/dead` | 사망 채널로 전환 |
| `/t 안녕` | 팀 채널로 바꾸고 바로 전송 |

### 4-4. 콘솔 명령 목록

| 명령 | 설명 |
|---|---|
| `MOU.Chat.Connect [호스트] [포트]` | 접속 (기본 `127.0.0.1 9000`) |
| `MOU.Chat.Login <이름> [팀ID]` | 로그인 |
| `MOU.Chat.ShowUI` / `MOU.Chat.HideUI` | 채팅창 표시 / 제거 |
| `MOU.Chat.ToggleInput` | 입력창 열기·닫기 (Enter 가 안 먹을 때) |
| `MOU.Chat.Say <채널> <메시지>` | UI 없이 전송. 채널 `0`=전체 `1`=팀 `2`=사망 |
| `MOU.Chat.Dead <0\|1>` | 생사 상태 변경 (테스트 전용) |
| `MOU.Chat.Disconnect` | 연결 종료 |

### 4-5. 서버 프레이밍 회귀 테스트

TCP 프레이밍이 깨지지 않았는지 확인하는 3종 세트. 서버 코드를 건드렸으면 반드시 돌린다.

```bash
TestClient.exe 127.0.0.1 9000 splitter split
```
```bash
TestClient.exe 127.0.0.1 9000 merger merge
```
```bash
TestClient.exe 127.0.0.1 9000 attacker bad
```

| 테스트 | 보내는 것 | 통과 조건 |
|---|---|---|
| `split` | 패킷 1개를 1바이트씩 38회 | 서버에 메시지가 **정확히 1번** |
| `merge` | 패킷 3개를 1번의 send 로 | 서버에 메시지가 **3줄** |
| `bad` | `BodySize=999999` 위조 헤더 | `[차단]` 후 **해당 연결만** 끊김. 서버는 생존 |

### 4-6. 헤드리스 자동 검증 (선택)

에디터 GUI 없이 CI 처럼 돌리는 방법. 실제로 이 방식으로 검증했다.

```bash
UnrealEditor-Cmd.exe "<경로>\TeamProject_MOU.uproject" -game -nullrhi -unattended -nosplash -nosound -ExecCmds="MOU.Chat.Connect 127.0.0.1 9000,MOU.Chat.Login UIClient 0,MOU.Chat.ShowUI" -LogCmds="LogMOUChat Verbose" -log -abslog="<경로>\ue_test.log"
```

로그에서 `LogMOUChat` 을 필터링해 확인한다. 한계: `-nullrhi` 라 **화면에 어떻게 보이는지는 확인 못 한다.**
레이아웃과 색상은 에디터에서 눈으로 봐야 한다.

또 `-ExecCmds` 는 모든 명령을 **한 프레임에** 실행하므로, 로그인 왕복을 기다려야 하는
`MOU.Chat.Say` 는 이 방식으로 테스트할 수 없다. 그건 PIE 콘솔에서 직접 확인한다.

---

## 5. 구조

```
   [ChatServer.exe :9000]   ← 엔진 없는 별도 프로세스. 게임과 무관하게 상시 가동
            ▲
            │ TCP (게임 리플리케이션과 완전히 별개)
            ▼
  ┌────────────────────────────────────────────────┐
  │ FChatClientRunnable   (워커 스레드)              │  소켓/바이트만 다룸
  │        ↕ TQueue (SPSC) ×3                       │  UObject 접근 절대 금지
  │ UChatSubsystem        (게임 스레드)              │  패킷 조립 / 델리게이트
  │        ↓ OnChatMessageReceived                  │
  │ UChatWidgetBase       (UMG)                     │  로그 표시 / 입력창
  └────────────────────────────────────────────────┘

  [리슨서버(게임)] ── UE 기본 리플리케이션 ── [게임 클라이언트]
     이동/전투/GAS. 채팅은 여기를 타지 않는다.
```

### 핵심 원칙 3가지

**1. 채팅은 UE 리플리케이션을 쓰지 않는다.**
Server RPC / Multicast 를 타지 않고 `ChatServer.exe` 로 가는 별도 TCP 소켓으로 간다.
그래서 호스트가 게임을 나가도 채팅이 유지된다.

**2. 워커 스레드에서 UObject 를 절대 건드리지 않는다.**
언리얼의 UObject 는 스레드 세이프하지 않다. 수신 데이터는 순수 데이터 구조체로 바꿔
`TQueue` 에 넣기만 하고, 게임 스레드의 `UChatSubsystem::Tick` 이 꺼내서 그때 델리게이트를 쏜다.
이 규칙을 어기면 재현이 어려운 랜덤 크래시가 난다.

**3. 신원은 서버가 확정한다.**
`ChatBroadcast` 의 `SenderUserId` / `SenderName` 은 항상 서버가 세션 정보로 채운다.
클라이언트가 보낸 값을 그대로 옮기지 않는다. 이게 없으면 사망자 채널이 성립하지 않는다.

### 스레드 경계 상세

```
     게임 스레드                            워커 스레드
   ┌─────────────────┐                 ┌──────────────────────────┐
   │ UChatSubsystem  │                 │  FChatClientRunnable     │
   │                 │                 │                          │
   │ SendChat()      │──OutboundQ────▶ │  PumpSend()  → Socket    │
   │ Login()         │                 │                          │
   │                 │                 │                          │
   │ Tick()          │◀──InboundMsgQ── │  PumpRecv()  ← Socket    │
   │  → 델리게이트    │◀──InboundEvtQ── │  (파싱 후 구조체로)        │
   └─────────────────┘                 └──────────────────────────┘
```

큐는 전부 `EQueueMode::Spsc`(단일 생산자·단일 소비자)다. 각 큐를 반대편에서 쓰면 안 된다.

---

## 6. 파일별 역할

### 서버 (`MOU_ChatServer/`)

| 파일 | 역할 |
|---|---|
| `Shared/ChatProtocol.h` | **패킷 정의. 언리얼과 공유하는 유일한 파일** |
| `Shared/Net.h` | 플랫폼별 소켓 API 래퍼. **언리얼에서 include 금지** |
| `Shared/Framing.h/.cpp` | 길이 프리픽스 프레이밍. **언리얼에서 include 금지** |
| `Server/Session.h/.cpp` | `ClientSession` + `SessionManager` |
| `Server/ChatServer.cpp` | accept 루프, 패킷 핸들러, `RouteChat()` 채널 라우팅 |
| `TestClient/TestClient.cpp` | 검증용 콘솔 클라이언트 |

### 언리얼 (`TeamProject_MOU/Source/TeamProject_MOU/`)

| 파일 | 역할 |
|---|---|
| `Public/Chat/ChatTypes.h` | BP 노출 타입: `FChatMessage`, `FChatLoginResult`, `EChatChannelBP`, `EChatConnectionState`, `LogMOUChat` |
| `Public/Chat/ChatFraming.h`<br>`Private/Chat/ChatFraming.cpp` | 프레이밍의 `TArray` 버전 + UTF-8 변환. 서버 `Framing.cpp` 와 로직 동일 |
| `Public/Chat/ChatClientRunnable.h`<br>`Private/Chat/ChatClientRunnable.cpp` | `FRunnable` 워커. 접속·재접속·송수신·패킷 파싱 |
| `Public/Chat/ChatSubsystem.h`<br>`Private/Chat/ChatSubsystem.cpp` | `UGameInstanceSubsystem`. **진입점. UI/게임플레이는 이것만 쓴다** |
| `Public/Chat/ChatWidgetBase.h`<br>`Private/Chat/ChatWidgetBase.cpp` | `UUserWidget`. 채팅 로그 + 입력창 |

### 왜 프레이밍을 두 번 구현했나

`Framing.h` 를 언리얼에서 그대로 include 하면 같은 폴더의 `Net.h` 가 딸려 들어오고,
`Net.h` 는 `winsock2.h` / `windows.h` 를 포함해서 언리얼 매크로(`TEXT`, `GetObject` 등)와 충돌한다.
그래서 **"선언은 공유(`ChatProtocol.h`), 구현은 각자"** 로 나눴다.

> **서버 `Framing.cpp` 를 고치면 언리얼 `ChatFraming.cpp` 도 같이 고쳐야 한다.**

### 왜 `UGameInstanceSubsystem` 인가

- 레벨을 이동해도(리슨서버 트래블 포함) 파괴되지 않아 연결이 유지된다.
  `PlayerController` 에 붙이면 트래블마다 재접속해야 한다.
- PIE 에서 클라이언트 창을 N개 띄우면 GameInstance 도 N개 생긴다.
  즉 채팅 연결도 자동으로 N개가 되어, 별도 작업 없이 다중 클라이언트 테스트가 된다.
- `GameMode` 에 붙이면 서버에만 존재해서 클라이언트가 못 쓴다.

---

## 7. API 레퍼런스

### `UChatSubsystem` — 진입점

블루프린트에서는 `Get Chat Subsystem` 노드로 얻는다 (`UChatSubsystem::Get(WorldContextObject)`).

| 함수 | 설명 |
|---|---|
| `ConnectToChatServer(Host, Port)` | 접속 시작. 즉시 반환, 실제 접속은 워커 스레드. 실패해도 자동 재시도 |
| `Login(DisplayName, TeamId)` | 로그인. **접속 전에 불러도 된다** — 보관했다가 연결되는 순간 자동 전송 |
| `SendChat(Channel, Text)` | 채팅 전송. 로그인 전이면 막고 경고 로그를 남긴다 |
| `SetDeadForTest(bDead)` | 생사 상태 변경. **테스트 전용, 8단계에서 제거 예정** |
| `Disconnect()` | 연결 종료 + 워커 스레드 정리 |
| `GetConnectionState()` | `EChatConnectionState` |
| `GetLoginResult()` | 서버가 확정한 내 신원 (`FChatLoginResult`) |

| 델리게이트 | 시그니처 |
|---|---|
| `OnChatMessageReceived` | `(const FChatMessage& Message)` |
| `OnChatStateChanged` | `(EChatConnectionState NewState, const FString& Detail)` |
| `OnChatLoginCompleted` | `(const FChatLoginResult& Result)` |

**`Connected` 와 `LoggedIn` 은 다르다.**
`Connected` = TCP 는 붙었지만 아직 `UserId` 가 없다. 이 상태로 채팅을 보내면 서버가 조용히 버린다.
`LoggedIn` = `LoginAck` 를 받아 신원이 확정됐다. 이때부터 채팅 가능.

### `UChatWidgetBase` — 채팅 UI

| 함수 | 설명 |
|---|---|
| `OpenChatInput()` / `CloseChatInput()` / `ToggleChatInput()` | 입력창 제어 |
| `SetActiveChannel(Channel)` / `CycleChannel()` | 채널 전환 |
| `AddSystemLine(Text)` | 클라이언트 로컬 안내 문구 추가 (서버로 안 감) |
| `GetActiveChannel()` / `IsChatInputOpen()` | 조회 |

| 프로퍼티 | 기본값 | 설명 |
|---|---|---|
| `MaxLogLines` | 200 | 넘으면 오래된 줄부터 제거 |
| `bBindToggleKeyToOwningPlayer` | true | PlayerController 의 InputComponent 에 토글 키 직접 바인딩 |
| `ChatToggleKey` | Enter | 위 옵션이 켜져 있을 때 쓸 키 |
| `bManageMouseCursor` | true | 입력창 열고 닫을 때 마우스 커서를 이 위젯이 제어할지 |

### 게임에 정식으로 붙이기

콘솔 명령은 검증용이다. 실제 흐름은 PlayerController 나 HUD 에서:

```cpp
// BeginPlay 등에서
if (UChatSubsystem* Chat = UChatSubsystem::Get(this))
{
    Chat->ConnectToChatServer(TEXT("127.0.0.1"), 9000);
    Chat->Login(PlayerDisplayName, MyTeamId);
}

UChatWidgetBase* ChatWidget = CreateWidget<UChatWidgetBase>(this, UChatWidgetBase::StaticClass());
ChatWidget->AddToViewport();
```

블루프린트도 동일하다. `Get Chat Subsystem` → `Connect To Chat Server` → `Login`.

EnhancedInput 으로 채팅 키를 따로 만들 거라면 `bBindToggleKeyToOwningPlayer` 를 끄고,
그 액션에서 `ToggleChatInput()` 을 호출하면 된다.

### UI 디자이너가 채팅창을 꾸미고 싶다면

지금 위젯은 **WBP 없이 C++ 로 레이아웃을 조립**한다 (좌하단 620×320 반투명 패널).
디자이너가 꾸미려면 `UChatWidgetBase` 를 부모로 하는 `WBP_Chat` 을 만들고
아래 **이름 그대로** 위젯을 배치하면 C++ 이 자동으로 집어서 쓴다.

| 위젯 이름 | 타입 | 필수 |
|---|---|---|
| `ChatLogBox` | Vertical Box | **필수** (여기에 줄이 쌓인다) |
| `ChatInputBox` | Editable Text Box | **필수** |
| `ChatScrollBox` | Scroll Box | 권장 (`ChatLogBox` 를 감싸야 함) |
| `StatusText` | Text Block | 선택 (연결 상태) |
| `ChannelText` | Text Block | 선택 (현재 채널) |
| `ChatRootBorder` | Border | 선택 (배경) |

WBP 를 만들면 C++ 기본 레이아웃은 자동으로 사용되지 않는다
(`WidgetTree->RootWidget` 이 비어있을 때만 조립하기 때문).

---

## 8. 프로토콜 레퍼런스

원본: `MOU_ChatServer/Shared/ChatProtocol.h`. 이 헤더만 언리얼과 공유한다.

### 패킷 헤더 (6바이트 고정, `#pragma pack(1)`)

```
┌──────────────────┬──────────┬─────────────────────┐
│ BodySize uint32  │ Opcode   │ Body (BodySize 바이트) │
│                  │ uint16   │                     │
└──────────────────┴──────────┴─────────────────────┘
```

TCP 는 바이트 스트림이라 `send()` 한 번이 `recv()` 한 번으로 오지 않는다.
`BodySize` 를 보고 직접 경계를 잘라야 한다. 이게 프레이밍의 전부다.

### 오피코드

| 값 | 이름 | 방향 | 상태 |
|---|---|---|---|
| 1 | `LoginReq` | C → S | 사용 중 |
| 2 | `LoginAck` | S → C | 사용 중 |
| 3 | `ChatSend` | C → S | 사용 중 |
| 4 | `ChatBroadcast` | S → C | 사용 중 |
| 5 | `HistoryReq` | C → S | 6단계 예정 |
| 6 | `HistoryAck` | S → C | 6단계 예정 |
| 7 | `WhisperSend` | C → S | 9단계 예정 |
| 8 | `SetDead` | C → S | 임시. 8단계에서 리슨서버 전용으로 |
| 9 | `Heartbeat` | C → S | 서버가 받고 무시만 함 |

### 상한값

| 상수 | 값 | 의미 |
|---|---|---|
| `kMaxBodySize` | 4096 | 넘으면 서버가 **연결을 끊는다** |
| `kMaxNameLen` | 32 | 이름 고정 배열 크기 (널 종료 포함) |
| `kMaxTextLen` | 512 | 메시지 본문 **UTF-8 바이트** 수. 한글 약 170자 |

> `TextLen` 은 글자 수가 아니라 **바이트 수**다.
> 상한을 넘겨 보내면 서버가 `Malformed` 로 판단해 연결을 끊으므로,
> 클라이언트가 `EncodeUtf8Clamped()` 로 **문자 경계에 맞춰** 미리 자른다.

### 채널

| 값 | 이름 | 수신자 |
|---|---|---|
| 0 | `All` | 접속자 전원 |
| 1 | `Team` | 나와 `TeamId` 가 같은 사람 |
| 2 | `Dead` | 죽은 사람끼리. 산 사람이 보내면 서버가 **무응답으로 폐기** |
| 3 | `Whisper` | 9단계 예정. 지금 보내면 아무도 못 받음 |
| 4 | `System` | 서버만 생성. 클라이언트가 보내면 무시 |

채널 번호는 `ChatProtocol.h` 의 `MOU::EChatChannel` 과 언리얼의 `EChatChannelBP` 가
반드시 일치해야 한다. `ChatFraming.h` 의 `static_assert` 가 컴파일 타임에 검사한다.

---

## 9. 팀 공용 파일 변경 내역

> **다른 파트 작업에 영향이 갈 수 있는 파일은 아래 하나뿐이다.**

### `TeamProject_MOU/Source/TeamProject_MOU/TeamProject_MOU.Build.cs` (수정)

| 변경 | 내용 | 다른 파트에 미치는 영향 |
|---|---|---|
| `using System.IO;` 추가 | `Path` 클래스 사용 | 없음 |
| `"Sockets"`, `"Networking"` 추가 | 엔진 소켓 API | 없음 |
| `"SlateCore"` 추가 | UMG 위젯의 `ETextCommit` 리플렉션 링크에 필요 | 없음. 원래 주석으로 추천돼 있던 모듈 |
| `PublicIncludePaths` 에 `MOU_ChatServer/Shared` 추가 | 서버와 프로토콜 헤더 공유 | **저장소 전체를 받아야 함** |

### 바뀌지 않은 것 (확인용)

- `TeamProject_MOU.uproject` — 플러그인 추가 없음. `Sockets`/`Networking`/`SlateCore` 는 엔진 기본 모듈
- `Config/*.ini` — 건드리지 않았다
- `MOU_ChatServer/Shared/ChatProtocol.h` — **서버 공유 파일이지만 수정하지 않았다.** 언리얼 쪽이 여기 맞췄다
- 기존 게임플레이 코드(`Base/`, `TeamProject_MOUCharacter` 등) — 전혀 건드리지 않았다
- 콘텐츠(`.uasset`) — 추가/수정 **0개**. 에셋 충돌 걱정 없음

### 추가된 파일 (전부 신규, 충돌 없음)

```
Source/TeamProject_MOU/Public/Chat/    ChatTypes.h  ChatFraming.h  ChatClientRunnable.h  ChatSubsystem.h  ChatWidgetBase.h
Source/TeamProject_MOU/Private/Chat/   ChatFraming.cpp  ChatClientRunnable.cpp  ChatSubsystem.cpp  ChatWidgetBase.cpp
```

### 커밋하면 안 되는 것

- `TeamProject_MOU/Automation_TeamProject_MOU.slnx` — UBT 가 자동 생성. `.gitignore` 에 `*.sln` 만 있어서 untracked 로 뜬다
- `%APPDATA%\...\BuildConfiguration.xml` — 사용자별 설정. 애초에 저장소 밖이다

> 참고: `TeamProject_MOU.slnx` 는 현재 git 에 추적되고 있는데, 내용에 엔진 경로가
> `F:/Epic Games/UE_5.8/...` 같은 **절대 경로**로 박혀 있다. 엔진을 다른 드라이브에 설치한
> 사람에게는 어차피 맞지 않아서 각자 재생성해서 쓰게 된다. 충돌이 반복되면
> `.gitignore` 에 `*.slnx` 를 넣는 것을 고려할 것.

---

## 10. 이 시스템의 코딩 규칙

### 파일 인코딩 — **UTF-8 (BOM 포함)**

`Chat/` 폴더의 모든 `.h`/`.cpp` 는 UTF-8 **BOM 포함**으로 저장돼 있다.
주석에 한글이 있어서 BOM 이 없으면 MSVC 가 CP949 로 오해해 깨진다.

**편집기에서 저장할 때 인코딩을 바꾸지 말 것.** VS 는 기존 인코딩을 유지하니 보통 문제없다.

> 기존 팀원 파일(`Base/` 등)은 CP949 로 저장돼 있다. 파일마다 인코딩이 달라도
> BOM 유무로 구분되므로 컴파일에는 문제가 없다.

### 주석 규칙

이 파트를 담당하지 않은 팀원이 리뷰할 수 있도록, **모든 파일 상단에**:

- 이 파일이 채팅 시스템 어디에 위치하는지 (다이어그램 포함)
- 어디와 정보를 주고받는지 (대응하는 서버 코드 파일·함수 이름)
- 수정 시 같이 고쳐야 하는 파일

그리고 함수 단위로 **"왜 이렇게 했는지"** 를 남긴다. 무엇을 하는지는 코드가 말해준다.

### 스레드 규칙

| 하는 곳 | 해도 되는 것 |
|---|---|
| `FChatClientRunnable` (워커) | 소켓, 바이트 배열, 순수 구조체, `TQueue` 넣기 |
| `FChatClientRunnable` (워커) | ❌ UObject, UMG, 델리게이트 브로드캐스트, `UE_LOG` 외 엔진 API |
| `UChatSubsystem::Tick` (게임) | 큐에서 꺼내기, 델리게이트 브로드캐스트 |
| `UChatWidgetBase` (게임) | 위젯 조작 |

### 종료 순서 (지키지 않으면 크래시)

워커 스레드 정리는 **반드시** 이 순서다 (`UChatSubsystem::ShutdownClient`):

1. `ChatClient->Stop()` — 종료 요청 플래그
2. `ChatThread->Kill(/*bShouldWait=*/true)` — **`true` 필수.** 워커가 `Run()` 을 빠져나올 때까지 대기
3. `delete ChatClient` — 스레드가 완전히 끝난 뒤에

`bShouldWait` 를 `false` 로 두면 **PIE 를 껐다 켤 때 에디터가 통째로 죽는다.**
언리얼에서 가장 흔하게 밟는 지뢰다.

---

## 11. 알려진 한계

### 언리얼 클라이언트

- **신원 위조 가능.** 클라이언트가 `Login()` 에 아무 이름·팀 ID나 넣을 수 있고 서버가 검증하지 않는다.
  `SetDeadForTest()` 도 마찬가지라 **산 사람이 사망 채널을 엿볼 수 있다.**
  8단계에서 리슨서버가 신원을 미러링하도록 바꿀 것이므로,
  **게임플레이 코드에서 `SetDeadForTest` 호출을 늘리지 말 것.**
- ~~프로토콜 버전 검사 없음~~ → **해결됨 (프로토콜 v2)**.
  `LoginReqBody::Version` 을 서버가 검사하고, 다르면 `LoginAck` 에
  `ELoginResult::VersionMismatch` 와 서버 버전을 담아 거부한다.
  서버 콘솔·에디터 로그·채팅창 안내문 세 곳에 원인이 표시된다.
  **`ChatProtocol.h` 를 고쳤으면 `kProtocolVersion` 을 올리고 서버와 언리얼을 같이 다시 빌드할 것.**
- **엔디안 변환 없음.** 서버·클라이언트 모두 x86 리틀엔디안 전제.
- 전송 지연이 최대 50ms 붙는다 (`FChatClientRunnable::WaitMilliseconds`).
  워커가 `Wait()` 에서 깨어나야 큐를 비우기 때문. 채팅에는 무해하다.
- **재로그인하면 UserId 가 새로 발급된다.** 서버 `HandleLoginReq` 가 무조건 `AssignUserId()` 를 부른다.
  지금은 무해하지만 **6단계에서 UserId 를 DB 키로 쓰기 전에** 반드시 막아야 한다.
- **콘솔 명령이 Shipping 빌드에도 남는다.** `MOU.Chat.Dead` 같은 치트성 명령에
  `ECVF_Cheat` 가 없다. 8단계에서 `SetDead` 를 리슨서버 전용으로 잠글 때 같이 정리한다.

### 채팅 로그 DB (6단계)

- **비정상 종료 시 큐에 남은 줄이 유실된다.** Ctrl+C(SIGINT/SIGTERM)로 내리면
  `ChatLog::Stop()` 이 큐를 비우고 닫으므로 손실이 없지만,
  작업 관리자에서 강제 종료하거나 정전이면 아직 커밋 안 된 만큼 사라진다.
  채팅 로그라 이 정도는 감수하고 지연 없는 쪽을 택했다.
- **`synchronous=NORMAL`.** OS 가 죽으면 최근 몇 커밋이 날아갈 수 있다. DB 가 깨지지는 않는다.
- **큐 상한 1만 줄.** 넘치면 버리고 `GetDroppedCount()` 로 집계한다.
  종료 시 "유실 N줄" 로 찍히므로, 0 이 아니면 디스크가 못 따라간다는 뜻이다.
- **아직 읽는 기능이 없다.** 쓰기 전용이다. 접속 시 최근 대화 N줄을 내려주는 기능은 별도 작업이다.

### 서버 (`MOU_ChatServer/README.md` 에서 그대로)

- **`send()` 를 세션 락 안에서 호출한다.** 느린 클라이언트가 있으면 락을 오래 잡아 전체 채팅이 지연된다.
  인원이 늘면 세션별 송신 큐 + 전용 송신 스레드로 바꿔야 한다.
- **스레드를 detach 한다.** 서버 종료 시 클라이언트 스레드를 정리하지 않는다.
- **인증이 없다.** 로그인은 이름만 받는다.
- 접속자 수 상한은 없다 (고정 배열이 아님).

---

## 12. 다음 단계

우선순위 순으로 정리했다.

### 8단계 — 리슨서버 → 채팅서버 신원 미러링 (권장 1순위)

지금 가장 큰 구멍이다. 팀 ID 와 생사 여부의 **권위자는 게임(리슨서버)** 인데 채팅 서버는 그걸 모른다.

방향:
1. 리슨서버(호스트)가 채팅 서버에 **관리 연결**을 하나 따로 연다
2. 그 연결에서만 `SetDead` / 팀 배정을 받는다. 일반 클라이언트가 보내면 거부
3. 로그인도 티켓 방식으로: 리슨서버가 채팅 서버에 `(UserId, Token, TeamId)` 를 등록하고
   클라이언트에게 `Token` 을 넘겨주면, 클라이언트는 이름 대신 `Token` 으로 로그인

프로토콜에 필드를 미리 잡아둬야 하므로 `ChatProtocol.h` 수정이 필요하다. **서버 공유 파일이니 팀에 알릴 것.**

### 6단계 — SQLite 적재 + 히스토리 조회

`RouteChat()` 안에 적재를 넣고, `HistoryReq`/`HistoryAck`(오피코드 5/6)을 구현한다.
클라이언트는 로그인 직후 최근 N줄을 요청해 채팅창을 채운다.

### 9단계 — 귓속말

서버 `RouteChat()` 의 `case EChatChannel::Whisper` 에 수신자 1명 찾는 로직만 추가하면 된다.
클라이언트는 `ChatSendBody::TargetUserId` 를 채운다. 이미 필드가 있다.
UI 쪽에 `/w <이름> <메시지>` 파싱을 추가한다 (`UChatWidgetBase::SubmitInput`).

### 그 외 개선 후보

- 접속 정보를 `DefaultGame.ini` 로 빼기 (`Host`/`Port` 하드코딩 제거)
- 서버: 세션별 송신 큐로 락 경합 제거
- 클라이언트: 채팅 로그를 파일로 저장

---

## 13. 문제 해결

실제로 겪은 것들이다.

### "이 프로젝트는 현재 버전의 Visual Studio와 호환되지 않습니다"

**원인**: 프로젝트 파일이 다른 VS 버전용으로 생성됐다.
VS 2022 와 2026 이 둘 다 설치돼 있으면 UBT 가 자동으로 최신(2026)을 고른다.

**확인**: `Intermediate/ProjectFiles/TeamProject_MOU.vcxproj` 첫 줄의 `ToolsVersion`
(2022 = `17.0`, 2026 = `18.0`) 과 `UECommon.props` 의 `PlatformToolset` (`v143` / `v145`).

**해결**: [2-3 절](#2-3-visual-studio-버전-고정--중요) 의 `BuildConfiguration.xml` 설정 후 프로젝트 파일 재생성.
**폴더를 지울 필요는 전혀 없다.** `Binaries`/`Intermediate`/`DerivedDataCache`/`Saved` 삭제는
이 문제와 무관하고 전체 리빌드·셰이더 재컴파일만 유발한다.

### 빌드 중 `VirtualAlloc failed commit ... The paging file is too small`

**원인**: 빌드 가속기(UBA)가 기본 40GB 를 예약하는데 시스템 커밋 한계에 걸렸다.
VS 가 UE5 솔루션(59개 프로젝트) 을 열어둔 채 IntelliSense 를 돌리면 특히 잘 터진다.

**해결 (택 1)**:
1. `BuildConfiguration.xml` 에 `<bAllowUBAExecutor>false</bAllowUBAExecutor>` — **가장 간단.**
   우리 모듈은 파일 몇 개뿐이라 속도 차이가 체감되지 않는다
2. 페이지 파일 늘리기 (시스템 속성 → 고급 → 성능 → 고급 → 가상 메모리)
3. `<UnrealBuildAccelerator><StoreCapacityGb>12</StoreCapacityGb></UnrealBuildAccelerator>`

### `LNK2019: Z_Construct_UEnum_SlateCore_ETextCommit`

`Build.cs` 의 `PublicDependencyModuleNames` 에 `"SlateCore"` 가 빠졌다. 이미 추가돼 있다.

### `ChatProtocol.h` 를 찾을 수 없음

`TeamProject_MOU` 폴더만 복사했을 때 발생한다. 저장소 전체를 받아야 한다.

### VS 에 새 파일이 안 보임

프로젝트 파일 재생성 후 VS 를 **완전히 껐다 켠다.** 열어둔 채 재생성하면 옛 정보를 들고 있다.

### 한글 주석이 깨져 보임 / `C2001` 같은 컴파일 에러

파일 인코딩이 UTF-8 BOM 에서 다른 것으로 바뀌었다. [10절](#10-이-시스템의-코딩-규칙) 참고.

### PIE 를 껐다 켜면 에디터가 크래시

워커 스레드 정리 순서 문제다. `ChatThread->Kill(true)` 의 인자가 `true` 인지 확인할 것.

### 채팅을 보냈는데 아무 일도 안 일어남

체크리스트:
1. `ChatServer.exe` 가 켜져 있나
2. 상태가 `LoggedIn` 인가 (`Connected` 만으로는 서버가 버린다)
3. 사망 채널로 보내고 있는데 살아있는 상태 아닌가 (서버가 **무응답으로 폐기**한다)
4. 메시지가 512바이트를 넘지 않나
5. 에디터 출력 로그에서 `LogMOUChat` 필터로 경고 확인
