//$ Copyright 2015-23, Code Respawn Technologies Pvt Ltd - All Rights Reserved $//

#pragma once
#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "GameFramework/Actor.h"

#include "Templates/Tuple.h"

#include "PrefabActor.generated.h"

class UPrefabricatorAsset;
class IPropertyHandle;


UCLASS(BlueprintType, EditInlineNew, CollapseCategories, HideDropdown)
class PREFABRICATORRUNTIME_API UPrefabPropertyChange : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere)
	TSoftObjectPtr<UObject> Object;

	UPROPERTY(VisibleAnywhere, meta=(EditCondition=false, EditConditionHides))
	FString ObjectDisplayName;

#if WITH_EDITORONLY_DATA
	UFUNCTION()
	void UpdateObjectDisplayName() 
	{ 
		ObjectDisplayName = Object->IsA(AActor::StaticClass()) ?
			Cast<AActor>(Object.Get())->GetActorLabel() :
			Object->GetName();
		ObjectDisplayName += ": " + PropertyPath;
	}
#endif
	FString GetObjectDisplayName()
	{
		UpdateObjectDisplayName();
		return ObjectDisplayName;
	}

	UPROPERTY(VisibleAnywhere)
	FString PropertyPath = "";

	UPROPERTY(EditAnywhere)
	bool bIsStaged = false;
};
using UPrefabPropertyChangePtr = TObjectPtr<UPrefabPropertyChange>;

USTRUCT(BlueprintType)
struct PREFABRICATORRUNTIME_API FPrefabPropertyChangeKey
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere)
	TSoftObjectPtr<UObject> Object = nullptr;

	UPROPERTY(VisibleAnywhere)
	FString PropertyPath = "";

	FPrefabPropertyChangeKey()
	{
	}

	FPrefabPropertyChangeKey(const UPrefabPropertyChange& Change)
		: Object(Change.Object)
		, PropertyPath(Change.PropertyPath)
	{
	}

	FPrefabPropertyChangeKey(TSoftObjectPtr<UObject> Object, FString PropertyPath)
		: Object(Object)
		, PropertyPath(PropertyPath)
	{
	}

	// Define the == operator so Unreal can compare two keys for equality.
	bool operator==(const FPrefabPropertyChangeKey& Other) const
	{
		return Object == Other.Object && PropertyPath == Other.PropertyPath;
	}
};

FORCEINLINE uint32 GetTypeHash(const FPrefabPropertyChangeKey& Key)
{
	return HashCombine(GetTypeHash(Key.Object), GetTypeHash(Key.PropertyPath));
}

UCLASS(Blueprintable, ConversionRoot, ComponentWrapperClass)
class PREFABRICATORRUNTIME_API APrefabActor : public AActor {
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (ExposeFunctionCategories = "Prefabricator,Mobility", AllowPrivateAccess = "true"))
	class UPrefabComponent* PrefabComponent;

public:
	UPROPERTY(EditAnywhere, Category = "Prefabricator", Instanced, Meta=(TitleProperty=ObjectDisplayName))
	TArray<TObjectPtr<UPrefabPropertyChange>> UnstagedChanges;

	UPROPERTY(EditAnywhere, Category = "Prefabricator", Instanced, Meta = (TitleProperty=ObjectDisplayName))
	TArray<TObjectPtr<UPrefabPropertyChange>> StagedChanges;

	UPROPERTY()
	TMap<FPrefabPropertyChangeKey, TWeakObjectPtr<UPrefabPropertyChange>> Changes;

	UPrefabPropertyChange* GetPropertyChange(const FPrefabPropertyChangeKey& Key)
	{
		if (auto ChangeWeakPtr = Changes.Find(Key))
		{
			return ChangeWeakPtr->Get();
		}

		return nullptr;
	}

	/// AActor Interface 
	virtual void Destroyed() override;
	virtual void PostLoad() override;
	virtual void PostActorCreated() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent&) override;
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent&) override;
	void OnObjectPropertyChangedChain(UObject*, struct FPropertyChangedChainEvent&);
	void StageAllChanges();
	void UnstageAllChanges();

	//void HandlePropertyChangedEvent(FPropertyChangedEvent& PropertyChangedEvent);

	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual FName GetCustomIconName() const override;
