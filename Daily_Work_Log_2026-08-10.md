# 일일 작업 결산

> 작성일: 2026-08-10
> 작성자: 이덕진

---

## 1. 오늘 작업 내용

### 작업 1. NPC Grab 애니메이션 흐름 분석 및 단일 몽타주 구조 정리

**작업 내용**

- 구현 및 점검한 기능:
  - `GA_Grab`의 Grab Start, Loop, Return/Throw 애니메이션 전환 흐름 분석
  - `On Blend Out`, `On Completed`, `On Interrupted` 콜백의 실제 실행 의미 확인
  - Grab 성공 여부와 관계없이 Blend Out 로그가 발생하던 원인 확인
  - Start/Loop/Return을 서로 다른 몽타주로 재생할 때 ABP Stay 포즈가 중간에 노출되는 문제 분석
  - 단일 Grab 몽타주와 Section 기반 전환 구조 설계
- 주요 확인 사항:
  - `On Blend Out`은 실패 신호가 아니라 정상 종료 시에도 호출됨
  - `On Completed` 이후 다음 몽타주를 재생하면 이미 ABP 포즈로 복귀한 뒤라 자세가 튈 수 있음
  - 긴 Blend Out은 복귀 애니메이션을 대신할 수 없으며, 팔을 뻗은 포즈와 Idle 포즈가 오랫동안 섞여 부자연스러워질 수 있음
  - 일반 Anim Notify State는 `Play Montage` 노드의 `On Notify Begin`에서 원하는 방식으로 전달되지 않을 수 있어 Montage Notify 사용 필요

**정리한 몽타주 Section 구조**

```text
GrabStart
GrabLoop
GrabRelease
GrabThrow
```

**작업 결과**

- [x] Blend Out 로그 의미 확인
- [x] ABP 포즈가 중간에 노출되는 원인 확인
- [x] 단일 몽타주 Section 구조 설계
- [ ] 전체 Section 전환 최종 검증

---

### 작업 2. Grab 성공 후 Release/Throw 반응 구조 설계

**작업 내용**

- NPC 공격 Ability와 플레이어 반응 Ability의 역할 분리
- 잡기 후 반드시 던지는 NPC의 경우 NPC `GA_Grab`에서 Grab부터 Throw 시점까지 관리하도록 구조 정리
- 플레이어의 `GA_Grabed`에서 잡힌 상태 유지 및 Release/Thrown 이벤트 반응 처리 구조 설계
- `StopGrabFollow` 이후 `Launch Character` 실행 순서 점검

**권장 흐름**

```text
NPC GA_Grab
 → Grab 판정
 → GrabFollow 시작
 → GrabLoop
 → Release 또는 Throw 결정
 → 반응 Gameplay Event 전송

플레이어 GA_Grabed
 → Event.Reaction.Grabbed 수신
 → 잡힌 상태 유지
 → GrabRelease 또는 Thrown 이벤트 대기
 → StopGrabFollow
 → 이동 상태 복구
 → 필요 시 Launch Character
```

**확인한 사실**

- `StopGrabFollow()`는 `CarrierCharacter`를 비우고 `State.Held`를 제거함
- 잡혀 있는 동안 `GrabFollowComponent`가 Tick에서 위치를 동기화하고 `StopMovementImmediately()`를 호출함
- 던지기 순서는 `StopGrabFollow → Movement Mode 복구 → Launch Character`가 안전함

**작업 결과**

- [x] Grab 해제 및 Throw 실행 순서 확인
- [x] Grab/Thrown 이벤트 역할 분리
- [x] `StopGrabFollow` 이후 Launch 동작 확인
- [ ] 멀티플레이 서버 권한 환경 반복 검증

---

### 작업 3. Behavior Tree Action Task의 Ability 종료 대기 처리

**작업 내용**

