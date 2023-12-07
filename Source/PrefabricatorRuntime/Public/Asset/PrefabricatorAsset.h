//$ Copyright 2015-23, Code Respawn Technologies Pvt Ltd - All Rights Reserved $//

#pragma once
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

#include "PrefabricatorAsset.generated.h"

class APrefabActor;

USTRUCT()
struct PREFABRICATORRUNTIME_API FPrefabricatorPropertyAssetMapping {
	GENERATED_BODY()

	UPROPERTY()
	FSoftObjectPath AssetReference;

	UPROPERTY()
	FString AssetClassName;

	UPROPERTY()
	FName AssetObjectPath;

	UPROPERTY()
	bool bUseQuotes = false;
};


USTRUCT()
struct PREFABRICATORRUNTIME_API FPrefabPropertySerializedItem
{
	GENERATED_BODY()

	/// Array length is used to initialize array inside FixupCrossReferences phase of serialization
	UPROPERTY()
	int32 ArrayLength = -1;

	UPROPERTY()
	FGuid CrossReferencePrefabActorId;

	UPROPERTY()
	FString ExportedValue;

	UPROPERTY()
	TArray<FPrefabricatorPropertyAssetMapping> AssetSoftReferenceMappings;

	//UPROPERTY()
	//bool bShouldSkipSerialization = false;
};


namespace
{
	//PREFABRICATORRUNTIME_API FString ResolveObjectPath(TSoftObjectPtr<UObject> Reference);

	PREFABRICATORRUNTIME_API bool LoadReferencedAssetValues(FPrefabricatorPropertyAssetMapping& Mapping, FString& OutExportedValue);
}

UCLASS(BlueprintType)
class PREFABRICATORRUNTIME_API UPrefabricatorProperty : public UObject {
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere)
	FString PropertyName;

	UPROPERTY(EditAnywhere)
	FString ExportedValue;

	UPROPERTY(EditAnywhere)
	TArray<FPrefabricatorPropertyAssetMapping> AssetSoftReferenceMappings;

	UPROPERTY(EditAnywhere)
	bool bIsCrossReferencedActor = false;

	UPROPERTY(EditAnywhere)
	bool bContainsStructProperty = false;

	UPROPERTY(EditAnywhere)
	TMap<FString, FPrefabPropertySerializedItem> SerializedItems;

	void SaveReferencedAssetValues();
	void LoadReferencedAssetValues();
};
using UPrefabricatorPropertyMap = TMap<FString, TObjectPtr<UPrefabricatorProperty>>;

USTRUCT(BlueprintType)
struct PREFABRICATORRUNTIME_API FPrefabricatorItemBase 
{
	GENERATED_BODY(EditAnywhere)

	UPROPERTY(EditAnywhere)
	FGuid PrefabItemID;

	UPROPERTY(EditAnywhere)
	FString ClassPath;

	UPROPERTY(EditAnywhere)
	FSoftClassPath ClassPathRef;

	UPROPERTY(EditAnywhere)
	FTransform RelativeTransform;

	UPROPERTY(EditAnywhere)
	TMap<FString, TObjectPtr<UPrefabricatorProperty>> Properties;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere)
	FString Name;

	UPROPERTY(EditAnywhere)
	bool bIsStale = false;
#endif // WITH_EDITORONLY_DATA
};

USTRUCT(BlueprintType)
struct PREFABRICATORRUNTIME_API FPrefabricatorComponentData : public FPrefabricatorItemBase
{
    GENERATED_BODY()
};

USTRUCT(BlueprintType)
struct PREFABRICATORRUNTIME_API FPrefabricatorActorData : public FPrefabricatorItemBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, meta = (TitleProperty = Name))
    TMap<FGuid, FPrefabricatorComponentData> Components;
};

struct FPrefabAssetSelectionConfig {
	int32 Seed = 0;
};

UCLASS(Blueprintable)
class PREFABRICATORRUNTIME_API UPrefabricatorEventListener : public UObject {
	GENERATED_BODY()
public:
	/** Called when the prefab and all its child prefabs have been spawned and initialized */
	UFUNCTION(BlueprintNativeEvent, Category = "Prefabricator")
	void PostSpawn(APrefabActor* Prefab);
	virtual void PostSpawn_Implementation(APrefabActor* Prefab);
};

