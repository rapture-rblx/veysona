"""Deterministic original audio sample generator (Python 3 standard library)."""
import math
import random
import struct
import wave
from pathlib import Path

RATE = 48000
OUT = Path(__file__).resolve().parent.parent / 'assets' / 'samples'
OUT.mkdir(parents=True, exist_ok=True)
R = random.Random(246801)
TWOPI = math.tau


def write(name, frames):
    path = OUT / name
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        b = bytearray()
        for l, r in frames:
            b += struct.pack('<hh', int(max(-1, min(1, l))*32767), int(max(-1, min(1, r))*32767))
        w.writeframes(b)
    print(name, f'{path.stat().st_size // 1024} KB')


def make(name, seconds, sound):
    def frames():
        for i in range(int(RATE*seconds)):
            t = i/RATE
            v = sound(t)
            yield (v*.98, v)
    write(name, frames())

make('Kick - Deep.wav', .55, lambda t: math.sin(TWOPI*(50*t+72*(1-math.exp(-t*34))/34))*math.exp(-12*t)*.9)
make('Kick - Punch.wav', .55, lambda t: (math.sin(TWOPI*(70*t+92*(1-math.exp(-t*49))/49))*.85+R.uniform(-.7,.7)*math.exp(-t*100)*.14)*math.exp(-t*17))
make('Snare - Clean.wav', .44, lambda t: (R.uniform(-1,1)*.58 + math.sin(TWOPI*185*t)*.32)*math.exp(-t*21))
make('Snare - Tight.wav', .34, lambda t: (R.uniform(-1,1)*.62 + math.sin(TWOPI*230*t)*.28)*math.exp(-t*32))
make('HiHat - Bright.wav', .17, lambda t: R.uniform(-.5,.5)*math.exp(-t*42))
make('HiHat - Open.wav', .48, lambda t: R.uniform(-.45,.45)*math.exp(-t*12))
make('Clap - Studio.wav', .4, lambda t: (R.uniform(-.7,.7)*.7)*math.exp(-t*18)*(1+.4*math.cos(70*t)))
make('Tom - Low.wav', .68, lambda t: math.sin(TWOPI*(120*t-48*t*t))*math.exp(-9*t)*.5)
make('Bass - Sub.wav', 1.8, lambda t: (math.sin(TWOPI*55*t)+.15*math.sin(TWOPI*110*t))*math.exp(-t*2)*.48)
make('Bass - Grit.wav', 1.2, lambda t: math.tanh((math.sin(TWOPI*82.41*t)+.5*math.sin(TWOPI*164.82*t))*1.3)*math.exp(-t*1.8)*.32)
make('Synth - Pluck.wav', 1.4, lambda t: (math.sin(TWOPI*440*t)+.4*math.sin(TWOPI*880*t))*.34*math.exp(-t*3))
make('Pad - Warm.wav', 2.1, lambda t: (math.sin(TWOPI*261.63*t)+math.sin(TWOPI*329.63*t)+math.sin(TWOPI*392*t))*.10*min(t*8,1)*math.exp(-t*.55))
make('FX - Rise.wav', 1.8, lambda t: (R.uniform(-.15,.15)+math.sin(TWOPI*(180*t+250*t*t))*.23)*(t/1.8))

# A royalty-free eight-bar demo loop composed from the original samples above.
def read(path):
    with wave.open(str(path),'rb') as w:
        raw=w.readframes(w.getnframes())
    vals=struct.iter_unpack('<hh',raw)
    return [(x/32768,y/32768) for x,y in vals]

kick=read(OUT/'Kick - Deep.wav')
snare=read(OUT/'Snare - Clean.wav')
hat=read(OUT/'HiHat - Bright.wav')
length=16
mix=[[0.,0.] for _ in range(RATE*length)]

def layer(sample,seconds,gain):
    pos=int(seconds*RATE)
    for i, (a,b) in enumerate(sample):
        if pos+i>=len(mix):break
        mix[pos+i][0]+=a*gain
        mix[pos+i][1]+=b*gain

for step in range(32):
    if step%2==0:layer(kick,step*.5,.62)
    if step%4 in (1,3):layer(snare,step*.5,.40)
    layer(hat,step*.5,.25)
write('Loop - Studio Groove 120BPM.wav',mix)
print('All original sample assets generated.')
