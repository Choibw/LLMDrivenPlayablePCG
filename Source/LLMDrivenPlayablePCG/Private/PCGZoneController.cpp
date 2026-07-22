// Fill out your copyright notice in the Description page of Project Settings.


#include "PCGZoneController.h"
#include "PCGComponent.h"
#include "PCGGraph.h"

#include "Components/SplineComponent.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"

// Sets default values
APCGZoneController::APCGZoneController()
{
	PrimaryActorTick.bCanEverTick = false;

}

void APCGZoneController::GenerateZone()
{
    UpdateZoneBoundsFromBaseEnvironment();
    ApplyBaseEnvironmentPCG();
    ApplyPathPCG();
    ApplyAreaPCG();

    ClearSpawnedGameplayMarkers();
    ApplyGameplayMarkers();
}

void APCGZoneController::UpdateZoneBoundsFromBaseEnvironment()
{
    if (!BaseEnvironmentPCGActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] BaseEnvironmentPCGActor is not set."));
        return;
    }

    FVector BoundsOrigin;
    FVector BoundsExtent;

    BaseEnvironmentPCGActor->GetActorBounds(false, BoundsOrigin, BoundsExtent);

    CurrentZoneData.ZoneSize = FVector2D(
        BoundsExtent.X * 2.0f,
        BoundsExtent.Y * 2.0f
    );

    if (!PathPCGActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] PathPCGActor is not set."));
        return;
    }

    CurrentZoneData.ZoneCenter = FVector(BoundsOrigin.X, BoundsOrigin.Y, PathPCGActor->GetActorLocation().Z);

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
        return;
    }

    BaseEnvironmentPCGComponent = BaseEnvironmentPCGActor->FindComponentByClass<UPCGComponent>();

    if (!BaseEnvironmentPCGComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] TargetPCGComponent is null."));
        return;
    }

    UPCGGraphInterface* GraphInterface = BaseEnvironmentPCGComponent->GetGraphInstance();

    if (!GraphInterface)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] PCG GraphInterface is null."));
        return;
    }

    const float Tree = FMath::Clamp(CurrentZoneData.TreeDensity, 0.0f, 1.0f);
    const float Rock = FMath::Clamp(CurrentZoneData.RockDensity, 0.0f, 1.0f);
    const float Grass = FMath::Clamp(CurrentZoneData.GrassDensity, 0.0f, 1.0f);

    GraphInterface->SetGraphParameter(FName("TreeDensity"), Tree);
    GraphInterface->SetGraphParameter(FName("RockDensity"), Rock);
    GraphInterface->SetGraphParameter(FName("GrassDensity"), Grass);

    BaseEnvironmentPCGComponent->Cleanup();
    BaseEnvironmentPCGComponent->GenerateLocal(true);

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
    if (!PathPCGActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] PathPCGActor is not set."));
        return;
    }

    const TArray<FVector2D>& NormalizedPoints = CurrentZoneData.MainPath.NormalizedPathPoints;

    // 최소 두개는 있어야함
    if (NormalizedPoints.Num() < 2)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[PCGZoneController] Path requires at least 2 normalized points. Current Count=%d"),
            NormalizedPoints.Num()
        );
        return;
    }

    // 현재 프로토타입에서는 Z축은 에디터에서 맞춘 PathPCGActor의 기존 Z를 사용한다.
    PathPCGActor->SetActorLocation(CurrentZoneData.ZoneCenter);

    USplineComponent* PathSplineComponent = PathPCGActor->FindComponentByClass<USplineComponent>();

    if (!PathSplineComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] SplineComponent is not found in PathPCGActor."));
        return;
    }

    PathSplineComponent->ClearSplinePoints(false);

    // Normalized 좌표를 Zone 기준 Local 좌표로 변환해서 Spline Point 추가
    // 현재는 ActorBounds가 생성된 에셋까지 포함해 실제 PCG Volume보다 커질 수 있으므로,
    // 길은 Zone 전체가 아니라 내부 영역만 사용하도록 축소된 PathUsableSize를 기준으로 생성한다.
    const float PathAreaScale = 0.65f;

    const FVector2D PathUsableSize(
        CurrentZoneData.ZoneSize.X * PathAreaScale,
        CurrentZoneData.ZoneSize.Y * PathAreaScale
    );

    for (const FVector2D& NormalizedPoint : NormalizedPoints)
    {
        const float LocalX = (NormalizedPoint.X - 0.5f) * PathUsableSize.X;
        const float LocalY = (NormalizedPoint.Y - 0.5f) * PathUsableSize.Y;

        // 현재는 Z 자동 보정 없이, PathPCGActor 기준 Local Z = 0으로 둔다.
        const FVector LocalPoint(LocalX, LocalY, 0.0f);

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[PCGZoneController] PathPoint Normalized=(%f, %f), Local=(%f, %f), PathUsableSize=(%f, %f)"),
            NormalizedPoint.X,
            NormalizedPoint.Y,
            LocalX,
            LocalY,
            PathUsableSize.X,
            PathUsableSize.Y
        );

        PathSplineComponent->AddSplinePoint(
            LocalPoint,
            ESplineCoordinateSpace::Local,
            false
        );
    }

    // SplineComponent 내부 곡선 데이터 갱신
    PathSplineComponent->UpdateSpline();

    UPCGComponent* PathPCGComponent = PathPCGActor->FindComponentByClass<UPCGComponent>();

    if (!PathPCGComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] PCGComponent is not found in PathPCGActor."));
        return;
    }

    // Spline 입력 변경을 PCG가 다시 반영하도록 생성 결과를 Dirty 처리 후 재생성
    PathPCGComponent->Cleanup();
    PathPCGComponent->DirtyGenerated(EPCGComponentDirtyFlag::All);
    PathPCGComponent->Generate(true);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[PCGZoneController] Path PCG applied. Type=%d, Width=%f, PointCount=%d, ZoneCenter=%s, ZoneSize=%s"),
        static_cast<int32>(CurrentZoneData.MainPath.PathType),
        CurrentZoneData.MainPath.PathWidth,
        NormalizedPoints.Num(),
        *CurrentZoneData.ZoneCenter.ToString(),
        *CurrentZoneData.ZoneSize.ToString()
    );
}

