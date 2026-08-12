# NPC 멀티플레이 전환 체크리스트

> 작성일: 2026-08-12  
> 대상: `/Game/01_LDJ/NPC`, `CharacterBase`, NPC GAS/BT/Perception/Grab 시스템

---

## 1. 현재 확인된 기반 상태

### 이미 서버 대응이 들어간 부분

- [x] `CharacterBase`의 ASC가 Replicated로 설정됨
- [x] AttributeSet의 주요 Attribute가 `ReplicatedUsing`으로 복제됨
- [x] 초기 Ability 지급이 `HasAuthority()`에서만 실행됨
- [x] `StatusComponent`의 상태이상 GA 자동 지급이 서버에서만 실행됨
- [x] CC 진입 시 Action GA 취소가 서버에서만 실행됨
- [x] `GrabFollowComponent`의 Carrier, Socket, Offset, Rotation 값이 복제됨
- [x] `State.Held` Loose Gameplay Tag 추가 및 제거가 서버에서만 실행됨

### 우선 확인 또는 수정이 필요한 부분

- [ ] 모든 NPC BP의 `Replicates` 활성화
- [ ] 모든 NPC BP의 `Replicate Movement` 활성화
- [ ] NPC GA의 Net Execution Policy를 `Server Only` 또는 `Server Initiated`로 통일
- [ ] `StartGrabFollow`, `StopGrabFollow`가 반드시 서버에서만 호출되도록 보장
- [ ] Grab Follow 위치 갱신을 서버와 클라이언트가 동시에 덮어쓰지 않도록 정책 결정
- [ ] Launch, Impulse, Gameplay Event, GE 적용이 서버에서 실행되는지 확인
- [ ] Dedicated Server에서는 AIController/BT/Perception만 실행되고 클라이언트는 결과만 받는지 확인

---

## 2. NPC Actor 및 AI 기본 설정

각 NPC Blueprint의 Class Defaults에서 확인한다.

- [ ] `Replicates = true`
- [ ] `Replicate Movement = true`
- [ ] `Net Load on Client = true`
- [ ] `Net Dormancy`가 이동 및 상태 변화에 방해되지 않는 기본값인지 확인
- [ ] `Net Cull Distance Squared`가 게임의 최대 NPC 관찰 거리보다 충분히 큰지 확인
- [ ] `AI Controller Class = BP_NPCAI`
- [ ] `Auto Possess AI = Placed in World or Spawned`

주의:

```text
AIController, AIPerception, Blackboard, Behavior Tree 판단은 서버에서만 수행하는 것이 정상이다.
클라이언트에서 Blackboard 값이나 BT 실행 상태가 보이지 않아도 오류가 아니다.
클라이언트는 NPC Actor의 위치, 애니메이션, GAS 상태 및 연출 결과를 복제로 받아야 한다.
```

---

## 3. BP_NPCAI, Perception 및 Target 선정

- [ ] `On Target Perception Updated`에서 Target 설정 및 `SetNPCState`가 서버에서만 실행되는지 확인
- [ ] `On Target Perception Forgotten`에서 Target 제거 및 LostTarget 정책 적용이 서버에서만 실행되는지 확인
- [ ] `OnCrowdControlChanged` 바인딩과 가장 가까운 플레이어 재선정이 서버 AIController에서만 실행되는지 확인
- [ ] `Get Player Character(0)` 또는 `Get Player Controller(0)`를 Target 판정에 사용하지 않음
- [x] `Character.Player` Gameplay Tag를 사용해 플레이어 후보 필터링
- [x] 여러 플레이어가 보일 때 가장 가까운 플레이어를 선택
- [ ] 현재 Target이 제거 또는 로그아웃됐을 때 다음 Target을 정상적으로 다시 선택
- [ ] 죽었거나 관전 중인 플레이어를 후보에서 제외할 조건 추가
- [ ] 팀 시스템 도입 시 적대 플레이어만 후보로 필터링

멀티플레이에서 금지할 패턴:

```text
Get Player Character(0)
Get Player Controller(0)
로컬 플레이어 기준 거리 및 상태 판정
```

