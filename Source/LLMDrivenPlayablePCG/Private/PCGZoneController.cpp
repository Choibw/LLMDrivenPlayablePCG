// Fill out your copyright notice in the Description page of Project Settings.


#include "PCGZoneController.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGSubsystem.h"

#include "Components/SplineComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"

#include "ZoneEnemySpawner.h"
#include "ZoneGoalPoint.h"
#include "ZonePlayerCharacter.h"
#include "ZoneDangerArea.h"

#include "TimerManager.h"
#include "Engine/World.h"
#include "Helpers/PCGGraphParametersHelpers.h"

// Sets default values
APCGZoneController::APCGZoneController()
{
	PrimaryActorTick.bCanEverTick = false;

}

void APCGZoneController::GenerateZone()
{
    if (bZoneGenerationInProgress)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[ZoneGeneration] Generation is already in progress.")
        );
        return;
    }

    // HTTP 응답을 기다리는 동안 수동 생성을 막음
    if (bBackendRequestInProgress && !bWaitingForBackendPCG)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[ZoneGeneration] Waiting for backend response.")
        );
        return;
    }

    PendingPCGComponents.Reset();
    TotalPCGComponentCount = 0;

    bZoneGenerationInProgress = true;
    bRegisteringPCGComponents = true;
    bZoneGenerationFailed = false;

    // 실행 중인 게임에서 생성 대기 시간 제한
    if (GetWorld() && GetWorld()->IsGameWorld())
    {
        GetWorldTimerManager().SetTimer(
            PCGGenerationTimeoutHandle,
            this,
            &APCGZoneController::HandlePCGGenerationTimeout,
            FMath::Max(PCGGenerationTimeoutSeconds, 1.0f),
            false
        );
    }

    UpdateZoneBoundsFromBaseEnvironment();

    ApplyPathPCG();
    ApplyAreaPCG();

    ClearSpawnedGameplayMarkers();
    ApplyGameplayMarkers();

    ApplyBaseEnvironmentPCG();

    // 이제 모든 생성 대상의 등록이 끝남
    bRegisteringPCGComponents = false;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ZoneGeneration] Registration finished. Total=%d Pending=%d"),
        TotalPCGComponentCount,
        PendingPCGComponents.Num()
    );

    TryFinishZoneGeneration();
}

void APCGZoneController::UpdateZoneBoundsFromBaseEnvironment()
{
    if (!IsValid(BaseEnvironmentPCGActor))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[PCGZoneController] BaseEnvironmentPCGActor is not set.")
        );
        return;
    }

    FVector BoundsOrigin;
    FVector BoundsExtent;

    BaseEnvironmentPCGActor->GetActorBounds(
        false,
        BoundsOrigin,
        BoundsExtent
    );

    CurrentZoneData.ZoneSize = FVector2D(
        BoundsExtent.X * 2.0f,
        BoundsExtent.Y * 2.0f
    );

    // XY는 기본 환경 Bounds, Z는 독립적인 설정값 사용
    CurrentZoneData.ZoneCenter = FVector(
        BoundsOrigin.X,
        BoundsOrigin.Y,
        ZoneBaseHeight
    );

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[PCGZoneController] Zone Bounds Updated. Center=%s Size=%s"),
        *CurrentZoneData.ZoneCenter.ToString(),
        *CurrentZoneData.ZoneSize.ToString()
    );
}

void APCGZoneController::ApplyBaseEnvironmentPCG()
{
    if (!BaseEnvironmentPCGActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] TargetPCGActor is not set."));

        MarkZoneGenerationFailed(TEXT("Base environment actor is missing."));

        return;
    }

    BaseEnvironmentPCGComponent = BaseEnvironmentPCGActor->FindComponentByClass<UPCGComponent>();

    if (!BaseEnvironmentPCGComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] TargetPCGComponent is null."));

        MarkZoneGenerationFailed(TEXT("Base environment PCG component is missing."));

        return;
    }

    UPCGGraphInterface* GraphInterface = BaseEnvironmentPCGComponent->GetGraphInstance();

    if (!GraphInterface)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] PCG GraphInterface is null."));

        MarkZoneGenerationFailed(TEXT("Base environment graph is missing."));

        return;
    }

    const float Tree = FMath::Clamp(CurrentZoneData.TreeDensity, 0.0f, 1.0f);
    const float Rock = FMath::Clamp(CurrentZoneData.RockDensity, 0.0f, 1.0f);
    const float Grass = FMath::Clamp(CurrentZoneData.GrassDensity, 0.0f, 1.0f);

    GraphInterface->SetGraphParameter(FName("TreeDensity"), Tree);
    GraphInterface->SetGraphParameter(FName("RockDensity"), Rock);
    GraphInterface->SetGraphParameter(FName("GrassDensity"), Grass);

    BaseEnvironmentPCGComponent->Cleanup();
    BaseEnvironmentPCGComponent->DirtyGenerated(EPCGComponentDirtyFlag::All);

    ObservePCGGeneration(BaseEnvironmentPCGComponent);

    BaseEnvironmentPCGComponent->Generate(true);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[PCGZoneController] Base environment PCG applied. Tree=%f, Rock=%f, Grass=%f"),
        Tree,
        Rock,
        Grass
    );
}

