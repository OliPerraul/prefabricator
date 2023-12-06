//$ Copyright 2015-23, Code Respawn Technologies Pvt Ltd - All Rights Reserved $//

#include "Prefab/PrefabTools.h"

#include "Asset/PrefabricatorAsset.h"
#include "Asset/PrefabricatorAssetUserData.h"
#include "Prefab/PrefabActor.h"
#include "Prefab/PrefabComponent.h"
#include "PrefabricatorSettings.h"
#include "Utils/PrefabricatorService.h"
#include "Utils/PrefabricatorStats.h"

#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/UnrealMemory.h"
#include "PropertyPathHelpers.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/NoExportTypes.h"

// Unsupported component (seem to be added by default), that's why this is causing issue
#include "Components/BillboardComponent.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogPrefabTools, Log, All);

#define LOCTEXT_NAMESPACE "PrefabTools"

namespace
{
	bool IsSupportedPrefabRootComponent(UActorComponent* Comp)
	{
		return Comp && !Comp->IsA(UBillboardComponent::StaticClass()) && !Comp->IsA(UPrefabComponent::StaticClass());
	}

	void ForceUpdateActorLabel(AActor* InActor, const FString ActorLabel)
	{
#if WITH_EDITOR
		// Must set actor's label after the item has actually been added to the hierarchy
		// Otherwise results in old names being used
		GEditor->GetTimerManager()->SetTimerForNextTick([InActor, ActorLabel]()
		{
			InActor->SetActorLabel(ActorLabel);
			InActor->Modify();
			FPropertyChangedEvent PropertyEvent(FindFProperty<FProperty>(AActor::StaticClass(), "ActorLabel"));
			InActor->PostEditChangeProperty(PropertyEvent);
			FCoreDelegates::OnActorLabelChanged.Broadcast(InActor);
		});
#endif
	}
}

void FPrefabTools::GetSelectedActors(TArray<AActor*>& OutActors)
{
	TSharedPtr<IPrefabricatorService> Service = FPrefabricatorService::Get();
	if (Service.IsValid()) {
		Service->GetSelectedActors(OutActors);
	}
}


int FPrefabTools::GetNumSelectedActors()
{
	TSharedPtr<IPrefabricatorService> Service = FPrefabricatorService::Get();
	return Service.IsValid() ? Service->GetNumSelectedActors() : 0;
}

void FPrefabTools::ParentActors(AActor* ParentActor, AActor* ChildActor)
{
	SCOPE_CYCLE_COUNTER(STAT_ParentActors);
	if (ChildActor && ParentActor) {
		{
			SCOPE_CYCLE_COUNTER(STAT_ParentActors1);
			ChildActor->DetachFromActor(FDetachmentTransformRules(EDetachmentRule::KeepWorld, false));
		}
		{
			SCOPE_CYCLE_COUNTER(STAT_ParentActors2);
			TSharedPtr<IPrefabricatorService> Service = FPrefabricatorService::Get();
			if (Service.IsValid()) {
				Service->ParentActors(ParentActor, ChildActor);
			}
		}
	}
}

void FPrefabTools::SelectPrefabActor(AActor* PrefabActor)
{
	TSharedPtr<IPrefabricatorService> Service = FPrefabricatorService::Get();
	if (Service.IsValid()) {
		Service->SelectPrefabActor(PrefabActor);
	}
}

UPrefabricatorAsset* FPrefabTools::CreatePrefabAsset()
{
	TSharedPtr<IPrefabricatorService> Service = FPrefabricatorService::Get();
	return Service.IsValid() ? Service->CreatePrefabAsset() : nullptr;
}

int32 FPrefabTools::GetRandomSeed(const FRandomStream& InRandom)
{
	return InRandom.RandRange(0, 10000000);
}

void FPrefabTools::IterateChildrenRecursive(APrefabActor* Prefab, TFunction<void(AActor*)> Visit)
{
	TArray<AActor*> Stack;
	{
		TArray<AActor*> AttachedActors;
		Prefab->GetAttachedActors(AttachedActors);
		for (AActor* Child : AttachedActors) {
			Stack.Push(Child);
		}
	}

	while (Stack.Num() > 0) {
		AActor* Top = Stack.Pop();

		Visit(Top);

		{
			TArray<AActor*> AttachedActors;
			Top->GetAttachedActors(AttachedActors);
			for (AActor* Child : AttachedActors) {
				Stack.Push(Child);
			}
		}
	}
}

bool FPrefabTools::CanCreatePrefab()
{
	return GetNumSelectedActors() > 0;
}

void FPrefabTools::CreatePrefab()
{
	TArray<AActor*> SelectedActors;
	GetSelectedActors(SelectedActors);

	CreatePrefabFromActors(SelectedActors);
}

namespace {

	void SanitizePrefabActorsForCreation(const TArray<AActor*>& InActors, TArray<AActor*>& OutActors) {
		// Find all the selected prefab actors
		TArray<APrefabActor*> PrefabActors;
		for (AActor* Actor : InActors) {
			if (APrefabActor* PrefabActor = Cast<APrefabActor>(Actor)) {
				PrefabActors.Add(PrefabActor);
			}
		}

		for (AActor* Actor : InActors) {
			bool bValid = true;
			// Make sure we do not include any actors that belong to these prefabs
			if (APrefabActor* ParentPrefab = Cast<APrefabActor>(Actor->GetAttachParentActor())) {
				if (PrefabActors.Contains(ParentPrefab)) {
					bValid = false;
				}
			}

			// Make sure the actor has a root component
			if (!Actor->GetRootComponent()) {
				bValid = false;
			}

			if (bValid) {
				OutActors.Add(Actor);
			}
		}
	}
}

APrefabActor* FPrefabTools::CreatePrefabFromActors(const TArray<AActor*>& InActors)
{
	TArray<AActor*> Actors;
	SanitizePrefabActorsForCreation(InActors, Actors);

	if (Actors.Num() == 0) {
		return nullptr;
	}

	UPrefabricatorAsset* PrefabAsset = CreatePrefabAsset();
	if (!PrefabAsset) {
		return nullptr;
	}

	TSharedPtr<IPrefabricatorService> Service = FPrefabricatorService::Get();
	if (Service.IsValid()) {
		Service->BeginTransaction(LOCTEXT("TransLabel_CreatePrefab", "Create Prefab"));
	}

	UWorld* World = Actors[0]->GetWorld();

	FVector Pivot = FPrefabricatorAssetUtils::FindPivot(Actors);
	APrefabActor* PrefabActor = World->SpawnActor<APrefabActor>(Pivot, FRotator::ZeroRotator);

	// Find the compatible mobility for the prefab actor
	EComponentMobility::Type Mobility = FPrefabricatorAssetUtils::FindMobility(Actors);
	PrefabActor->GetRootComponent()->SetMobility(Mobility);

	PrefabActor->PrefabComponent->PrefabAssetInterface = PrefabAsset;
	// Attach the actors to the prefab
	for (AActor* Actor : Actors) {
		ParentActors(PrefabActor, Actor);
	}

	if (Service.IsValid()) {
		Service->EndTransaction();
	}

	SaveStateToPrefabAsset(PrefabActor);

	SelectPrefabActor(PrefabActor);

	return PrefabActor;
}

void FPrefabTools::AssignAssetUserData(AActor* InActor, const FGuid& InItemID, APrefabActor* Prefab)
{
	if (!InActor || !InActor->GetRootComponent()) {
		return;
	}

	UActorComponent* RootComponent = InActor->GetRootComponent();
	UPrefabricatorAssetUserData* PrefabUserData = RootComponent->GetAssetUserData<UPrefabricatorAssetUserData>();
	if (!PrefabUserData) {
		PrefabUserData = NewObject<UPrefabricatorAssetUserData>(InActor->GetRootComponent());
		RootComponent->AddAssetUserData(PrefabUserData);
	}

	PrefabUserData->PrefabActor = Prefab;
	PrefabUserData->ItemID = InItemID;
}

void FPrefabTools::AssignAssetUserData(UActorComponent* InComp, const FGuid& InItemID, APrefabActor* Prefab)
{
	UPrefabricatorAssetUserData* PrefabUserData = InComp->GetAssetUserData<UPrefabricatorAssetUserData>();
	if (!PrefabUserData) {
		PrefabUserData = NewObject<UPrefabricatorAssetUserData>(InComp);
		InComp->AddAssetUserData(PrefabUserData);
	}

	PrefabUserData->PrefabActor = Prefab;
	PrefabUserData->ItemID = InItemID;
}


