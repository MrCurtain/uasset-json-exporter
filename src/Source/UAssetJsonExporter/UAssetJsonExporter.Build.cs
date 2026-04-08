using UnrealBuildTool;

public class UAssetJsonExporter : ModuleRules
{
    public UAssetJsonExporter(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "UnrealEd",
                "Json",
                "BlueprintGraph",
                "KismetCompiler",
                "UMG",
                "UMGEditor",
                "MovieScene",
                "SlateCore",
                "Niagara",
                "NiagaraCore",
                "AIModule",
                "AnimGraph"
            }
        );
    }
}
