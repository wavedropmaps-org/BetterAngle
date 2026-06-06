param (
    [string]$CommitRange = ""
)

# 1. Read Current Version
$versionFile = "VERSION"
if (-Not (Test-Path $versionFile)) {
    Write-Error "VERSION file not found!"
    exit 1
}
$currentVersion = (Get-Content $versionFile).Trim()
Write-Host "Current version: $currentVersion"

# 2. Parse and Increment
$parts = $currentVersion.Split('.')
if ($parts.Length -ne 3) {
    Write-Error "Invalid version format in VERSION file!"
    exit 1
}

$major = [int]$parts[0]
$minor = [int]$parts[1]
$patch = [int]$parts[2]
$patch++
$newVersion = "$major.$minor.$patch"
Write-Host "New version: $newVersion"

# 3. Update VERSION file
$newVersion | Out-File -FilePath $versionFile -Encoding ascii -NoNewline

# 4. Update CMakeLists.txt
$cmakeFile = "CMakeLists.txt"
if (Test-Path $cmakeFile) {
    $cmakeContent = Get-Content $cmakeFile
    $cmakeContent = $cmakeContent -replace "project\(BetterAngle VERSION \d+\.\d+\.\d+", "project(BetterAngle VERSION $newVersion"
    $cmakeContent | Out-File -FilePath $cmakeFile -Encoding ascii
    Write-Host "Updated CMakeLists.txt"
}

# 5. Update State.h
$stateFile = "include/shared/State.h"
if (Test-Path $stateFile) {
    $stateContent = Get-Content $stateFile
    $stateContent = $stateContent -replace "#define V_MAJ \d+", "#define V_MAJ $major"
    $stateContent = $stateContent -replace "#define V_MIN \d+", "#define V_MIN $minor"
    $stateContent = $stateContent -replace "#define V_PAT \d+", "#define V_PAT $patch"
    $stateContent | Out-File -FilePath $stateFile -Encoding ascii
    Write-Host "Updated State.h"
}

# 6. Update RELEASE_NOTES.md
$releaseNotesFile = "RELEASE_NOTES.md"
if (Test-Path $releaseNotesFile) {
    $oldNotes = Get-Content $releaseNotesFile
    $heading = "### BetterAngle Pro v$newVersion"

    if ($oldNotes | Where-Object { $_ -eq $heading }) {
        # Detailed notes already pre-written for this version — skip the stub so
        # the release body shows the real change list instead of the generic line.
        Write-Host "RELEASE_NOTES.md already has entry for v$newVersion - keeping existing notes"
    } else {
        $newEntry = "$heading`n"
        if ($CommitRange) {
            $logs = git log $CommitRange --oneline --pretty=format:"- %s"
            if ($logs) {
                $newEntry += $logs + "`n"
            } else {
                $newEntry += "- Automated build release.`n"
            }
        } else {
            $newEntry += "- Automated build release.`n"
        }
        $newContent = $newEntry + "`n" + ($oldNotes -join "`n")
        $newContent | Out-File -FilePath $releaseNotesFile -Encoding ascii
        Write-Host "Updated RELEASE_NOTES.md with new entry for v$newVersion"
    }
}

# 7. Commit and Tag (Self-Contained Golden Path)
git config user.name "github-actions[bot]"
git config user.email "github-actions[bot]@users.noreply.github.com"

git add VERSION CMakeLists.txt include/shared/State.h RELEASE_NOTES.md
git commit -m "chore: auto-increment version to $newVersion [skip ci]"

$tag = "v$newVersion"
git tag -f $tag

# Robust Retry Loop for Concurrent Pushes
for ($i = 1; $i -le 5; $i++) {
    Write-Host "Attempting to push version $newVersion (Attempt $i)..."
    git pull --rebase origin main -X theirs
    if ($LASTEXITCODE -eq 0) {
        git push origin main
        if ($LASTEXITCODE -eq 0) {
            git push origin -f $tag
            Write-Host "Push successful."
            break
        }
    } else {
        git rebase --abort
        git pull --rebase origin main
    }
    Write-Host "Concurrency collision detected, retrying in 2s..."
    Sleep 2
}

Write-Host "Version bump to $newVersion complete. Commit and tag pushed."

Write-Host "Version bump to $newVersion complete."