대신 Perception이 넘겨준 Actor 또는 현재 감지 Actor 배열을 기준으로 처리한다.

---

## 4. Behavior Tree와 Blackboard

- [x] `IsCrowdControlled`를 통해 CC 중 일반 BT 시퀀스 실행 차단
- [ ] Blackboard 값은 게임 상태의 원본으로 복제하려 하지 않음
- [ ] `State`, `Target`, `IsActionRange`, `IsCrowdControlled` 변경은 서버에서만 수행
- [ ] BTT에서 NPC 위치, 속도, 상태를 직접 변경하는 경우 서버 실행인지 확인
- [ ] Latent BTT가 중단될 때 `Finish Execute` 또는 Abort 정리가 정상 실행되는지 확인
- [ ] Action BTT가 GA 완료 전에 종료되지 않도록 Ability 종료 신호 대기
- [ ] CC로 Action GA가 취소됐을 때 BTT도 Running 상태에서 정상 해제되는지 확인

Blackboard는 클라이언트에 복제되지 않는다. 클라이언트 UI나 연출에 필요한 상태는 다음 중 하나로 전달한다.

```text
Replicated Gameplay Tag / Gameplay Effect
Replicated Actor 프로퍼티
Gameplay Cue
필요한 경우 OnRep 또는 Multicast 연출 이벤트
```

---

## 5. Gameplay Ability 및 Gameplay Effect

아래 NPC GA를 에셋별로 확인한다.

```text
GA_Grab
GA_Grabed
GA_Throw / GA_Thrown
GA_Push
GA_Pushed
GA_Stund
GA_Electric
```

공통 체크:

- [ ] `Net Execution Policy = Server Only` 또는 `Server Initiated`
- [ ] NPC AI 행동 GA를 `Local Predicted`로 사용하지 않음
- [ ] Gameplay Event를 보내는 주체가 서버인지 확인
- [ ] GE 생성, 적용, 제거가 서버 권한에서 실행되는지 확인
- [ ] Ability 취소 및 `EndAbility`가 서버에서 확정됨
- [ ] Ability의 Instancing Policy가 보유하는 대상 참조와 충돌하지 않는지 확인
- [ ] `On End Ability` 정리 함수가 정상 종료와 Cancel 양쪽에서 한 번만 안전하게 실행됨
- [ ] 저장한 Actor 참조를 Ability 종료 시 초기화
- [ ] GameplayCue로 처리할 연출과 서버 게임 로직을 분리

권장 정책:

| Ability 유형 | 권장 Net Execution Policy |
|---|---|
| NPC가 스스로 실행하는 Grab, Push, Throw | Server Only |
| NPC에게 적용되는 Stun, Electric 반응 | Server Initiated 또는 Server Only |
| 플레이어 입력으로 시작하는 예측 필요 행동 | 별도 검토 후 Local Predicted |

---

## 6. Grab 및 Throw

### 현재 코드에서 확인된 위험 지점

`GrabFollowComponent.StartGrabFollow()`와 `StopGrabFollow()`에는 자체 `HasAuthority()` 차단이 없다.
Blueprint GA가 클라이언트에서도 실행되면 복제 프로퍼티를 클라이언트가 로컬로 먼저 변경할 수 있다.

- [ ] Start/Stop 노드가 서버 실행 GA에서만 호출되는지 확인
- [ ] 안전성을 높이려면 C++ Start/Stop 함수 내부에도 Authority Guard 추가
- [ ] `CarrierCharacter`가 서버에서 설정되고 클라이언트는 `OnRep_GrabState`로 반응하는지 확인
- [ ] 잡는 주체와 잡힌 대상 모두 `Replicates`, `Replicate Movement` 활성화
- [ ] Grab 시작과 종료가 늦게 접속한 클라이언트에도 올바르게 보이는지 확인
- [ ] Grab 중 Carrier가 파괴 또는 로그아웃될 때 자동 `StopGrabFollow` 처리
- [ ] 잡힌 대상이 죽거나 CC에 걸릴 때 해제 정책 확인
- [ ] Grab 종료 후 Movement Mode, Collision, Gravity가 원복되는지 확인

### 위치 동기화 정책 결정 필요