void FPrefabTools::SaveStateToPrefabAsset(APrefabActor* PrefabActor)
{
	if (!PrefabActor) {
		UE_LOG(LogPrefabTools, Error, TEXT("Invalid prefab actor reference"));
		return;
	}

	UPrefabricatorAsset* PrefabAsset = PrefabAsset = Cast<UPrefabricatorAsset>(PrefabActor->PrefabComponent->PrefabAssetInterface.LoadSynchronous());
	if (!PrefabAsset) {
		//UE_LOG(LogPrefabTools, Error, TEXT("Prefab asset is not assigned correctly"));
		return;
	}

	PrefabAsset->PrefabMobility = PrefabActor->GetRootComponent()->Mobility;

	PrefabAsset->ActorData.Reset();
	PrefabAsset->ComponentData.Reset();

	TArray<UActorComponent*> Components;
	PrefabActor->GetComponents(Components, false);

	TArray<AActor*> Children;
	GetActorChildren(PrefabActor, Children);

	// Make sure the children do not have duplicate asset user data template ids
	{
		TSet<FGuid> VisitedItemId;
		for (UActorComponent* Comp : Components) {
			if (!IsSupportedPrefabRootComponent(Comp))
				continue;
			UPrefabricatorAssetUserData* CompUserData = Comp->GetAssetUserData<UPrefabricatorAssetUserData>();
			if (CompUserData) {
				if (VisitedItemId.Contains(CompUserData->ItemID)) {
					CompUserData->ItemID = FGuid::NewGuid();
					CompUserData->Modify();
				}
				VisitedItemId.Add(CompUserData->ItemID);
			}
		}

		for (AActor* ChildActor : Children) {
			if (ChildActor && ChildActor->GetRootComponent()) {
				UPrefabricatorAssetUserData* ChildUserData = ChildActor->GetRootComponent()->GetAssetUserData<UPrefabricatorAssetUserData>();
				if (ChildUserData) {
					if (VisitedItemId.Contains(ChildUserData->ItemID)) {
						ChildUserData->ItemID = FGuid::NewGuid();
						ChildUserData->Modify();
					}
					VisitedItemId.Add(ChildUserData->ItemID);
				}
			}
		}
	}

	struct FSaveContext {
		UActorComponent* Comp = nullptr;
		AActor* ChildActor = nullptr;
		int32 ItemIndex = 0;
	};

	FPrefabActorLookup ActorCrossReferences;
	TArray<FSaveContext> ItemsToSave;
	// Support for saving components at the root of the prefab
	for (UActorComponent* Comp : Components) {
		if (!IsSupportedPrefabRootComponent(Comp))
			continue;
		UPrefabricatorAssetUserData* CompUserData = Comp->GetAssetUserData<UPrefabricatorAssetUserData>();
		FGuid ItemID;
		if (CompUserData && CompUserData->PrefabActor == PrefabActor) {
			ItemID = CompUserData->ItemID;
		}
		else {
			ItemID = FGuid::NewGuid();
		}
		AssignAssetUserData(Comp, ItemID, PrefabActor);
		int32 NewItemIndex = PrefabAsset->ComponentData.AddDefaulted();
		FPrefabricatorComponentData& CompData = PrefabAsset->ComponentData[NewItemIndex];
		CompData.PrefabItemID = ItemID;
		// TODO: Support child actor referencing prefab component
		// ActorCrossReferences.Register(ChildActor, ItemID);

		FSaveContext SaveInfo;
		SaveInfo.Comp = Comp;
		SaveInfo.ItemIndex = NewItemIndex;
		ItemsToSave.Add(SaveInfo);
	}

	for (AActor* ChildActor : Children) {
		if (ChildActor && ChildActor->GetRootComponent()) {
			UPrefabricatorAssetUserData* ChildUserData = ChildActor->GetRootComponent()->GetAssetUserData<UPrefabricatorAssetUserData>();
			FGuid ItemID;
			if (ChildUserData && ChildUserData->PrefabActor == PrefabActor) {
				ItemID = ChildUserData->ItemID;
			}
			else {
				ItemID = FGuid::NewGuid();
			}
			AssignAssetUserData(ChildActor, ItemID, PrefabActor);
			int32 NewItemIndex = PrefabAsset->ActorData.AddDefaulted();
			FPrefabricatorActorData& ActorData = PrefabAsset->ActorData[NewItemIndex];
			ActorData.PrefabItemID = ItemID;
			ActorCrossReferences.Register(ChildActor, ItemID);

			FSaveContext SaveInfo;
			SaveInfo.ChildActor = ChildActor;
			SaveInfo.ItemIndex = NewItemIndex;
			ItemsToSave.Add(SaveInfo);
		}
	}

	for (const FSaveContext& SaveInfo : ItemsToSave) {
		if (AActor* ChildActor = SaveInfo.ChildActor)
		{
			if (ChildActor && ChildActor->GetRootComponent()) {
				FPrefabricatorActorData& ActorData = PrefabAsset->ActorData[SaveInfo.ItemIndex];
				SaveActorState(ChildActor, PrefabActor, ActorCrossReferences, ActorData);
			}
		}
		else if (UActorComponent* Comp = SaveInfo.Comp)
		{
			FPrefabricatorComponentData& CompData = PrefabAsset->ComponentData[SaveInfo.ItemIndex];
			SaveComponentState(Comp, PrefabActor, ActorCrossReferences, CompData);
		}
	}
	PrefabAsset->Version = (uint32)EPrefabricatorAssetVersion::LatestVersion;

	PrefabActor->PrefabComponent->UpdateBounds();

	// Regenerate a new update id for the prefab asset
	PrefabAsset->LastUpdateID = FGuid::NewGuid();
	PrefabActor->LastUpdateID = PrefabAsset->LastUpdateID;
	PrefabAsset->Modify();

	TSharedPtr<IPrefabricatorService> Service = FPrefabricatorService::Get();
	if (Service.IsValid()) {
		Service->CaptureThumb(PrefabAsset);
	}
}

namespace {

	FString GetNestedPropertyPath(const FString& PropertyPath, const FProperty* Property, int32 PropertyElementIndex = -1)
	{
		FString NestedPropertyName = Property->GetName();
		FString NewPropertyPath(PropertyPath);
		NewPropertyPath += PropertyElementIndex < 0 ?
			FString::Format(TEXT("|{0}"), { NestedPropertyName }) :
			FString::Format(TEXT("[{1}]"), { NestedPropertyName, PropertyElementIndex });
		return NewPropertyPath;
	}

	template<typename TContext>
	FPrefabricatorNestedPropertyData* GetOrCreateNestedPropertyData(TContext& Context, FString PropertyPath)
	{
		FPrefabricatorNestedPropertyData* NestedPropertyData = Context.PrefabProperty->NestedPropertyData.Find(PropertyPath);
		if (!NestedPropertyData)
		{
			NestedPropertyData = &Context.PrefabProperty->NestedPropertyData.Add(PropertyPath, FPrefabricatorNestedPropertyData());
		}
		return NestedPropertyData;
	}

	template<typename TContext>
	FPrefabricatorNestedPropertyData* GetNestedPropertyData(TContext& Context, FString PropertyPath)
	{
		return Context.PrefabProperty->NestedPropertyData.Find(PropertyPath);
	}

	bool GetPropertyData(const FProperty* Property, UObject* Obj, UObject * ObjTemplate, FString& OutPropertyData) {
		if (!Obj || !Property) return false;
		
		UObject* DefaultObject = ObjTemplate;
		if (!DefaultObject) {
			UClass* ObjClass = Obj->GetClass();
			if (!ObjClass) return false;
			DefaultObject = ObjClass->GetDefaultObject();
		}

		Property->ExportTextItem_Direct(OutPropertyData, Property->ContainerPtrToValuePtr<void>(Obj), Property->ContainerPtrToValuePtr<void>(DefaultObject), Obj, PPF_Copy);
		return true;
	}

	bool ContainsOuterParent(UObject* ObjectToTest, UObject* Outer) {
		while (ObjectToTest) {
			if (ObjectToTest == Outer) return true;
			ObjectToTest = ObjectToTest->GetOuter();
		}
		return false;
	}

	bool HasDefaultValue(UObject* InContainer, UObject* InDiff, const FString& InPropertyPath) {
		if (!InContainer) return false;

		UObject* DefaultObject = InDiff;
		if (!DefaultObject) {
			UClass* ObjClass = InContainer->GetClass();
			if (!ObjClass) return false;
			DefaultObject = ObjClass->GetDefaultObject();
		}

		FString PropertyValue, DefaultValue;
		PropertyPathHelpers::GetPropertyValueAsString(InContainer, InPropertyPath, PropertyValue);
		PropertyPathHelpers::GetPropertyValueAsString(DefaultObject, InPropertyPath, DefaultValue);
		if (PropertyValue != DefaultValue) {
			UE_LOG(LogPrefabTools, Log, TEXT("Property differs: %s\n> %s\n> %s"), *InPropertyPath, *PropertyValue, *DefaultValue);
		}
		return PropertyValue == DefaultValue;
	}

