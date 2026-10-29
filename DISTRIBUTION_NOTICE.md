# Veysona Studio — source, audio content, and dependency licensing

Veysona-owned source files under `src/`, `plugins/`, `tools/` and `tests/` carry the project's MIT licence (`LICENSE`), unless an individual file says otherwise. Bundled generated samples and instrument patches in `assets/samples`, `assets/instruments`, and MIDI starter patterns are newly synthesised by this project; no third-party recordings are distributed in the package. The Veysona logo is provided as part of the project assets.

**JUCE**: The desktop application and five VST3 plugins depend on JUCE 8.0.10. JUCE offers AGPLv3 and commercial licensing routes. The MIT licence of the Veysona-owned source alone **does not** grant permission to redistribute a closed-source JUCE-linked binary. Before publicly distributing a built installer, the maintainer must use a valid JUCE commercial licence or meet the complete AGPLv3 obligations for the combined software, including providing corresponding source and required notices. See https://juce.com/get-juce/ and https://github.com/juce-framework/JUCE. This document is not legal advice.

**VST3**: The VST3 SDK v3.8+ uses the MIT licence (https://www.steinberg.net/developers/vstsdk/). Include notices for any SDK code actually distributed. The installer bundles **only five plugins built from Veysona's original source**. Third-party plugin binaries are not bundled and must not be redistributed without permission.

**Inno Setup**: Built installer created using Inno Setup 6 (https://jrsoftware.org/isinfo.php). Consult its licence before public redistribution.

**Source access**: If distributing JUCE-linked binaries under JUCE's **AGPLv3 option**, comply with the AGPLv3 corresponding-source and other obligations. **For the owner's newly selected free-but-private-source distribution model, first obtain a suitable commercial JUCE licence** and review all other dependencies and contributions. Do not automatically publish Veysona source. Existing MIT grants for earlier released versions remain effective. The installer currently includes this notice and the historical MIT notice and is **not cleared for public proprietary distribution**. See `SOURCE_AND_DISTRIBUTION_POLICY.md`.

**Audio samples**: The new 1.1 expansion samples are reproducible with `tools/generate_v11_content.py` (NumPy needed only to regenerate assets, not to run the DAW). The existing samples are reproducible using the older generation tools.
