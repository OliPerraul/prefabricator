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
struct PREFABRICATORRUNTIME_API FPrefabricatorNestedPropertyData
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

TSoftObjectPtr<UObject> ResolveObjectPath(TSoftObjectPtr<UObject>)
{
	static const FString SoftReferenceSearchPattern = "([A-Za-z0-9_]+)'(.*?)'";
	const FRegexPattern Pattern(*SoftReferenceSearchPattern);
	auto& NestedPropertyDataItem = Entry.Value;
	FRegexMatcher Matcher(Pattern, NestedPropertyDataItem.ExportedValue);

	while (Matcher.FindNext()) {
		FString FullPath = Matcher.GetCaptureGroup(0);
		FString ClassName = Matcher.GetCaptureGroup(1);
		FString ObjectPath = Matcher.GetCaptureGroup(2);
		if (ClassName == "PrefabricatorAssetUserData") {
			continue;
		}
		bool bUseQuotes = false;
		if (ObjectPath.Len() >= 2 && ObjectPath.StartsWith("\"") && ObjectPath.EndsWith("\"")) {
			ObjectPath = ObjectPath.Mid(1, ObjectPath.Len() - 2);
			bUseQuotes = true;
		}

		FSoftObjectPath SoftPath(ObjectPath);

		FPrefabricatorPropertyAssetMapping Mapping;
		Mapping.AssetReference = SoftPath;
		{
			Mapping.AssetClassName = ClassName;
			Mapping.AssetObjectPath = *ObjectPath;
			Mapping.bUseQuotes = bUseQuotes;			
			//UE_LOG(LogPrefabricatorAsset, Log, TEXT("######>>> Found Asset Ref: [%s][%s] | %s"), *Mapping.AssetClassName, *Mapping.AssetObjectPath.ToString(), *Mapping.AssetReference.GetAssetPathName().ToString());
		}
	}

}

UCLASS()
class PREFABRICATORRUNTIME_API UPrefabricatorProperty : public UObject {
	GENERATED_BODY()
public:
	UPROPERTY()
	FString PropertyName;

	UPROPERTY()
	FString ExportedValue;

	UPROPERTY()
	TArray<FPrefabricatorPropertyAssetMapping> AssetSoftReferenceMappings;

	UPROPERTY()
	bool bIsCrossReferencedActor = false;

	UPROPERTY()
	bool bContainsStructProperty = false;

	UPROPERTY()
	TMap<FString, FPrefabricatorNestedPropertyData> NestedPropertyData;

	void SaveReferencedAssetValues();
	void LoadReferencedAssetValues();

	private:
	bool LoadReferencedAssetValues(FPrefabricatorPropertyAssetMapping& Mapping, FString& OutExportedValue);
};

USTRUCT()
struct PREFABRICATORRUNTIME_API FPrefabricatorEntryBase 
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid PrefabItemID;

	UPROPERTY()
	FString ClassPath;

	UPROPERTY()
	FSoftClassPath ClassPathRef;

	UPROPERTY()
	FTransform RelativeTransform;

	UPROPERTY()
	TArray<TObjectPtr<UPrefabricatorProperty>> Properties;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FString Name;
#endif // WITH_EDITORONLY_DATA
};

USTRUCT()
struct PREFABRICATORRUNTIME_API FPrefabricatorComponentData : public FPrefabricatorEntryBase
{
    GENERATED_BODY()
};

USTRUCT()
struct PREFABRICATORRUNTIME_API FPrefabricatorActorData : public FPrefabricatorEntryBase
{
    GENERATED_BODY()

    UPROPERTY()
    TMap<TSoftObjectPtr<UObject>, FPrefabricatorComponentData> Components;
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

USTRUCT(BlueprintType)
struct PREFABRICATORRUNTIME_API FPrefabricatorPropertyCacheEntry
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FString, TWeakObjectPtr<UPrefabricatorProperty>> Properties;
}

UCLASS(Blueprintable)
class PREFABRICATORRUNTIME_API UPrefabricatorAsset : public UPrefabricatorAssetInterface {
	GENERATED_UCLASS_BODY()
public:

	UPROPERTY()
	TArray<TSoftObjectPtr<UObject>, FPrefabricatorComponentData> ComponentData;

	UPROPERTY()
	TMap<TSoftObjectPtr<UObject>, FPrefabricatorActorData> ActorData;

	UPROPERTY()
	TEnumAsByte<EComponentMobility::Type> PrefabMobility;

	// The ID that is regenerated on every update
	// This allows prefab actors to test against their own LastUpdateID and determine if a refresh is needed
	UPROPERTY()
	FGuid LastUpdateID;


	/** Information for thumbnail rendering */
	UPROPERTY()
	class UThumbnailInfo* ThumbnailInfo;

	UPROPERTY()
	uint32 Version;

public:
	virtual UPrefabricatorAsset* GetPrefabAsset(const FPrefabAssetSelectionConfig& InConfig) override;

	void AddCacheEntry(UPrefabricatorProperty* Property, const FString& PropertyPath, const FGuid& Guid);

	UPrefabricatorProperty* GetCacheEntry(const FGuid& Guid, const FString& PropertyPath);
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