void APCGZoneController::ApplyAreaPCG()
{
    const int32 AreaDataCount = CurrentZoneData.Areas.Num();
    const int32 AreaActorCount = AreaPCGActors.Num();
    const int32 ApplyCount = FMath::Min(AreaDataCount, AreaActorCount);

    if (ApplyCount <= 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[PCGZoneController] No area data or area PCG actors. AreaDataCount=%d, AreaActorCount=%d"),
            AreaDataCount,
            AreaActorCount
        );
        return;
    }

    // Area 반지름은 Zone의 짧은 축을 기준으로 계산한다.
    const float BaseZoneLength = FMath::Min(CurrentZoneData.ZoneSize.X, CurrentZoneData.ZoneSize.Y);

    // 현재 프로토타입에서 Area BP의 기본 원형 반지름 기준값.
    // 에디터에서 실제 Area BP 크기를 보면서 조정 가능.
    const float DefaultAreaRadius = 500.0f;

    for (int32 i = 0; i < ApplyCount; ++i)
    {
        AActor* AreaPCGActor = AreaPCGActors[i];
        const FZoneAreaData& AreaData = CurrentZoneData.Areas[i];

        if (!AreaPCGActor)
        {
            UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] AreaPCGActor[%d] is null."), i);
            continue;
        }

        // NormalizedCenter를 Zone 중심 기준 Local 좌표로 변환
        const float LocalX = (AreaData.NormalizedCenter.X - 0.5f) * CurrentZoneData.ZoneSize.X;
        const float LocalY = (AreaData.NormalizedCenter.Y - 0.5f) * CurrentZoneData.ZoneSize.Y;

        // World 위치 계산
        // Z는 현재 AreaPCGActor가 에디터에서 맞춰둔 높이를 유지한다.
        const FVector CurrentActorLocation = AreaPCGActor->GetActorLocation();

        const FVector AreaWorldLocation(
            CurrentZoneData.ZoneCenter.X + LocalX,
            CurrentZoneData.ZoneCenter.Y + LocalY,
            CurrentActorLocation.Z
        );

        AreaPCGActor->SetActorLocation(AreaWorldLocation);

        // NormalizedRadius를 실제 WorldRadius로 변환
        const float WorldRadius = AreaData.NormalizedRadius * BaseZoneLength;

        // Area BP의 기본 반지름 대비 스케일 계산
        const float SafeDefaultAreaRadius = FMath::Max(DefaultAreaRadius, 1.0f);
        const float AreaScale = WorldRadius / SafeDefaultAreaRadius;

        AreaPCGActor->SetActorScale3D(FVector(AreaScale, AreaScale, 1.0f));

        // Area 내부 PCGComponent 갱신
        UPCGComponent* AreaPCGComponent = AreaPCGActor->FindComponentByClass<UPCGComponent>();

        if (AreaPCGComponent)
        {
            AreaPCGComponent->Cleanup();
            AreaPCGComponent->DirtyGenerated(EPCGComponentDirtyFlag::All);
            AreaPCGComponent->Generate(true);
        }
        else
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[PCGZoneController] PCGComponent is not found in AreaPCGActor[%d]."),
                i
            );
        }

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[PCGZoneController] Area PCG applied. Index=%d, Type=%d, NormalizedCenter=(%f, %f), WorldLocation=%s, NormalizedRadius=%f, WorldRadius=%f, Scale=%f"),
            i,
            static_cast<int32>(AreaData.AreaType),
            AreaData.NormalizedCenter.X,
            AreaData.NormalizedCenter.Y,
            *AreaWorldLocation.ToString(),
            AreaData.NormalizedRadius,
            WorldRadius,
            AreaScale
        );
    }

    if (AreaDataCount != AreaActorCount)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[PCGZoneController] Area data count and actor count are different. AreaDataCount=%d, AreaActorCount=%d, Applied=%d"),
            AreaDataCount,
            AreaActorCount,
            ApplyCount
        );
    }
}

