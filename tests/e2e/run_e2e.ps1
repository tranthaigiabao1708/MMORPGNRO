[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [ValidateSet('1', '2', '3', '4', 'All')]
    [string]$Tier = 'All',

    [Parameter(Mandatory = $false)]
    [switch]$SkipDocker,

    [Parameter(Mandatory = $false)]
    [switch]$ListOnly
)

$ErrorActionPreference = 'Stop'

$ScriptDir = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($ScriptDir)) {
    $ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
}

$RepoRootPath = Join-Path $ScriptDir '..\..'
if (-not (Test-Path $RepoRootPath)) {
    Write-Error "Cannot resolve repository root from '$ScriptDir'."
    exit 2
}
$RepoRoot = (Resolve-Path $RepoRootPath).Path

if (-not (Test-Path (Join-Path $RepoRoot 'README.md'))) {
    Write-Error "Repository root '$RepoRoot' is missing README.md."
    exit 2
}
if (-not (Test-Path (Join-Path $ScriptDir 'go.mod'))) {
    Write-Error "E2E directory '$ScriptDir' is missing go.mod."
    exit 2
}

function Set-DefaultEnvVar {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$DefaultValue
    )
    $existing = [Environment]::GetEnvironmentVariable($Name, 'Process')
    if ([string]::IsNullOrEmpty($existing)) {
        [Environment]::SetEnvironmentVariable($Name, $DefaultValue, 'Process')
    }
}

[Environment]::SetEnvironmentVariable('IU_E2E_REPO_ROOT', $RepoRoot, 'Process')
Set-DefaultEnvVar -Name 'IU_E2E_COMPOSE_FILE' -DefaultValue 'deploy/docker-compose.local.yml'
Set-DefaultEnvVar -Name 'IU_E2E_PG_CONTAINER' -DefaultValue 'iu-postgres'
Set-DefaultEnvVar -Name 'IU_E2E_REDIS_CONTAINER' -DefaultValue 'iu-redis'
Set-DefaultEnvVar -Name 'IU_E2E_NATS_CONTAINER' -DefaultValue 'iu-nats'
Set-DefaultEnvVar -Name 'IU_E2E_PG_HOST_PORT' -DefaultValue '5433'
Set-DefaultEnvVar -Name 'IU_E2E_PG_USER' -DefaultValue 'iu_admin'
Set-DefaultEnvVar -Name 'IU_E2E_PG_PASSWORD' -DefaultValue 'iu_dev_secret_2026'
Set-DefaultEnvVar -Name 'IU_E2E_PG_DB' -DefaultValue 'infinite_universe'

if ($SkipDocker) {
    [Environment]::SetEnvironmentVariable('IU_E2E_SKIP_DOCKER', '1', 'Process')
} else {
    Set-DefaultEnvVar -Name 'IU_E2E_SKIP_DOCKER' -DefaultValue '0'
}

$RegexMap = @{
    '1'   = '^TestTier1_'
    '2'   = '^TestTier2_'
    '3'   = '^TestTier3_'
    '4'   = '^TestTier4_'
    'All' = '^TestTier[1-4]_'
}
$TestRegex = $RegexMap[$Tier]

Write-Verbose "RepoRoot=$RepoRoot Tier=$Tier Regex=$TestRegex SkipDocker=$($env:IU_E2E_SKIP_DOCKER) ListOnly=$ListOnly"

Push-Location $ScriptDir
try {
    if ($ListOnly) {
        & go test -list $TestRegex ./...
        $code = $LASTEXITCODE
        exit $code
    }

    & go test -v -count=1 -run $TestRegex ./...
    $code = $LASTEXITCODE
    exit $code
} finally {
    Pop-Location
}
