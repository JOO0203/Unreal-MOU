# 일일 작업 결산

> 작성일: 2026-08-11
> 작성자: 이덕진

---

## 1. 오늘 작업 내용

### 작업 1. NPC Home 복귀 상태 및 정책 정리

**작업 내용**

- 구현한 기능:
  - NPC 시작 상태 enum에 `Home` 추가
  - Gameplay Tag `State.NPC.Home` 추가
  - 타깃 상실 정책이 `ReturnHome`일 때 `State.NPC.Home`을 반환하도록 변경
  - Home 도착 후 NPC의 기본 시작 상태로 복귀하는 BT 구성 방향 정리
- 수정한 기능:
  - `OneShotThenTracking`을 에디터 선택 목록에서 숨김
  - 기존 에셋의 enum 직렬화 호환을 위해 내부 값은 유지
  - 기존 `OneShotThenTracking` 값은 행동 종료 후 `Stay`를 반환하도록 변경
- 제거한 기능:
  - NPC별 위치 지정에 부적합했던 `NPCData.HomePosition` 제거
  - 홈 위치는 레벨에 배치된 NPC BP의 Instance Editable 변수 또는 Target Point로 관리하도록 변경

**수정한 파일 및 에셋**

```text
Source/TeamProject_MOU/Public/Enum/NPCEnum.h
Source/TeamProject_MOU/Public/Data/NPCData.h
Source/TeamProject_MOU/Private/Data/NPCData.cpp
Config/DefaultGameplayTags.ini
/Game/01_LDJ/NPC/BT/BB_NPC_Base
/Game/01_LDJ/NPC/BT/BT_NPC_Base
```

**작업 결과**

- [x] 정상 동작
- [ ] 일부 동작
- [ ] 미완료
- [ ] 오류 발생

---

### 작업 2. AI Perception 타깃 상실 처리 개선

**작업 내용**

- 구현한 기능:
  - 순간적인 시야 이탈과 완전한 감각 정보 제거를 구분
  - `On Target Perception Updated`의 감지 실패 시 타깃을 즉시 제거하지 않도록 구조 변경
  - `On Target Perception Forgotten`에서 최종 타깃 제거 및 복귀 정책 처리
- 주요 변경 사항:
  - Forgotten Actor와 현재 Blackboard Target을 비교한 후 동일한 대상일 때만 Target 제거
  - 타깃 상실 후 `ShouldReturnHomeOnLostTarget` 결과에 따라 Home 또는 기본 상실 정책 상태 적용
  - AI Sight의 `Max Age`, `Forget Stale Actors`, 시야각 설정 관계 확인

**수정한 파일 및 에셋**

```text
/Game/01_LDJ/NPC/BP/AI/BP_NPCAI
```

**작업 결과**

- [x] 정상 동작
- [ ] 일부 동작
- [ ] 미완료
- [ ] 오류 발생

---

### 작업 3. NPC 정찰 타입 확장

**작업 내용**

- 구현한 정찰 타입:
  - `RandomRadius`: NPC 주변 지정 반경 안에서 랜덤 정찰
  - `Spline`: 지정한 스플라인을 순차적으로 따라가는 정찰
  - `Area`: 지정한 Box 영역 내부에서만 정찰
- Data Asset 확장:
  - `ENPCPatrolType` enum 추가
  - `PatrolType` 추가
  - `PatrolRadius` 추가
  - `SplinePatrolRadius` 추가
  - 선택한 정찰 타입에 필요한 값만 Details에 표시하도록 Edit Condition 적용
- Blackboard 및 BT 구성:
  - Blackboard에 `PatrolType` Native Enum 키 추가
  - Patrol Selector를 반경, 스플라인, 영역 시퀀스로 분리
  - 초기화 시 `NPCData.PatrolType`을 Blackboard에 저장

**수정한 파일 및 에셋**

```text
Source/TeamProject_MOU/Public/Enum/NPCEnum.h
Source/TeamProject_MOU/Public/Data/NPCData.h
Source/TeamProject_MOU/Private/Data/NPCData.cpp
/Game/01_LDJ/NPC/BT/BB_NPC_Base
/Game/01_LDJ/NPC/BT/BT_NPC_Base
```