void APCGZoneController::ApplyPathPCG()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        MarkZoneGenerationFailed(TEXT("World is unavailable for path generation."));

        return;
    }

    if (!PathPCGClass)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[PCGZoneController] PathPCGClass is not set.")
        );

        MarkZoneGenerationFailed(TEXT("Path PCG class is missing."));

        return;
    }

    // 이전에 자동 생성한 경로와 PCG 결과 정리
    ClearSpawnedPathPCGActors();

    const TArray<FZonePathData>& Paths = CurrentZoneData.Paths;

    if (Paths.Num() == 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[PCGZoneController] No path data.")
        );

        MarkZoneGenerationFailed(TEXT("Path data is empty."));

        return;
    }

    // Area·게임플레이 마커와 동일한 XY 변환 기준
    const float PathAreaScale = 0.9f;

    const FVector2D PathUsableSize(
        CurrentZoneData.ZoneSize.X * PathAreaScale,
        CurrentZoneData.ZoneSize.Y * PathAreaScale
    );

    for (int32 PathIndex = 0; PathIndex < Paths.Num(); ++PathIndex)
    {
        const FZonePathData& PathData = Paths[PathIndex];

        if (PathData.NormalizedPathPoints.Num() < 2)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[PCGZoneController] Path requires at least 2 points. Index=%d"),
                PathIndex
            );

            MarkZoneGenerationFailed(TEXT("Path has fewer than two points."));

            continue;
        }

        const FTransform SpawnTransform(
            FRotator::ZeroRotator,
            CurrentZoneData.ZoneCenter,
            FVector::OneVector
        );

        FActorSpawnParameters SpawnParams;
        SpawnParams.Owner = this;
        SpawnParams.SpawnCollisionHandlingOverride =
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        AActor* PathActor = World->SpawnActor<AActor>(
            PathPCGClass,
            SpawnTransform,
            SpawnParams
        );

        if (!IsValid(PathActor))
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[PCGZoneController] Failed to spawn Path Actor. Index=%d"),
                PathIndex
            );

            MarkZoneGenerationFailed(TEXT("Failed to spawn a path actor."));

            continue;
        }

        USplineComponent* SplineComponent =
            PathActor->FindComponentByClass<USplineComponent>();

        UPCGComponent* PCGComponent =
            PathActor->FindComponentByClass<UPCGComponent>();

        if (!SplineComponent || !PCGComponent)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[PCGZoneController] Path Actor requires Spline and PCG components. Index=%d Actor=%s"),
                PathIndex,
                *PathActor->GetName()
            );

            MarkZoneGenerationFailed(TEXT("Path actor is missing required components."));

            if (PCGComponent)
            {
                PCGComponent->Cleanup();
            }

            PathActor->Destroy();
            continue;
        }

        SpawnedPathPCGActors.Add(PathActor);

        // BP의 기본 Spline으로 생성된 결과 정리
        //PCGComponent->Cleanup();
        SplineComponent->ClearSplinePoints(false);

        for (const FVector2D& Point : PathData.NormalizedPathPoints)
        {
            const float LocalX =
                (Point.X - 0.5f) * PathUsableSize.X;

            const float LocalY =
                (Point.Y - 0.5f) * PathUsableSize.Y;

            const FVector WorldPoint =
                CurrentZoneData.ZoneCenter
                + FVector(LocalX, LocalY, 0.0f);

            // 월드 좌표로 전달하여 SplineComponent의 상대 변환에 대응
            SplineComponent->AddSplinePoint(
                WorldPoint,
                ESplineCoordinateSpace::World,
                false
            );
        }

        SplineComponent->UpdateSpline();

        UPCGGraphInstance* PathGraphInstance =
            PCGComponent->GetGraphInstance();

        if (!IsValid(PathGraphInstance))
        {
            MarkZoneGenerationFailed(
                TEXT("Path PCG graph instance is missing.")
            );
            continue;
        }

        const float SafeBaseWidth =
            FMath::Max(PathAssetBaseWidth, 1.0f);

        const float SafePathWidth =
            FMath::Max(PathData.PathWidth, 1.0f);

        const float WidthScale = SafePathWidth / SafeBaseWidth;

        // 길이와 높이는 유지하고 가로 폭만 조절
        const FVector PathScaleValue(
            1.0f,
            WidthScale,
            1.0f
        );

        UPCGGraphParametersHelpers::SetVectorParameter(
            PathGraphInstance,
            FName(TEXT("PathScale")),
            PathScaleValue
        );

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[PathWidth] Index=%d Width=%.1f BaseWidth=%.1f ScaleY=%.3f"),
            PathIndex,
            SafePathWidth,
            SafeBaseWidth,
            WidthScale
        );

        AddPathEnvironmentExclusion(
            PathActor,
            SplineComponent,
            SafePathWidth
        );

        PCGComponent->DirtyGenerated(EPCGComponentDirtyFlag::All);

        ObservePCGGeneration(PCGComponent);

        PCGComponent->Generate(true);

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[PCGZoneController] Path PCG Actor spawned. Index=%d Type=%d PointCount=%d Width=%f Actor=%s"),
            PathIndex,
            static_cast<int32>(PathData.PathType),
            PathData.NormalizedPathPoints.Num(),
            PathData.PathWidth,
            *PathActor->GetName()
        );
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[PCGZoneController] Path generation finished. Created=%d Requested=%d"),
        SpawnedPathPCGActors.Num(),
        Paths.Num()
    );
}

void APCGZoneController::ClearSpawnedPathPCGActors()
{
    for (AActor* PathActor : SpawnedPathPCGActors)
    {
        if (!IsValid(PathActor))
        {
            continue;
        }

        UPCGComponent* PCGComponent =
            PathActor->FindComponentByClass<UPCGComponent>();

        if (PCGComponent)
        {
            PCGComponent->Cleanup();
        }

        PathActor->Destroy();
    }

    SpawnedPathPCGActors.Empty();
}

void APCGZoneController::ApplyAreaPCG()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        MarkZoneGenerationFailed(TEXT("World is unavailable for area generation."));

        return;
    }

    // 이전에 자동 생성했던 Area Actor와 PCG 결과 정리
    ClearSpawnedAreaPCGActors();

    // 경로·게임플레이 마커와 동일한 XY 좌표 변환 기준
    const float AreaPositionScale = 0.9f;

    const FVector2D AreaUsableSize(
        CurrentZoneData.ZoneSize.X * AreaPositionScale,
        CurrentZoneData.ZoneSize.Y * AreaPositionScale
    );

    const float BaseZoneLength = FMath::Min(
        CurrentZoneData.ZoneSize.X,
        CurrentZoneData.ZoneSize.Y
    );

    const float DefaultAreaRadius = 500.0f;

    for (int32 i = 0; i < CurrentZoneData.Areas.Num(); ++i)
    {
        const FZoneAreaData& AreaData = CurrentZoneData.Areas[i];

        // 구역 유형에 맞는 BP 클래스 선택
        TSubclassOf<AActor> AreaClass = nullptr;

        switch (AreaData.AreaType)
        {
        case EZoneAreaType::Spawn:
            AreaClass = SpawnAreaPCGClass;
            break;

        case EZoneAreaType::Combat:
            AreaClass = CombatAreaPCGClass;
            break;

        case EZoneAreaType::Goal:
            AreaClass = GoalAreaPCGClass;
            break;

        case EZoneAreaType::Danger: 
            continue;

        default:
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[PCGZoneController] Unsupported Area PCG type. Index=%d Type=%d"),
                i,
                static_cast<int32>(AreaData.AreaType)
            );
            continue;
        }

        if (!AreaClass)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[PCGZoneController] Area PCG class is not set. Index=%d Type=%d"),
                i,
                static_cast<int32>(AreaData.AreaType)
            );

            MarkZoneGenerationFailed(TEXT("Area PCG class is missing."));

            continue;
        }

        // 정규화 좌표 → 월드 위치
        const float LocalX =
            (AreaData.NormalizedCenter.X - 0.5f) * AreaUsableSize.X;

        const float LocalY =
            (AreaData.NormalizedCenter.Y - 0.5f) * AreaUsableSize.Y;

        const FVector AreaWorldLocation =
            CurrentZoneData.ZoneCenter + FVector(LocalX, LocalY, 0.0f);

        // 정규화 반지름 → Actor의 XY 스케일
        const float WorldRadius =
            AreaData.NormalizedRadius * BaseZoneLength;

        const float AreaScale = WorldRadius / DefaultAreaRadius;

        const FTransform SpawnTransform(
            FRotator::ZeroRotator,
            AreaWorldLocation,
            FVector(AreaScale, AreaScale, 1.0f)
        );

        FActorSpawnParameters SpawnParams;
        SpawnParams.Owner = this;
        SpawnParams.SpawnCollisionHandlingOverride =
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        AActor* AreaActor = World->SpawnActor<AActor>(
            AreaClass,
            SpawnTransform,
            SpawnParams
        );

        if (!IsValid(AreaActor))
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[PCGZoneController] Failed to spawn Area PCG Actor. Index=%d"),
                i
            );

            MarkZoneGenerationFailed(TEXT("Failed to spawn an area actor."));

            continue;
        }

        // 다음 재생성 시 정리할 수 있도록 기록
        SpawnedAreaPCGActors.Add(AreaActor);

        UPCGComponent* AreaPCGComponent =
            AreaActor->FindComponentByClass<UPCGComponent>();

        if (AreaPCGComponent)
        {
            //AreaPCGComponent->Cleanup();
            AreaPCGComponent->DirtyGenerated(EPCGComponentDirtyFlag::All);

            ObservePCGGeneration(AreaPCGComponent);

            AreaPCGComponent->Generate(true);
        }
        else
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[PCGZoneController] PCGComponent missing in spawned Area Actor. Index=%d Actor=%s"),
                i,
                *AreaActor->GetName()
            );

            MarkZoneGenerationFailed(TEXT("Area PCG component is missing."));
        }

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[PCGZoneController] Area PCG Actor spawned. Index=%d Type=%d Location=%s Radius=%f Scale=%f"),
            i,
            static_cast<int32>(AreaData.AreaType),
            *AreaWorldLocation.ToString(),
            WorldRadius,
            AreaScale
        );
    }
}