- `BTT_ExecuteNPCAction`이 Ability 실행 직후 `Finish Execute`되어 다음 BT 노드로 넘어가던 문제 분석
- 밀기 같은 짧은 Ability에서는 드러나지 않았지만 Grab/Throw처럼 긴 Ability에서 BT가 동작 중간에 개입하는 원인 확인
- `State.NPC.ActionRunning` Activation Owned Tag를 이용해 BTT가 Ability 종료까지 기다리도록 구조 변경
- `Try Activate Abilities by Tag` 반환값을 이용한 활성화 실패 처리 추가
- BTT Tick에서 NPC의 Ability System Component 태그를 확인하도록 구성

**수정 구조**

```text
Receive Execute AI
 → ASC 저장
 → Try Activate Abilities by Tag
 → 성공: Finish Execute 없이 대기
 → 실패: Finish Execute(false)

Receive Tick AI
 → ASC에 State.NPC.ActionRunning 존재 여부 확인
 → 존재: 계속 대기
 → 제거됨: Finish Execute(true)
```

**확인한 문제와 해결**

- 태그 검사 대상을 AIController로 연결해 계속 false가 반환되는 문제 확인
- 검사 대상을 NPC의 Ability System Component로 변경
- `CachedASC`를 BTT 변수로 저장하여 Tick에서 안정적으로 참조하도록 정리

**작업 결과**

- [x] 긴 Ability 실행 중 BT가 다음 노드로 넘어가는 원인 확인
- [x] ActionRunning 태그 기반 대기 구조 적용
- [x] ASC 태그 검사 대상 오류 수정
- [x] BTT가 Ability 종료 시점까지 대기하는 동작 확인

---

### 작업 4. NPC Action 데이터 구조 추가 및 NPCData 연동

**작업 내용**

- NPC별 메시에 따라 다른 밀기/잡기/던지기 몽타주와 수치를 설정할 수 있도록 C++ 구조체 추가
- 잡기 종료 방식을 bool 대신 확장 가능한 Enum으로 구성
- `UNPCData`에서 Action 설정을 Data Asset별로 편집할 수 있도록 연동
- NPC Home Position 설정 추가

**추가한 C++ 타입**

```text
ENPCGrabFinishType
FNPCPushActionData
FNPCGrabActionData
FNPCThrowActionData
FNPCActionStruct
```

**추가한 주요 설정**

```text
Push
- Montage
- HorizontalPower
- VerticalPower
- Velocity Override 설정

Grab
- Montage
- FinishType
- SocketName
- RelativeOffset
- Start/Loop/Release Section 이름
- HoldDuration
- Throw 설정

Throw
- Montage
- HorizontalPower
- VerticalPower
- Throw Section 이름
- Velocity Override 설정
```

**수정한 파일**

```text
Source/TeamProject_MOU/Public/NPC/NPCActionStruct.h
Source/TeamProject_MOU/Public/Data/NPCData.h
```

**추가 연동 내용**

```cpp
FNPCActionStruct ActionData;
FVector HomePosition;
```

**작업 결과**

- [x] NPC Action C++ 구조체 생성
- [x] 한글 주석 및 Blueprint 편집 지원
- [x] NPCData ActionData 연동
- [x] NPCData HomePosition 추가
- [ ] 기존 NPC Data Asset별 실제 값 입력

---

### 작업 5. CharacterBase에 GrabFollowComponent 기본 추가

**작업 내용**

- 플레이어와 NPC 공통 베이스인 `ACharacterBase`에 `UGrabFollowComponent` 추가
- 생성자에서 기본 서브오브젝트로 생성하도록 구성
- 블루프린트에서 참조할 수 있도록 `VisibleAnywhere`, `BlueprintReadOnly`로 노출

**수정한 파일**

```text
Source/TeamProject_MOU/Public/Base/CharacterBase.h
Source/TeamProject_MOU/Private/Base/CharacterBase.cpp
```

**추가 코드 요약**

```cpp
GrabFollowComponent =
    CreateDefaultSubobject<UGrabFollowComponent>(TEXT("GrabFollowComponent"));
```

**작업 결과**

