# NPC 공용 Behavior Tree 구현 정리

## 1. 현재 목표

공용 Behavior Tree 하나에서 다음 행동을 처리한다.

```text
Patrol
→ 플레이어 감지
→ Tracking
→ Action
→ 타깃 상태에 따라 재공격 또는 재추적
→ 타깃 상실 시 Patrol
```

NPC별 고유 행동은 추후 Gameplay Ability로 분리한다. 현재 단계에서는 GA를 연결하지 않고 `BTT_Action`으로 동작 흐름만 검증한다.

---

## 2. Gameplay Tag

프로젝트에서는 다음 태그 이름으로 통일한다.

```text
State.NPC.Patrol
State.NPC.Tracking
State.NPC.Stay
```

AI 상태는 한 번에 하나만 유지한다.

### `SetNPCState`

`BP_Base_NPC`에 상태 변경 함수 `SetNPCState`를 둔다.

입력:

```text
NPCState : Gameplay Tag Container
```

처리:

```text
ASC 유효성 검사
→ State.NPC.Patrol 제거
→ State.NPC.Tracking 제거
→ State.NPC.Stay 제거
→ 입력받은 NPCState 추가
```

`Gameplay Tag Container`를 사용하는 것은 문제없지만, 상태 태그는 반드시 하나만 전달한다.

```text
올바른 예
NPCState = { State.NPC.Tracking }

피해야 할 예
NPCState = { State.NPC.Tracking, State.NPC.Stay }
```

---

## 3. Blackboard

현재 프로젝트에서 사용하는 주요 키 이름은 다음과 같다.

| 키 | 타입 | 역할 |
|---|---|---|
| `Self` | Object, Base Class Actor | 상태 태그를 가진 NPC Pawn |
| `Target` | Object, Base Class Actor | 감지하고 추적하는 플레이어 |
| `PatrolLocation` | Vector | 랜덤 순찰 목적지 |
| `IsActionRange` | Bool | 타깃이 행동 범위 안에 있는지 |
| `ActionRange` | Float | 행동 가능 거리 |
| `HomeLocation` | Vector | 선택 사항, NPC 초기 위치 |

### `Self`와 `Target`의 차이

```text
Self
→ NPC 자신
→ State.NPC.* 검사에 사용

Target
→ 플레이어
→ 추적, 거리 계산, Is Set 조건에 사용
```

AIController Blueprint 안에서 `Self` 노드는 AIController 자신을 의미한다. 따라서 Blackboard의 `Self` 값에 AIController의 `Self`를 넣으면 안 된다.

```text
잘못된 연결
Object Value = BP_NPCAI의 Self

올바른 연결
Object Value = On Possess의 Possessed Pawn
또는 Cast 결과인 As BP_Base_NPC
```

---

## 4. AIController 초기화

`BP_NPCAI`의 `Event On Possess`는 다음 순서로 구성한다.

```text
Event On Possess
→ Possessed Pawn을 BP_Base_NPC로 Cast
→ Use Blackboard(BB_NPC_Base)
→ Blackboard Self = As BP_Base_NPC
→ HomeLocation = Possessed Pawn의 Actor Location
→ BP_Base_NPC.SetNPCState(State.NPC.Patrol)
→ Run Behavior Tree(BT_NPC_Base)
```

`Use Blackboard`가 반환한 Blackboard Component를 `Set Value as Object`의 Target으로 사용하면 초기화 순서가 명확하다.

---

## 5. AI Perception

`BP_NPCAI`에서 AI Sight를 사용한다.

권장 초기값:

```text
Sight Radius = 1500
Lose Sight Radius = 2000
Peripheral Vision Half Angle = 60~70
Detect Neutrals = true
```

### 감지 성공

```text
On Target Perception Updated
→ Successfully Sensed = true
→ Target = 감지된 플레이어
→ NPC.SetNPCState(State.NPC.Tracking)
```

### 감지 실패

```text
Successfully Sensed = false
→ Target Clear
→ IsActionRange = false
→ NPC.SetNPCState(State.NPC.Patrol)
```

Stay 중 감지를 무시하는 설계를 사용할 경우, 감지 이벤트에서 NPC ASC의 `State.NPC.Stay` 보유 여부를 먼저 검사한다.

---

## 6. 최종 Behavior Tree 구조