**작업 결과**

- [x] 정상 동작
- [ ] 일부 동작
- [ ] 미완료
- [ ] 오류 발생

---

### 작업 4. 스플라인 정찰 BTT 구현

**작업 내용**

- 구현한 기능:
  - `BTT_FindSplinePatrolPoint` 제작
  - NPC별 `PatrolSplineActor` 지정
  - `SplineDistance`, `SplineDirection`, `SplineStepDistance`를 이용해 스플라인 위 다음 지점 계산
  - Closed Loop 스플라인은 거리 Wrap 처리
  - Open Spline은 끝점 도달 시 방향을 반전해 왕복 이동
  - 계산된 스플라인 위치를 NavMesh에 투영한 후 `PatrolLocation`에 저장
- 수정한 기능:
  - 스플라인 위 랜덤 지점으로 최단 경로 이동하던 구조를 순차 지점 이동 구조로 변경
  - 최종 Wrap 결과가 NPC의 `SplineDistance`에 저장되도록 수정
  - Navigation 투영 실패 시 BTT가 실패하도록 분기 추가

**수정한 파일 및 에셋**

```text
/Game/01_LDJ/NPC/BT/BTT/BTT_FindSplinePatrolPoint
/Game/01_LDJ/NPC/BP/Patrol 스플라인 Actor
/Game/01_LDJ/NPC/BP/NPCs/BP_Base_NPC
```

**작업 결과**

- [x] 정상 동작
- [ ] 일부 동작
- [ ] 미완료
- [ ] 오류 발생

---

### 작업 5. 영역 정찰 BTT 구현

**작업 내용**

- 구현한 기능:
  - Box Collision 기반 `BP_PatrolBound` 제작
  - NPC별 `PatrolBoundActor` 지정
  - Box Component Bounds 내부에서 랜덤 목적지 생성
  - 생성한 위치를 NavMesh에 투영해 `PatrolLocation`에 저장
  - Actor, Area, Navigation 투영 실패 경로에서 `Finish Execute(false)` 처리
- 주요 설정:
  - `Project Point to Navigation`의 Query Extent를 이용해 랜덤 좌표 주변 NavMesh 검색
  - 회전된 Box의 월드 Bounds 오차를 피하기 위해 현재 영역 Actor는 회전 없이 사용하는 방향으로 구성

**수정한 파일 및 에셋**

```text
/Game/01_LDJ/NPC/BT/BTT/BTT_FindAreaPatrolPoint
/Game/01_LDJ/NPC/BP/Patrol/BP_PatrolBound
/Game/01_LDJ/NPC/BP/NPCs/BP_Base_NPC
```

**작업 결과**

- [ ] 정상 동작
- [x] 일부 동작
- [ ] 미완료
- [ ] 오류 발생

---

### 작업 6. GAS 기반 상태이상 데이터 구조 구현

**작업 내용**

- 상태이상 종류 추가:
  - 실명
  - 스턴
  - 감전
  - 도트류
  - Slow
  - 넘어짐
  - 공포
  - 도발
- `StatusEffectDataAsset` 구현:
  - 공통 값: 애니메이션 몽타주, 지속시간
  - Slow 전용 값: 이동속도 배율
  - 도트류 전용 값: 틱당 피해량, 틱 간격
- `StatusEffectAbilitySetDataAsset` 구현:
  - 자동 지급할 상태이상 GA 목록
  - Ability Level
  - 유닛별 상태이상 Data Asset 목록
- 유닛별 설정 조회:
  - `StatusComponent.GetStatusEffectData(EffectType)` 추가
  - 공용 GA가 Owner의 상태이상 Data Asset에서 지속시간과 몽타주를 읽을 수 있도록 구성

**추가한 파일**