void APCGZoneController::ClearSpawnedAreaPCGActors()
{
    UE_LOG(
        LogTemp,
        Log,
        TEXT("[AreaCleanup] Tracked=%d GameWorld=%s"),
        SpawnedAreaPCGActors.Num(),
        GetWorld() && GetWorld()->IsGameWorld() ? TEXT("true") : TEXT("false")
    );

    for (AActor* AreaActor : SpawnedAreaPCGActors)
    {
        if (!IsValid(AreaActor))
        {
            continue;
        }

        // PCG가 생성한 결과물을 먼저 정리
        UPCGComponent* PCGComponent =
            AreaActor->FindComponentByClass<UPCGComponent>();

        if (PCGComponent)
        {
            PCGComponent->Cleanup();
        }

        const FString ActorName = AreaActor->GetName();
        const bool bDestroyed = AreaActor->Destroy();

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[AreaCleanup] Actor=%s DestroyResult=%s"),
            *ActorName,
            bDestroyed ? TEXT("true") : TEXT("false")
        );
    }

    SpawnedAreaPCGActors.Empty();
}

void APCGZoneController::LoadSampleZoneJson()
{
    const FString SampleJson = TEXT(R"(
{
  "theme": "desert",
  "base_environment": {
    "tree_density": 0.05,
    "rock_density": 0.65,
    "grass_density": 0.5
  },
  "main_path": {
    "path_type": "main",
    "path_width": 800,
    "normalized_points": [
      {"x": 0.5, "y": 0.92},
      {"x": 0.58, "y": 0.7},
      {"x": 0.4, "y": 0.5},
      {"x": 0.24, "y": 0.34},
      {"x": 0.32, "y": 0.16},
      {"x": 0.14, "y": 0.08}
    ]
  },
  "areas": [
    {
      "area_type": "spawn",
      "normalized_center": {"x": 0.5, "y": 0.92},
      "normalized_radius": 0.04,
      "detail_density": 0.2
    },
    {
      "area_type": "danger",
      "normalized_center": {"x": 0.33, "y": 0.6},
      "normalized_radius": 0.05,
      "detail_density": 0.6
    },
    {
      "area_type": "combat",
      "normalized_center": {"x": 0.4, "y": 0.5},
      "normalized_radius": 0.05,
      "detail_density": 0.4
    },
    {
      "area_type": "combat",
      "normalized_center": {"x": 0.28, "y": 0.25},
      "normalized_radius": 0.12,
      "detail_density": 0.8
    },
    {
      "area_type": "goal",
      "normalized_center": {"x": 0.14, "y": 0.08},
      "normalized_radius": 0.06,
      "detail_density": 0.5
    }
  ],
  "branch_paths": [
    {
      "path_type": "shortcut",
      "path_width": 480,
      "normalized_points": [
        {"x": 0.58, "y": 0.7},
        {"x": 0.43, "y": 0.67},
        {"x": 0.33, "y": 0.6},
        {"x": 0.26, "y": 0.49},
        {"x": 0.24, "y": 0.34}
      ]
    }
  ]
}
    )");

    if (ApplyZoneJsonString(SampleJson))
    {
        UE_LOG(LogTemp, Log, TEXT("[PCGZoneController] Sample zone JSON applied successfully."));

        GenerateZone();
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] Failed to apply sample zone JSON."));
    }
}

void APCGZoneController::RequestZoneDataFromBackend()
{
    const TArray<FString> Questions = { TestNPCQuestion };
    const TArray<FString> Answers = { TestPlayerAnswer };

    RequestZoneDataFromDialogue(Questions, Answers);
}

bool APCGZoneController::RequestZoneDataFromDialogue(
    const TArray<FString>& Questions,
    const TArray<FString>& Answers
)
{
    if (bBackendRequestInProgress || bZoneGenerationInProgress)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Backend] A request or zone generation is already in progress.")
        );

        return false;
    }

    if (BackendUrl.TrimStartAndEnd().IsEmpty()
        || Questions.IsEmpty()
        || Questions.Num() != Answers.Num())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Backend] Invalid URL or question/answer count.")
        );
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> DialogueArray;

    for (int32 Index = 0; Index < Questions.Num(); ++Index)
    {
        const FString Question = Questions[Index].TrimStartAndEnd();
        const FString Answer = Answers[Index].TrimStartAndEnd();

        if (Question.IsEmpty() || Answer.IsEmpty())
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[Backend] Empty question or answer. Index=%d"),
                Index
            );
            return false;
        }

        // NPC 질문
        TSharedPtr<FJsonObject> QuestionObject =
            MakeShared<FJsonObject>();

        QuestionObject->SetStringField(
            TEXT("speaker"),
            TEXT("npc")
        );
        QuestionObject->SetStringField(TEXT("text"), Question);

        DialogueArray.Add(
            MakeShared<FJsonValueObject>(QuestionObject)
        );

        // 해당 질문에 대한 플레이어 답변
        TSharedPtr<FJsonObject> AnswerObject =
            MakeShared<FJsonObject>();

        AnswerObject->SetStringField(
            TEXT("speaker"),
            TEXT("player")
        );
        AnswerObject->SetStringField(TEXT("text"), Answer);

        DialogueArray.Add(
            MakeShared<FJsonValueObject>(AnswerObject)
        );
    }

    TSharedPtr<FJsonObject> RequestRootObject =
        MakeShared<FJsonObject>();

    RequestRootObject->SetArrayField(
        TEXT("dialogue"),
        DialogueArray
    );

    FString RequestBody;

    TSharedRef<TJsonWriter<>> Writer =
        TJsonWriterFactory<>::Create(&RequestBody);

    if (!FJsonSerializer::Serialize(
        RequestRootObject.ToSharedRef(),
        Writer))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Backend] Failed to serialize dialogue.")
        );
        return false;
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest =
        FHttpModule::Get().CreateRequest();

    HttpRequest->SetURL(BackendUrl);
    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetHeader(
        TEXT("Content-Type"),
        TEXT("application/json")
    );
    HttpRequest->SetContentAsString(RequestBody);

    HttpRequest->SetTimeout(
        FMath::Max(BackendRequestTimeoutSeconds, 1.0f)
    );

    HttpRequest->OnProcessRequestComplete().BindUObject(
        this,
        &APCGZoneController::OnZoneDataResponseReceived
    );

    bBackendRequestInProgress = true;

    if (!HttpRequest->ProcessRequest())
    {
        bBackendRequestInProgress = false;

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Backend] Failed to start HTTP request.")
        );
        return false;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[Backend] Request started. QuestionCount=%d URL=%s"),
        Questions.Num(),
        *BackendUrl
    );

    return true;
}

