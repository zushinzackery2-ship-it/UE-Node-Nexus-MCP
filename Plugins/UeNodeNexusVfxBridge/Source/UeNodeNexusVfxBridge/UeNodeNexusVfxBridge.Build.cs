using UnrealBuildTool;

public class UeNodeNexusVfxBridge : ModuleRules
{
    public UeNodeNexusVfxBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        IWYUSupport = IWYUSupport.Full;
        ConfigureBuildIdentity();

        PrivateIncludePaths.AddRange(new string[]
        {
            System.IO.Path.Combine(ModuleDirectory, "Private", "Cascade"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Module"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Niagara"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Niagara", "Formats"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Niagara", "Lint"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Niagara", "Modules"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Niagara", "Transcode")
        });

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "UeNodeNexusBridge"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AssetRegistry",
            "Json",
            "JsonUtilities",
            "Niagara",
            "NiagaraCore",
            "NiagaraEditor",
            "Sequencer",
            "UnrealEd"
        });
    }

    private void ConfigureBuildIdentity()
    {
        string Root = System.IO.Path.GetFullPath(System.IO.Path.Combine(ModuleDirectory, "..", ".."));
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
        ExternalDependencies.AddRange(Files);
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
            ExternalDependencies.Add(Metadata);
            var Data = EpicGames.Core.JsonObject.Parse(System.IO.File.ReadAllText(Metadata));
            Commit = Data.GetStringField("source_commit");
            Recorded = Data.GetStringField("source_fingerprint") == Fingerprint;
            Dirty = !Recorded || Data.GetBoolField("source_dirty");
        }
        if (!System.Text.RegularExpressions.Regex.IsMatch(Commit, @"^([a-f0-9]{40}|[a-f0-9]{64}|unrecorded)$"))
        {
            throw new BuildException("Invalid source commit in build identity");
        }
        PrivateDefinitions.Add("NEXUS_BUILD_VERSION=\"" + Version + "\"");
        PrivateDefinitions.Add("NEXUS_SOURCE_COMMIT=\"" + Commit + "\"");
        PrivateDefinitions.Add("NEXUS_SOURCE_FINGERPRINT=\"" + Fingerprint + "\"");
        PrivateDefinitions.Add("NEXUS_SOURCE_DIRTY=" + (Dirty ? "1" : "0"));
        PrivateDefinitions.Add("NEXUS_IDENTITY_RECORDED=" + (Recorded ? "1" : "0"));
        PrivateDefinitions.Add("NEXUS_CONTRACT_VERSION=2");
    }

}