void APCGZoneController::LoadSampleZoneJson()
{
    const FString SampleJson = TEXT(R"(
    {
      "theme": "forest",
      "base_environment": {
        "tree_density": 0.3,
        "rock_density": 0.3,
        "grass_density": 0.7
      },
      "main_path": {
        "path_type": "main",
        "path_width": 500,
        "normalized_points": [
          { "x": 0.05, "y": 0.60 },
          { "x": 0.25, "y": 0.55 },
          { "x": 0.55, "y": 0.40 },
          { "x": 0.80, "y": 0.60 },
          { "x": 0.95, "y": 0.50 }
        ]
      },
      "areas": [
        {
          "area_type": "spawn",
          "normalized_center": { "x": 0.12, "y": 0.50 },
          "normalized_radius": 0.05,
          "detail_density": 0.2
        },
        {
          "area_type": "combat",
          "normalized_center": { "x": 0.40, "y": 0.60 },
          "normalized_radius": 0.10,
          "detail_density": 0.6
        },
        {
          "area_type": "goal",
          "normalized_center": { "x": 0.80, "y": 0.47 },
          "normalized_radius": 0.06,
          "detail_density": 0.4
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
    if (TestNPCQuestion.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] TestNPCQuestion is empty."));
        return;
    }
    if (TestPlayerAnswer.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] TestPlayerAnswer is empty."));
        return;
    }

    // Root JSON Object 생성
    TSharedPtr<FJsonObject> RequestRootObject = MakeShared<FJsonObject>();

    // dialogue 배열 생성
    TArray<TSharedPtr<FJsonValue>> DialogueArray;

    // NPC 대화 Object
    TSharedPtr<FJsonObject> NPCDialogueObject = MakeShared<FJsonObject>();
    NPCDialogueObject->SetStringField(TEXT("speaker"), TEXT("npc"));
    NPCDialogueObject->SetStringField(TEXT("text"), TestNPCQuestion);

    DialogueArray.Add(MakeShared<FJsonValueObject>(NPCDialogueObject));

    // Player 대화 Object
    TSharedPtr<FJsonObject> PlayerDialogueObject = MakeShared<FJsonObject>();
    PlayerDialogueObject->SetStringField(TEXT("speaker"), TEXT("player"));
    PlayerDialogueObject->SetStringField(TEXT("text"), TestPlayerAnswer);

    DialogueArray.Add(MakeShared<FJsonValueObject>(PlayerDialogueObject));

    // Root에 dialogue 배열 추가
    RequestRootObject->SetArrayField(TEXT("dialogue"), DialogueArray);

    // JSON Object를 FString으로 변환
    FString RequestBody;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);

    if (!FJsonSerializer::Serialize(RequestRootObject.ToSharedRef(), Writer))
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] Failed to serialize request JSON."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[PCGZoneController] Backend Request Body:\n%s"), *RequestBody);


    // HTTP 요청하기
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();

    HttpRequest->SetURL(BackendUrl);
    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    HttpRequest->SetContentAsString(RequestBody);

    HttpRequest->OnProcessRequestComplete().BindUObject(
        this,
        &APCGZoneController::OnZoneDataResponseReceived
    );

    if (!HttpRequest->ProcessRequest())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] Failed to start HTTP request."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[PCGZoneController] HTTP request sent to backend. URL=%s"), *BackendUrl);
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