	bool ShouldSkipSerialization(const FProperty* Property, UObject* ObjToSerialize, APrefabActor* PrefabActor) {
		if (const FObjectProperty* ObjProperty = CastField<FObjectProperty>(Property)) {
			UObject* PropertyObjectValue = ObjProperty->GetObjectPropertyValue_InContainer(ObjToSerialize);
			if (ContainsOuterParent(PropertyObjectValue, ObjToSerialize) ||
				ContainsOuterParent(PropertyObjectValue, PrefabActor)) {
				//UE_LOG(LogPrefabTools, Log, TEXT("Skipping Property: %s"), *Property->GetName());
				return true;
			}
		}

		return false;
	}

	struct FDeserializeContext
	{
		APrefabActor* PrefabActor = nullptr;
		UObject* ObjToDeserialize = nullptr;
		//void* BackupValuePtr = nullptr;
		UPrefabricatorProperty* PrefabProperty = nullptr;

	};

	struct FRestorePartialSerializationPtrs
	{
		void* Src = nullptr;
		void* Dest = nullptr;
	};

	void _DeserializeProperty(
		FDeserializeContext& Context
		, void* ValuePtr
		, FProperty* Property
		, const FString& PropertyPath
		, int32 PropertyElementIndex=-1);

	void _DeserializeProperty_ArrayHelper(
		FDeserializeContext& Context
		, void* ArrayPtr
		, const FArrayProperty* ArrayProperty
		, const FString& PropertyPath
	)
	{
		auto InnerProperty = ArrayProperty->Inner;
		FScriptArrayHelper Helper(ArrayProperty, ArrayPtr);

		if (auto NestedPropertyData = GetNestedPropertyData(Context, PropertyPath))
		{
			if (Helper.Num() < NestedPropertyData->ArrayLength)
			{
				Helper.AddValues(NestedPropertyData->ArrayLength - Helper.Num());
			}
		}

		TSoftObjectPtr<UObject> ObjectToSerializeSoftPtr(Context.ObjToDeserialize);
		for (int32 Index = 0; Index < Helper.Num(); Index++)
		{			
			void* ValuePtr = Helper.GetRawPtr(Index);
			_DeserializeProperty(Context, ValuePtr, InnerProperty, PropertyPath, Index);
		}
	}

