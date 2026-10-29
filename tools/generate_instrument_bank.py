"""Original, freely redistributable oscillator-based WAV presets for Veysona v0.8.
No third-party recordings. Deterministic and safe to regenerate.
"""
from pathlib import Path
import math, wave
import numpy as np

ROOT=Path(__file__).resolve().parents[1]/'assets'/'instruments'
ROOT.mkdir(parents=True,exist_ok=True)
SR=48000
PRESETS=[
  ('Warm Electric Piano',2.2,'rhodes',261.6256),
  ('Glass Celesta',2.5,'bell',261.6256),
  ('Dreamy Felt Keys',2.6,'felt',261.6256),
  ('Vintage Organ',4.0,'organ',261.6256),
  ('Nylon String Pluck',2.4,'nylon',261.6256),
  ('Analog Soft Pad',4.8,'pad',261.6256),
  ('Dark Cinematic Pad',4.8,'dark',261.6256),
  ('Plucked Marimba',2.1,'marimba',261.6256),
  ('Velvet Strings',4.7,'strings',261.6256),
  ('Sub Bass C3',2.8,'sub',130.8128),
  ('Analog Bass C3',2.8,'bass',130.8128),
  ('FM Bell',3.1,'fm',261.6256),
  ('Airy Choir',4.4,'choir',261.6256),
  ('Bright Synth Pluck',2.0,'bright',261.6256),
  ('House Stab',1.5,'stab',261.6256),
  ('Soft Guitar Harmonics',3.0,'guitar',261.6256),
]

for ix,(name,seconds,kind,hz) in enumerate(PRESETS):
 n=int(SR*seconds);t=np.arange(n,dtype=np.float64)/SR
 rng=np.random.default_rng(8041+ix)
 if kind=='rhodes':
  tone=np.sin(2*np.pi*hz*t)+.40*np.sin(2*np.pi*hz*2.01*t)*np.exp(-t*2.1)+.17*np.sin(2*np.pi*hz*3.97*t)*np.exp(-t*4.5)
  env=(1-np.exp(-t*350))*np.exp(-t*1.25)
 elif kind=='bell':
  tone=sum(a*np.sin(2*np.pi*hz*m*t) for a,m in [(1,1),(.42,2.7),(.23,4.05),(.12,6.95)])
  env=(1-np.exp(-t*600))*np.exp(-t*.95)
 elif kind=='felt':
  tone=np.sin(2*np.pi*hz*t)+.21*np.sin(2*np.pi*hz*2*t)*np.exp(-t*3)+.035*rng.standard_normal(n)*np.exp(-t*12)
  env=(1-np.exp(-t*120))*np.exp(-t*1.1)
 elif kind=='organ':
  tone=np.sin(2*np.pi*hz*t)+.50*np.sin(2*np.pi*2*hz*t)+.30*np.sin(2*np.pi*3*hz*t)+.20*np.sin(2*np.pi*4*hz*t)
  env=np.minimum(1,t*50)*np.minimum(1,(seconds-t)*3)
 elif kind in ('nylon','guitar'):
  tone=sum((.4/h)*np.sin(2*np.pi*hz*h*t + .03*np.sin(2*np.pi*4.6*t)) for h in range(1,11))
  env=(1-np.exp(-t*200))*np.exp(-t*(1.1 if kind=='nylon' else .8))
 elif kind in ('pad','dark','strings','choir'):
  harmonics={'pad':(1,.25,.13,.08),'dark':(1,.42,.23,.16),'strings':(.7,.44,.18,.09),'choir':(1,.10,.22,.06)}[kind]
  tone=sum(a*np.sin(2*np.pi*(h+1)*hz*t+(.015*(h-1))*np.sin(2*np.pi*.8*t)) for h,a in enumerate(harmonics))
  env=np.minimum(1,t*(3 if kind=='choir' else 5))*np.minimum(1,(seconds-t)*1.8)
 elif kind=='marimba':
  tone=np.sin(2*np.pi*hz*t)+.31*np.sin(2*np.pi*hz*3.98*t)*np.exp(-t*3)+.09*np.sin(2*np.pi*hz*9.2*t)*np.exp(-t*8)
  env=(1-np.exp(-t*450))*np.exp(-t*1.7)
 elif kind=='sub':
  tone=np.sin(2*np.pi*hz*t)+.10*np.sin(2*np.pi*2*hz*t)
  env=np.minimum(1,t*80)*np.minimum(1,(seconds-t)*3)
 elif kind=='bass':
  tone=sum(np.sin(2*np.pi*hz*h*t)/h for h in range(1,12))
  env=np.minimum(1,t*150)*np.exp(-t*.48)
 elif kind=='fm':
  tone=np.sin(2*np.pi*hz*t + 2.5*np.exp(-t*1.3)*np.sin(2*np.pi*hz*2.31*t))
  env=(1-np.exp(-t*450))*np.exp(-t*.92)
 elif kind=='bright':
  tone=sum(np.sin(2*np.pi*hz*h*t)/(h**1.1) for h in range(1,14))
  env=(1-np.exp(-t*200))*np.exp(-t*2.9)
 elif kind=='stab':
  tone=sum(np.sin(2*np.pi*hz*h*t)/h for h in range(1,9))
  env=np.minimum(1,t*130)*np.exp(-t*4.6)
 else:raise AssertionError(kind)
 stereo=np.stack([tone*env,tone*env],axis=1)
 stereo[:,1]=np.roll(stereo[:,1],7+ix%8)*.93
 stereo=np.tanh(stereo*.75)
 stereo*=.7/max(.7,np.max(np.abs(stereo)))
 fade=np.minimum(1.0,(n-np.arange(n))/(SR*.04))
 stereo*=fade[:,None]
 pcm=(np.clip(stereo,-.99,.99)*32767).astype('<i2')
 with wave.open(str(ROOT/(name+'.wav')),'wb') as f:
  f.setnchannels(2);f.setsampwidth(2);f.setframerate(SR);f.writeframes(pcm.tobytes())
print(f'Generated {len(PRESETS)} original sampled instrument presets in {ROOT}')