```text
Root
└─ Selector
   ├─ Sequence [Stay]
   │  ├─ Decorator: State.NPC.Stay 보유
   │  ├─ BTT_StopMovement
   │  ├─ BTT_ClearTarget
   │  ├─ Wait
   │  └─ BTT_SetState(State.NPC.Patrol)
   │
   ├─ Sequence [Tracking]
   │  ├─ Decorator: State.NPC.Tracking 보유
   │  ├─ Decorator: Target Is Set
   │  │  └─ Observer Aborts = Lower Priority
   │  ├─ Service: BTS_UpdateTargetRange
   │  └─ Selector
   │     ├─ Sequence [Action]
   │     │  ├─ Decorator: IsActionRange == true
   │     │  │  └─ Observer Aborts = Self
   │     │  ├─ BTT_StopMovement
   │     │  ├─ BTT_Action
   │     │  └─ Wait(ActionInterval)
   │     └─ Move To(Target)
   │
   └─ Sequence [Patrol]
      ├─ Decorator: State.NPC.Patrol 보유
      ├─ Decorator: Target Is Not Set
      ├─ BTT_FindRandomPatrolPoint
      ├─ Move To(PatrolLocation)
      └─ Wait
```

### Selector 우선순위

왼쪽부터 다음 순서를 유지한다.

```text
Stay
→ Tracking
→ Patrol
```

---

## 7. 반복 Action 흐름

전투형 NPC는 타깃이 시야와 Blackboard에 남아 있는 동안 Patrol로 돌아가지 않는다.

```text
Target 유효
├─ IsActionRange = true
│  └─ Action → Wait → 다시 조건 평가
└─ IsActionRange = false
   └─ Move To(Target)

Target 상실
└─ Patrol
```

Action 다음에 바로 Stay 상태와 Target Clear를 실행하면 공격 한 번마다 순찰로 돌아가게 된다. 반복 공격형 NPC에서는 다음 노드를 Action Sequence에 두지 않는다.

```text
BTT_SetState(State.NPC.Stay)
BTT_ClearTarget
```

공격 간격을 제어하기 위해 `BTT_Action` 뒤에는 반드시 `Wait` 또는 GA 쿨다운을 둔다. Wait가 없으면 BT가 매우 빠르게 Action을 반복한다.

---

## 8. `BTT_Action`

현재 테스트용 Task는 다음과 같이 구성한다.

```text
Event Receive Execute AI
→ Print String("Action")
→ Finish Execute(true)
```

주의:

```text
Receive Tick AI에서 Action을 실행하지 않는다.
Finish Execute를 빠뜨리지 않는다.
```

Action Sequence 안에서 노드 순서는 다음과 같다.

```text
BTT_StopMovement
→ BTT_Action
→ Wait
```

---

## 9. `BTS_UpdateTargetRange`

Tracking Sequence 최상단에 붙인다. Move To에만 붙이면 Action/Wait 중에는 거리를 갱신하지 못한다.

권장 설정:

```text
Interval = 0.1
Random Deviation = 0
Call Tick on Search Start = true
```

그래프:

```text
Event Receive Tick AI
→ Blackboard Target 가져오기
→ Is Valid(Target)
   ├─ Valid
   │  ├─ Controlled Pawn과 Target의 거리 계산
   │  ├─ Distance <= ActionRange
   │  └─ IsActionRange = 비교 결과
   └─ Not Valid
      └─ IsActionRange = false
```

비교 결과를 `Set Blackboard Value as Bool`의 Value에 직접 연결한다.

```text
Distance <= ActionRange의 Return Value
→ Set IsActionRange의 Value
```

다음처럼 true일 때만 값을 저장하면 안 된다.

```text
잘못된 예
Branch True  → IsActionRange = true
Branch False → 아무것도 하지 않음
```

이 경우 한 번 true가 된 값이 false로 돌아오지 않는다.

올바른 방식:

```text
Branch True  → IsActionRange = true
Branch False → IsActionRange = false
```

또는 비교 결과를 Bool 값으로 바로 저장한다.

---

## 10. Gameplay Tag 데코레이터 주의사항

Unreal 기본 `Gameplay Tag Condition`은 선택한 Actor의 ASC를 자동으로 찾아보지 않는다. 해당 Actor가 `IGameplayTagAssetInterface`를 직접 구현했을 때만 태그를 읽는다.

현재 `ACharacterBase`는 `IAbilitySystemInterface`를 구현하지만 `IGameplayTagAssetInterface`는 구현하지 않으므로, 기본 데코레이터는 NPC ASC에 추가된 Loose Gameplay Tag를 읽지 못한다.

결과적으로 기본 데코레이터를 그대로 사용하면 다음 문제가 생긴다.

```text
NPC ASC에 State.NPC.Stay 추가
→ 기본 Gameplay Tag Condition은 false
→ Stay 진입 실패
→ Tracking 조건은 계속 true
→ Action 반복
```

### Blueprint 커스텀 데코레이터

부모 클래스:

```text
BTDecorator_BlueprintBase
```

이름 예시:

```text
BTD_GameplayTagCondition
```

변수:

```text
GameplayTag : Gameplay Tag
ShouldHaveTag : Bool
```

두 변수 모두 `Instance Editable`로 설정한다.