	void _DeserializeProperty_StructHelper(
		FDeserializeContext& Context
		, void* StructPtr
		, const FStructProperty* StructProperty
		, const FString& PropertyPath
		, int32 PropertyElementIndex = -1)
	{
		for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It) {
			if (FProperty* InnerProperty = *It)
			{
				void* ValuePtr = InnerProperty->ContainerPtrToValuePtr<void>(StructPtr, 0);
				_DeserializeProperty(Context, ValuePtr, InnerProperty, PropertyPath);
			}
		}
	}

	void _DeserializeProperty(
		FDeserializeContext& Context
		, void* ValuePtr
		, FProperty* Property
		, const FString& PropertyPath
		, int32 PropertyElementIndex)
	{
		FString NewPropertyPath = GetNestedPropertyPath(PropertyPath, Property, PropertyElementIndex);

		TSoftObjectPtr<UObject> ObjectToSerializeSoftPtr(Context.ObjToDeserialize);
		if (
			Context.PrefabActor &&
			Context.PrefabActor->GetPropertyChange({ObjectToSerializeSoftPtr, NewPropertyPath})
			)
		{
			return;
		}

		// Support for USTRUCT
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property)) {
			_DeserializeProperty_StructHelper(Context, ValuePtr, StructProperty, NewPropertyPath);
		}
		// Support for TArrays (TODO: Adds support for sets and maps).
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			_DeserializeProperty_ArrayHelper(Context, ValuePtr, ArrayProperty, NewPropertyPath);
		}
		else
		{
			if (auto NestedPropertyData = GetNestedPropertyData(Context, NewPropertyPath))
			{
				Property->ImportText_Direct(*NestedPropertyData->ExportedValue, ValuePtr, Context.ObjToDeserialize, PPF_None);
			}
		}
	}

	void DeserializeProperty(
		FDeserializeContext& Context
		, FProperty* Property)
	{
		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Context.ObjToDeserialize, 0);
		_DeserializeProperty(Context, ValuePtr, Property, "");
	}

	template<typename T>
	T* _GetNearestActorOfType(AActor* ChildActor)
	{
		if (!ChildActor)
			return nullptr;

		if (ChildActor->IsA(T::StaticClass()))
		{
			return StaticCast<T*>(ChildActor);
		}

		AActor* ParentActor = ChildActor->GetAttachParentActor();

		while (ParentActor != nullptr)
		{
			if (ParentActor->IsA(T::StaticClass()))
			{
				return StaticCast<T*>(ParentActor);
			}

			ParentActor = ParentActor->GetAttachParentActor();
		}

		return nullptr;
	}

	void DeserializeFields(UObject* InObjToDeserialize, const TArray<UPrefabricatorProperty*>& InProperties) {
		if (!InObjToDeserialize) return;

		auto Comp = Cast<UActorComponent>(InObjToDeserialize);
		AActor*  Actor = Comp ? Comp->GetOwner() : Cast<AActor>(InObjToDeserialize);
		APrefabActor* PrefabActor = Actor ? _GetNearestActorOfType<APrefabActor>(Actor) : nullptr;

		for (UPrefabricatorProperty* PrefabProperty : InProperties) {
			// If its a struct property, still let us use the value found in PrefabProperty->ExportedValue
			// as a starting point, only the object reference will be fixed-up.
			if (!PrefabProperty || (PrefabProperty->bIsCrossReferencedActor && !PrefabProperty->bContainsStructProperty)) continue;
			FString PropertyName = PrefabProperty->PropertyName;
			if (PropertyName == "AssetUserData") continue;		// Skip this as assignment is very slow and is not needed

			FProperty* Property = InObjToDeserialize->GetClass()->FindPropertyByName(*PropertyName);
			if (Property) {
				// do not overwrite properties that have a default sub object or an archetype object
				if (FObjectProperty* ObjProperty = CastField<FObjectProperty>(Property)) {
					UObject* PropertyObjectValue = ObjProperty->GetObjectPropertyValue_InContainer(InObjToDeserialize);
					if (PropertyObjectValue && PropertyObjectValue->HasAnyFlags(RF_DefaultSubObject | RF_ArchetypeObject)) {
						continue;
					}
				}

				{
					SCOPE_CYCLE_COUNTER(STAT_DeserializeFields_Iterate_LoadValue);
					PrefabProperty->LoadReferencedAssetValues();
				}

				{
					SCOPE_CYCLE_COUNTER(STAT_DeserializeFields_Iterate_SetValue);			
					FDeserializeContext Context;
					Context.ObjToDeserialize = InObjToDeserialize;
					Context.PrefabActor = PrefabActor;
					Context.PrefabProperty = PrefabProperty;
					DeserializeProperty(Context, Property);					
				}
			}
		}
	}

	struct FSerializePropertyContext
	{
		APrefabActor* PrefabActor = nullptr;
		UObject* ObjToSerialize = nullptr;
		UObject* ObjTemplate = nullptr;
		const FPrefabActorLookup& CrossReferences;
		UPrefabricatorProperty* PrefabProperty = nullptr;
	};

	// SerializeObjectProperty method to reuse the code.
	bool _SerializeProperty_ObjectHelper(
		FSerializePropertyContext& Context
		, const void* ValuePtr
		, const FObjectPropertyBase* ObjProperty
		, const FString& PropertyPath
		, int32 PropertyElementIndex=-1
	)
	{
		UObject* PropertyObjectValue = ObjProperty->GetObjectPropertyValue(ValuePtr);
		if (PropertyObjectValue == nullptr || PropertyObjectValue->HasAnyFlags(RF_DefaultSubObject | RF_ArchetypeObject)) {
			return false;
		}

		FString ObjectPath = PropertyObjectValue->GetPathName();
		FGuid CrossRefPrefabItem;
		if (Context.CrossReferences.GetPrefabItemId(ObjectPath, CrossRefPrefabItem)) {
			Context.PrefabProperty->bIsCrossReferencedActor = true;
			// Obtain property path relative to object provided

			auto NestedPropertyData = Context.PrefabProperty->NestedPropertyData.Find(PropertyPath);
			if(!NestedPropertyData)
			{
				NestedPropertyData = &Context.PrefabProperty->NestedPropertyData.Add(PropertyPath, FPrefabricatorNestedPropertyData());
			}

			if (ensure(NestedPropertyData))
			{
				NestedPropertyData->CrossReferencePrefabActorId = CrossRefPrefabItem;
				return true;
			}
		}
		return false;
	}

	void _SerializeProperty(
		FSerializePropertyContext& Context
		, void* ValuePtr
		, const FProperty* Property
		, const FString& PropertyPath
		, int32 PropertyElementIndex = -1
	);

	void _SerializeProperty_ArrayHelper(
		FSerializePropertyContext& Context
		, void* ArrayPtr
		, const FArrayProperty* ArrayProperty
		, const FString& PropertyPath
		, int32 PropertyElementIndex = -1
	)
	{
		auto InnerProperty = ArrayProperty->Inner;

		FScriptArrayHelper Helper(ArrayProperty, ArrayPtr);
		if (FPrefabricatorNestedPropertyData* NestedPropertyData = GetOrCreateNestedPropertyData(Context, PropertyPath))
		{
			NestedPropertyData->ArrayLength = Helper.Num();
		}

		for (int32 Index = 0; Index < Helper.Num(); Index++)
		{
			void* ValuePtr = Helper.GetRawPtr(Index);
			_SerializeProperty(Context, ValuePtr, InnerProperty, PropertyPath, Index);
		}
	}

	void _SerializeProperty_StructHelper(
		FSerializePropertyContext& Context
		, void* StructPtr
		, const FStructProperty* StructProperty
		, const FString& PropertyPath
		, int32 PropertyElementIndex=-1
		)
	{
		Context.PrefabProperty->bContainsStructProperty = true;
	
		for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It) {
			if (FProperty* InnerProperty = *It)
			{
				void* ValuePtr = InnerProperty->ContainerPtrToValuePtr<void>(StructPtr, 0);
				FString NewPropertyPath = GetNestedPropertyPath(PropertyPath, InnerProperty);
				_SerializeProperty(Context, ValuePtr, InnerProperty, NewPropertyPath);
			}
		}
	}

	void _SerializeProperty(
		FSerializePropertyContext& Context
		, void* ValuePtr
		, const FProperty* Property
		, const FString& PropertyPath
		, int32 PropertyElementIndex
	)
	{
		if (!ensure(Context.PrefabActor))
			return;

		TSoftObjectPtr<UObject> ObjectToSerializeSoftPtr(Context.ObjToSerialize);
		if (auto Change = Context.PrefabActor->GetPropertyChange({ObjectToSerializeSoftPtr, PropertyPath}))
		{
			if (!Change->bIsStaged)
			{
				if (auto NestedPropertyData = GetOrCreateNestedPropertyData(Context, PropertyPath))
				{
					NestedPropertyData->ExportedValue = Change->OriginalValue;
				}
				return;
			}

			Context.PrefabActor->StagedChanges.Remove(*Change);
			Context.PrefabActor->Changes.Remove(*Change);
		}

		// Support for USTRUCT
		bool bExportValue = true;
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property)) {
			bExportValue = false;
			_SerializeProperty_StructHelper(Context, ValuePtr, StructProperty, PropertyPath);
		}
		// Support for TArrays (TODO: Adds support for sets and maps).
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			bExportValue = false;
			_SerializeProperty_ArrayHelper(Context, ValuePtr, ArrayProperty, PropertyPath);
		}
		// FObjectPropertyBase instead of FObjectProperty to also support soft references.
		else if (const FObjectPropertyBase* ObjProperty = CastField<FObjectPropertyBase>(Property)) {
			_SerializeProperty_ObjectHelper(Context, ValuePtr, ObjProperty, PropertyPath);
		}		
		
		if(bExportValue)
		{
			if (auto NestedPropertyData = GetOrCreateNestedPropertyData(Context, PropertyPath))
			{
				Property->ExportTextItem_Direct(
					NestedPropertyData->ExportedValue
					, ValuePtr
					, nullptr,
					Context.ObjToSerialize,
					PPF_Copy);
			}
		}
	}

	void SerializeProperty(
		FSerializePropertyContext& Context
		, const FProperty* Property
		, FString& PropertyPath
	)
	{		
		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Context.ObjToSerialize, 0);
		_SerializeProperty(Context, ValuePtr, Property, PropertyPath);
	}

	void SerializeFields(
		FPrefabricatorEntryBase& Entry
		, UObject* ObjToSerialize
		, UObject* ObjTemplate
		, APrefabActor* PrefabActor
		, const FPrefabActorLookup& CrossReferences
	) {
		if (!ObjToSerialize || !PrefabActor) {
			return;
		}

		UPrefabricatorAsset* PrefabAsset = Cast<UPrefabricatorAsset>(PrefabActor->PrefabComponent->PrefabAssetInterface.LoadSynchronous());

		if (!PrefabAsset) {
			return;
		}

		TSet<const FProperty*> PropertiesToSerialize;
		for (TFieldIterator<FProperty> PropertyIterator(ObjToSerialize->GetClass()); PropertyIterator; ++PropertyIterator) {
			FProperty* Property = *PropertyIterator;
			if (!Property) continue;
			if (Property->HasAnyPropertyFlags(CPF_Transient)) {
				continue;
			}

			if (FPrefabTools::ShouldIgnorePropertySerialization(Property->GetFName())) {
				continue;
			}

			bool bForceSerialize = FPrefabTools::ShouldForcePropertySerialization(Property->GetFName());

			// Check if it has the default value
			if (!bForceSerialize && HasDefaultValue(ObjToSerialize, ObjTemplate, Property->GetName())) {
				continue;
			}

			if (const FObjectProperty* ObjProperty = CastField<FObjectProperty>(Property)) {
				UObject* PropertyObjectValue = ObjProperty->GetObjectPropertyValue_InContainer(ObjToSerialize);
				if (PropertyObjectValue && PropertyObjectValue->HasAnyFlags(RF_DefaultSubObject | RF_ArchetypeObject)) {
					continue;
				}
			}


			PropertiesToSerialize.Add(Property);
		}

		for (const FProperty* Property : PropertiesToSerialize) {
			if (!Property) continue;
			if (FPrefabTools::ShouldIgnorePropertySerialization(Property->GetFName())) {
				continue;
			}

			UPrefabricatorProperty* PrefabProperty = nullptr;
			FString PropertyName = Property->GetName();

			
			if (ShouldSkipSerialization(Property, ObjToSerialize, PrefabActor)) {
				continue;
			}

			FString PropertyPath = GetNestedPropertyPath("", Property);			

			PrefabProperty = NewObject<UPrefabricatorProperty>(PrefabAsset);
			PrefabProperty->PropertyName = PropertyName;

			FSerializePropertyContext Context{ PrefabActor, ObjToSerialize, ObjTemplate, CrossReferences, PrefabProperty };
			SerializeProperty(Context, Property, PropertyPath);

			// Root export value only supported for this property for legacy reasons.
			//!PrefabProperty->bIsCrossReferencedActor || PrefabProperty->bContainsStructProperty)
			if (PrefabProperty->PropertyName == "PrefabAssetInterface")
			{
				GetPropertyData(Property, ObjToSerialize, ObjTemplate, PrefabProperty->ExportedValue);
			}
			PrefabProperty->SaveReferencedAssetValues();

			OutProperties.Add(PrefabProperty);
		}
	}

	void CollectAllSubobjects(UObject* Object, TArray<UObject*>& OutSubobjectArray)
	{
		const bool bIncludedNestedObjects = true;
		GetObjectsWithOuter(Object, OutSubobjectArray, bIncludedNestedObjects);

		// Remove contained objects that are not subobjects.
		for (int32 ComponentIndex = 0; ComponentIndex < OutSubobjectArray.Num(); ComponentIndex++)
		{
			UObject* PotentialComponent = OutSubobjectArray[ComponentIndex];
			if (!PotentialComponent->IsDefaultSubobject() && !PotentialComponent->HasAnyFlags(RF_DefaultSubObject))
			{
				OutSubobjectArray.RemoveAtSwap(ComponentIndex--);
			}
		}
	}

	// TODO: THIS IS BROKEN
	void DumpSerializedProperties(const TArray<UPrefabricatorProperty*>& InProperties) {
		// for (UPrefabricatorProperty* Property : InProperties) {
		// 	UE_LOG(LogPrefabTools, Log, TEXT("%s: %s"), *Property->PropertyName, *Property->ExportedValue);
		// }

	}

	void DumpSerializedData(const FPrefabricatorActorData& InActorData) {
		UE_LOG(LogPrefabTools, Log, TEXT("############################################################"));
		UE_LOG(LogPrefabTools, Log, TEXT("Actor Properties: %s"), *InActorData.ClassPathRef.GetAssetPathString());
		UE_LOG(LogPrefabTools, Log, TEXT("================="));
		DumpSerializedProperties(InActorData.Properties);

		for (const FPrefabricatorComponentData& ComponentData : InActorData.Components) {
			UE_LOG(LogPrefabTools, Log, TEXT(""));
			UE_LOG(LogPrefabTools, Log, TEXT("Component Properties: %s"), *ComponentData.Name);
			UE_LOG(LogPrefabTools, Log, TEXT("================="));
			DumpSerializedProperties(ComponentData.Properties);
		}
	}
}