void APCGZoneController::MovePlayerToSpawnPoint()
{
    if (!IsValid(SpawnPointActor))
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] SpawnPointActor is not valid."));
        return;
    }

    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
    if (!IsValid(PlayerPawn))
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] PlayerPawn is not valid."));
        return;
    }

    FVector TargetLocation = SpawnPointActor->GetActorLocation();

    PlayerPawn->SetActorLocation(
        TargetLocation,
        false,
        nullptr,
        ETeleportType::TeleportPhysics
    );

    UE_LOG(LogTemp, Log, TEXT("[PCGZoneController] Player moved to SpawnPoint. Location=%s"),
        *TargetLocation.ToString());
}

bool APCGZoneController::RespawnPlayerAtSpawnPoint()
{
    if (!IsValid(SpawnPointActor))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[PCGZoneController] Respawn failed: SpawnPointActor is not valid.")
        );

        return false;
    }

    AZonePlayerCharacter* Player = Cast<AZonePlayerCharacter>(
        UGameplayStatics::GetPlayerPawn(this, 0)
    );

    if (!IsValid(Player))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[PCGZoneController] Respawn failed: player is not valid.")
        );

        return false;
    }

    return Player->RespawnAtLocation(
        SpawnPointActor->GetActorLocation()
    );
}

bool APCGZoneController::ApplyZoneJsonString(const FString& JsonString)
{
    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] Failed to parse zone JSON."));
        return false;
    }

    FString Theme;
    if (RootObject->TryGetStringField(TEXT("theme"), Theme))
    {
        UE_LOG(LogTemp, Log, TEXT("[PCGZoneController] Parsed theme: %s"), *Theme);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] theme field is missing."));
        return false;
    }

    // base_environment 파싱
    const TSharedPtr<FJsonObject>* BaseEnvironmentObject = nullptr;

    if (RootObject->TryGetObjectField(TEXT("base_environment"), BaseEnvironmentObject) && BaseEnvironmentObject && BaseEnvironmentObject->IsValid())
    {
        double TreeDensity = CurrentZoneData.TreeDensity;
        double RockDensity = CurrentZoneData.RockDensity;
        double GrassDensity = CurrentZoneData.GrassDensity;

        (*BaseEnvironmentObject)->TryGetNumberField(TEXT("tree_density"), TreeDensity);
        (*BaseEnvironmentObject)->TryGetNumberField(TEXT("rock_density"), RockDensity);
        (*BaseEnvironmentObject)->TryGetNumberField(TEXT("grass_density"), GrassDensity);

        CurrentZoneData.TreeDensity = FMath::Clamp(static_cast<float>(TreeDensity), 0.0f, 1.0f);
        CurrentZoneData.RockDensity = FMath::Clamp(static_cast<float>(RockDensity), 0.0f, 1.0f);
        CurrentZoneData.GrassDensity = FMath::Clamp(static_cast<float>(GrassDensity), 0.0f, 1.0f);

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[PCGZoneController] Parsed base environment. Tree=%f, Rock=%f, Grass=%f"),
            CurrentZoneData.TreeDensity,
            CurrentZoneData.RockDensity,
            CurrentZoneData.GrassDensity
        );
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] base_environment field is missing."));
        return false;
    }

    // main_path 파싱
    const TSharedPtr<FJsonObject>* MainPathObject = nullptr;

    if (RootObject->TryGetObjectField(TEXT("main_path"), MainPathObject) && MainPathObject && MainPathObject->IsValid())
    {
        FString PathTypeString;
        if ((*MainPathObject)->TryGetStringField(TEXT("path_type"), PathTypeString))
        {
            CurrentZoneData.MainPath.PathType = ParsePathType(PathTypeString);
        }
        else
        {
            CurrentZoneData.MainPath.PathType = EZonePathType::Main;
        }

        double PathWidth = CurrentZoneData.MainPath.PathWidth;
        if ((*MainPathObject)->TryGetNumberField(TEXT("path_width"), PathWidth))
        {
            CurrentZoneData.MainPath.PathWidth = FMath::Clamp(static_cast<float>(PathWidth), 100.0f, 1500.0f);
        }

        const TArray<TSharedPtr<FJsonValue>>* PointsArray = nullptr;

        if ((*MainPathObject)->TryGetArrayField(TEXT("normalized_points"), PointsArray) && PointsArray)
        {
            CurrentZoneData.MainPath.NormalizedPathPoints.Empty();

            for (const TSharedPtr<FJsonValue>& PointValue : *PointsArray)
            {
                if (!PointValue.IsValid())
                {
                    continue;
                }

                const TSharedPtr<FJsonObject> PointObject = PointValue->AsObject();

                if (!PointObject.IsValid())
                {
                    continue;
                }

                double X = 0.5;
                double Y = 0.5;

                if (!PointObject->TryGetNumberField(TEXT("x"), X) || !PointObject->TryGetNumberField(TEXT("y"), Y))
                {
                    continue;
                }

                const float ClampedX = FMath::Clamp(static_cast<float>(X), 0.0f, 1.0f);
                const float ClampedY = FMath::Clamp(static_cast<float>(Y), 0.0f, 1.0f);

                CurrentZoneData.MainPath.NormalizedPathPoints.Add(FVector2D(ClampedX, ClampedY));
            }

            if (CurrentZoneData.MainPath.NormalizedPathPoints.Num() < 2)
            {
                UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] main_path.normalized_points requires at least 2 points."));
                return false;
            }

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[PCGZoneController] Parsed main path. PointCount=%d, Width=%f"),
                CurrentZoneData.MainPath.NormalizedPathPoints.Num(),
                CurrentZoneData.MainPath.PathWidth
            );
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] main_path.normalized_points field is missing."));
            return false;
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] main_path field is missing."));
        return false;
    }

    // areas 파싱
    const TArray<TSharedPtr<FJsonValue>>* AreasArray = nullptr;

    if (RootObject->TryGetArrayField(TEXT("areas"), AreasArray) && AreasArray)
    {
        CurrentZoneData.Areas.Empty();

        for (const TSharedPtr<FJsonValue>& AreaValue : *AreasArray)
        {
            if (!AreaValue.IsValid())
            {
                continue;
            }

            const TSharedPtr<FJsonObject> AreaObject = AreaValue->AsObject();

            if (!AreaObject.IsValid())
            {
                continue;
            }

            FZoneAreaData AreaData;

            FString AreaTypeString;
            if (AreaObject->TryGetStringField(TEXT("area_type"), AreaTypeString))
            {
                AreaData.AreaType = ParseAreaType(AreaTypeString);
            }
            else
            {
                AreaData.AreaType = EZoneAreaType::Combat;
            }

            const TSharedPtr<FJsonObject>* CenterObject = nullptr;
            if (AreaObject->TryGetObjectField(TEXT("normalized_center"), CenterObject) && CenterObject && CenterObject->IsValid())
            {
                double CenterX = 0.5;
                double CenterY = 0.5;

                (*CenterObject)->TryGetNumberField(TEXT("x"), CenterX);
                (*CenterObject)->TryGetNumberField(TEXT("y"), CenterY);

                AreaData.NormalizedCenter = FVector2D(
                    FMath::Clamp(static_cast<float>(CenterX), 0.0f, 1.0f),
                    FMath::Clamp(static_cast<float>(CenterY), 0.0f, 1.0f)
                );
            }
            else
            {
                AreaData.NormalizedCenter = FVector2D(0.5f, 0.5f);
            }

            double NormalizedRadius = AreaData.NormalizedRadius;
            if (AreaObject->TryGetNumberField(TEXT("normalized_radius"), NormalizedRadius))
            {
                AreaData.NormalizedRadius = FMath::Clamp(static_cast<float>(NormalizedRadius), 0.01f, 0.30f);
            }

            double DetailDensity = AreaData.DetailDensity;
            if (AreaObject->TryGetNumberField(TEXT("detail_density"), DetailDensity))
            {
                AreaData.DetailDensity = FMath::Clamp(static_cast<float>(DetailDensity), 0.0f, 1.0f);
            }

            CurrentZoneData.Areas.Add(AreaData);
        }

        if (CurrentZoneData.Areas.Num() <= 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] areas array is empty."));
            return false;
        }

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[PCGZoneController] Parsed areas. AreaCount=%d"),
            CurrentZoneData.Areas.Num()
        );
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] areas field is missing."));
        return false;
    }

    // 메인 경로와 보조 경로를 임시 배열에 구성
    TArray<FZonePathData> ParsedPaths;
    ParsedPaths.Add(CurrentZoneData.MainPath);

    // branch_paths는 선택 항목
    if (RootObject->HasField(TEXT("branch_paths")))
    {
        const TArray<TSharedPtr<FJsonValue>>* BranchPathsArray = nullptr;

        if (!RootObject->TryGetArrayField(
            TEXT("branch_paths"),
            BranchPathsArray
        ) || !BranchPathsArray)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[PCGZoneController] branch_paths must be an array.")
            );
            return false;
        }

        for (int32 Index = 0; Index < BranchPathsArray->Num(); ++Index)
        {
            const TSharedPtr<FJsonValue>& PathValue =
                (*BranchPathsArray)[Index];

            if (!PathValue.IsValid() || PathValue->Type != EJson::Object)
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[PCGZoneController] Invalid branch path object. Index=%d"),
                    Index
                );
                return false;
            }

            const TSharedPtr<FJsonObject> PathObject =
                PathValue->AsObject();

            FZonePathData BranchPath;
            BranchPath.PathType = EZonePathType::Branch;

            FString PathTypeString;

            if (PathObject->TryGetStringField(
                TEXT("path_type"),
                PathTypeString
            ))
            {
                BranchPath.PathType = ParsePathType(PathTypeString);
            }

            double PathWidth = BranchPath.PathWidth;

            if (PathObject->TryGetNumberField(
                TEXT("path_width"),
                PathWidth
            ))
            {
                if (!FMath::IsFinite(PathWidth))
                {
                    return false;
                }

                BranchPath.PathWidth = FMath::Clamp(
                    static_cast<float>(PathWidth),
                    100.0f,
                    1500.0f
                );
            }

            const TArray<TSharedPtr<FJsonValue>>* PointsArray = nullptr;

            if (!PathObject->TryGetArrayField(
                TEXT("normalized_points"),
                PointsArray
            ) || !PointsArray || PointsArray->Num() < 2)
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[PCGZoneController] Branch path requires at least two points. Index=%d"),
                    Index
                );
                return false;
            }

            for (const TSharedPtr<FJsonValue>& PointValue : *PointsArray)
            {
                if (!PointValue.IsValid()
                    || PointValue->Type != EJson::Object)
                {
                    UE_LOG(
                        LogTemp,
                        Warning,
                        TEXT("[PCGZoneController] Invalid branch path point. Index=%d"),
                        Index
                    );
                    return false;
                }

                const TSharedPtr<FJsonObject> PointObject =
                    PointValue->AsObject();

                double X = 0.0;
                double Y = 0.0;

                if (!PointObject->TryGetNumberField(TEXT("x"), X)
                    || !PointObject->TryGetNumberField(TEXT("y"), Y)
                    || !FMath::IsFinite(X)
                    || !FMath::IsFinite(Y)
                    || X < 0.0 || X > 1.0
                    || Y < 0.0 || Y > 1.0)
                {
                    UE_LOG(
                        LogTemp,
                        Warning,
                        TEXT("[PCGZoneController] Invalid branch path coordinates. Index=%d"),
                        Index
                    );
                    return false;
                }

                BranchPath.NormalizedPathPoints.Add(
                    FVector2D(X, Y)
                );
            }

            ParsedPaths.Add(BranchPath);

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[PCGZoneController] Parsed branch path. Index=%d PointCount=%d Width=%f"),
                Index,
                BranchPath.NormalizedPathPoints.Num(),
                BranchPath.PathWidth
            );
        }
    }

    // 보조 경로까지 정상적으로 읽었을 때 경로 배열 갱신
    CurrentZoneData.Paths = MoveTemp(ParsedPaths);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[PCGZoneController] Path data prepared. PathCount=%d"),
        CurrentZoneData.Paths.Num()
    );

    UE_LOG(LogTemp, Log, TEXT("[PCGZoneController] Zone JSON applied to CurrentZoneData."));

    return true;
}