- [x] CharacterBase 기본 컴포넌트 생성
- [x] 플레이어/NPC 공통 사용 가능
- [ ] 에디터 재실행 후 기존 Blueprint 컴포넌트 중복 여부 확인

---

### 작업 6. Push Launch 궤적 급정지 문제 분석

**작업 내용**

- `GA_Pushed`에서 `Launch Character`로 밀린 대상이 포물선을 그리다가 수평 이동을 멈추고 수직 낙하하는 문제 분석
- NPCData의 Push Power 및 Character Movement 공중 감속 설정 비교

**확인한 원인**

```text
Push HorizontalPower 기본값 = 500
BrakingDecelerationFalling = 1500
```

- 공중에서 수평 속도가 약 0.33초 만에 소진되어 수직 낙하처럼 보일 수 있음
- Throw는 수평 힘이 더 커서 착지 시점까지 속도가 남아 상대적으로 자연스러운 포물선으로 보임

**검토한 해결 방향**

```text
Launch 전 BrakingDecelerationFalling 임시 감소
→ Launch Character
→ 착지 시 기존 감속값 복구
→ EndAbility
```

**작업 결과**

- [x] 급격한 수직 낙하 원인 분석
- [x] Push/Throw 데이터 구조체 수치 연동 확인
- [ ] 착지 이벤트 기반 공중 감속값 복구 구현

---

## 2. 테스트 결과

### 프로젝트 기본 테스트

- [ ] 에디터/프로젝트 전체 정상 실행
- [ ] C++ 전체 빌드 성공
- [ ] 블루프린트 전체 컴파일 오류 없음
- [ ] 멀티플레이 서버/클라이언트 검증
- [x] 추가한 헤더 코드 형식 검사

### 기능 테스트

| 테스트 항목 | 결과 | 비고 |
|---|---|---|
| Grab Blend Out 콜백 구분 | 성공 | 정상 종료에서도 Blend Out 발생 확인 |
| Start/Return 분리 몽타주 전환 | 일부 성공 | ABP Stay 포즈 노출 및 블렌딩 문제 확인 |
| 단일 Grab 몽타주 설계 | 설계 완료 | Section 최종 적용 및 검증 필요 |
| StopGrabFollow 후 Launch | 성공 | 실행 순서 및 대상 확인 후 동작 |
| BT ActionRunning 대기 | 성공 | ASC 태그 검사 대상으로 수정 후 정상 |
| NPC ActionData 연동 | 코드 완료 | Data Asset별 값 입력 필요 |
| CharacterBase GrabFollow 기본 추가 | 코드 완료 | 에디터에서 중복 컴포넌트 확인 필요 |
| Push 포물선 유지 | 원인 확인 | Falling 감속값 조정 미적용 |

---

## 3. 발생한 문제

### 문제 1. Grab 몽타주 사이에 ABP Stay 포즈가 노출됨

**문제 내용**

```text
GrabStart가 완전히 종료된 후 Return 몽타주를 실행하면서
중간에 ABP Stay 포즈가 보이고 팔 자세가 튀는 현상이 발생함.
```

**확인한 원인**

```text
OnCompleted는 몽타주가 ABP로 완전히 Blend Out된 뒤 호출됨.
OnBlendOut에서 다른 몽타주를 실행해도 두 블렌딩이 겹칠 수 있음.
```

**해결 방향**

```text
GrabStart/GrabLoop/GrabRelease/GrabThrow를 단일 몽타주 Section으로 구성하고
GA에서 Section 전환을 직접 제어함.
```

**현재 상태**

- [ ] 해결 완료
- [x] 구조 변경 중

---

### 문제 2. BT가 긴 Ability 실행 도중 다음 행동으로 넘어감

**문제 내용**

```text
BTT_ExecuteNPCAction이 Ability 활성화 직후 Finish Execute를 호출해
Grab/Throw가 끝나기 전에 BT가 다음 노드로 이동함.
```

**해결 방법**

```text
Action GA에 State.NPC.ActionRunning Activation Owned Tag를 적용하고,
BTT가 ASC의 태그 제거 시점까지 Running 상태를 유지하도록 수정함.
```