bool FPrefabTools::ShouldIgnorePropertySerialization(const FName& InPropertyName)
{
	static const TSet<FName> IgnoredFields = {
		"AttachParent",
		"AttachSocketName",
		"AttachChildren",
		"ClientAttachedChildren",
		"bIsEditorPreviewActor",
		"bIsEditorOnlyActor",
		"UCSModifiedProperties",
		"BlueprintCreatedComponents"
	};

	return IgnoredFields.Contains(InPropertyName);
}

bool FPrefabTools::ShouldForcePropertySerialization(const FName& PropertyName)
{
	static const TSet<FName> FieldsToForceSerialize = {
		"Mobility"
	};

	return FieldsToForceSerialize.Contains(PropertyName);
}

namespace {
	UActorComponent* FindBestComponentInCDO(AActor* CDO, UActorComponent* Component) {
		if (!CDO || !Component) return nullptr;

		for (UActorComponent* DefaultComponent : CDO->GetComponents()) {
			if (DefaultComponent && DefaultComponent->GetFName() == Component->GetFName() && DefaultComponent->GetClass() == Component->GetClass()) {
				return DefaultComponent;
			}
		}
		return nullptr;
	}
}

void FPrefabTools::SaveActorState(
	AActor* InActor
	, APrefabActor* PrefabActor
	, const FPrefabActorLookup& CrossReferences
	, FPrefabricatorActorData& OutActorData
	)
{
	if (!InActor) return;

	FTransform InversePrefabTransform = PrefabActor->GetTransform().Inverse();
	FTransform LocalTransform = InActor->GetTransform() * InversePrefabTransform;
	OutActorData.RelativeTransform = LocalTransform;
	FString ClassPath = InActor->GetClass()->GetPathName();
	OutActorData.ClassPathRef = FSoftClassPath(ClassPath);
	OutActorData.ClassPath = ClassPath;
	AActor* ActorCDO = Cast<AActor>(InActor->GetArchetype());
	SerializeFields(OutActorData, InActor, ActorCDO, PrefabActor, CrossReferences);

#if WITH_EDITOR
	OutActorData.Name = InActor->GetActorLabel();
#endif // WITH_EDITOR

	TArray<UActorComponent*> Components;
	InActor->GetComponents(Components);

	for (UActorComponent* Component : Components) {
		int32 ComponentDataIdx = OutActorData.Components.Emplace();
		FPrefabricatorComponentData& ComponentData = OutActorData.Components[ComponentDataIdx];
		ComponentData.Name = Component->GetPathName(InActor);
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component)) {
			ComponentData.RelativeTransform = SceneComponent->GetComponentTransform();
		}
		else {
			ComponentData.RelativeTransform = FTransform::Identity;
		}
		UObject* ComponentTemplate = FindBestComponentInCDO(ActorCDO, Component);
		SerializeFields(ComponentData, Component, ComponentTemplate, PrefabActor, CrossReferences);
	}

	//DumpSerializedData(OutActorData);
}

void FPrefabTools::SaveComponentState(UActorComponent* InComp, APrefabActor* PrefabActor, const FPrefabActorLookup& CrossReferences, FPrefabricatorComponentData& OutCompData)
{
	// TODO: Support scene component
	// FPrefabricatorComponentData::RelativeTransform
	OutCompData.Name = InComp->GetPathName(PrefabActor);
	FString ClassPath = InComp->GetClass()->GetPathName();
	OutCompData.ClassPathRef = FSoftClassPath(ClassPath);
	OutCompData.ClassPath = ClassPath;
	AActor* ActorCDO = Cast<AActor>(PrefabActor->GetArchetype());
	UObject* CompCDO = FindBestComponentInCDO(ActorCDO, InComp);
	SerializeFields(OutCompData, InComp, CompCDO, PrefabActor, CrossReferences);
}


void FPrefabTools::LoadComponentState(UActorComponent* InComp, const FPrefabricatorComponentData& InCompData, const FPrefabLoadSettings& InSettings)
{
	SCOPE_CYCLE_COUNTER(STAT_LoadActorState);
	if (!InComp) {
		return;
	}

	TSharedPtr<IPrefabricatorService> Service = FPrefabricatorService::Get();
	if (Service.IsValid()) {
		SCOPE_CYCLE_COUNTER(STAT_LoadActorState_BeginTransaction);
		//Service->BeginTransaction(LOCTEXT("TransLabel_LoadPrefab", "Load Prefab"));
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_LoadActorState_DeserializeFieldsActor);
		DeserializeFields(InComp, InCompData.Properties);
	}

	bool bPreviouslyRegister;
	{
		//SCOPE_CYCLE_COUNTER(STAT_LoadActorState_UnregisterComponent);
		bPreviouslyRegister = InComp->IsRegistered();
		if (InSettings.bUnregisterComponentsBeforeLoading && bPreviouslyRegister) {
			InComp->UnregisterComponent();
		}
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_LoadActorState_DeserializeFieldsComponents);
		DeserializeFields(InComp, InCompData.Properties);
	}

	{
		//SCOPE_CYCLE_COUNTER(STAT_LoadActorState_RegisterComponent);
		if (InSettings.bUnregisterComponentsBeforeLoading && bPreviouslyRegister) {
			InComp->RegisterComponent();
		}
	}		

	//InComp->PostLoad();

	if (Service.IsValid()) {
		SCOPE_CYCLE_COUNTER(STAT_LoadActorState_EndTransaction);
		//Service->EndTransaction();
	}
}

void FPrefabTools::LoadActorState(AActor* InActor, const FPrefabricatorActorData& InActorData, const FPrefabLoadSettings& InSettings)
{
	SCOPE_CYCLE_COUNTER(STAT_LoadActorState);
	if (!InActor) {
		return;
	}

	TSharedPtr<IPrefabricatorService> Service = FPrefabricatorService::Get();
	if (Service.IsValid()) {
		SCOPE_CYCLE_COUNTER(STAT_LoadActorState_BeginTransaction);
		//Service->BeginTransaction(LOCTEXT("TransLabel_LoadPrefab", "Load Prefab"));
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_LoadActorState_DeserializeFieldsActor);
		DeserializeFields(InActor, InActorData.Properties);
	}

	TMap<FString, UActorComponent*> ComponentsByName;
	for (UActorComponent* Comp : InActor->GetComponents()) {
		FString ComponentPath = Comp->GetPathName(InActor);
		ComponentsByName.Add(ComponentPath, Comp);
	}

	{
		for (const FPrefabricatorComponentData& ComponentData : InActorData.Components) {
			if (UActorComponent** SearchResult = ComponentsByName.Find(ComponentData.Name)) {
				UActorComponent* Component = *SearchResult;
				bool bPreviouslyRegister;
				{
					//SCOPE_CYCLE_COUNTER(STAT_LoadActorState_UnregisterComponent);
					bPreviouslyRegister = Component->IsRegistered();
					if (InSettings.bUnregisterComponentsBeforeLoading && bPreviouslyRegister) {
						Component->UnregisterComponent();
					}
				}

				{
					SCOPE_CYCLE_COUNTER(STAT_LoadActorState_DeserializeFieldsComponents);
					DeserializeFields(Component, ComponentData.Properties);
				}

				{
					//SCOPE_CYCLE_COUNTER(STAT_LoadActorState_RegisterComponent);
					if (InSettings.bUnregisterComponentsBeforeLoading && bPreviouslyRegister) {
						Component->RegisterComponent();
					}
				}

				// Check if we need to recreate the physics state
				{
					if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component)) {
						bool bRecreatePhysicsState = false;
						for (UPrefabricatorProperty* Property : ComponentData.Properties) {
							if (Property->PropertyName == "BodyInstance") {
								bRecreatePhysicsState = true;
								break;
							}
						}
						if (bRecreatePhysicsState) {
							Primitive->InitializeComponent();
							Primitive->RecreatePhysicsState();
						}
					}
				}
			}
		}
	}

#if WITH_EDITOR
	if (InActorData.Name.Len() > 0) {
		ForceUpdateActorLabel(InActor, InActorData.Name);
	}
#endif // WITH_EDITOR

	//InActor->PostLoad();
	InActor->ReregisterAllComponents();

	if (Service.IsValid()) {
		SCOPE_CYCLE_COUNTER(STAT_LoadActorState_EndTransaction);
		//Service->EndTransaction();
	}
}

void FPrefabTools::UnlinkAndDestroyPrefabActor(APrefabActor* PrefabActor)
{
	TSharedPtr<IPrefabricatorService> Service = FPrefabricatorService::Get();
	if (Service.IsValid()) {
		Service->BeginTransaction(LOCTEXT("TransLabel_CreatePrefab", "Unlink Prefab"));
	}

	// Grab all the actors directly attached to this prefab actor
	TArray<AActor*> ChildActors;
	PrefabActor->GetAttachedActors(ChildActors);

	// Detach them from the prefab actor and cleanup
	for (AActor* ChildActor: ChildActors) {
		ChildActor->DetachFromActor(FDetachmentTransformRules(EDetachmentRule::KeepWorld, true));
		ChildActor->GetRootComponent()->RemoveUserDataOfClass(UPrefabricatorAssetUserData::StaticClass());
	}

	// Finally delete the prefab actor
	PrefabActor->Destroy();

	if (Service.IsValid()) {
		Service->EndTransaction();
	}

}

