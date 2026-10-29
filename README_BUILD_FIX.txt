Veysona Studio v1.31 Windows CMake configuration fix

Replace scripts\restore_sound_bank.ps1 in your repository with the file in this patch.
Re-run your Windows build. This removes dependency on the Get-FileHash cmdlet while retaining SHA256 validation using .NET.

If the download hash check fails, do not bypass it: report the actual expected and observed hashes.
