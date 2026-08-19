// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TeamProject_MOUPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;
class ULoginWidgetBase;

/**
 *  Basic PlayerController class for a third person game
 *  Manages input mappings
 */
UCLASS(abstract)
class ATeamProject_MOUPlayerController : public APlayerController
{
	GENERATED_BODY()
	
protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/**
	 * PIE/게임 시작 시 채팅 로그인 화면을 자동으로 띄울지.
	 *
	 * 이미 로그인되어 있으면(예: 방장이 방을 만들고 리슨서버로 여행해온 경우)
	 * 다시 묻지 않는다 — ShowLoginWidgetIfNeeded() 가 UChatSubsystem 의 연결 상태로 판단한다.
	 */
	UPROPERTY(EditAnywhere, Category = "MOU|Chat")
	bool bAutoShowLoginWidget = true;

	/** 자동으로 띄울 로그인 위젯 클래스. 비워두면 ULoginWidgetBase 의 C++ 기본 레이아웃을 쓴다. */
	UPROPERTY(EditAnywhere, Category = "MOU|Chat")
	TSubclassOf<ULoginWidgetBase> LoginWidgetClass;

	/** 로그인 위젯의 ServerHost/ServerPort 로 그대로 넘어간다. */
	UPROPERTY(EditAnywhere, Category = "MOU|Chat")
	FString ChatServerHost = TEXT("127.0.0.1");

	UPROPERTY(EditAnywhere, Category = "MOU|Chat")
	int32 ChatServerPort = 9000;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

private:
	/** bAutoShowLoginWidget 이 켜져 있고 아직 로그인 전이면 로그인 위젯을 띄운다. */
	void ShowLoginWidgetIfNeeded();

};