void FPrefabTools::GetActorChildren(AActor* InParent, TArray<AActor*>& OutChildren)
{
	InParent->GetAttachedActors(OutChildren);
}

namespace {
	void GetPrefabBoundsRecursive(AActor* InActor, FBox& OutBounds, bool bNonColliding, const TSet<UClass*>& IgnoreActorClasses) {
		if (InActor && InActor->IsLevelBoundsRelevant()) {
			const bool bIgnoreBounds = InActor->IsA<APrefabActor>() || IgnoreActorClasses.Contains(InActor->GetClass()); 
			if (!bIgnoreBounds) {
				FBox ActorBounds(ForceInit);
				for (const UActorComponent* ActorComponent : InActor->GetComponents()) {
					if (const UPrimitiveComponent* InPrimComp = Cast<UPrimitiveComponent>(ActorComponent)) {
						if (!IgnoreActorClasses.Contains(InPrimComp->GetClass())) {
							if (InPrimComp->IsRegistered() && (bNonColliding || InPrimComp->IsCollisionEnabled())) {
								ActorBounds += InPrimComp->Bounds.GetBox();
							}
						}
					}
				}
				
				if (ActorBounds.GetExtent() == FVector::ZeroVector) {
					ActorBounds = FBox({ InActor->GetActorLocation() });
				}
				OutBounds += ActorBounds;
			}

			TArray<AActor*> AttachedActors;
			InActor->GetAttachedActors(AttachedActors);
			for (AActor* AttachedActor : AttachedActors) {
				GetPrefabBoundsRecursive(AttachedActor, OutBounds, bNonColliding, IgnoreActorClasses);
			}
		}
	}

	void DestroyActorTree(AActor* InActor) {
		if (!InActor) return;
		TArray<AActor*> Children;
		InActor->GetAttachedActors(Children);

		for (AActor* Child : Children) {
			DestroyActorTree(Child);
		}

		InActor->Destroy();
	}
}

FBox FPrefabTools::GetPrefabBounds(AActor* PrefabActor, bool bNonColliding)
{
	const UPrefabricatorSettings* Settings = GetDefault<UPrefabricatorSettings>();
	FBox Result(EForceInit::ForceInit);
	GetPrefabBoundsRecursive(PrefabActor, Result, bNonColliding, Settings->IgnoreBoundingBoxForObjects);
	return Result;
}

namespace
{
	void DestroyComponent(APrefabActor* PrefabActor, UActorComponent* Comp)
	{
		const FString RemovedName = Comp->GetName() + TEXT("_REMOVED_") + FGuid::NewGuid().ToString();
		Comp->Rename(*RemovedName);
		PrefabActor->RemoveOwnedComponent(Comp);
		Comp->UnregisterComponent();
		Comp->DestroyComponent();
	}
}

void FPrefabTools::LoadStateFromPrefabAsset(APrefabActor* PrefabActor, const FPrefabLoadSettings& InSettings)
{
	SCOPE_CYCLE_COUNTER(STAT_LoadStateFromPrefabAsset);
	if (!PrefabActor) {
		UE_LOG(LogPrefabTools, Error, TEXT("Invalid prefab actor reference"));
		return;
	}

	UPrefabricatorAsset* PrefabAsset = PrefabActor->GetPrefabAsset();
	if (!PrefabAsset) {
		//UE_LOG(LogPrefabTools, Error, TEXT("Prefab asset is not assigned correctly"));
		return;
	}

	PrefabActor->GetRootComponent()->SetMobility(PrefabAsset->PrefabMobility);

	// Pool existing child actors that belong to this prefab
	TArray<AActor*> ExistingActorPool;
	GetActorChildren(PrefabActor, ExistingActorPool);	

	FPrefabInstanceTemplates* LoadState = FGlobalPrefabInstanceTemplates::Get();

	bool bPrefabOutOfDate = PrefabActor->LastUpdateID != PrefabAsset->LastUpdateID;

	// If prefab is out of data, clear out all old components
	TMap<FGuid, UActorComponent*> ReusableCompByItemID;
	{
		TArray<UActorComponent*> PrefabComponents;
		PrefabActor->GetComponents(PrefabComponents, false);
		for (int32 i = PrefabComponents.Num() - 1; i >= 0; i--)
		{
			if (!IsSupportedPrefabRootComponent(PrefabComponents[i]))
			{
				PrefabComponents.RemoveAt(i);
			}
		}

		if (bPrefabOutOfDate)
		{
			for (auto Comp : PrefabComponents)
			{
				DestroyComponent(PrefabActor, Comp);
			}
		}
		else
		{
			for (auto Comp : PrefabComponents)
			{
				UPrefabricatorAssetUserData* PrefabUserData = Comp->GetAssetUserData<UPrefabricatorAssetUserData>();
				if (!(PrefabUserData && PrefabUserData->PrefabActor == PrefabActor))
				{
					DestroyComponent(PrefabActor, Comp);
					continue;
				}

				ReusableCompByItemID.Add(PrefabUserData->ItemID, Comp);
			}
		}
	}

	TMap<FGuid, AActor*> ActorByItemID;	
	for (AActor* ExistingActor : ExistingActorPool) {
		if (ExistingActor && ExistingActor->GetRootComponent()) {
			UPrefabricatorAssetUserData* PrefabUserData = ExistingActor->GetRootComponent()->GetAssetUserData<UPrefabricatorAssetUserData>();
			if (PrefabUserData && PrefabUserData->PrefabActor == PrefabActor) {
				TArray<AActor*> ChildActors;
				ExistingActor->GetAttachedActors(ChildActors);
				if (ChildActors.Num() == 0) {
					// Only reuse actors that have no children
					ActorByItemID.Add(PrefabUserData->ItemID, ExistingActor);
				}
			}
		}
	}

	// PostLoad after internal object references have been resolved
	TArray<UObject*> PostLoadObjects;
	TMap<FGuid, UActorComponent*> PrefabItemToComponentMap;

	// Prefab component support
	for (FPrefabricatorComponentData& CompItemData : PrefabAsset->ComponentData) {
		if (!CompItemData.ClassPathRef.IsValid()) {
			CompItemData.ClassPathRef = CompItemData.ClassPath;
		}

		if (CompItemData.ClassPathRef.GetAssetPathString() != CompItemData.ClassPath) {
			CompItemData.ClassPath = CompItemData.ClassPathRef.GetAssetPathString();
		}

		UClass* CompClass = LoadObject<UClass>(nullptr, *CompItemData.ClassPathRef.GetAssetPathString());
		if (!CompClass) continue;

		UActorComponent* Comp = nullptr;
		if (!bPrefabOutOfDate) {
			// The prefab is not out of date. try to reuse an existing component
			Comp = ReusableCompByItemID.FindRef(CompItemData.PrefabItemID);
			if (Comp) {
				FString ExistingClassName = Comp->GetClass()->GetPathName();
				FString RequiredClassName = CompItemData.ClassPathRef.GetAssetPathString();
				if (ExistingClassName == RequiredClassName) {
					// We can reuse this component
					ReusableCompByItemID.Remove(CompItemData.PrefabItemID);
				}
				else {
					Comp = nullptr;
				}					
			}
		}

		if (!Comp) {
			// Create a new child actor.  Try to create it from an existing template actor that is already preset in the scene

			Comp = PrefabActor->AddComponentByClass(CompClass, false, CompItemData.RelativeTransform, false);
			if (Comp->GetName() != CompItemData.Name) {
				Comp->Rename(*CompItemData.Name);
			}
			// Load the prefab properties in
			LoadComponentState(Comp, CompItemData, InSettings);
			PostLoadObjects.Add(Comp);
		}

		AssignAssetUserData(Comp, CompItemData.PrefabItemID, PrefabActor);

		{
			UActorComponent*& CompRef = PrefabItemToComponentMap.FindOrAdd(CompItemData.PrefabItemID);
			CompRef = Comp;
		}
	}

	// Actor component support
	TMap<FGuid, AActor*> PrefabItemToActorMap;
	if(TSharedPtr<IPrefabricatorService> Service = FPrefabricatorService::Get()) {
		UWorld* World = PrefabActor->GetWorld();
		for (FPrefabricatorActorData& ActorItemData : PrefabAsset->ActorData) {
			// Handle backward compatibility
			if (!ActorItemData.ClassPathRef.IsValid()) {
				ActorItemData.ClassPathRef = ActorItemData.ClassPath;
			}

			if (ActorItemData.ClassPathRef.GetAssetPathString() != ActorItemData.ClassPath) {
				ActorItemData.ClassPath = ActorItemData.ClassPathRef.GetAssetPathString();
			}

			UClass* ActorClass = LoadObject<UClass>(nullptr, *ActorItemData.ClassPathRef.GetAssetPathString());
			if (!ActorClass) continue;

			// Try to re-use an existing actor from this prefab
			AActor* ChildActor = nullptr;
			if (!bPrefabOutOfDate) {
				// The prefab is not out of date. try to reuse an existing actor item
				if (AActor** SearchResult = ActorByItemID.Find(ActorItemData.PrefabItemID)) {
					ChildActor = *SearchResult;
					if (ChildActor) {
						FString ExistingClassName = ChildActor->GetClass()->GetPathName();
						FString RequiredClassName = ActorItemData.ClassPathRef.GetAssetPathString();
						if (ExistingClassName == RequiredClassName) {
							// We can reuse this actor
							ExistingActorPool.Remove(ChildActor);
							ActorByItemID.Remove(ActorItemData.PrefabItemID);
						}
						else {
							ChildActor = nullptr;
						}
					}
				}
			}


			FTransform WorldTransform = ActorItemData.RelativeTransform * PrefabActor->GetTransform();
			if (!ChildActor) {
				// Create a new child actor.  Try to create it from an existing template actor that is already preset in the scene
				AActor* Template = nullptr;
				if (LoadState && InSettings.bCanLoadFromCachedTemplate) {
					Template = LoadState->GetTemplate(ActorItemData.PrefabItemID, PrefabAsset->LastUpdateID);
				}

				ChildActor = Service->SpawnActor(ActorClass, WorldTransform, PrefabActor->GetLevel(), Template);

				ParentActors(PrefabActor, ChildActor);

				if (Template == nullptr || bPrefabOutOfDate) {
					// We couldn't use a template,  so load the prefab properties in
					LoadActorState(ChildActor, ActorItemData, InSettings);
					PostLoadObjects.Add(ChildActor);

					// Save this as a template for future reuse
					if (LoadState && InSettings.bCanSaveToCachedTemplate) {
						LoadState->RegisterTemplate(ActorItemData.PrefabItemID, PrefabAsset->LastUpdateID, ChildActor);
					}
				}
			}
			else {
				// This actor was reused.  re-parent it
				ChildActor->DetachFromActor(FDetachmentTransformRules(EDetachmentRule::KeepWorld, true));
				ParentActors(PrefabActor, ChildActor);

				// Update the world transform.   The reuse happens only on leaf actors (which don't have any further child actors)
				if (ChildActor->GetRootComponent()) {
					EComponentMobility::Type OldChildMobility = ChildActor->GetRootComponent()->Mobility;
					ChildActor->GetRootComponent()->SetMobility(EComponentMobility::Movable);
					ChildActor->SetActorTransform(WorldTransform);
					ChildActor->GetRootComponent()->SetMobility(OldChildMobility);
				}			
			}

			// Force update actor label. I have found that sometimes actor label update would 
			// fall through the cracks.
			ForceUpdateActorLabel(ChildActor, ActorItemData.Name);

			AssignAssetUserData(ChildActor, ActorItemData.PrefabItemID, PrefabActor);

			{
				AActor*& ChildActorRef = PrefabItemToActorMap.FindOrAdd(ActorItemData.PrefabItemID);
				ChildActorRef = ChildActor;
			}

			if (APrefabActor* ChildPrefab = Cast<APrefabActor>(ChildActor)) {
				SCOPE_CYCLE_COUNTER(STAT_LoadStateFromPrefabAsset5);
				if (InSettings.bRandomizeNestedSeed && InSettings.Random) {
					// This is a nested child prefab.  Randomize the seed of the child prefab
					ChildPrefab->Seed = FPrefabTools::GetRandomSeed(*InSettings.Random);
				}
				if (InSettings.bSynchronousBuild) {
					LoadStateFromPrefabAsset(ChildPrefab, InSettings);
				}
			}
		}		
	}

	// Fix up the cross references
	for (const FPrefabricatorComponentData& ComponentData : PrefabAsset->ComponentData) {
		UActorComponent** CompPtr = PrefabItemToComponentMap.Find(ComponentData.PrefabItemID);
		if (!CompPtr) continue;
		{
			UActorComponent* Comp = *CompPtr;
			FixupCrossReferences(ComponentData.Properties, Comp, PrefabItemToActorMap);
		}
	}

	for (const FPrefabricatorActorData& ActorItemData : PrefabAsset->ActorData) {
		AActor** ActorPtr = PrefabItemToActorMap.Find(ActorItemData.PrefabItemID);
		if (!ActorPtr) continue;

		AActor* Actor = *ActorPtr;
		FixupCrossReferences(ActorItemData.Properties, Actor, PrefabItemToActorMap);

		TMap<FString, UActorComponent*> ComponentByPath;
		for (UActorComponent* Component : Actor->GetComponents()) {
			FString ComponentPath = Component->GetPathName(Actor);
			UActorComponent*& ComponentRef = ComponentByPath.FindOrAdd(ComponentPath);
			ComponentRef = Component;
		}

		for (const FPrefabricatorComponentData& ComponentData : ActorItemData.Components) {
			UActorComponent** ComponentPtr = ComponentByPath.Find(ComponentData.Name);
			UActorComponent* Component = ComponentPtr ? *ComponentPtr : nullptr;
			if (!ComponentPtr) continue;

			FixupCrossReferences(ComponentData.Properties, Component, PrefabItemToActorMap);
		}
	}
	for (auto Comp : PostLoadObjects)
	{
		Comp->PostLoad();
	}

	// Destroy the unused actors from the pool
	for (AActor* UnusedActor : ExistingActorPool) {
		DestroyActorTree(UnusedActor);
	}

	PrefabActor->LastUpdateID = PrefabAsset->LastUpdateID;

	if (InSettings.bSynchronousBuild) {
		PrefabActor->HandleBuildComplete();
	}
}

