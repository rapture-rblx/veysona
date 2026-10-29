#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
manifest="$root/assets/packed_soundbank/sha256_manifest.tsv"
[[ -e "$manifest" ]] || exit 0

count=0
while IFS=$'\t' read -r digest path; do
 [[ -z "$digest" ]] && continue
 target="$root/$path"
 if [[ -f "$target" ]] && [[ "$(sha256sum "$target" | cut -d' ' -f1)" == "$digest" ]]; then
  count=$((count+1));continue
 fi
 command -v flac >/dev/null 2>&1 || { echo "Install Xiph flac to restore the bundled bank" >&2;exit 1; }
 src="$root/assets/packed_soundbank/${path#assets/}.flac"
 [[ -f "$src" ]] || { echo "Missing packed sample $src" >&2;exit 1; }
 mkdir -p "$(dirname "$target")"
 flac --silent --decode --force --keep-foreign-metadata "$src" -o "$target"
 [[ "$(sha256sum "$target" | cut -d' ' -f1)" == "$digest" ]] || { echo "WAV hash mismatch $target" >&2;exit 1; }
 count=$((count+1))
done < "$manifest"
[[ "$count" == 2489 ]] || { echo "Incomplete restoration: $count/2489" >&2;exit 1; }
echo "Veysona sound bank verified: $count WAV files"