void APCGZoneController::OnZoneDataResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    UE_LOG(
        LogTemp,
        Log,
        TEXT("[PCGZoneController] Backend response received. Success=%s"),
        bWasSuccessful ? TEXT("true") : TEXT("false")
    );

    if (!bWasSuccessful || !Response.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] Backend request failed or response is invalid."));
        return;
    }

    const int32 ResponseCode = Response->GetResponseCode();
    const FString ResponseString = Response->GetContentAsString();

    UE_LOG(LogTemp, Log, TEXT("[PCGZoneController] Backend Response Code: %d"), ResponseCode);
    UE_LOG(LogTemp, Log, TEXT("[PCGZoneController] Backend Response Body:\n%s"), *ResponseString);

    if (ResponseCode < 200 || ResponseCode >= 300)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[PCGZoneController] Backend returned non-success status code: %d"),
            ResponseCode
        );
        return;
    }

    if (ResponseString.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] Backend response body is empty."));
        return;
    }

    if (!ApplyZoneJsonString(ResponseString))
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCGZoneController] Failed to apply backend response JSON."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[PCGZoneController] Backend zone JSON applied successfully."));

    GenerateZone();
}

void APCGZoneController::ClearSpawnedGameplayMarkers()
{
    for (AActor* Marker : SpawnedGameplayMarkers)
    {
        if (IsValid(Marker))
        {
            Marker->Destroy();
        }
    }

    SpawnedGameplayMarkers.Empty();
    SpawnPointActor = nullptr;
}

void APCGZoneController::ApplyGameplayMarkers()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

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
        default:
            break;
        }

        if (!MarkerClass)
        {
            continue;
        }

        const float LocalX = (AreaData.NormalizedCenter.X - 0.5f) * CurrentZoneData.ZoneSize.X;
        const float LocalY = (AreaData.NormalizedCenter.Y - 0.5f) * CurrentZoneData.ZoneSize.Y;

        FVector SpawnLocation = CurrentZoneData.ZoneCenter + FVector(LocalX, LocalY, 0.0f);

        // 디버그 마커가 바닥에 묻히지 않도록 살짝 위로 올림
        SpawnLocation.Z += 100.0f;

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

            if (AreaData.AreaType == EZoneAreaType::Spawn)
            {
                SpawnPointActor = SpawnedMarker;
            }

            UE_LOG(LogTemp, Log, TEXT("[GameplayMarker] Spawned marker. Type=%d Location=%s"),
                static_cast<int32>(AreaData.AreaType),
                *SpawnLocation.ToString());
        }
    }
}