namespace
{
	struct FFixupCrossReferencesContext
	{
		UObject* ObjPtr;
		UPrefabricatorProperty* PrefabProperty;
		TMap<FGuid, AActor*>& PrefabItemToActorMap;
	};

	void FixupCrossReferences_ObjectHelper(
		FFixupCrossReferencesContext& Context
		, void* ObjectPtr
		, const FObjectPropertyBase* ObjectProperty
		, const FString& PropertyPath
		, int32 Index=-1
	)
	{
		FString NewPropertyPath = GetNestedPropertyPath(PropertyPath, ObjectProperty, Index);

		auto NestedPropertyData = Context.PrefabProperty->NestedPropertyData.Find(NewPropertyPath);
		if (!NestedPropertyData) return;

		AActor** SearchResult = Context.PrefabItemToActorMap.Find(NestedPropertyData->CrossReferencePrefabActorId);
		if (!SearchResult) return;
		AActor* CrossReference = *SearchResult;

		ObjectProperty->SetObjectPropertyValue(ObjectPtr, CrossReference);

		////////
		FString ActorName = CrossReference ? CrossReference->GetName() : "[NONE]";
		UE_LOG(LogPrefabTools, Log, TEXT("Cross Reference: %s -> %s"), *NestedPropertyData->CrossReferencePrefabActorId.ToString(), *ActorName);
		////////
	}

	void FixupCrossReferences_StructHelper(
		FFixupCrossReferencesContext& Context
		, void* StructPtr
		, const FStructProperty* StructProperty
		, const FString& PropertyPath
		, int32 PropertyElementIndex=-1
	);