EZonePathType APCGZoneController::ParsePathType(const FString& PathTypeString) const
{
    if (PathTypeString.Equals(TEXT("branch"), ESearchCase::IgnoreCase))
    {
        return EZonePathType::Branch;
    }

    if (PathTypeString.Equals(TEXT("shortcut"), ESearchCase::IgnoreCase))
    {
        return EZonePathType::Shortcut;
    }

    return EZonePathType::Main;
}

EZoneAreaType APCGZoneController::ParseAreaType(const FString& AreaTypeString) const
{
    if (AreaTypeString.Equals(TEXT("spawn"), ESearchCase::IgnoreCase))
    {
        return EZoneAreaType::Spawn;
    }

    if (AreaTypeString.Equals(TEXT("goal"), ESearchCase::IgnoreCase))
    {
        return EZoneAreaType::Goal;
    }

    if (AreaTypeString.Equals(TEXT("danger"), ESearchCase::IgnoreCase))
    {
        return EZoneAreaType::Danger;
    }

    return EZoneAreaType::Combat;
}

void APCGZoneController::OnZoneDataResponseReceived(
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool bWasSuccessful
)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        const FString Message =
            TEXT("Backend request failed or timed out.");

        UE_LOG(LogTemp, Warning, TEXT("[Backend] %s"), *Message);

        FinishBackendRequest(false, Message);
        return;
    }

    const int32 ResponseCode = Response->GetResponseCode();
    const FString ResponseString = Response->GetContentAsString();

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[Backend] Response Code=%d"),
        ResponseCode
    );

    if (ResponseCode < 200 || ResponseCode >= 300)
    {
        const FString Message = FString::Printf(
            TEXT("Backend returned HTTP %d."),
            ResponseCode
        );

        UE_LOG(LogTemp, Warning, TEXT("[Backend] %s"), *Message);

        FinishBackendRequest(false, Message);
        return;
    }

    if (ResponseString.TrimStartAndEnd().IsEmpty())
    {
        const FString Message =
            TEXT("Backend response was empty.");

        UE_LOG(LogTemp, Warning, TEXT("[Backend] %s"), *Message);

        FinishBackendRequest(false, Message);
        return;
    }

    if (!ApplyZoneJsonString(ResponseString))
    {
        const FString Message =
            TEXT("Backend response contained invalid zone data.");

        UE_LOG(LogTemp, Warning, TEXT("[Backend] %s"), *Message);

        FinishBackendRequest(false, Message);
        return;
    }

    bWaitingForBackendPCG = true;

    OnZoneRequestStatusChanged.Broadcast(
        TEXT("Building your map. Please wait...")
    );

    GenerateZone();
}

