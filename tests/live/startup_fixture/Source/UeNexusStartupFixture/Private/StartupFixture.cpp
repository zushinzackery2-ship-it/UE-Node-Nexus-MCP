#include "HAL/FileManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

// This module is copied only to isolated acceptance projects. The marker belongs
// to the fixture project, so normal projects never receive an injected advisory.
class FNexusStartupFixture final : public IModuleInterface
{
public:
    void StartupModule() override
    {
        if (IFileManager::Get().FileExists(*(FPaths::ProjectDir() / TEXT("StartupAdvisory.fixture"))))
        {
            Handle = FCoreDelegates::OnPostEngineInit.AddLambda([]()
            {
                const FText Title = NSLOCTEXT("DriveSpaceDialog", "LowHardDriveSpaceMsgTitle", "Warning: Low Drive Space");
                const FText Message = FText::FromString(TEXT("Nexus isolated startup advisory acceptance fixture"));
                UE_LOG(LogTemp, Display, TEXT("Nexus startup fixture entering the editor modal handler"));
                FMessageDialog::Open(EAppMsgType::Ok, Message, Title);
                UE_LOG(LogTemp, Display, TEXT("Nexus startup fixture advisory returned"));
            });
        }
    }

    void ShutdownModule() override
    {
        FCoreDelegates::OnPostEngineInit.Remove(Handle);
    }

private:
    FDelegateHandle Handle;
};

IMPLEMENT_MODULE(FNexusStartupFixture, UeNexusStartupFixture)