UCLASS(Abstract, BlueprintType, Blueprintable)
class PREFABRICATORRUNTIME_API UPrefabricatorAssetInterface : public UObject {
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Prefabricator")
	TSubclassOf<UPrefabricatorEventListener> EventListener;

	UPROPERTY(EditAnywhere, Category = "Replication")
	bool bReplicates = false;

public:
	virtual class UPrefabricatorAsset* GetPrefabAsset(const FPrefabAssetSelectionConfig& InConfig) { return nullptr; }
};

enum class EPrefabricatorAssetVersion {
	InitialVersion = 0,
	AddedSoftReference,
	AddedSoftReference_PrefabFix,

	//----------- Versions should be placed above this line -----------------
	LastVersionPlusOne,
	LatestVersion = LastVersionPlusOne -1
};

//USTRUCT(BlueprintType)
//struct PREFABRICATORRUNTIME_API FPrefabricatorPropertyCacheEntry
//{
//	GENERATED_BODY()
//
//	UPROPERTY()
//	TMap<FString, TWeakObjectPtr<UPrefabricatorProperty>> Properties;
//};

UCLASS(Blueprintable)
class PREFABRICATORRUNTIME_API UPrefabricatorAsset : public UPrefabricatorAssetInterface {
	GENERATED_UCLASS_BODY()
public:

	UPROPERTY(EditAnywhere, meta = (TitleProperty = Name))
	TMap<FGuid, FPrefabricatorComponentData> ComponentData;

	UPROPERTY(EditAnywhere, meta=(TitleProperty=Name))
	TMap<FGuid, FPrefabricatorActorData> ActorData;

	UPROPERTY(EditAnywhere)
	TEnumAsByte<EComponentMobility::Type> PrefabMobility;

	// The ID that is regenerated on every update
	// This allows prefab actors to test against their own LastUpdateID and determine if a refresh is needed
	UPROPERTY(EditAnywhere)
	FGuid LastUpdateID;


	/** Information for thumbnail rendering */
	UPROPERTY(EditAnywhere)
	class UThumbnailInfo* ThumbnailInfo;

	UPROPERTY(EditAnywhere)
	uint32 Version;

public:
	virtual UPrefabricatorAsset* GetPrefabAsset(const FPrefabAssetSelectionConfig& InConfig) override;

	//void AddCacheEntry(UPrefabricatorProperty* Property, const FString& PropertyPath, const FGuid& Guid);

	//UPrefabricatorProperty* GetCacheEntry(const FGuid& Guid, const FString& PropertyPath);
};


USTRUCT(BlueprintType)
struct PREFABRICATORRUNTIME_API FPrefabricatorAssetCollectionItem {
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = "Prefabricator")
	TSoftObjectPtr<UPrefabricatorAsset> PrefabAsset;

	UPROPERTY(EditAnywhere, Category = "Prefabricator")
	float Weight = 1.0f;
};

enum class EPrefabricatorCollectionAssetVersion {
	InitialVersion = 0,

	//----------- Versions should be placed above this line -----------------
	LastVersionPlusOne,
	LatestVersion = LastVersionPlusOne - 1
};

UCLASS(Blueprintable)
class PREFABRICATORRUNTIME_API UPrefabricatorAssetCollection : public UPrefabricatorAssetInterface {
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Prefabricator")
	TArray<FPrefabricatorAssetCollectionItem> Prefabs;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Prefabricator")
	TSoftObjectPtr<UTexture2D> CustomThumbnail;
#endif // WITH_EDITORONLY_DATA

	UPROPERTY()
	uint32 Version;

public:
	virtual UPrefabricatorAsset* GetPrefabAsset(const FPrefabAssetSelectionConfig& InConfig) override;
};


class PREFABRICATORRUNTIME_API FPrefabricatorAssetUtils {
public:
	static FVector FindPivot(const TArray<AActor*>& InActors);
	static EComponentMobility::Type FindMobility(const TArray<AActor*>& InActors);
	
};

