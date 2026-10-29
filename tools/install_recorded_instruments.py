#!/usr/bin/env python3
"""Install AUTHENTIC recorded, CC0 piano / guitar / acoustic drum WAVs for Veysona.

This script never synthesizes instrument audio. Source archives contain recordings
released for redistribution; decoding and resampling are the only signal changes.

Usage: py -3 tools/install_recorded_instruments.py --quality compact
       py -3 tools/install_recorded_instruments.py --quality full
       py -3 tools/install_recorded_instruments.py --verify-only
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ASSETS = (Path(os.environ['APPDATA']) / 'Veysona Studio' / 'Recorded Assets'
          if os.name == 'nt' and os.environ.get('APPDATA') else ROOT / 'assets')
RELEASE = 'https://github.com/facebookincubator/Meta-open-sound/releases/download/'

META = {
 'Piano - Recorded Upright': {
  'tag': 'meta-piano-v1.0.0',
  'files': {
   'compact': ('meta-open-sound-meta-piano-ogg.zip', '4ba8095f5c2a70396d14f3b9c563f6c9010a093893159bad50e83145e1c35d40'),
   'full': ('meta-open-sound-meta-piano-wav.zip', 'ee9023dbde849ecab393a098aa0cfbd6f2ef48d4a096b9c08808b72fc913f70e'),
  },
  'roots': [36,43,48,55,60,67,72,79],
  'kind':'piano',
  'layers':4,
  'credit': 'Meta Piano, Meta Open Sound, CC0 1.0',
 },
 'Guitar - Recorded Nylon': {
  'tag':'meta-nylon-guitar-v1.0.0',
  'files': {
   'compact': ('meta-open-sound-meta-nylon-guitar-ogg.zip','2ff8f6296cfdb1a98f67116c5091a4f9f01c0c46ac538dd1c5756acc8b76889b'),
   'full': ('meta-open-sound-meta-nylon-guitar-wav.zip','e7fa168b97a72ecc1257917f85cb524fe7c2e1f098dfdcaf8f5c0e29cabe768e'),
  },
  'roots':[40,45,48,52,55,60,64,69,72],
  'kind':'nylon',
  'layers':3,
  'credit':'Meta Nylon Guitar, Meta Open Sound, CC0 1.0',
 },
 'Guitar - Recorded Steel': {
  'tag':'meta-steel-guitar-v1.0.0',
  'files': {
   'compact':('meta-open-sound-meta-steel-guitar-ogg.zip','a06baf193497ba36f20ccb67018e9f106155ad5e4f81d8a672530498038f68ed'),
   'full':('meta-open-sound-meta-steel-guitar-wav.zip','caaa8a9c07217f4a82d1dfd9f5657c286155464b66bf4dd27797811c2558a909'),
  },
  'roots':[40,45,48,52,55,60,64,69,72],
  'kind':'steel',
  'layers':3,
  'credit':'Meta Steel Guitar, Meta Open Sound, CC0 1.0',
 },
}

CRATES = {
 'drumkit':('ferrosintesis-samples-drumkit','0.2.0'),
 'drumkit2':('ferrosintesis-samples-drumkit2','0.2.0'),
}
# Exact GM pad names that correspond to Veysona's 8-pad Drum Rack.
DRUMS = [
 (36,'Kick','kick_vl4_rr1.flac','drumkit'),
 (38,'Snare','snare_vl5_rr1.flac','drumkit'),
 (42,'Closed Hat','hhc_vl3_rr1.flac','drumkit'),
 (46,'Open Hat','hho_vl3_rr1.flac','drumkit'),
 (37,'Rim','sidestick_vl2_rr1.flac','drumkit'),
 (41,'Low Tom','tomlo_vl3_rr1.flac','drumkit'),
 (49,'Crash','crash_vl3_rr1.flac','drumkit2'),
 (43,'High Tom','tomhi_vl3_rr1.flac','drumkit'),
]
# All three kits use separate *recorded* acoustic hits from the original multisampled
# drum-session recordings; no oscillator/synthesized drum audio is created.
DRUM_KITS = {
 'Recorded Acoustic - Soft': [
  (36,'Kick','kick_vl1_rr1.flac','drumkit'),
  (38,'Snare','snare_vl1_rr1.flac','drumkit'),
  (42,'Closed Hat','hhc_vl1_rr1.flac','drumkit'),
  (46,'Open Hat','hho_vl1_rr1.flac','drumkit'),
  (37,'Rim','sidestick_vl1_rr1.flac','drumkit'),
  (41,'Low Tom','tomlo_vl1_rr1.flac','drumkit'),
  (49,'Crash','crash_vl1_rr1.flac','drumkit2'),
  (43,'High Tom','tomhi_vl1_rr1.flac','drumkit'),
 ],
 'Recorded Acoustic': DRUMS,
 'Recorded Acoustic - Hard': [
  (36,'Kick','kick_vl4_rr2.flac','drumkit'),
  (38,'Snare','snare_vl6_rr1.flac','drumkit'),
  (42,'Closed Hat','hhc_vl4_rr1.flac','drumkit'),
  (46,'Open Hat','hho_vl4_rr1.flac','drumkit'),
  (37,'Rim','sidestick_vl3_rr1.flac','drumkit'),
  (41,'Low Tom','tomlo_vl4_rr1.flac','drumkit'),
  (49,'Crash','crash_vl3_rr2.flac','drumkit2'),
  (43,'High Tom','tomhi_vl4_rr1.flac','drumkit'),
 ],
}

NOTE_NAMES=('C','Cs','D','Ds','E','F','Fs','G','Gs','A','As','B')
def note_name(pitch:int)->str:
 return NOTE_NAMES[pitch%12]+str(pitch//12-1)

def sha256(path:Path)->str:
 h=hashlib.sha256()
 with path.open('rb') as f:
  for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
 return h.hexdigest()

def download(url:str,dest:Path, expected_digest:str|None=None)->None:
 if dest.exists() and (not expected_digest or sha256(dest)==expected_digest):
  print('Using previously downloaded',dest.name);return
 print('Downloading',url)
 req=urllib.request.Request(url,headers={'User-Agent':'VeysonaStudio-SampleInstaller/1.30'})
 tmp=dest.with_suffix(dest.suffix+'.downloading')
 try:
  with urllib.request.urlopen(req,timeout=120) as source,tmp.open('wb') as out:
   while True:
    block=source.read(1024*1024)
    if not block:break
    out.write(block)
  if expected_digest and sha256(tmp).lower()!=expected_digest.lower():
   raise ValueError('SHA-256 mismatch for '+dest.name+'; refusing this archive')
  tmp.replace(dest)
 finally:
  tmp.unlink(missing_ok=True)

def wav_good(path:Path)->bool:
 import wave
 try:
  with wave.open(str(path),'rb') as f:
   return f.getnframes()>300 and f.getnchannels()==2 and f.getframerate()==44100
 except (OSError,EOFError,ValueError):return False

def convert(src:Path, dest:Path,ffmpeg:str):
 # 16-bit 44.1 kHz stereo, output size controlled for instant preload.
 command=[ffmpeg,'-hide_banner','-nostdin','-loglevel','error','-y','-i',str(src),'-ac','2','-ar','44100','-c:a','pcm_s16le',str(dest)]
 p=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=90)
 if p.returncode or not wav_good(dest):
  raise RuntimeError('Audio conversion failed for '+src.name+': '+p.stderr[-600:])

def notes_from_zip(zf:zipfile.ZipFile,kind:str,roots:list[int],layer:int|None=None)->dict[int,str]:
 entries=[n for n in zf.namelist() if n.lower().endswith(('.ogg','.wav')) and not n.endswith('/')]
 mapping={}
 for root in roots:
  suffix='_'+note_name(root)+'_01'
  options=[n for n in entries if Path(n).stem.endswith(suffix)]
  if kind=='piano':
   options=[n for n in options if 'INSPiano_' in n]
  elif kind=='nylon':
   options=[n for n in options if 'INSGuitarNylon_' in n]
  elif kind=='steel':
   options=[n for n in options if 'INSGuitarSteel_' in n]
  if layer is not None:
   velocity_tag=f'Vel{layer:02d}'
   options=[n for n in options if velocity_tag in Path(n).stem]
  # prefer mid-dynamic recorded articulation, then deterministic string choices
  options.sort(key=lambda n:(not ('Vel03_' in n or 'Vel02Pick_' in n or 'Vel02_' in n),n))
  if not options:raise RuntimeError(f'Archive contains no {kind} recording for MIDI {root} ({note_name(root)})')
  mapping[root]=options[0]
 return mapping

def install_meta(name:str,spec:dict,quality:str,cache:Path,ffmpeg:str):
 archive,hash_value=spec['files'][quality]
 archive_path=cache/archive
 url=f"{RELEASE}{spec['tag']}/{archive}"
 download(url,archive_path,hash_value)
 out=ASSETS/'instrument_packs'/name
 staged=out.with_name(out.name+'.installing')
 if staged.exists():shutil.rmtree(staged)
 staged.mkdir(parents=True)
 try:
  with zipfile.ZipFile(archive_path) as zf, tempfile.TemporaryDirectory() as tmp:
   files=[]
   for layer in range(1,spec['layers']+1):
    mapping=notes_from_zip(zf,spec['kind'],spec['roots'],layer)
    for root,member in mapping.items():
     src=Path(tmp)/('source'+Path(member).suffix)
     with zf.open(member) as input_file,src.open('wb') as target:shutil.copyfileobj(input_file,target)
     out_wav=staged/(str(root)+f'_v{layer}.wav')
     convert(src,out_wav,ffmpeg)
     files.append({'midi_note':root,'velocity_layer':layer,'velocity_layers':spec['layers'],
                   'source_filename':Path(member).name,'filename':out_wav.name,'sha256':sha256(out_wav)})
  manifest={'recording_source':spec['credit'],'licence':'CC0-1.0',
       'source_release':url,'source_archive_sha256':hash_value,
       'quality':quality,'velocity_layers':spec['layers'],
       'transform':'decoded & converted to stereo PCM16 44.1kHz; no synthesis',
       'files':files}
  (staged/'RECORDED_SOURCE.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8')
  if out.exists():shutil.rmtree(out)
  staged.rename(out)
  print('Installed',name,len(files),'recorded zones')
 finally:
  if staged.exists():shutil.rmtree(staged)

def install_drums(cache:Path,ffmpeg:str):
 files={}
 for key,(crate,version) in CRATES.items():
  filename=f'{crate}-{version}.crate'
  archive=cache/filename
  url=f'https://static.crates.io/crates/{crate}/{filename}'
  download(url,archive)
  files[key]=archive
 # Extract only the individual audio files needed by the three real kits.
 # Do not extract crate paths directly: this avoids archive path traversal.
 for kit_name,drum_list in DRUM_KITS.items():
  out=ASSETS/'drum_kits'/kit_name
  staged=out.with_name(out.name+'.installing')
  if staged.exists():shutil.rmtree(staged)
  staged.mkdir(parents=True)
  try:
   with tempfile.TemporaryDirectory() as tmp:
    samples=[]
    for midi,name,flac,crate_key in drum_list:
     with tarfile.open(files[crate_key],'r:gz') as tf:
      members=[m for m in tf.getmembers() if m.isfile() and m.name.endswith('/samples/'+flac)]
      if len(members)!=1:raise RuntimeError('Cannot find original recorded drum hit: '+flac)
      src=Path(tmp)/flac
      with tf.extractfile(members[0]) as inp,src.open('wb') as target:shutil.copyfileobj(inp,target)
     output=staged/f'{midi:03d} - {name}.wav'
     convert(src,output,ffmpeg)
     samples.append({'midi_note':midi,'name':name,'original_file':flac,'file':output.name,'sha256':sha256(output)})
   manifest={'recording_source':'Versilian Studios Virtuosity Drums via ferrosintesis sample crates',
      'licence':'CC0-1.0','source_url':'https://github.com/0x4D44/ferrosintesis',
      'crate_versions':{k:v[1] for k,v in CRATES.items()},
      'kit_variant':kit_name,
      'transform':'decoded & converted to stereo PCM16 44.1kHz; no synthesis','files':samples}
   (staged/'RECORDED_SOURCE.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8')
   if out.exists():shutil.rmtree(out)
   staged.rename(out)
   print('Installed',kit_name,': 8 recorded acoustic hits')
  finally:
   if staged.exists():shutil.rmtree(staged)

def verify_installed():
 root_dirs=[ASSETS/'instrument_packs'/n for n in META]+[ASSETS/'drum_kits'/n for n in DRUM_KITS]
 count=0
 for d in root_dirs:
  mf=d/'RECORDED_SOURCE.json'
  if not mf.exists():
   print('NOT INSTALLED:',d.relative_to(ROOT));continue
  record=json.loads(mf.read_text())
  entries=record['files']
  for item in entries:
   filename=item.get('filename',item.get('file'))
   path=d/filename
   if not path.exists() or sha256(path)!=item['sha256'] or not wav_good(path):
    raise RuntimeError('Recorded sample integrity failed: '+str(path))
  print('VERIFIED:',d.name,len(entries),'recorded WAVs')
  count+=1
 return count==len(root_dirs)

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--quality',choices=('compact','full'),default='compact',help='compact: ~90 MB original OGG; full: ~1.8 GB source WAV')
 ap.add_argument('--verify-only',action='store_true')
 ap.add_argument('--cache',type=Path,default=ROOT/'.recorded-download-cache')
 a=ap.parse_args()
 if a.verify_only:
  sys.exit(0 if verify_installed() else 1)
 ffmpeg=shutil.which('ffmpeg')
 if not ffmpeg:
  print('ERROR: FFmpeg is required to decode the recorded OGG/FLAC samples.',file=sys.stderr)
  print('Windows: winget install Gyan.FFmpeg   (then reopen Terminal)',file=sys.stderr)
  print('Or install FFmpeg from https://ffmpeg.org/download.html',file=sys.stderr)
  sys.exit(2)
 a.cache.mkdir(parents=True,exist_ok=True)
 for name,spec in META.items():install_meta(name,spec,a.quality,a.cache,ffmpeg)
 install_drums(a.cache,ffmpeg)
 if not verify_installed():sys.exit(1)
 print('Success: 3 recorded piano/guitar programs with velocity layers + 3 authentic acoustic drum kits installed.')
 print('Restart Veysona and open Load Instrument / Drum Rack to select them.')
if __name__=='__main__':main()
