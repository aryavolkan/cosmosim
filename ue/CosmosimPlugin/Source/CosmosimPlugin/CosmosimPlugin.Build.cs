using UnrealBuildTool;
using System.IO;

public class CosmosimPlugin : ModuleRules
{
    public CosmosimPlugin(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "Niagara",
            "NiagaraCore",
            "NiagaraShader",
            "RenderCore",
            "RHI",
            "EnhancedInput",
            "UMG",
            "Slate",
            "SlateCore"
        });

        string ThirdPartyPath = Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "libcosmosim");
        string IncludePath = Path.Combine(ThirdPartyPath, "include");
        string LibPath = Path.Combine(ThirdPartyPath, "lib");

        PublicIncludePaths.Add(IncludePath);

        if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libcosmosim.dylib"));
            RuntimeDependencies.Add(Path.Combine(LibPath, "libcosmosim.dylib"));
        }
        else if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "cosmosim.lib"));
            RuntimeDependencies.Add(Path.Combine(LibPath, "cosmosim.dll"));
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libcosmosim.so"));
            RuntimeDependencies.Add(Path.Combine(LibPath, "libcosmosim.so"));
        }
    }
}