#endif // WITH_EDITOR
	/// End of AActor Interface 

	UFUNCTION(BlueprintCallable, Category = "Prefabricator")
	void LoadPrefab();

	UFUNCTION(BlueprintCallable, Category = "Prefabricator")
	void SavePrefab();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Prefabricator")
	bool IsPrefabOutdated();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Prefabricator")
	UPrefabricatorAsset* GetPrefabAsset();

	UFUNCTION(BlueprintCallable, Category = "Prefabricator")
	void RandomizeSeed(const FRandomStream& InRandom, bool bRecursive = true);
	void HandleBuildComplete();

public:
	// The last update ID of the prefab asset when this actor was refreshed from it
	// This is used to test if the prefab has changed since we last recreated it
	UPROPERTY()
	FGuid LastUpdateID;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prefabricator")
	int32 Seed;
};

/////////////////////////////// BuildSystem /////////////////////////////// 

class FPrefabBuildSystem;

class PREFABRICATORRUNTIME_API FPrefabBuildSystemCommand {
public:
	virtual ~FPrefabBuildSystemCommand() {}
	virtual void Execute(FPrefabBuildSystem& BuildSystem) = 0;
};
typedef TSharedPtr<FPrefabBuildSystemCommand> FPrefabBuildSystemCommandPtr;


class PREFABRICATORRUNTIME_API FPrefabBuildSystemCommand_BuildPrefab : public FPrefabBuildSystemCommand {
public:
	FPrefabBuildSystemCommand_BuildPrefab(TWeakObjectPtr<APrefabActor> InPrefab, bool bInRandomizeNestedSeed, FRandomStream* InRandom);

	virtual void Execute(FPrefabBuildSystem& BuildSystem) override;

private:
	TWeakObjectPtr<APrefabActor> Prefab;
	bool bRandomizeNestedSeed = false;
	FRandomStream* Random = nullptr;
};

class PREFABRICATORRUNTIME_API FPrefabBuildSystemCommand_BuildPrefabSync : public FPrefabBuildSystemCommand {
public:
	FPrefabBuildSystemCommand_BuildPrefabSync(TWeakObjectPtr<APrefabActor> InPrefab, bool bInRandomizeNestedSeed, FRandomStream* InRandom);

	virtual void Execute(FPrefabBuildSystem& BuildSystem) override;

private:
	TWeakObjectPtr<APrefabActor> Prefab;
	bool bRandomizeNestedSeed = false;
	FRandomStream* Random = nullptr;
};

class PREFABRICATORRUNTIME_API FPrefabBuildSystemCommand_NotifyBuildComplete : public FPrefabBuildSystemCommand {
public:
	FPrefabBuildSystemCommand_NotifyBuildComplete(TWeakObjectPtr<APrefabActor> InPrefab);
	virtual void Execute(FPrefabBuildSystem& BuildSystem) override;

private:
	TWeakObjectPtr<APrefabActor> Prefab;
};


class PREFABRICATORRUNTIME_API FPrefabBuildSystem {
public:
	FPrefabBuildSystem(double InTimePerFrame);
	void Tick();
	void Reset();
	void PushCommand(FPrefabBuildSystemCommandPtr InCommand);
	int32 GetNumPendingCommands() const { return BuildStack.Num(); }

private:
	TArray<FPrefabBuildSystemCommandPtr> BuildStack;
	double TimePerFrame = 0;
};



UCLASS(Blueprintable, ConversionRoot, ComponentWrapperClass)
class PREFABRICATORRUNTIME_API AReplicablePrefabActor : public APrefabActor {
	GENERATED_UCLASS_BODY()
public:
	void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

};

