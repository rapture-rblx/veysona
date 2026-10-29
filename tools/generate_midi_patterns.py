"""Original, freely redistributable Type-0 MIDI phrase generator using Python 3 stdlib."""
from pathlib import Path
import struct
OUT=Path(__file__).resolve().parent.parent/'assets'/'midi';OUT.mkdir(parents=True,exist_ok=True)
TPQ=960

def vlq(n):
 n=max(0,int(n));parts=[n&127];n>>=7
 while n:parts.insert(0,(n&127)|128);n>>=7
 return bytes(parts)

def make(name,bpm,notes):
 ev=[(0,bytes([0xff,0x51,0x03])+int(60_000_000/bpm).to_bytes(3,'big'))]
 for p,beat,length,velocity in notes:
  start=int(beat*TPQ);end=int((beat+length)*TPQ)
  ev.extend([(start,bytes([0x90,p,max(1,int(velocity*127))])),(end,bytes([0x80,p,0]))])
 ev.sort(key=lambda x:(x[0],0 if x[1][0]==0x80 else 1))
 body=bytearray();prev=0
 for pos,payload in ev:body+=vlq(pos-prev)+payload;prev=pos
 body+=b'\x00\xff\x2f\x00'
 header=b'MThd'+struct.pack('>IHHH',6,0,1,TPQ)
 track=b'MTrk'+struct.pack('>I',len(body))+body
 (OUT/name).write_bytes(header+track)
 print(name)

make('House - Bassline C minor.mid',124,[(p,i*.5,.4,.76) for i,p in enumerate([36,36,43,36,39,43,41,36,36,36,43,46,41,39,43,36])])
make('Chords - Dream Pad.mid',120,[(p,i*4,3.8,.56) for i,ch in enumerate([[60,63,67],[56,60,63],[58,62,65],[55,59,62]]) for p in ch])
make('Arpeggio - Neon C.mid',128,[(p,i*.25,.22,.62) for i,p in enumerate([60,63,67,72,67,63,60,67]*4)])
make('Lead - Minor Pentatonic.mid',120,[(p,i*.5,.43,.84) for i,p in enumerate([60,63,65,67,70,67,65,63,60,63,67,72,70,67,65,60])])
make('Drums - Four On Floor.mid',126,[(36,i,.2,.94) for i in range(16)]+[(38,i,.15,.87) for i in range(16) if i%4 in (1,3)]+[(42,i*.5,.10,.45) for i in range(32)])
make('Keys - Seventh Chords.mid',90,[(p,i*2,1.8,.62) for i,ch in enumerate([[60,64,67,71],[57,60,64,67],[65,69,72,76],[55,59,62,65]]) for p in ch])
# Veysona original expansion patterns; MIDI files are editable in the piano roll.
make('Veysona - DnB Break 174.mid',174,[(36,b,.14,.96) for b in [0,2.5,4,6.5,8,10.5,12,14.5]]+[(38,b,.12,.88) for b in [1,3,5,7,9,11,13,15]]+[(42,b*.5,.07,.38 if b%2 else .6) for b in range(32)])
make('Veysona - Ambient Seventh Chords.mid',100,[(p,i*4,3.8,.48) for i,chord in enumerate([[60,64,67,71],[57,60,64,67],[62,65,69,72],[55,59,62,65]]) for p in chord])
make('Veysona - UK Garage Bass.mid',132,[(p,b,.3,.7) for b,p in [(0,36),(.75,43),(1.5,36),(2.5,39),(3.5,43),(4,36),(5.5,46),(6.25,43),(7.5,39)]])
make('Veysona - Synth Hook.mid',128,[(p,i*.5,.4,.74) for i,p in enumerate([72,75,79,77,75,72,67,70,72,75,79,82,79,77,75,72])])