	void FixupCrossReferences_ArrayHelper(
		FFixupCrossReferencesContext& Context
		, void* ArrayPtr
		, const FArrayProperty* ArrayProperty
		, const FString& PropertyPath
		, int32 PropertyElementIndex=-1
	)
	{
		FString NewPropertyPath = GetNestedPropertyPath(PropertyPath, ArrayProperty, PropertyElementIndex);

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(ArrayProperty->Inner))
		{
			auto NestedPropertyData = GetNestedPropertyData(Context, NewPropertyPath);
			if (!NestedPropertyData)
				return;
			
			FScriptArrayHelper Helper(ArrayProperty, ArrayPtr);
			// The array may have different length by default.
			if (Helper.Num() < NestedPropertyData->ArrayLength)
			{
				Helper.AddValues(NestedPropertyData->ArrayLength - Helper.Num());
			}
			for (int32 Index = 0; Index < Helper.Num(); Index++)
			{
				void* ValuePtr = Helper.GetRawPtr(Index);
				FixupCrossReferences_ObjectHelper(Context, ValuePtr, ObjectProperty, NewPropertyPath, Index);
			}
		}
		else if (const FStructProperty* StructProperty = CastField<FStructProperty>(ArrayProperty->Inner))
		{
			auto NestedPropertyData = GetNestedPropertyData(Context, NewPropertyPath);
			if (!NestedPropertyData)
				return;

			FScriptArrayHelper Helper(ArrayProperty, ArrayPtr);
			// The array may have different length by default.
			if (Helper.Num() < NestedPropertyData->ArrayLength)
			{
				Helper.AddValues(NestedPropertyData->ArrayLength - Helper.Num());
			}
			for (int32 Index = 0; Index < Helper.Num(); Index++)
			{
				void* ValuePtr = Helper.GetRawPtr(Index);
				FixupCrossReferences_StructHelper(Context, ValuePtr, StructProperty, NewPropertyPath, Index);
			}
		}
	}

    void FixupCrossReferences_StructHelper(
        FFixupCrossReferencesContext& Context
        , void* StructPtr
        , const FStructProperty* StructProperty
        , const FString& PropertyPath
        , int32 PropertyElementIndex
    )
    {
        FString NewPropertyPath = GetNestedPropertyPath(PropertyPath, StructProperty, PropertyElementIndex);

        for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It) {
            FProperty* InnerProperty = *It;

            if (const FStructProperty* StructProperty = CastField<FStructProperty>(InnerProperty)) {
                void* ValuePtr = StructProperty->ContainerPtrToValuePtr<void>(StructPtr, 0);
                FixupCrossReferences_StructHelper(Context, ValuePtr, StructProperty, NewPropertyPath);
            }
            // Support for TArrays (TODO: Adds support for sets and maps).
            else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InnerProperty)) {
                void* ValuePtr = ArrayProperty->ContainerPtrToValuePtr<void>(StructPtr, 0);
                FixupCrossReferences_ArrayHelper(Context, ValuePtr, ArrayProperty, NewPropertyPath);
            }
            // Changed from FObjectProperty to FObjectPropertyBase to support also soft references.
            else if (const FObjectPropertyBase* ObjProperty = CastField<FObjectPropertyBase>(InnerProperty)) {
                void* ValuePtr = ObjProperty->ContainerPtrToValuePtr<void>(StructPtr, 0);
                FixupCrossReferences_ObjectHelper(Context, ValuePtr, ObjProperty, NewPropertyPath);
            }
        }
    }
}

void FPrefabTools::FixupCrossReferences(const TArray<UPrefabricatorProperty*>& PrefabProperties, UObject* ObjToWrite, TMap<FGuid, AActor*>& PrefabItemToActorMap)
{
	for (UPrefabricatorProperty* PrefabProperty : PrefabProperties) {
		if (!PrefabProperty || !PrefabProperty->bIsCrossReferencedActor) continue;

		FFixupCrossReferencesContext Context{ ObjToWrite , PrefabProperty , PrefabItemToActorMap };

		FProperty* Property = ObjToWrite->GetClass()->FindPropertyByName(*PrefabProperty->PropertyName);
		FString PropertyPath = "";

		// Support for TArrays (TODO: Adds support for sets and maps).
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ObjToWrite);
			FixupCrossReferences_StructHelper(Context, ValuePtr, StructProperty, PropertyPath);
		}
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ObjToWrite);
			FixupCrossReferences_ArrayHelper(Context, ValuePtr, ArrayProperty, PropertyPath);
		}
		// FObjectProperty instead of FObjectPropertyBase to support also soft references
		else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property)) 
		{
			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ObjToWrite);
			FixupCrossReferences_ObjectHelper(Context, ValuePtr, ObjectProperty, PropertyPath);
		}
		// Note: there cannot be more than array, structs, and straight up object deemed as cross ref
		// Nothing to do here
	}
}

void FPrefabVersionControl::UpgradeToLatestVersion(UPrefabricatorAsset* PrefabAsset)
{
	if (PrefabAsset->Version == (int32)EPrefabricatorAssetVersion::InitialVersion) {
		UpgradeFromVersion_InitialVersion(PrefabAsset);
	}

	if (PrefabAsset->Version == (int32)EPrefabricatorAssetVersion::AddedSoftReference) {
		UpgradeFromVersion_AddedSoftReferences(PrefabAsset);
	}

	if (PrefabAsset->Version == (int32)EPrefabricatorAssetVersion::AddedSoftReference_PrefabFix) {
		UpgradeFromVersion_AddedSoftReferencesPrefabFix(PrefabAsset);
	}

	//....

}

void FPrefabVersionControl::UpgradeFromVersion_InitialVersion(UPrefabricatorAsset* PrefabAsset)
{
	check(PrefabAsset->Version == (int32)EPrefabricatorAssetVersion::InitialVersion);

	RefreshReferenceList(PrefabAsset);

	PrefabAsset->Version = (int32)EPrefabricatorAssetVersion::AddedSoftReference;
}

void FPrefabVersionControl::UpgradeFromVersion_AddedSoftReferences(UPrefabricatorAsset* PrefabAsset)
{
	check(PrefabAsset->Version == (int32)EPrefabricatorAssetVersion::AddedSoftReference);

	RefreshReferenceList(PrefabAsset);

	PrefabAsset->Version = (int32)EPrefabricatorAssetVersion::AddedSoftReference_PrefabFix;
}

void FPrefabVersionControl::UpgradeFromVersion_AddedSoftReferencesPrefabFix(UPrefabricatorAsset* PrefabAsset)
{
	check(PrefabAsset->Version == (int32)EPrefabricatorAssetVersion::AddedSoftReference_PrefabFix);

	// Handle upgrade here to move to the next version
}

void FPrefabVersionControl::RefreshReferenceList(UPrefabricatorAsset* PrefabAsset)
{
	for (FPrefabricatorComponentData& ComponentData : PrefabAsset->ComponentData) {
		for (UPrefabricatorProperty* ComponentProperty : ComponentData.Properties) {
			ComponentProperty->SaveReferencedAssetValues();
		}
	}
	for (FPrefabricatorActorData& Entry : PrefabAsset->ActorData) {
		for (UPrefabricatorProperty* ActorProperty : Entry.Properties) {
			ActorProperty->SaveReferencedAssetValues();
		}

		for (FPrefabricatorComponentData& ComponentData : Entry.Components) {
			for (UPrefabricatorProperty* ComponentProperty : ComponentData.Properties) {
				ComponentProperty->SaveReferencedAssetValues();
			}
		}
	}

	PrefabAsset->Modify();
}

/////////////////////// FGlobalPrefabLoadState /////////////////////// 

FPrefabInstanceTemplates* FGlobalPrefabInstanceTemplates::Instance = nullptr;
void FGlobalPrefabInstanceTemplates::_CreateSingleton()
{
	check(Instance == nullptr);
	Instance = new FPrefabInstanceTemplates();
}

void FGlobalPrefabInstanceTemplates::_ReleaseSingleton()
{
	delete Instance;
	Instance = nullptr;
}

void FPrefabInstanceTemplates::RegisterTemplate(const FGuid& InPrefabItemId, FGuid InPrefabLastUpdateId, AActor* InActor)
{
	FPrefabInstanceTemplateInfo& TemplateRef = PrefabItemTemplates.FindOrAdd(InPrefabItemId);
	TemplateRef.TemplatePtr = InActor;
	TemplateRef.PrefabLastUpdateId = InPrefabLastUpdateId;
}

AActor* FPrefabInstanceTemplates::GetTemplate(const FGuid& InPrefabItemId, FGuid InPrefabLastUpdateId)
{
	FPrefabInstanceTemplateInfo* SearchResult = PrefabItemTemplates.Find(InPrefabItemId);
	if (!SearchResult) return nullptr;
	FPrefabInstanceTemplateInfo& Info = *SearchResult;
	AActor* Actor = Info.TemplatePtr.Get();

	if (Info.PrefabLastUpdateId != InPrefabLastUpdateId) {
		// The prefab has been changed since we last cached this template. Invalidate it
		Actor = nullptr;
	}

	// Remove from the map if the actor state is stale
	if (!Actor) {
		PrefabItemTemplates.Remove(InPrefabItemId);
	}

	return Actor;
}


///////////////////////////////// FPrefabSaveModeCrossReferences ///////////////////////////////// 


void FPrefabActorLookup::Register(const FString& InActorPath, const FGuid& InPrefabItemId)
{
	FGuid& ItemIdRef = ActorPathToItemId.FindOrAdd(InActorPath);
	ItemIdRef = InPrefabItemId;
}

void FPrefabActorLookup::Register(AActor* InActor, const FGuid& InPrefabItemId)
{
	if (!InActor) return;
	Register(InActor->GetPathName(), InPrefabItemId);
}

void FPrefabActorLookup::Register(UActorComponent* InComp, const FGuid& InPrefabItemId)
{
	if (!InComp) return;
	Register(InComp->GetName(), InPrefabItemId);
}

bool FPrefabActorLookup::GetPrefabItemId(const FString& InObjectPath, FGuid& OutCrossRefPrefabItem) const
{
	const FGuid* SearchResult = ActorPathToItemId.Find(InObjectPath);
	if (SearchResult) {
		OutCrossRefPrefabItem = *SearchResult;
		return true;
	}
	return false;
}

#undef LOCTEXT_NAMESPACE