현재 `GrabFollowComponent`는 서버와 클라이언트 Tick에서 모두 `SetActorLocationAndRotation()`을 실행한다.

선택지 A — 서버 권위 이동:

```text
서버 Tick만 Grab 위치 갱신
Character Movement 복제로 클라이언트에 전달
장점: 권위와 충돌 판정이 명확함
단점: 지연 환경에서 소켓 추적이 약간 떨릴 수 있음
```

선택지 B — 서버 권위 + 클라이언트 시각 보간:

```text
캡슐과 실제 Actor 위치는 서버가 결정
클라이언트는 Mesh Offset 또는 별도 시각 컴포넌트만 보간
장점: 판정과 화면 움직임을 모두 안정화 가능
단점: 구현량이 증가함
```

- [ ] 최소 구현 단계에서는 서버 Tick만 위치 갱신하는 방식으로 테스트
- [ ] 네트워크 지연에서 떨림이 확인되면 클라이언트 Mesh 보간 추가

Throw 체크:

- [ ] `StopGrabFollow`가 먼저 서버에서 처리됨
- [ ] `LaunchCharacter` 또는 Impulse가 서버에서 한 번만 실행됨
- [ ] 클라이언트에서 동일 Launch를 중복 실행하지 않음
- [ ] Throw 직후 Character Movement 복제로 궤적이 모든 클라이언트에 동일하게 표시됨
- [ ] 충돌 및 피해 판정은 서버에서만 처리

---

## 7. StatusComponent와 CC

- [x] 상태이상 GA 지급은 서버 전용
- [x] CC 진입 시 `Ability.NPC.Action` 취소는 서버 전용
- [x] `State.Primary.Stuned`는 GE 기반으로 관리
- [ ] `MovementBlockingCCTags`가 모든 NPC 자식 BP에 올바르게 상속됨
- [ ] `OnCrowdControlChanged`의 AI 상태 변경 로직은 서버에서만 실행
- [ ] CC GE Duration이 서버 시간 기준으로 적용됨
- [ ] 중첩 CC에서 하나가 먼저 끝나도 마지막 CC가 남아 있으면 BT 잠금 유지
- [ ] 마지막 CC 제거 시 한 번만 해제 이벤트 발생
- [ ] 스턴 몽타주가 서버와 모든 클라이언트에서 같은 시점에 시작 및 종료
- [ ] CC 도중 Target 상실 이벤트가 서버 State에 반영됨
- [x] CC 해제 시 현재 감지 플레이어를 기준으로 Tracking 또는 LostTarget 정책 선택

주의할 코드:

```text
CharacterBase의 Attribute 변경 콜백은 서버와 클라이언트 양쪽에서 호출될 수 있다.
UpdateEncumbranceState에서 Loose Gameplay Tag를 변경하는 로직은 서버 Authority Guard를 추가하거나
GE 기반 상태로 전환해야 중복 및 클라이언트 로컬 태그 불일치를 방지할 수 있다.
```

- [ ] `UpdateEncumbranceState()`의 Loose Tag 변경을 서버 전용으로 제한

---

## 8. 애니메이션 및 연출

- [ ] GA 몽타주가 ASC를 통해 정상적으로 복제되는지 2클라이언트에서 확인
- [ ] Anim Blueprint가 서버 전용 Blackboard 값을 직접 읽지 않음
- [ ] Anim BP는 복제된 Gameplay Tag, Attribute, Velocity 또는 Replicated 프로퍼티 사용
- [ ] Cosmetic 전용 효과는 GameplayCue 또는 Multicast로 처리
- [ ] Dedicated Server에서 Mesh 또는 AnimInstance가 없더라도 게임 로직이 실패하지 않음
- [ ] Anim Notify가 피해, Grab, Launch 같은 서버 게임 로직을 실행한다면 서버 권한 확인
- [ ] Anim Notify가 각 클라이언트에서 실행돼 Gameplay Event를 중복 전송하지 않는지 확인

특히 `ANS_COL`처럼 충돌 판정을 여는 Notify State는 서버에서만 실제 판정을 수행하고,
클라이언트는 시각 효과만 수행하는지 확인한다.

---

## 9. 스폰, 파괴 및 레벨 이동