```text
Source/TeamProject_MOU/Public/StatusEffect/StatusEffectDataAsset.h
Source/TeamProject_MOU/Private/StatusEffect/StatusEffectDataAsset.cpp
Source/TeamProject_MOU/Public/StatusEffect/StatusEffectAbilitySetDataAsset.h
Source/TeamProject_MOU/Private/StatusEffect/StatusEffectAbilitySetDataAsset.cpp
```

**작업 결과**

- [x] 정상 동작
- [ ] 일부 동작
- [ ] 미완료
- [ ] 오류 발생

---

### 작업 7. StatusComponent GAS 기반 통합

**작업 내용**

- 기존 `StatusComponent`와 신규 상태이상 컴포넌트 역할 통합
- 기존 자체 `ActiveStatusTags` 컨테이너 및 별도 복제 로직 제거
- 상태 태그의 보관과 복제를 ASC 및 Gameplay Effect가 담당하도록 변경
- Owner ASC 자동 탐색 및 캐싱
- `AbilitySetData`에 등록된 상태이상 GA를 BeginPlay에서 서버 권한으로 자동 지급
- 동일한 GA가 이미 있는 경우 중복 지급 방지
- 기존 코드 호환을 위해 `AddStatusTag`, `RemoveStatusTag`는 Deprecated ASC Loose Tag 래퍼로 유지
- `HasStatusTag`, `CanMove`, `CanAct`는 ASC가 가진 태그를 직접 조회하도록 변경

**수정한 파일**

```text
Source/TeamProject_MOU/Public/Components/StatusComponent.h
Source/TeamProject_MOU/Private/Components/StatusComponent.cpp
```

**작업 결과**

- [x] 정상 동작
- [ ] 일부 동작
- [ ] 미완료
- [ ] 오류 발생

---

### 작업 8. 스턴 Gameplay Ability 구조 설계 및 태그 연동

**작업 내용**

- 사용 태그 정리:
  - `Event.StatusEffect.Stun`: 공용 스턴 GA 실행 이벤트
  - `Data.Duration.Stun`: Set By Caller 스턴 지속시간
  - `State.Primary.Stuned`: 스턴 적용 중 상태
- 코드 태그 변경:
  - 기존 `State.Stunned` 참조를 `State.Primary.Stuned`로 변경
  - `StatusComponent.CanMove`, `CanAct` 판정 변경
  - `MainAnimInstance` 스턴 애니메이션 판정 변경
- GA/GE 흐름 설계:
  - Gameplay Event 수신으로 `GA_Stun` 자동 실행
  - Owner의 `StatusComponent.GetStatusEffectData(Stun)`으로 유닛별 데이터 조회
  - `Data.Duration.Stun` Set By Caller 값으로 GE 지속시간 전달
  - 유닛별 스턴 루프 몽타주 재생
  - GE 제거 시 GA 종료 및 몽타주 정지
  - GE의 `MoveSpeed × 0` Modifier를 사용해 플레이어와 NPC를 공통으로 이동 제한

**수정한 파일 및 에셋**

```text
Config/DefaultGameplayTags.ini
Source/TeamProject_MOU/Private/Components/StatusComponent.cpp
Source/TeamProject_MOU/Private/Animation/MainAnimInstance.cpp
/Game 상태이상 GA/GE/Data Asset
```

**작업 결과**

- [ ] 정상 동작
- [x] 일부 동작
- [ ] 미완료
- [ ] 오류 발생

---

## 2. 테스트 결과

### 프로젝트 기본 테스트

- [x] Unreal Header Tool 실행 성공
- [x] C++ 에디터 타깃 빌드 성공
- [x] 신규 Data Asset 클래스 에디터 노출 확인 가능 상태
- [x] 기존 CharacterBase 및 StatusComponent 참조 컴파일 성공
- [ ] 전체 블루프린트 컴파일 확인
- [ ] 멀티플레이 환경 상태이상 GA 지급 및 GE 복제 확인

### 기능 테스트

