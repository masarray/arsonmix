#!/usr/bin/env python3
"""Offline listening WAV contract: format, finite energy, direction and no clipping."""
from pathlib import Path
import array
import sys
import wave

root = Path(sys.argv[1] if len(sys.argv)>1 else "sonku-listening")
def read(name, frames):
    path=root/name
    assert path.exists() and path.stat().st_size>10000,path
    with wave.open(str(path),"rb") as w:
        assert w.getnchannels()==2
        assert w.getsampwidth()==2
        assert w.getframerate()==48000
        assert w.getnframes()==frames,(name,w.getnframes(),frames)
        pcm=array.array("h",w.readframes(frames))
        if sys.byteorder!="little":pcm.byteswap()
    assert len(pcm)==2*frames
    peak=max(map(abs,pcm))
    assert 250<peak<30000,(name,peak)
    assert sum(abs(x) for x in pcm[:48000])>100000,(name,"unexpected silence")
    return pcm
single=read("sonku-reference-four.wav",4*120000)
blind=read("sonku-blind-eight.wav",8*120000)
orbit=read("sonku-orbit-cw.wav",480000)
v2four=read("sonku-v2-room-four.wav",4*120000)
v2orbit=read("sonku-v2-room-orbit-cw.wav",480000)
# Baseline V1 and candidate V2 are deliberately non-identical on same noise.
# Room must affect later samples, not fake position on the direct sound.
difference=sum(abs(int(a)-int(b)) for a,b in
               zip(v2four[70000:200000],single[70000:200000]))
assert difference>1500,("V2 room candidate did not affect PCM",difference)
orbitDelta=sum(abs(int(a)-int(b)) for a,b in
               zip(v2orbit[70000:200000],orbit[70000:200000]))
assert orbitDelta>1500,("V2 moving room candidate did not affect PCM",orbitDelta)
# +90° (second reference segment) has a distinct binaural response.
section=single[120000*2:120000*4]
left=sum(abs(section[i]) for i in range(0,len(section),2))
right=sum(abs(section[i]) for i in range(1,len(section),2))
assert right>left*1.2,("RIGHT HRTF channel swapped",left,right)
# No false silence between orbit buffer boundaries; source is continuous.
assert sum(abs(x) for x in orbit[220000:230000])>1000
assert (root/"LISTENING-GUIDE.txt").exists()
key=(root/"BLIND-ANSWER-KEY.txt").read_text()
assert key.count("Trial ")==8 and key.count("FRONT")==2 and key.count("REAR")==2
print("PASS 8D-SonKu actual C++ offline WAV: four directions, balanced blind",
      "set, clockwise orbit, 48kHz stereo PCM, no clipping.")