- [ ] NPC Spawn은 서버에서만 실행
- [ ] 서버에서 Spawn한 NPC에 `Owner`를 잘못된 PlayerController로 지정하지 않음
- [ ] NPC 파괴 및 사망 처리는 서버에서 실행
- [ ] NPC 사망 시 실행 중 GA, GE, Grab, BT를 정리
- [ ] NPC가 파괴되면 Target으로 잡고 있던 다른 AI가 참조를 제거
- [ ] Seamless Travel 또는 레벨 재시작 시 Ability가 중복 지급되지 않음
- [x] Ability 자동 지급 코드에 동일 클래스 중복 방지 존재

---

## 10. 권장 멀티플레이 테스트 순서

### PIE 기본 설정

```text
Number of Players: 2 이상
Net Mode 1차: Play As Listen Server
Net Mode 2차: Play As Client + Dedicated Server
```

### 테스트 시나리오

1. [ ] 서버와 클라이언트 양쪽에서 동일 NPC 위치와 이동 확인
2. [ ] NPC가 서버 플레이어를 감지하고 추적
3. [ ] NPC가 원격 클라이언트 플레이어를 감지하고 추적
4. [ ] 두 플레이어가 동시에 보일 때 가장 가까운 플레이어 선택
5. [ ] 현재 Target이 시야 밖으로 나가면 다른 플레이어로 Target 변경
6. [ ] 감지 플레이어가 없으면 Data Asset의 LostTarget 정책 실행
7. [ ] NPC Grab, Hold, Release가 모든 화면에서 동일하게 표시
8. [ ] Grab 도중 NPC에 스턴 적용 시 모든 화면에서 즉시 해제
9. [ ] Push 및 Throw 궤적이 서버와 클라이언트에서 일치
10. [ ] 스턴 적용, 지속시간, 해제, 몽타주가 모든 화면에서 일치
11. [ ] CC 해제 시 가장 가까운 현재 감지 플레이어를 재선정
12. [ ] 플레이어 접속 종료 중 Target 또는 Carrier 참조가 안전하게 정리
13. [ ] 100~150ms Packet Lag에서 Grab 위치 떨림과 Throw 궤적 확인
14. [ ] Packet Loss 환경에서 Reliable 이벤트 과다 사용 또는 누락 확인

### 콘솔 네트워크 모사 예시

```text
Net PktLag=100
Net PktLoss=5
```

테스트 후 반드시 값을 0으로 돌린다.

---

## 11. 우선순위 정리

### P0 — 멀티플레이 전환 전에 반드시 확인

- [ ] NPC Actor Replicates 및 Replicate Movement
- [ ] NPC GA Net Execution Policy
- [ ] Grab Start/Stop 서버 권한
- [ ] Launch, Impulse, GE, Gameplay Event 서버 단일 실행
- [ ] Dedicated Server에서 AI/BT/Perception 정상 작동

### P1 — 기능 테스트 중 반드시 확인

- [ ] Grab 위치 동기화 정책
- [ ] 스턴 및 Action 취소 복제
- [ ] Anim Notify 서버 권한
- [ ] 여러 플레이어 Target 선정 및 로그아웃 처리
- [ ] CC 중첩 및 해제 처리

### P2 — 폴리싱 단계

- [ ] 네트워크 지연 환경 Grab 보간
- [ ] GameplayCue 기반 상태이상 연출
- [ ] Net Cull Distance 및 Dormancy 최적화
- [ ] NPC 수 증가 시 Perception 및 BT 서버 성능 측정

---

## 12. 현재 코드 기준 우선 수정 후보

1. `GrabFollowComponent.StartGrabFollow()`와 `StopGrabFollow()`에 Authority Guard 추가
2. Grab Follow Tick의 위치 갱신을 서버 권위 방식으로 정리
3. 각 NPC GA의 Net Execution Policy 확인 및 서버 실행으로 통일
4. `CharacterBase.UpdateEncumbranceState()`의 Loose Tag 변경을 서버 전용으로 제한
5. `ANS_COL` 및 GA Notify 기반 충돌/이벤트 로직의 서버 중복 실행 여부 확인