| 테스트 항목 | 결과 | 비고 |
|---|---|---|
| ReturnHome 상태 태그 반환 | 성공 | `State.NPC.Home` 반환 |
| OneShot Tracking 제거 | 성공 | 에디터 숨김 및 기존 값 Stay 처리 |
| AI 타깃 Forgotten 처리 | 성공 | 현재 Target과 동일할 때만 제거 |
| 반경 정찰 | 성공 | 기존 정찰 방식 유지 |
| 스플라인 순차 정찰 | 성공 | Loop 및 왕복 거리 처리 |
| 영역 Bounds 랜덤 위치 생성 | 일부 성공 | 실제 NavMesh 이동 반복 테스트 필요 |
| 상태이상 Data Asset C++ 빌드 | 성공 | UHT 및 Editor 빌드 통과 |
| 상태이상 GA 자동 지급 코드 | 성공 | 런타임 실제 지급 확인 필요 |
| 스턴 GE 및 루프 몽타주 | 진행 중 | BP GA/GE 최종 연결 필요 |

---

## 3. 발생한 문제

### 문제 1. ReturnHome이 실제 Home 위치로 이동하지 않음

**문제 내용**

```text
ReturnHome 정책이 선택되어도 기존 구현은 Patrol 또는 Stay 태그만 반환하고,
Home 위치를 Blackboard 이동 목표로 사용하지 않았다.
```

**확인된 원인**

```text
Home 전용 상태와 BT 시퀀스가 없었으며 Data Asset의 HomePosition도 실제 BT에서 사용되지 않았다.
```

**해결 방법**

```text
State.NPC.Home 상태를 추가하고 Home 전용 Move To 시퀀스를 구성했다.
NPC별 Home 위치는 Data Asset이 아니라 레벨 BP 인스턴스에서 지정하도록 변경했다.
```

**현재 상태**

- [x] 해결 완료
- [ ] 임시 해결
- [ ] 미해결
- [ ] 추가 확인 필요

---

### 문제 2. NPC 주변 이동 시 타깃이 순간적으로 해제됨

**문제 내용**

```text
플레이어가 NPC 주변 또는 시야 경계를 이동할 때 Successfully Sensed가 false가 되며
Blackboard Target이 즉시 제거됐다.
```

**확인된 원인**

```text
현재 감지 실패와 감각 정보의 완전한 만료를 동일하게 처리하고 있었다.
```

**해결 방법**

```text
감지 성공 시에만 Target을 설정하고, 최종 제거는 On Target Perception Forgotten에서 처리했다.
Forgotten Actor와 현재 Target 비교도 추가했다.
```

**현재 상태**

- [x] 해결 완료
- [ ] 임시 해결
- [ ] 미해결
- [ ] 추가 확인 필요

---

### 문제 3. 스플라인 정찰이 스플라인을 따라가지 않고 최단 경로로 이동함

**문제 내용**

```text
스플라인의 랜덤 좌표 하나만 목적지로 설정해 NPC가 스플라인 곡선을 무시하고
NavMesh 최단 경로로 이동했다.
```

**확인된 원인**

```text
스플라인 위 지점을 순차적으로 이동하지 않고 매번 독립적인 랜덤 목적지를 사용했다.
```

**해결 방법**

```text
SplineDistance와 StepDistance를 이용해 다음 지점을 순차 계산하고,
Closed Loop Wrap 및 Open Spline 왕복 방향 전환을 구현했다.
```

**현재 상태**

- [x] 해결 완료
- [ ] 임시 해결
- [ ] 미해결
- [ ] 추가 확인 필요

---

### 문제 4. 플레이어와 NPC의 스턴 이동 제한 방식이 다름

**문제 내용**

```text
MainCharacter의 입력 차단은 플레이어에게만 적용되며 NPC의 AI Move To에는 적용되지 않는다.
```

**확인된 원인**

```text
입력 함수 기반 CanMove 판정은 NPC 이동 컴포넌트와 BT 이동을 직접 제한하지 않는다.
```

**해결 방법**

```text
GE_Stun에서 MoveSpeed Attribute를 0배로 적용해 CharacterBase 기반 플레이어와 NPC를 공통 제한한다.
GE 만료 시 GAS Modifier가 제거되면서 원래 속도로 자동 복구한다.
```