UE 5.8에서는 오버라이드 함수 이름이 다음과 같이 표시된다.

```text
PerformConditionCheckAI
```

그래프:

```text
PerformConditionCheckAI
→ Controlled Pawn
→ Get Ability System Component
→ Is Valid
   ├─ Valid
   │  ├─ Has Matching Gameplay Tag(GameplayTag)
   │  ├─ 결과 == ShouldHaveTag
   │  └─ Return 비교 결과
   └─ Not Valid
      └─ Return false
```

BT 설정 예시:

```text
Stay
→ GameplayTag = State.NPC.Stay
→ ShouldHaveTag = true

Tracking
→ GameplayTag = State.NPC.Tracking
→ ShouldHaveTag = true

Patrol
→ GameplayTag = State.NPC.Patrol
→ ShouldHaveTag = true
```

### 장기적인 C++ 대안

`ACharacterBase`가 `IGameplayTagAssetInterface`를 구현하고 `GetOwnedGameplayTags`를 ASC로 전달하면 기본 Gameplay Tag Condition도 사용할 수 있다.

```cpp
void ACharacterBase::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
    if (AbilitySystemComponent)
    {
        AbilitySystemComponent->GetOwnedGameplayTags(TagContainer);
    }
}
```

---

## 11. 상태 조건 설계

초기에는 다음과 같은 조건을 사용했다.

```text
Stay
→ State.NPC.Stay 보유

Tracking
→ State.NPC.Stay 미보유 + Target Is Set

Patrol
→ State.NPC.Stay 미보유 + Target Is Not Set
```

더 명확한 상태 머신을 원하면 각 브랜치에서 정확한 상태를 검사한다.

```text
Stay
→ State.NPC.Stay 보유

Tracking
→ State.NPC.Tracking 보유 + Target Is Set

Patrol
→ State.NPC.Patrol 보유 + Target Is Not Set
```

이 방식을 사용하려면 브랜치 진입 전에 상태 전환이 반드시 완료되어야 한다.

```text
게임 시작
→ Patrol 설정

타깃 감지
→ Target 설정
→ Tracking 설정

타깃 상실
→ Target Clear
→ Patrol 설정
```

상태 태그를 브랜치 진입 조건으로 사용하면서, 같은 브랜치에 진입해야만 해당 상태를 설정하도록 만들면 진입 불가능한 순환 의존성이 생기므로 피한다.

---

## 12. 디버깅 체크리스트

PIE 실행 중 BT의 Debug Actor를 NPC로 선택하고 Blackboard 값을 확인한다.

### 게임 시작

```text
Self = NPC Pawn
Target = None
State = State.NPC.Patrol
```

### 플레이어 감지

```text
Target = Player
State = State.NPC.Tracking
IsActionRange = 거리 비교 결과
```

### 공격 범위 진입

```text
IsActionRange = true
→ StopMovement
→ Action 한 번
→ Wait
```

### 플레이어가 공격 범위 밖으로 이동

```text
IsActionRange = false
→ Action Sequence 중단 또는 완료
→ Move To(Target)
```

### 플레이어 감지 상실

```text
Target = None
IsActionRange = false
State = State.NPC.Patrol
```

문제 발생 시 각 위치에 임시 Print String을 둔다.

```text
BTT_Action       → "Action Execute"
BTT_SetState     → 변경할 State 출력
BTT_ClearTarget  → "Clear Target"
Perception       → Sensed / Lost 출력
Range Service    → Distance, ActionRange, IsActionRange 출력
```

---

## 13. 현재 완료 범위와 다음 작업

현재 구성된 에셋:

```text
BB_NPC_Base
BT_NPC_Base
BP_NPCAI
BP_Base_NPC
BTS_SetPatrol
BTS_UpdateTargetRange
BTT_FindRandomPatrolPoint
BTT_StopMovement
BTT_ClearTarget
BTT_Action
BTT_SetState
BTT_ExecuteNPCAction
BTD_GameplayTagCondition
```

현재 우선 검증할 흐름:

```text
Patrol
→ Target 감지
→ Tracking
→ Action
→ Wait 중 거리 갱신
→ 범위 밖이면 재추적
→ 범위 안이면 재공격
→ Target 상실 시 Patrol
```

이 흐름이 정상 작동한 뒤 다음을 진행한다.

1. `BTT_Action`을 `BTT_ExecuteNPCAction`으로 교체한다.
2. NPCData의 `PrimaryAbilityTag`로 GA를 실행한다.
3. GA 종료/실패/취소 시 BT Task를 완료한다.
4. NPC별 반복 행동과 단발 행동 정책을 Data Asset로 분리한다.

예시:

```text
RepeatWhileTargetVisible
→ 공격, 기절 공격 등

OneShotThenStay
→ 밀치기, 빼앗기, 들기 등
```
