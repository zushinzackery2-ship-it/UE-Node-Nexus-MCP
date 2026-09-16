using UnrealBuildTool;

public class UeNodeNexusBridge : ModuleRules
{
    public UeNodeNexusBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        IWYUSupport = IWYUSupport.Full;
        ConfigureBuildIdentity(this);

        PrivateIncludePaths.AddRange(new string[]
        {
            System.IO.Path.Combine(ModuleDirectory, "Private", "Animation"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Audio"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Assets"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "AutoIndex"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Blueprint"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Blueprint", "NodeInterface"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Core"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Diagnostics"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Dispatch"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Graph"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Input"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Level"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Material"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Material", "NodeInterface"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Material", "Patch"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Module"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Object"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Texture"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Transcode"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Transport")
        });

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Json"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AnimGraph",
            "AssetRegistry",
            "AssetTools",
            "BlueprintGraph",
            "EditorFramework",
            "EnhancedInput",
            "InputCore",
            "JsonUtilities",
            "Landscape",
            "MaterialEditor",
            "MessageLog",
            "PhysicsCore",
            "Projects",
            "RenderCore",
            "RHI",
            "UnrealEd",
            "UeNodeNexusGuard"
        });
    }

    public static void ConfigureBuildIdentity(ModuleRules Rules)
    {
        string Root = System.IO.Path.GetFullPath(System.IO.Path.Combine(Rules.ModuleDirectory, "..", ".."));
        string Descriptor = System.IO.Path.Combine(Root, System.IO.Path.GetFileName(Root) + ".uplugin");
        var Files = new System.Collections.Generic.List<string>();
        Files.Add(Descriptor);
        foreach (string Name in new string[]
        {
            "Source", "Config"
        })
        {
            string Directory = System.IO.Path.Combine(Root, Name);
            if (System.IO.Directory.Exists(Directory))
            {
                Files.AddRange(System.IO.Directory.GetFiles(Directory, "*", System.IO.SearchOption.AllDirectories));
            }
        }
        Files.Sort((Left, Right) => System.StringComparer.Ordinal.Compare(
            System.IO.Path.GetRelativePath(Root, Left).Replace('\\', '/'),
            System.IO.Path.GetRelativePath(Root, Right).Replace('\\', '/')));
        Rules.ExternalDependencies.AddRange(Files);
        string Fingerprint;
        using (var Hash = System.Security.Cryptography.IncrementalHash.CreateHash(System.Security.Cryptography.HashAlgorithmName.SHA256))
        {
            byte[] Separator = new byte[1];
            foreach (string File in Files)
            {
                string Relative = System.IO.Path.GetRelativePath(Root, File).Replace('\\', '/');
                Hash.AppendData(System.Text.Encoding.UTF8.GetBytes(Relative));
                Hash.AppendData(Separator);
                Hash.AppendData(System.IO.File.ReadAllBytes(File));
                Hash.AppendData(Separator);
            }
            Fingerprint = System.Convert.ToHexString(Hash.GetHashAndReset()).ToLowerInvariant();
        }
        string Version = EpicGames.Core.JsonObject.Parse(System.IO.File.ReadAllText(Descriptor)).GetStringField("VersionName");
        if (!System.Text.RegularExpressions.Regex.IsMatch(Version, @"^\d+\.\d+\.\d+([+-][A-Za-z0-9.-]+)?$"))
        {
            throw new BuildException("Build identity requires a semantic VersionName in " + Descriptor);
        }
        string Commit = "unrecorded";
        bool Dirty = true;
        bool Recorded = false;
        string Metadata = System.IO.Path.Combine(Root, "BuildIdentity.json");
        if (System.IO.File.Exists(Metadata))
        {
            Rules.ExternalDependencies.Add(Metadata);
            var Data = EpicGames.Core.JsonObject.Parse(System.IO.File.ReadAllText(Metadata));
            Commit = Data.GetStringField("source_commit");
            Recorded = Data.GetStringField("source_fingerprint") == Fingerprint;
            Dirty = !Recorded || Data.GetBoolField("source_dirty");
        }
        if (!System.Text.RegularExpressions.Regex.IsMatch(Commit, @"^([a-f0-9]{40}|[a-f0-9]{64}|unrecorded)$"))
        {
            throw new BuildException("Invalid source commit in build identity");
        }
        Rules.PrivateDefinitions.Add("NEXUS_BUILD_VERSION=\"" + Version + "\"");
        Rules.PrivateDefinitions.Add("NEXUS_SOURCE_COMMIT=\"" + Commit + "\"");
        Rules.PrivateDefinitions.Add("NEXUS_SOURCE_FINGERPRINT=\"" + Fingerprint + "\"");
        Rules.PrivateDefinitions.Add("NEXUS_SOURCE_DIRTY=" + (Dirty ? "1" : "0"));
        Rules.PrivateDefinitions.Add("NEXUS_IDENTITY_RECORDED=" + (Recorded ? "1" : "0"));
        Rules.PrivateDefinitions.Add("NEXUS_CONTRACT_VERSION=4");
    }

}
