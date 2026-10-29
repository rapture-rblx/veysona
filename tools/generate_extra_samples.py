"""Produce additional original, copyright-free demonstration one-shots, pads and loops.
Uses only Python 3 stdlib. All generated waveforms are deterministic and may be used royalty-free.
The output is 48 kHz, 16-bit stereo WAV; these are synthesized samples, not recordings.
"""
from pathlib import Path
import math, random, struct, wave
RATE=48000
OUT=Path(__file__).resolve().parent.parent/'assets'/'samples'
OUT.mkdir(parents=True,exist_ok=True)
TAU=math.tau

def save(name,seconds,fn):
 r=random.Random(1001+sum(ord(x) for x in name)); N=int(seconds*RATE)
 with wave.open(str(OUT/name),'wb') as w:
  w.setnchannels(2);w.setsampwidth(2);w.setframerate(RATE)
  block=bytearray()
  for i in range(N):
   t=i/RATE
   l,rr=fn(t,i,r)
   l=max(-1,min(1,l));rr=max(-1,min(1,rr))
   block.extend(struct.pack('<hh',int(l*32767),int(rr*32767)))
   if len(block)>8192:
    w.writeframesraw(block);block.clear()
  if block:w.writeframesraw(block)
 print(name)

def drum(t,i,r,pitch,decay,noise=.0,click=0):
 hz=pitch*(.45+.55*math.exp(-t*34))
 v=math.sin(TAU*hz*t)*math.exp(-t*decay)*.85
 v+=r.uniform(-1,1)*math.exp(-t*80)*noise
 v+=r.uniform(-1,1)*math.exp(-t*190)*click
 return v

for name,base,decay,noise in [('Kick - Vinyl.wav',95,20,.05),('Kick - Club.wav',135,14,.1),('Kick - Tight 808.wav',74,9,.03),('Kick - Boom.wav',54,8,.04)]:
 save(name,.7,lambda t,i,r,b=base,d=decay,n=noise:(lambda v:(v*.99,v*.99))(drum(t,i,r,b,d,n)))
for name,f,d in [('Snare - Room.wav',175,12),('Snare - Noise.wav',240,16),('Snare - Vintage.wav',190,26),('Rim - Click.wav',830,75)]:
 save(name,.42,lambda t,i,r,f=f,d=d:(lambda v:(v*.9,v*.88))( (r.uniform(-1,1)*.55+math.sin(TAU*f*t)*.35)*math.exp(-t*d)))
for name,duration,decay,bright in [('HiHat - Closed.wav',.16,53,.45),('HiHat - Dust.wav',.29,35,.26),('HiHat - Loose.wav',.48,14,.25),('Ride - Airy.wav',1.15,5,.18),('Shaker - Short.wav',.22,28,.3),('Perc - Wood.wav',.3,24,.25),('Perc - Metal.wav',.7,13,.36)]:
 save(name,duration,lambda t,i,r,d=decay,b=bright:(lambda v:(v,v*.94))(r.uniform(-1,1)*b*math.exp(-t*d)*(1-.55*math.sin(TAU*580*t))))
for name,hz,tau in [('Bass - Root C.wav',65.41,1.6),('Bass - Root F.wav',87.31,1.4),('Bass - Root G.wav',98,1.4),('Synth - Crystal.wav',784,.9),('Synth - Soft Keys.wav',523.25,1.4),('Synth - Bell.wav',659.25,2.)]:
 save(name,2.,lambda t,i,r,f=hz,d=tau:(lambda v:(v,v*.96))( (.6*math.sin(TAU*f*t)+.17*math.sin(TAU*f*2.02*t)+.12*math.sin(TAU*f*3.01*t))*math.exp(-t/d)*.45))
for name,freq in [('Pad - Cloud.wav',220),('Pad - Dark.wav',146.83),('Pad - Glass.wav',392)]:
 save(name,3.,lambda t,i,r,f=freq:(lambda v:(v*.98,v))(sum(math.sin(TAU*f*m*t+math.sin(t*.4)*.2) for m in (1,1.503,1.995))*.12*min(1,t*2)*max(0,1-t/4)))
for name,length in [('FX - Downlifter.wav',1.8),('FX - White Sweep.wav',2.4),('FX - Reverse Air.wav',1.4),('FX - Impact.wav',.9)]:
 reverse='Reverse' in name;impact='Impact' in name;down='Down' in name
 save(name,length,lambda t,i,r,n=length,reverse=reverse,impact=impact,down=down:(lambda v:(v,v*.97))(
  (math.sin(TAU*(120*t+(1.-t/n if down else t/n)*650*t))*.2+r.uniform(-1,1)*.18) *
  ((t/n)**1.7 if reverse else math.exp(-t*7) if impact else max(0,(1-t/n) if down else t/n)) ))
# Four-bar percussion-only demonstrations at 120 and 140 BPM, respectively.
def read_wav(name):
 with wave.open(str(OUT/name),'rb') as w:raw=w.readframes(w.getnframes())
 return [(a/32768.,b/32768.) for a,b in struct.iter_unpack('<hh',raw)]
for bpm,label in ((120,'Loop - Dust House 120BPM.wav'),(140,'Loop - Minimal Techno 140BPM.wav')):
 kick=read_wav('Kick - Club.wav');snare=read_wav('Snare - Vintage.wav');hat=read_wav('HiHat - Dust.wav')
 sec=4*4*60/bpm
 N=int(sec*RATE);mix=[[0.,0.] for _ in range(N)]
 def layer(samples,beat,gain):
  start=int(beat*60/bpm*RATE)
  for j,(l,rr) in enumerate(samples):
   if start+j>=N:break
   mix[start+j][0]+=l*gain;mix[start+j][1]+=rr*gain
 for beat in range(16):
  layer(kick,beat,.73)
  if beat%4 in (1,3):layer(snare,beat,.42)
  layer(hat,beat,.28)
  if beat%2==1:layer(hat,beat+.5,.22)
 def play(t,i,r):return mix[i][0],mix[i][1]
 save(label,sec,play)
print('Extra samples generated. These are synthesized demonstration sounds, not commercial recordings.')
