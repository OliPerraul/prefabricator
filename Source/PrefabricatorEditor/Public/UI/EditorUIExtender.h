//$ Copyright 2015-23, Code Respawn Technologies Pvt Ltd - All Rights Reserved $//

#pragma once
#include "CoreMinimal.h"
#include "LevelEditor.h"

class PREFABRICATOREDITOR_API FEditorUIExtender {
public:
	void Extend();
	void Release();

private:
	FDelegateHandle LevelViewportExtenderHandle;
	FDelegateHandle LevelViewportExtenderHandleNoActors;
	TSharedPtr<class FExtender> LevelToolbarExtender;
};

