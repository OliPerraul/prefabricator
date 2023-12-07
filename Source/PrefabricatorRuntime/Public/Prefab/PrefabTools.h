//$ Copyright 2015-23, Code Respawn Technologies Pvt Ltd - All Rights Reserved $//

#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

class APrefabActor;
class UPrefabricatorAsset;
struct FPrefabricatorActorData;
struct FPrefabricatorComponentData;
class UPrefabricatorProperty;
struct FRandomStream;

struct PREFABRICATORRUNTIME_API FPrefabLoadSettings {
	bool bUnregisterComponentsBeforeLoading = true;
	bool bRandomizeNestedSeed = false;
	bool bSynchronousBuild = true;
	bool bCanLoadFromCachedTemplate = true;
	bool bCanSaveToCachedTemplate = true;
	const FRandomStream* Random = nullptr;
};

struct PREFABRICATORRUNTIME_API FPrefabInstanceTemplateInfo {
	TWeakObjectPtr<AActor> TemplatePtr;
	FGuid PrefabLastUpdateId;
};

class PREFABRICATORRUNTIME_API FPrefabInstanceTemplates {
public:
	void RegisterTemplate(const FGuid& InPrefabItemId, FGuid InPrefabLastUpdateId, AActor* InActor);
	AActor* GetTemplate(const FGuid& InPrefabItemId, FGuid InPrefabLastUpdateId);

private:
	TMap<FGuid, FPrefabInstanceTemplateInfo> PrefabItemTemplates;
};

class PREFABRICATORRUNTIME_API FGlobalPrefabInstanceTemplates {
public:
	FORCEINLINE static FPrefabInstanceTemplates* Get() { return Instance; }

	static void _CreateSingleton();
	static void _ReleaseSingleton();

private:
	static FPrefabInstanceTemplates* Instance;
};

class FPrefabActorLookup {
public:
	void Register(const FString& InActorPath, const FGuid& InPrefabItemId);
	void Register(AActor* InActor, const FGuid& InPrefabItemId);
	void Register(UActorComponent* InComp, const FGuid& InPrefabItemId);
	bool GetPrefabItemId(const FString& InObjectPath, FGuid& OutCrossRefPrefabItem) const;

private:
	TMap<FString, FGuid> ActorPathToItemId;
};

using UPrefabricatorPropertyMap = TMap<FString, TObjectPtr<class UPrefabricatorProperty>>;
class PREFABRICATORRUNTIME_API FPrefabTools {
public:
	static bool CanCreatePrefab();
	static bool CanCreatePrefabNoActors();
	static void CreatePrefab();
	static void CreatePrefabNoActors();
	static APrefabActor* CreatePrefabFromActors(const TArray<AActor*>& Actors, UWorld* World=nullptr);
	static void AssignAssetUserData(AActor* InActor, const FGuid& InItemID, APrefabActor* Prefab);
	static void AssignAssetUserData(UActorComponent* InComp, const FGuid& InItemID, APrefabActor* Prefab);

	static void SaveStateToPrefabAsset(APrefabActor* PrefabActor);
	static void LoadStateFromPrefabAsset(APrefabActor* PrefabActor, const FPrefabLoadSettings& InSettings = FPrefabLoadSettings());

	static void FixupCrossReferences(const UPrefabricatorPropertyMap& PrefabProperties, UObject* ObjToWrite, TMap<FGuid, AActor*>& PrefabItemToActorMap);

	static void UnlinkAndDestroyPrefabActor(APrefabActor* PrefabActor);
	static void GetActorChildren(AActor* InParent, TArray<AActor*>& OutChildren);

	static FBox GetPrefabBounds(AActor* PrefabActor, bool bNonColliding = true);
	static bool ShouldIgnorePropertySerialization(const FName& PropertyName);
	static bool ShouldForcePropertySerialization(const FName& PropertyName);

	static void ParentActors(AActor* ParentActor, AActor* ChildActor);
	static void SelectPrefabActor(AActor* PrefabActor);
	static void GetSelectedActors(TArray<AActor*>& OutActors);
	static int GetNumSelectedActors();
	static UPrefabricatorAsset* CreatePrefabAsset();
	static int32 GetRandomSeed(const FRandomStream& Random);

	static void IterateChildrenRecursive(APrefabActor* Actor, TFunction<void(AActor*)> Visit);

private:
	static void SaveActorState(AActor* InActor, APrefabActor* PrefabActor, const FPrefabActorLookup& CrossReferences, FPrefabricatorActorData& OutActorData);
	static void SaveComponentState(UActorComponent* InComp, APrefabActor* PrefabActor, const FPrefabActorLookup& CrossReferences, FPrefabricatorComponentData& OutCompData);
	static void LoadActorState(AActor* InActor, const FPrefabricatorActorData& InActorData, const FPrefabLoadSettings& InSettings);
	static void LoadComponentState(UActorComponent* InComp, const FPrefabricatorComponentData& InCompData, const FPrefabLoadSettings& InSettings);

};

class PREFABRICATORRUNTIME_API FPrefabVersionControl {
public:
	static void UpgradeToLatestVersion(UPrefabricatorAsset* Prefab);

private:
	static void UpgradeFromVersion_InitialVersion(UPrefabricatorAsset* Prefab);
	static void UpgradeFromVersion_AddedSoftReferences(UPrefabricatorAsset* Prefab);
	static void UpgradeFromVersion_AddedSoftReferencesPrefabFix(UPrefabricatorAsset* Prefab);

private:
	static void RefreshReferenceList(UPrefabricatorAsset* Prefab);
};

