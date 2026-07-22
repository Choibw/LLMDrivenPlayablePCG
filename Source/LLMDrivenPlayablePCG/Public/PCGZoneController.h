// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PCGZoneController.generated.h"

class UPCGComponent;
class IHttpRequest;
class IHttpResponse;

UENUM(BlueprintType)
enum class EZonePathType : uint8
{
    Main,
    Branch,
    Shortcut
};

USTRUCT(BlueprintType)
struct FZonePathData
{
    GENERATED_BODY()

    // 길의 종류. 현재 프로토타입에서는 Main만 사용
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
    EZonePathType PathType = EZonePathType::Main;

    // Zone 내부 0~1 비율 좌표 기반의 Spline 제어점들
    // 예: (0.0, 0.5) = Zone 왼쪽 중앙, (1.0, 0.5) = Zone 오른쪽 중앙
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
    TArray<FVector2D> NormalizedPathPoints;

    // 길 폭. 현재는 확장용 데이터로 두고, 나중에 PCG Graph Parameter에 연결
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path", meta = (ClampMin = "0.0"))
    float PathWidth = 500.0f;
};

UENUM(BlueprintType)
enum class EZoneAreaType : uint8
{
    Spawn,
    Combat,
    Goal,
    Danger
};

USTRUCT(BlueprintType)
struct FZoneAreaData
{
    GENERATED_BODY()

    // 구역의 의미. 현재 프로토타입에서는 Spawn / Combat / Goal 중심으로 사용
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area")
    EZoneAreaType AreaType = EZoneAreaType::Combat;

    // Zone 내부 0~1 비율 좌표 기반의 구역 중심 위치
    // 예: (0.5, 0.5) = Zone 중앙, (0.1, 0.5) = Zone 왼쪽 중앙
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area")
    FVector2D NormalizedCenter = FVector2D(0.5f, 0.5f);

    // Zone 크기 기준의 구역 반지름 비율
    // 예: 0.1이면 Zone의 짧은 축 기준 약 10% 크기의 반지름으로 사용 가능
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float NormalizedRadius = 0.1f;

    // 구역 내부 오브젝트/디테일 배치 밀도. 현재는 확장용 데이터
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DetailDensity = 0.5f;
};

USTRUCT(BlueprintType)
struct FZoneGenerationData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone")
    FVector ZoneCenter = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone", meta = (ClampMin = "0.0"))
    FVector2D ZoneSize = FVector2D(10000.0f, 10000.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Data", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float TreeDensity = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Data", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float RockDensity = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Data", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float GrassDensity = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
    FZonePathData MainPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area")
    TArray<FZoneAreaData> Areas;
};

UCLASS()
class LLMDRIVENPLAYABLEPCG_API APCGZoneController : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	APCGZoneController();

public:	
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG")
    TObjectPtr<AActor> BaseEnvironmentPCGActor;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG")
    TObjectPtr<AActor> PathPCGActor;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area")
    TArray<TObjectPtr<AActor>> AreaPCGActors;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay Marker")
    TSubclassOf<AActor> SpawnPointClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay Marker")
    TSubclassOf<AActor> EnemySpawnPointClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay Marker")
    TSubclassOf<AActor> GoalPointClass;

    // 에디터에서 직접 바꿀 값들
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Data")
    FZoneGenerationData CurrentZoneData;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Backend")
    FString BackendUrl = TEXT("http://127.0.0.1:8000/generate-zone");

    // 테스트 임시변수/ 질문&답변
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Backend")
    FString TestNPCQuestion = TEXT("");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Backend")
    FString TestPlayerAnswer = TEXT("");

public:
    // 에디터/블루프린트에서 눌러서 적용할 함수
    // Zone 생성 전체 흐름을 시작하는 함수
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Zone Generation")
    void GenerateZone();

    // 테스트 임시함수
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Backend")
    void LoadSampleZoneJson();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Backend")
    void RequestZoneDataFromBackend();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Gameplay")
    void MovePlayerToSpawnPoint();

private:
    UPROPERTY()
    TObjectPtr<UPCGComponent> BaseEnvironmentPCGComponent;

    // 생성된 액터들을 기억해뒀다가 다음 생성 전에 지우기 위한 액터 배열
    UPROPERTY()
    TArray<TObjectPtr<AActor>> SpawnedGameplayMarkers;

    /*
    * ApplyGameplayMarkers()에서 Spawn Area 마커를 만들었을 때
    * 그 Actor를 따로 기억해두는 변수
    */
    UPROPERTY()
    TObjectPtr<AActor> SpawnPointActor;

private:
    void UpdateZoneBoundsFromBaseEnvironment();

    // CurrentZoneData의 기본 환경 밀도값을 Base Environment PCG에 적용하는 내부 함수
    void ApplyBaseEnvironmentPCG();

    // 현재 프로토타입에서는 PathPCGActor의 기존 Z 높이를 기준으로 길을 생성한다.
    // 추후에는 Landscape LineTrace와 테마별 PathHeightOffset을 사용해 길 에셋 높이를 보정할 예정이다.
    void ApplyPathPCG();

    void ApplyAreaPCG();

    bool ApplyZoneJsonString(const FString& JsonString);

    EZonePathType ParsePathType(const FString& PathTypeString) const;

    EZoneAreaType ParseAreaType(const FString& AreaTypeString) const;

    void OnZoneDataResponseReceived(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful
    );

    // 게임플레이 연동 함수들

    void ClearSpawnedGameplayMarkers();

    void ApplyGameplayMarkers();
};
