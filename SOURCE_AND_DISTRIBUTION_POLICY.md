# Veysona Studio — Private-source transition (development policy, not a licence grant)

**Intended product model:** free Windows desktop application; publisher-controlled, private source code.

**Important:** Veysona's earlier source releases carried an MIT licence. Those already-granted MIT rights cannot be retroactively revoked. This package retains the historical `LICENSE` notice; it is not evidence that all contributors have assigned their rights or that all third-party dependencies can be relicensed. Future original work may be released under more restrictive terms only by the people who own the applicable copyrights.

Before releasing a private-source executable:

1. Obtain and retain the **appropriate JUCE commercial licence** if distributing JUCE-linked binaries without meeting the AGPLv3 source-sharing obligations. The dependency terms must be checked against the chosen JUCE edition and your business eligibility.
2. Audit original contributions, MIT/other third-party code and VST3 SDK version; preserve mandatory notices. A commercial JUCE licence does not supersede licences of other components.
3. Prepare a reviewed **free end-user licence agreement (EULA)** allowing personal and commercial music production and protecting users' ownership of their music. Do not ship unreviewed legal text as final.
4. Prepare a clear assets licensing statement for original samples and any external content, including permission to release finished songs without royalties from Veysona's original content.
5. Keep development sources in a **private GitHub repository**. `publish_to_github.ps1` is restricted to creating private repos and refuses to push into an existing public repo. Do not upload source ZIPs to public Releases or your public website.
6. Verify Windows application/VST3 and signed installer builds before distributing. Source ZIPs here are for development, not public end-user distribution.

**Not legal advice.** Have the code/dependency licences and the EULA reviewed before marketing Veysona as a closed-source product. The current `LICENSE` documents earlier MIT source distribution and remains preserved for compliance.
