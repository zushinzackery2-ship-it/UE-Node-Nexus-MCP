$ErrorActionPreference = "Stop"

$Patterns = @("SpecialAgent", "specialagent", "special-agent")
$Roots = @(
    (Join-Path $env:USERPROFILE ".codex"),
    (Join-Path $env:USERPROFILE ".codeium\windsurf"),
    (Join-Path $env:USERPROFILE ".windsurf"),
    (Join-Path $env:USERPROFILE ".config\opencode"),
    (Join-Path $env:USERPROFILE ".octonic"),
    (Join-Path $env:APPDATA "Windsurf"),
    (Join-Path $env:APPDATA "opencode")
) | Where-Object { Test-Path $_ }

$Removed = @()
$RemovedMaterialGraphSkills = @()
$FailedMaterialGraphSkillRemovals = @()
$ConfigHits = @()
$LikelyAuthoringSkills = @()
$EditedConfigs = @()
$FailedConfigEdits = @()

$MaterialGraphSkillPaths = @(
    (Join-Path $env:USERPROFILE ".codex\skills\ue-material-graph"),
    (Join-Path $env:USERPROFILE ".config\opencode\skills\ue-material-graph"),
    (Join-Path $env:USERPROFILE ".codeium\windsurf\skills\ue-material-graph"),
    (Join-Path $env:USERPROFILE ".octonic\skills\ue-material-graph")
)

foreach ($SkillPath in $MaterialGraphSkillPaths)
{
    if (Test-Path $SkillPath)
    {
        try
        {
            Remove-Item -LiteralPath $SkillPath -Recurse -Force -ErrorAction Stop
            $RemovedMaterialGraphSkills += $SkillPath
        }
        catch
        {
            $FailedMaterialGraphSkillRemovals += [pscustomobject]@{
                path = $SkillPath
                error = $_.Exception.Message
            }
        }
    }
}

$OpencodeConfig = Join-Path $env:USERPROFILE ".config\opencode\opencode.jsonc"
if (Test-Path $OpencodeConfig)
{
    $Original = Get-Content -Raw -LiteralPath $OpencodeConfig
    $Updated = $Original -replace '(?ms),?\s*"special-agent"\s*:\s*\{\s*"type"\s*:\s*"remote"\s*,\s*"url"\s*:\s*"http://localhost:8767/sse"\s*,\s*"enabled"\s*:\s*true\s*\}\s*', ''
    if ($Updated -ne $Original)
    {
        try
        {
            Set-Content -LiteralPath $OpencodeConfig -Value $Updated -NoNewline -ErrorAction Stop
            $EditedConfigs += $OpencodeConfig
        }
        catch
        {
            $FailedConfigEdits += [pscustomobject]@{
                path = $OpencodeConfig
                error = $_.Exception.Message
            }
        }
    }
}

$OpencodeWriteSkill = Join-Path $env:USERPROFILE ".config\opencode\skills\write-a-skill"
if (Test-Path $OpencodeWriteSkill)
{
    $LikelyAuthoringSkills += $OpencodeWriteSkill
}

foreach ($Root in $Roots)
{
    Get-ChildItem -LiteralPath $Root -Recurse -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match "SpecialAgent|specialagent|special-agent" } |
        ForEach-Object {
            $Path = $_.FullName
            try
            {
                Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop
                $Removed += $Path
            }
            catch
            {
                $ConfigHits += $Path
            }
        }
}

$ConfigExtensions = @(".json", ".toml", ".yaml", ".yml", ".md", ".txt")
foreach ($Root in $Roots)
{
    Get-ChildItem -LiteralPath $Root -Recurse -Force -File -ErrorAction SilentlyContinue |
        Where-Object { $ConfigExtensions -contains $_.Extension.ToLowerInvariant() } |
        ForEach-Object {
            $Path = $_.FullName
            $Matches = Select-String -LiteralPath $Path -Pattern "SpecialAgent|specialagent|special-agent" -ErrorAction SilentlyContinue
            if ($Matches)
            {
                $ConfigHits += $Path
            }
        }
}

[pscustomobject]@{
    removed_specialagent_name_paths = $Removed
    removed_ue_material_graph_skills = $RemovedMaterialGraphSkills
    failed_ue_material_graph_skill_removals = $FailedMaterialGraphSkillRemovals
    edited_configs = $EditedConfigs
    failed_config_edits = $FailedConfigEdits
    likely_specialagent_script_authoring_skills = $LikelyAuthoringSkills
    config_files_still_containing_specialagent = $ConfigHits | Select-Object -Unique
} | ConvertTo-Json -Depth 4