void APCGZoneController::ClearSpawnedGameplayMarkers()
{
    // 이전 스포너의 전투 완료 이벤트 연결 해제
    for (AActor* SpawnerActor : SpawnedEnemySpawners)
    {
        if (IsValid(SpawnerActor))
        {
            AZoneEnemySpawner* EnemySpawner =
                Cast<AZoneEnemySpawner>(SpawnerActor);

            if (EnemySpawner)
            {
                EnemySpawner->OnCombatAreaCleared.RemoveAll(this);
            }
        }
    }

    // 이전 Zone의 전투 진행 상태 초기화
    ExpectedCombatAreaCount = 0;
    RegisteredCombatSpawners.Empty();
    ClearedCombatSpawners.Empty();

    // 기존 게임플레이 마커 제거
    for (AActor* Marker : SpawnedGameplayMarkers)
    {
        if (IsValid(Marker))
        {
            Marker->Destroy();
        }
    }

    SpawnedGameplayMarkers.Empty();

    SpawnPointActor = nullptr;
    GoalPointActor = nullptr;

    // 기존 스포너 제거
    // 스포너의 EndPlay()에서 자신이 생성한 적도 정리한다.
    for (AActor* SpawnerActor : SpawnedEnemySpawners)
    {
        if (IsValid(SpawnerActor))
        {
            SpawnerActor->Destroy();
        }
    }

    SpawnedEnemySpawners.Empty();
}

void APCGZoneController::ApplyGameplayMarkers()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    // JSON에서 요구하는 전체 Combat 구역 수 계산
    ExpectedCombatAreaCount = 0;

    for (const FZoneAreaData& AreaData : CurrentZoneData.Areas)
    {
        if (AreaData.AreaType == EZoneAreaType::Combat)
        {
            ++ExpectedCombatAreaCount;
        }
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[PCGZoneController] Expected Combat Areas=%d"),
        ExpectedCombatAreaCount
    );

    // 길 및 Area PCG와 동일한 위치 변환 기준
    const float AreaPositionScale = 0.9f;

    const FVector2D AreaUsableSize(
        CurrentZoneData.ZoneSize.X * AreaPositionScale,
        CurrentZoneData.ZoneSize.Y * AreaPositionScale
    );

    for (const FZoneAreaData& AreaData : CurrentZoneData.Areas)
    {
        TSubclassOf<AActor> MarkerClass = nullptr;

        switch (AreaData.AreaType)
        {
        case EZoneAreaType::Spawn:
            MarkerClass = SpawnPointClass;
            break;

        case EZoneAreaType::Combat:
            MarkerClass = EnemySpawnPointClass;
            break;

        case EZoneAreaType::Goal:
            MarkerClass = GoalPointClass;
            break;

        case EZoneAreaType::Danger:
            MarkerClass = DangerAreaClass;
            break;

        default:
            break;
        }

        if (!MarkerClass)
        {
            if (AreaData.AreaType == EZoneAreaType::Danger)
            {
                MarkZoneGenerationFailed(TEXT("Danger area class is missing."));
            }

            continue;
        }

        const float LocalX =
            (AreaData.NormalizedCenter.X - 0.5f) * AreaUsableSize.X;

        const float LocalY =
            (AreaData.NormalizedCenter.Y - 0.5f) * AreaUsableSize.Y;

        // 마커 높이는 기존처럼 ZoneCenter.Z 사용
        const FVector SpawnLocation =
            CurrentZoneData.ZoneCenter + FVector(LocalX, LocalY, 0.0f);

        FActorSpawnParameters SpawnParams;
        SpawnParams.Owner = this;

        AActor* SpawnedMarker = World->SpawnActor<AActor>(
            MarkerClass,
            SpawnLocation,
            FRotator::ZeroRotator,
            SpawnParams
        );

        if (IsValid(SpawnedMarker))
        {
            SpawnedGameplayMarkers.Add(SpawnedMarker);

            const float ZoneShortSide = FMath::Min(
                CurrentZoneData.ZoneSize.X,
                CurrentZoneData.ZoneSize.Y
            );

            const float AreaRadius =
                AreaData.NormalizedRadius * ZoneShortSide;

            if (AZoneDangerArea* DangerArea =
                Cast<AZoneDangerArea>(SpawnedMarker))
            {
                // Danger는 기존 피해 판정 캡슐을 사용
                DangerArea->SetDangerRadius(AreaRadius);
            }
            else
            {
                // Spawn·Combat·Goal은 환경 제외용 캡슐 추가
                AddEnvironmentExclusion(SpawnedMarker, AreaRadius);
            }

            if (AreaData.AreaType == EZoneAreaType::Spawn)
            {
                SpawnPointActor = SpawnedMarker;
            }
            else if (AreaData.AreaType == EZoneAreaType::Goal)
            {
                GoalPointActor = SpawnedMarker;

                AZoneGoalPoint* GoalPoint = Cast<AZoneGoalPoint>(GoalPointActor);

                if (IsValid(GoalPoint))
                {
                    GoalPoint->DeactivateGoal();
                }
                else
                {
                    UE_LOG(
                        LogTemp,
                        Warning,
                        TEXT("[PCGZoneController] GoalPointClass must derive from AZoneGoalPoint.")
                    );
                }
            }

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[GameplayMarker] Spawned marker. Type=%d Location=%s"),
                static_cast<int32>(AreaData.AreaType),
                *SpawnLocation.ToString()
            );
        }
        else if (AreaData.AreaType == EZoneAreaType::Danger)
        {
            MarkZoneGenerationFailed(TEXT("Failed to spawn a danger area."));
        }

        // Combat 구역마다 적 스포너 생성
        if (AreaData.AreaType == EZoneAreaType::Combat && EnemySpawnerClass)
        {
            FActorSpawnParameters EnemySpawnerSpawnParams;
            EnemySpawnerSpawnParams.Owner = this;

            AActor* SpawnedEnemySpawner = World->SpawnActor<AActor>(
                EnemySpawnerClass,
                SpawnLocation,
                FRotator::ZeroRotator,
                EnemySpawnerSpawnParams
            );

            if (IsValid(SpawnedEnemySpawner))
            {
                SpawnedEnemySpawners.Add(SpawnedEnemySpawner);

                AZoneEnemySpawner* EnemySpawner =
                    Cast<AZoneEnemySpawner>(SpawnedEnemySpawner);

                if (EnemySpawner)
                {
                    RegisteredCombatSpawners.Add(EnemySpawner);

                    UE_LOG(
                        LogTemp,
                        Log,
                        TEXT("[PCGZoneController] Combat spawner registered. Count=%d / Expected=%d"),
                        RegisteredCombatSpawners.Num(),
                        ExpectedCombatAreaCount
                    );

                    EnemySpawner->OnCombatAreaCleared.AddUObject(
                        this,
                        &APCGZoneController::HandleCombatAreaCleared
                    );

                    UE_LOG(
                        LogTemp,
                        Log,
                        TEXT("[PCGZoneController] Bound combat clear event. Spawner=%s"),
                        *EnemySpawner->GetName()
                    );
                }
                else
                {
                    UE_LOG(
                        LogTemp,
                        Warning,
                        TEXT("[PCGZoneController] Spawned enemy spawner is not AZoneEnemySpawner.")
                    );
                }

                UE_LOG(
                    LogTemp,
                    Log,
                    TEXT("[GameplayMarker] EnemySpawner spawned at Combat Area. Location=%s"),
                    *SpawnLocation.ToString()
                );
            }
        }
    }

    UpdateGoalActivation();
}

