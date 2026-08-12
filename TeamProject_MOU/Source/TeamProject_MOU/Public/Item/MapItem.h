#pragma once

#include "CoreMinimal.h"
#include "Base/ItemBase.h"
#include "MapItem.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UUserWidget;

UCLASS()
class TEAMPROJECT_MOU_API AMapItem : public AItemBase
{
	GENERATED_BODY()

public:
	AMapItem();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
#pragma region [CAP] Top-down 캡처 카메라
	// 지도에 부착된 하향 캡처 카메라 (소지 플레이어 위를 비춤)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Map|Capture")
	TObjectPtr<USceneCaptureComponent2D> CaptureCamera;

	// 캡처 카메라 높이 (소지 플레이어 위 cm)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Capture")
	float CaptureHeight = 3000.0f;

	// 직교 투영 폭 (한 번에 담는 가로 범위 cm)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Capture")
	float CaptureOrthoWidth = 4096.0f;

	// 실시간 캡처 결과 렌더타깃 (에디터 지정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Capture")
	TObjectPtr<UTextureRenderTarget2D> CaptureRT;
#pragma endregion

#pragma region [FOG] 지도별 누적 Fog 마스크
	// 이 지도의 밝힘 기록 (지도 아이템에 귀속, 맵 전체 크기 고정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Fog")
	TObjectPtr<UTextureRenderTarget2D> FogMaskRT;

	// 마스크에 밝힘 원을 그리는 머티리얼 (에디터 지정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Fog")
	TObjectPtr<UMaterialInterface> FogBrushMaterial;

	// 소지 플레이어 주변 밝힘 반경 (cm)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Fog")
	float RevealRadius = 800.0f;

	// Fog 마스크 갱신 주기 (초). Tick 대신 타이머로 갱신
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Fog")
	float FogUpdateInterval = 0.15f;

	// 맵 월드 원점(좌하단 XY) — 좌표→UV 변환 기준
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Fog")
	FVector2D MapWorldOrigin = FVector2D::ZeroVector;

	// 맵 전체 크기 (cm) — 좌표→UV 변환 기준
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Fog")
	FVector2D MapWorldSize = FVector2D(20000.0f, 20000.0f);
#pragma endregion

#pragma region [MAP] 소지/갱신/토글
	// 이 지도를 현재 소지 중인 플레이어 (OnEquipped 기준으로 세팅)
	UPROPERTY(BlueprintReadOnly, Category = "Map|State")
	TObjectPtr<AActor> HoldingPlayer;

	// 지도 오버레이 위젯 클래스 (에디터 지정, WBP)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|UI")
	TSubclassOf<UUserWidget> MapWidgetClass;

	// [MAP-001] 좌클릭: 전체 지도 오버레이 켜기/끄기 토글
	virtual void OnUse_Implementation() override;

	// [MAP-002] 손에 들었을 때: 소지자 세팅 + Fog 갱신 타이머 시작
	virtual void OnEquipped_Implementation(AActor* Equipper) override;

	// [MAP-003] 손에서 해제될 때: 소지자 해제 + 타이머 정지
	virtual void OnUnequipped_Implementation(AActor* Equipper) override;

	// [MAP-004] 월드 좌표 → 지도 UV(0~1). 위젯 마커용
	UFUNCTION(BlueprintCallable, Category = "Map")
	FVector2D WorldToMapUV(const FVector& WorldLocation) const;
#pragma endregion

private:
	// [MAP-005] 레벨의 MapBounds 태그 볼륨을 찾아 Origin/Size 자동 설정
	void ResolveMapBoundsFromVolume();

	// [CAP-001] 캡처 카메라를 소지 플레이어 위로 이동/정렬
	void UpdateCaptureTransform();

	// [FOG-001] 소지 플레이어 위치를 Fog 마스크에 누적으로 밝힘
	void UpdateFogMask();

	// 지도 오버레이 위젯 인스턴스 (토글 상태 추적)
	UPROPERTY()
	TObjectPtr<UUserWidget> MapWidgetInstance;

	// Fog 브러시 동적 머티리얼 인스턴스
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> FogBrushMID;

	// Fog 주기 갱신 타이머 핸들
	FTimerHandle FogUpdateTimerHandle;

	// 오버레이 열림 상태
	bool bMapOpen = false;
};