**현재 상태**

- [ ] 해결 완료
- [x] 임시 해결
- [ ] 미해결
- [x] 추가 확인 필요

---

## 4. 미완료 작업

- [x] `GA_Stun`의 Gameplay Event Trigger 최종 구성
- [x] `GE_Stun`의 `Data.Duration.Stun` Set By Caller 연결
- [x] `GE_Stun`의 `State.Primary.Stuned` Granted Tag 적용
- [x] `GE_Stun`의 MoveSpeed Modifier 적용 및 NPC 이동 테스트
- [x] 유닛별 스턴 Data Asset 및 몽타주 지정
- [x] GE 제거 이벤트와 루프 몽타주 종료 연결
- [x] 영역 정찰이 회전된 Box 또는 복층 NavMesh에서 영역을 벗어나지 않는지 확인
- [x] 상태이상 GA 자동 지급 및 멀티플레이 복제 확인

**미완료 사유**

```text
상태이상 C++ 기반 구조와 데이터 조회 기능까지 구현했으며,
현재 GA_Stun과 GE_Stun의 블루프린트 노드 연결 및 런타임 검증 단계가 남아 있다.
영역 정찰도 기본 Bounds 위치 생성은 완료했으나 실제 반복 이동 테스트가 추가로 필요하다.
```

---

## 5. 다음 작업 계획

1. `GA_Stun`에서 Owner의 `GetStatusEffectData(Stun)` 결과 유효성 확인
2. Data Asset Duration을 `Data.Duration.Stun` Set By Caller 값으로 GE에 전달
3. 유닛별 루프 몽타주 재생 및 GE 제거 시 종료 처리
4. `State.Primary.Stuned`와 MoveSpeed Modifier의 플레이어/NPC 공통 동작 테스트
5. 상태이상 GA 자동 지급 및 멀티플레이 복제 테스트
6. 영역 정찰 반복 이동과 NavMesh Projection 실패 경로 테스트

---

## 6. 팀 공유 내용

### 새로 추가한 항목

- C++ Data Asset:
  - `StatusEffectDataAsset`
  - `StatusEffectAbilitySetDataAsset`
- NPC 정찰 타입:
  - `RandomRadius`
  - `Spline`
  - `Area`
- Gameplay Tag:
  - `State.NPC.Home`
  - `Event.StatusEffect.Stun`
  - `Data.Duration.Stun`
  - `State.Primary.Stuned`

### 변경한 항목

- `StatusComponent`는 자체 태그 저장소 대신 ASC 기반으로 동작
- 상태이상 GA는 `AbilitySetData`에서 자동 지급
- 상태이상 공용 GA는 유닛의 Data Asset에서 지속시간과 몽타주 조회
- NPC 정찰은 Data Asset의 Patrol Type에 따라 BT 시퀀스 분기
- ReturnHome은 전용 Home 상태를 사용

### 추가 확인 필요 사항

```text
상태이상은 GA가 실행을 담당하고 GE가 상태 태그, 지속시간, Attribute Modifier를 담당한다.
GameplayCue는 상태이상 연출을 담당하도록 역할을 분리한다.
기존 AddStatusTag/RemoveStatusTag는 호환용 Deprecated 함수이므로 신규 상태이상에서는 사용하지 않는다.
```

### 작업 요약

```text
오늘은 NPC의 Home 복귀와 AI 타깃 상실 처리를 정리하고,
정찰 시스템을 반경, 스플라인, 영역 세 종류로 확장했다.
스플라인 순차 정찰과 Box Bounds 기반 영역 정찰 BTT를 구성했으며,
상태이상 시스템을 GAS 중심의 Data Asset, Ability Set, StatusComponent 구조로 통합했다.
현재 스턴 상태이상을 기준으로 유닛별 지속시간과 몽타주를 공용 GA에서 조회하는 구조까지 완료했고,
다음 작업에서는 GA/GE/GameplayCue 블루프린트 연결과 실제 런타임 검증을 진행할 예정이다.
```
