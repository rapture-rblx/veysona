# Private GitHub backup and Windows CI for Veysona Studio

The owner has selected a **free-to-use, private-source** development model. **Never create a public repository for this project.**

1. Install [Git for Windows](https://git-scm.com/downloads/win) and [GitHub CLI](https://cli.github.com/).
2. Extract the source ZIP locally. Do not upload the ZIP to public Releases or websites.
3. Double-click `publish_to_github.cmd` and log into the GitHub account that should own the private repository.
4. Read the confirmation and type `YES` to create or update a **private** `Veysona` repository.
5. Open the private repo's **Actions** page to view the Windows build. Only collaborators with access can view private artifacts.

The PowerShell script checks an existing repository's visibility before pushing. If `Veysona` is public, it refuses to upload any source. Create a new private repo or change the existing repo's visibility after reviewing the implications. The script has not been executed or validated on Windows in this environment.

**Licence prerequisite:** A private repository does not itself satisfy JUCE's AGPLv3 redistribution obligations. Closed-source binary distribution requires a suitable commercial JUCE licence plus other dependency rights checks. See `SOURCE_AND_DISTRIBUTION_POLICY.md`.

For a manual workflow, create a new **private** repository at https://github.com/new, then upload the full extracted source including `.github/` and `assets/`. An uploaded ZIP file does not trigger a source build workflow.
