#!/usr/bin/env python3
"""Offline sound-bank integrity and possible near-duplicate audit.

Exact uniqueness is objective. Spectral similarity merely flags pairs for
manual listening; it never deletes files or claims perceptual equivalence.
Requires numpy; no Internet or source recordings.
"""
from pathlib import Path
import numpy as np, wave, hashlib, json, collections
ROOT=Path(__file__).resolve().parents[1]
DIR=ROOT/'assets/samples'
files=sorted(DIR.glob('*.wav'))
exact=collections.defaultdict(list)
features=[];names=[];silence=[];clipped=[];bad=[]
for f in files:
 try:
  with wave.open(str(f),'rb') as w:
   rate,channels,width,frames=w.getframerate(),w.getnchannels(),w.getsampwidth(),w.getnframes()
   raw=w.readframes(frames)
  exact[(channels,rate,width,hashlib.sha256(raw).hexdigest())].append(f.name)
  if width!=2:continue
  pcm=np.frombuffer(raw,dtype='<i2').astype(np.float32).reshape(-1,channels).mean(axis=1)/32768
  if np.max(np.abs(pcm))<.01:silence.append(f.name)
  if np.max(np.abs(pcm))>.995:clipped.append(f.name)
  # Time-normalized 0.35 second window, log-frequency approximate spectrum.
  rateScale=max(1,round(rate/12000))
  x=pcm[::rateScale][:4096]
  if len(x)<1024:x=np.pad(x,(0,1024-len(x)))
  if len(x)<4096:x=np.pad(x,(0,4096-len(x)))
  spectrum=np.abs(np.fft.rfft(x*np.hanning(4096)))
  edges=np.geomspace(2,len(spectrum),33).astype(int)
  bins=np.array([np.sqrt(np.mean(spectrum[edges[j]:max(edges[j]+1,edges[j+1])]**2)) for j in range(32)])
  bins=np.log1p(bins)
  bins/=np.linalg.norm(bins)+1e-10
  features.append(bins)
  names.append(f.name)
 except Exception as e:bad.append([f.name,str(e)])
arr=np.array(features,dtype=np.float32)
# Perceptual similarity is only a screening metric. Exact duplicates are separately proven.
similar=[]
for start in range(0,len(arr),64):
 scores=arr[start:start+64]@arr.T
 for a in range(scores.shape[0]):
  j=start+a
  # Limit candidate volume; keep only higher similarities, mostly same category.
  ix=np.where(scores[a,j+1:]>.9995)[0]+j+1
  for k in ix[:20]:similar.append([round(float(scores[a,k]),6),names[j],names[k]])
similar.sort(reverse=True)
report={'files':len(files),'new_v125_files':sum(n.startswith('V125 ') for n in names),
 'unique_exact_pcm':len(exact),'exact_duplicate_groups':[v for v in exact.values() if len(v)>1],
 'very_quiet':silence[:100],'almost_full_scale':clipped[:100],'unreadable':bad,
 'spectral_similarity_threshold':.9995,'high_similarity_pairs_count':len(similar),
 'manual_listening_candidates':similar[:35],
 'note':'Spectral nearest-neighbour screening alone does not prove that two WAVs sound the same. Never auto-delete or rename a sample used in existing projects.'}
(ROOT/'assets/V125_SAMPLE_QUALITY_REPORT.json').write_text(json.dumps(report,indent=2))
print({k:len(v) if isinstance(v,list) else v for k,v in report.items() if k not in ('note','spectral_similarity_threshold')})
print('closest',similar[:8])
