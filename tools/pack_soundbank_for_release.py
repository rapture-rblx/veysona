#!/usr/bin/env python3
"""Developer-side lossless packing of the bundled PCM WAV bank for one-file ZIP distribution.
Requires the Xiph `flac` CLI. Original WAV files remain in the source tree and are excluded
from the distributable ZIP; restoration happens automatically when building on Windows.
"""
from concurrent.futures import ThreadPoolExecutor, as_completed
from hashlib import sha256
from pathlib import Path
import subprocess, sys, time

ROOT=Path(__file__).resolve().parents[1]
PACKED=ROOT/'assets'/'packed_soundbank'
FILES=sorted([*ROOT.glob('assets/samples/*.wav'),*ROOT.glob('assets/instruments/*.wav')])
if len(FILES)!=2489:raise SystemExit(f'Expected 2,489 bundled WAVs; got {len(FILES)}')
PACKED.mkdir(parents=True,exist_ok=True)

def pack(path):
    rel=path.relative_to(ROOT)
    output=PACKED/rel.relative_to('assets')
    output=output.with_name(output.name+'.flac')
    output.parent.mkdir(parents=True,exist_ok=True)
    if not output.exists():
        subprocess.run(['flac','--silent','--force','--keep-foreign-metadata','-5',str(path),'-o',str(output)],check=True)
    with path.open('rb') as f:
        original=sha256(f.read()).hexdigest()
    return rel.as_posix(),original,path.stat().st_size,output.stat().st_size

start=time.time();results=[]
with ThreadPoolExecutor(max_workers=4) as pool:
    futures=[pool.submit(pack,p) for p in FILES]
    for i,f in enumerate(as_completed(futures),1):
        results.append(f.result())
        if i%500==0: print(i,'/',len(FILES),'packed',flush=True)
results.sort()
manifest=PACKED/'sha256_manifest.tsv'
manifest.write_text(''.join(f'{hash_value}\t{path}\n' for path,hash_value,_,__ in results),encoding='utf-8')
print('packed',len(results),'original_bytes',sum(row[2] for row in results),'flac_bytes',sum(row[3] for row in results),'seconds',round(time.time()-start,1),flush=True)