void APCGZoneController::HandleCombatAreaCleared(AZoneEnemySpawner* ClearedSpawner)
{
    // 현재 Zone에 정상 등록된 스포너인지 확인
    if (!IsValid(ClearedSpawner) ||
        !RegisteredCombatSpawners.Contains(ClearedSpawner))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[PCGZoneController] Ignored clear event from unregistered spawner.")
        );
        return;
    }

    // 같은 스포너의 완료 이벤트는 한 번만 처리
    if (ClearedCombatSpawners.Contains(ClearedSpawner))
    {
        return;
    }

    ClearedCombatSpawners.Add(ClearedSpawner);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[PCGZoneController] Combat progress: %d / %d. Cleared=%s"),
        ClearedCombatSpawners.Num(),
        ExpectedCombatAreaCount,
        *ClearedSpawner->GetName()
    );

    UpdateGoalActivation();
}

void APCGZoneController::UpdateGoalActivation()
{
    // JSON에서 요구한 수만큼 스포너가 등록되어야 한다.
    if (RegisteredCombatSpawners.Num() != ExpectedCombatAreaCount)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[PCGZoneController] Combat registration mismatch. Registered=%d Expected=%d"),
            RegisteredCombatSpawners.Num(),
            ExpectedCombatAreaCount
        );
        return;
    }

    // 모든 전투 구역을 완료하기 전에는 Goal을 열지 않는다.
    if (ClearedCombatSpawners.Num() != ExpectedCombatAreaCount)
    {
        return;
    }

    AZoneGoalPoint* GoalPoint = Cast<AZoneGoalPoint>(GoalPointActor);

    if (!IsValid(GoalPoint))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[PCGZoneController] GoalPointActor is invalid.")
        );
        return;
    }

    if (GoalPoint->IsGoalActive())
    {
        return;
    }

    GoalPoint->ActivateGoal();

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[PCGZoneController] All combat areas cleared. Goal activated.")
    );
}

void APCGZoneController::FinishBackendRequest(
    bool bSuccess,
    const FString& Message
)
{
    bBackendRequestInProgress = false;

    OnZoneRequestFinished.Broadcast(bSuccess, Message);
}

void APCGZoneController::ObservePCGGeneration(
    UPCGComponent* PCGComponent
)
{
    if (!bZoneGenerationInProgress || !IsValid(PCGComponent))
    {
        return;
    }

    if (!PCGComponent->GetGraphInstance())
    {
        MarkZoneGenerationFailed(
            TEXT("A PCG component has no graph assigned.")
        );
        return;
    }

    const TWeakObjectPtr<UPCGComponent> ComponentKey(PCGComponent);

    // 같은 컴포넌트 중복 등록 방지
    if (PendingPCGComponents.Contains(ComponentKey))
    {
        return;
    }

    PendingPCGComponents.Add(ComponentKey);
    ++TotalPCGComponentCount;

    PCGComponent->OnPCGGraphGeneratedExternal.AddUniqueDynamic(
        this,
        &APCGZoneController::HandlePCGGenerationCompleted
    );

    PCGComponent->OnPCGGraphCancelledExternal.AddUniqueDynamic(
        this,
        &APCGZoneController::HandlePCGGenerationCancelled
    );
}

void APCGZoneController::HandlePCGGenerationCompleted(
    UPCGComponent* PCGComponent
)
{
    const TWeakObjectPtr<UPCGComponent> ComponentKey(PCGComponent);

    if (!bZoneGenerationInProgress
        || !PendingPCGComponents.Contains(ComponentKey))
    {
        return;
    }

    PendingPCGComponents.Remove(ComponentKey);

    PCGComponent->OnPCGGraphGeneratedExternal.RemoveDynamic(
        this,
        &APCGZoneController::HandlePCGGenerationCompleted
    );

    PCGComponent->OnPCGGraphCancelledExternal.RemoveDynamic(
        this,
        &APCGZoneController::HandlePCGGenerationCancelled
    );

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ZoneGeneration] PCG completed. Actor=%s Progress=%d/%d"),
        *GetNameSafe(PCGComponent->GetOwner()),
        TotalPCGComponentCount - PendingPCGComponents.Num(),
        TotalPCGComponentCount
    );

    TryFinishZoneGeneration();
}

void APCGZoneController::HandlePCGGenerationCancelled(
    UPCGComponent* PCGComponent
)
{
    const TWeakObjectPtr<UPCGComponent> ComponentKey(PCGComponent);

    if (!bZoneGenerationInProgress
        || !PendingPCGComponents.Contains(ComponentKey))
    {
        return;
    }

    PendingPCGComponents.Remove(ComponentKey);
    bZoneGenerationFailed = true;

    PCGComponent->OnPCGGraphGeneratedExternal.RemoveDynamic(
        this,
        &APCGZoneController::HandlePCGGenerationCompleted
    );

    PCGComponent->OnPCGGraphCancelledExternal.RemoveDynamic(
        this,
        &APCGZoneController::HandlePCGGenerationCancelled
    );

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[ZoneGeneration] PCG cancelled. Actor=%s"),
        *GetNameSafe(PCGComponent->GetOwner())
    );

    TryFinishZoneGeneration();
}

void APCGZoneController::TryFinishZoneGeneration()
{
    // 생성 중이 아니거나, 아직 대상을 등록 중이거나,
    // 완료되지 않은 PCG가 남아 있으면 기다림
    if (!bZoneGenerationInProgress
        || bRegisteringPCGComponents
        || !PendingPCGComponents.IsEmpty())
    {
        return;
    }

    GetWorldTimerManager().ClearTimer(
        PCGGenerationTimeoutHandle
    );

    const bool bGameplayReady =
        IsValid(SpawnPointActor)
        && IsValid(GoalPointActor)
        && RegisteredCombatSpawners.Num() == ExpectedCombatAreaCount;

    if (!bGameplayReady)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[ZoneGeneration] Required gameplay markers or spawners are missing.")
        );
    }

    const bool bSuccess =
        !bZoneGenerationFailed
        && TotalPCGComponentCount > 0
        && bGameplayReady;

    const bool bNotifyBackendResult = bWaitingForBackendPCG;

    bZoneGenerationInProgress = false;
    bWaitingForBackendPCG = false;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ZoneGeneration] Finished. Success=%s Total=%d"),
        bSuccess ? TEXT("true") : TEXT("false"),
        TotalPCGComponentCount
    );

    // 로컬 생성은 로그만 출력하고,
    // 백엔드 요청으로 시작한 생성은 결과도 전달
    if (bNotifyBackendResult)
    {
        FinishBackendRequest(
            bSuccess,
            bSuccess
            ? TEXT("Zone generation completed.")
            : TEXT("Zone generation failed.")
        );
    }
}

void APCGZoneController::ClearPCGGenerationTracking(
    bool bCancelPending
)
{
    GetWorldTimerManager().ClearTimer(
        PCGGenerationTimeoutHandle
    );

    // 취소 중 이벤트가 발생해도 원본 목록에 영향이 없도록 복사
    const TArray<TWeakObjectPtr<UPCGComponent>> Components =
        PendingPCGComponents.Array();

    PendingPCGComponents.Reset();

    // 먼저 모든 이벤트 연결 해제
    for (const TWeakObjectPtr<UPCGComponent>& ComponentRef : Components)
    {
        UPCGComponent* PCGComponent = ComponentRef.Get();

        if (!IsValid(PCGComponent))
        {
            continue;
        }

        PCGComponent->OnPCGGraphGeneratedExternal.RemoveDynamic(
            this,
            &APCGZoneController::HandlePCGGenerationCompleted
        );

        PCGComponent->OnPCGGraphCancelledExternal.RemoveDynamic(
            this,
            &APCGZoneController::HandlePCGGenerationCancelled
        );
    }

    if (!bCancelPending || !GetWorld())
    {
        return;
    }

    UPCGSubsystem* PCGSubsystem =
        GetWorld()->GetSubsystem<UPCGSubsystem>();

    if (!PCGSubsystem)
    {
        return;
    }

    // 아직 남아 있는 생성 작업 취소
    for (const TWeakObjectPtr<UPCGComponent>& ComponentRef : Components)
    {
        if (UPCGComponent* PCGComponent = ComponentRef.Get())
        {
            PCGSubsystem->CancelGeneration(PCGComponent);
        }
    }
}