**현재 상태**

- [x] 해결 완료

---

### 문제 3. BTT에서 ActionRunning 태그가 계속 false로 조회됨

**확인한 원인**

```text
태그 검사 대상이 NPC의 ASC가 아니라 AIController로 연결되어 있었음.
```

**해결 방법**

```text
NPC의 AbilitySystemComponent를 CachedASC로 저장하고
Tick에서 CachedASC의 State.NPC.ActionRunning 태그를 검사함.
```

**현재 상태**

- [x] 해결 완료

---

### 문제 4. Push가 포물선 중간에 수직 낙하함

**확인한 원인**

```text
Push 수평 힘에 비해 BrakingDecelerationFalling 값이 커서
착지 전에 수평 속도가 0이 됨.
```

**해결 방법**

```text
밀려나는 동안 공중 감속값을 낮추고 착지 시 원래 값으로 복구하는 방식 검토.
```

**현재 상태**

- [ ] 해결 완료
- [x] 원인 확인

---

## 4. 미완료 작업

- [ ] 단일 Grab 몽타주에 Start/Loop/Release/Throw Section 최종 구성
- [ ] Section별 전환 및 성공/실패/Throw 분기 검증
- [ ] NPC Data Asset별 ActionData와 몽타주 값 입력
- [ ] 기존 플레이어/NPC Blueprint의 GrabFollowComponent 중복 여부 확인
- [ ] Push 중 공중 감속값 임시 변경 및 착지 복구 구현
- [ ] Grab/Throw 멀티플레이 서버 권한 테스트
- [ ] 전체 C++ 빌드 및 Blueprint 컴파일 확인

---

## 5. 다음 작업 계획

1. `Oni_GrabAndThrow` 단일 몽타주 Section 구성 완료
2. `GA_Grab`에서 NPCData의 `ActionData.Grab`을 읽어 몽타주와 종료 방식을 결정하도록 연결
3. `GA_Grabed`에서 Release/Thrown 이벤트별 반응 분기 구현
4. NPC Data Asset별 Push/Grab/Throw 수치와 몽타주 입력
5. Push 착지 이벤트와 공중 감속값 복구 처리
6. Grab 실패, 성공, Throw, 반복 실행 및 BT 대기 통합 테스트

---

## 6. 팀 공유 내용

### 새로 추가한 항목

- C++ 구조체:
  - `FNPCActionStruct`
  - `FNPCPushActionData`
  - `FNPCGrabActionData`
  - `FNPCThrowActionData`
- Enum:
  - `ENPCGrabFinishType`
- NPCData:
  - `ActionData`
  - `HomePosition`
- CharacterBase:
  - `GrabFollowComponent`
- Gameplay Tag 활용:
  - `State.NPC.ActionRunning`

### 구조 변경 사항

```text
기존:
BTT가 GA 실행 직후 완료되어 다음 행동으로 넘어감

변경:
BTT가 State.NPC.ActionRunning이 제거될 때까지 대기
```

```text
기존:
NPC마다 GA 또는 몽타주를 개별 하드코딩

변경:
공통 GA가 NPCData.ActionData를 읽어 NPC별 몽타주와 힘을 사용
```

### 작업 요약

```text
오늘은 NPC Grab/Throw의 애니메이션 전환과 BT 인터럽트 문제를 집중적으로 정리했다.
긴 Ability가 끝나기 전에 BT가 다음 행동으로 넘어가던 문제는 ActionRunning 태그 기반 대기로 해결했다.
NPC별 밀기, 잡기, 던지기 설정을 공통 GA에서 사용할 수 있도록 C++ Action 구조체와 NPCData 연동을 추가했으며,
CharacterBase에 GrabFollowComponent를 기본 탑재했다.
Grab 애니메이션은 분리 몽타주보다 단일 몽타주 Section 방식으로 전환하는 방향을 확정했고,
Push가 포물선 중간에 수직 낙하하는 문제는 공중 감속값이 큰 것이 원인임을 확인했다.
```
