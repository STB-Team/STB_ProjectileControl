$ErrorActionPreference = 'Stop'

$preset = 'default'

Write-Host ''
$cmakeArgs = @('--preset', $preset, '-B', 'build', '-S', '.', '--fresh')

$pluginDir = (Read-Host 'Enter mod SKSE/Plugins directory for post-build copy (leave blank to skip copying)').Trim()
if (-not [string]::IsNullOrWhiteSpace($pluginDir)) {
    Write-Host "Using deploy directory: $pluginDir"
    $cmakeArgs += "-DSTB_PROJECTILECONTROL_DEPLOY_DIR=$pluginDir"
}
else {
    Write-Host 'No deploy directory: the DLL stays in build/Release.'
}

Push-Location $PSScriptRoot
try {
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
finally {
    Pop-Location
}