void APCGZoneController::HandlePCGGenerationTimeout()
{
    if (!bZoneGenerationInProgress)
    {
        return;
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[ZoneGeneration] Timed out. Pending=%d Total=%d"),
        PendingPCGComponents.Num(),
        TotalPCGComponentCount
    );

    const bool bNotifyBackendResult = bWaitingForBackendPCG;

    // 취소 이벤트가 들어와도 완료로 처리하지 않도록 먼저 종료
    bZoneGenerationInProgress = false;
    bRegisteringPCGComponents = false;
    bZoneGenerationFailed = true;
    bWaitingForBackendPCG = false;

    ClearPCGGenerationTracking(true);

    if (bNotifyBackendResult)
    {
        FinishBackendRequest(
            false,
            TEXT("Map generation timed out.")
        );
    }
}

void APCGZoneController::EndPlay(
    const EEndPlayReason::Type EndPlayReason
)
{
    bZoneGenerationInProgress = false;
    bRegisteringPCGComponents = false;
    bWaitingForBackendPCG = false;

    ClearPCGGenerationTracking(true);

    Super::EndPlay(EndPlayReason);
}

void APCGZoneController::MarkZoneGenerationFailed(
    const FString& Reason
)
{
    if (bZoneGenerationInProgress)
    {
        bZoneGenerationFailed = true;
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[ZoneGeneration] Preparation failed: %s"),
        *Reason
    );
}

void APCGZoneController::AddEnvironmentExclusion(
    AActor* MarkerActor,
    float Radius
)
{
    if (!IsValid(MarkerActor)
        || !IsValid(MarkerActor->GetRootComponent()))
    {
        MarkZoneGenerationFailed(
            TEXT("Environment exclusion marker has no valid root.")
        );
        return;
    }

    const float SafeRadius = FMath::Max(Radius, 1.0f);

    UCapsuleComponent* ExclusionShape =
        NewObject<UCapsuleComponent>(MarkerActor);

    ExclusionShape->SetupAttachment(
        MarkerActor->GetRootComponent()
    );

    ExclusionShape->SetRelativeLocation(FVector::ZeroVector);

    // 마커의 스케일과 관계없이 지정한 반지름 사용
    ExclusionShape->SetAbsolute(false, false, true);

    // 현재 지면까지 포함하도록 세로로 넉넉하게 설정
    ExclusionShape->InitCapsuleSize(
        SafeRadius,
        SafeRadius + 600.0f
    );

    // 플레이어나 적의 이동·구역 판정에 영향을 주지 않음
    ExclusionShape->SetCollisionEnabled(
        ECollisionEnabled::QueryOnly
    );
    ExclusionShape->SetCollisionResponseToAllChannels(ECR_Ignore);
    ExclusionShape->SetGenerateOverlapEvents(false);
    ExclusionShape->SetCanEverAffectNavigation(false);

    ExclusionShape->ComponentTags.AddUnique(
        FName(TEXT("ZoneExclusionShape"))
    );

    MarkerActor->AddInstanceComponent(ExclusionShape);
    ExclusionShape->RegisterComponent();

    MarkerActor->Tags.AddUnique(
        FName(TEXT("ZoneEnvExclusion"))
    );

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[EnvironmentExclusion] Added. Actor=%s Radius=%.1f"),
        *MarkerActor->GetName(),
        SafeRadius
    );
}

void APCGZoneController::AddPathEnvironmentExclusion(
    AActor* PathActor,
    USplineComponent* SplineComponent,
    float PathWidth
)
{
    if (!IsValid(PathActor)
        || !IsValid(SplineComponent)
        || !IsValid(PathActor->GetRootComponent()))
    {
        MarkZoneGenerationFailed(
            TEXT("Path exclusion requires a valid actor and spline.")
        );
        return;
    }

    const float SplineLength = SplineComponent->GetSplineLength();

    if (!FMath::IsFinite(SplineLength)
        || SplineLength <= KINDA_SMALL_NUMBER
        || !FMath::IsFinite(PathWidth)
        || PathWidth <= 0.0f)
    {
        MarkZoneGenerationFailed(
            TEXT("Path exclusion has invalid length or width.")
        );
        return;
    }

    const float Padding = FMath::Max(PathExclusionPadding, 0.0f);
    const float HalfWidth = PathWidth * 0.5f + Padding;

    const int32 SegmentCount = FMath::Max(
        FMath::CeilToInt(SplineLength / 200.0f),
        1
    );

    const float SegmentDistance =
        SplineLength / static_cast<float>(SegmentCount);

    int32 CreatedCount = 0;

    for (int32 Index = 0; Index < SegmentCount; ++Index)
    {
        const FVector Start =
            SplineComponent->GetLocationAtDistanceAlongSpline(
                SegmentDistance * Index,
                ESplineCoordinateSpace::World
            );

        const FVector End =
            SplineComponent->GetLocationAtDistanceAlongSpline(
                SegmentDistance * (Index + 1),
                ESplineCoordinateSpace::World
            );

        FVector Direction = End - Start;
        Direction.Z = 0.0f;

        const float HorizontalLength = Direction.Size();

        if (HorizontalLength <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        const FVector Center = (Start + End) * 0.5f;

        UBoxComponent* ExclusionBox =
            NewObject<UBoxComponent>(PathActor);

        ExclusionBox->SetupAttachment(
            PathActor->GetRootComponent()
        );

        // 지정한 폭을 월드 단위로 사용
        ExclusionBox->SetAbsolute(false, false, true);

        ExclusionBox->SetWorldLocationAndRotation(
            Center,
            Direction.Rotation()
        );

        // 짧은 박스들을 겹쳐서 곡선 구간도 비움
        ExclusionBox->InitBoxExtent(
            FVector(
                HorizontalLength * 0.5f + Padding,
                HalfWidth,
                1000.0f
            )
        );

        ExclusionBox->SetCollisionEnabled(
            ECollisionEnabled::QueryOnly
        );
        ExclusionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
        ExclusionBox->SetGenerateOverlapEvents(false);
        ExclusionBox->SetCanEverAffectNavigation(false);

        ExclusionBox->ComponentTags.AddUnique(
            FName(TEXT("ZoneExclusionShape"))
        );

        PathActor->AddInstanceComponent(ExclusionBox);
        ExclusionBox->RegisterComponent();

        ++CreatedCount;
    }

    if (CreatedCount == 0)
    {
        MarkZoneGenerationFailed(
            TEXT("No path exclusion boxes were created.")
        );
        return;
    }

    PathActor->Tags.AddUnique(
        FName(TEXT("ZoneEnvExclusion"))
    );

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[PathExclusion] Created. Actor=%s Boxes=%d Width=%.1f Padding=%.1f"),
        *PathActor->GetName(),
        CreatedCount,
        PathWidth,
        Padding
    );
}
