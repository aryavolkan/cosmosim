#include "CosmosimModule.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "FCosmosimModule"

void FCosmosimModule::StartupModule()
{
    // Locate ThirdParty relative to this plugin's base directory
    FString PluginBaseDir = FPaths::Combine(
        FPaths::ProjectPluginsDir(), TEXT("CosmosimPlugin"));
    if (!FPaths::DirectoryExists(PluginBaseDir))
    {
        // Fallback: try engine plugins path
        PluginBaseDir = FPaths::Combine(
            FPaths::EnginePluginsDir(), TEXT("CosmosimPlugin"));
    }
    FString LibDir = FPaths::Combine(PluginBaseDir, TEXT("ThirdParty/libcosmosim/lib"));

#if PLATFORM_MAC
    FString LibName = TEXT("libcosmosim.dylib");
#elif PLATFORM_WINDOWS
    FString LibName = TEXT("cosmosim.dll");
#elif PLATFORM_LINUX
    FString LibName = TEXT("libcosmosim.so");
#endif

    FString LibPath = FPaths::Combine(LibDir, LibName);
    LibHandle = FPlatformProcess::GetDllHandle(*LibPath);

    if (!LibHandle)
    {
        UE_LOG(LogTemp, Error, TEXT("Cosmosim: Failed to load %s"), *LibPath);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("Cosmosim: Loaded %s"), *LibPath);
    }
}

void FCosmosimModule::ShutdownModule()
{
    if (LibHandle)
    {
        FPlatformProcess::FreeDllHandle(LibHandle);
        LibHandle = nullptr;
    }
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FCosmosimModule, CosmosimPlugin)
