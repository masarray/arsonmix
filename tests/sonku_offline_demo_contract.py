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
orbit=read("sonku-orbit-cw-2x.wav",960000)
v2four=read("sonku-v2-room-four.wav",4*120000)
v2orbit=read("sonku-v2-room-orbit-cw-2x.wav",960000)
squareOrbit=read("sonku-v2-square-orbit-cw-2x.wav",960000)
reverse=read("sonku-orbit-ccw-2x.wav",960000)
reverseRoom=read("sonku-v2-room-orbit-ccw-2x.wav",960000)
reverseSquare=read("sonku-v2-square-orbit-ccw-2x.wav",960000)
glue=read("sonku-v3-square-glue-cw-2x.wav",960000)
airy=read("sonku-v3-square-airy-cw-2x.wav",960000)
musicBase=read("sonku-musical-square-base-2x.wav",960000)
musicGlue=read("sonku-musical-square-glue-2x.wav",960000)
musicAiry=read("sonku-musical-square-airy-2x.wav",960000)
# Baseline V1 and candidate V2 are deliberately non-identical on same noise.
# Room must affect later samples, not fake position on the direct sound.
difference=sum(abs(int(a)-int(b)) for a,b in
               zip(v2four[70000:200000],single[70000:200000]))
assert difference>1500,("V2 room candidate did not affect PCM",difference)
orbitDelta=sum(abs(int(a)-int(b)) for a,b in
               zip(v2orbit[70000:200000],orbit[70000:200000]))
assert orbitDelta>1500,("V2 moving room candidate did not affect PCM",orbitDelta)
shapeDelta=sum(abs(int(a)-int(b)) for a,b in
               zip(squareOrbit[70000:200000],v2orbit[70000:200000]))
assert shapeDelta>1500,("Square and rectangle rendered the same",shapeDelta)
# Reverse motion must change the binaural path (not merely rename a WAV).
reverseDelta=sum(abs(int(a)-int(b)) for a,b in
                 zip(reverse[350000:500000],orbit[350000:500000]))
assert reverseDelta>1500,("CW/CCW unexpectedly identical",reverseDelta)
for name,candidate in (("glue",glue),("airy",airy)):
    fdnDelta=sum(abs(int(a)-int(b)) for a,b in
                 zip(candidate[400000:550000],squareOrbit[400000:550000]))
    assert fdnDelta>1500,("FDN stage produced identical PCM",name,fdnDelta)
distinct=sum(abs(int(a)-int(b)) for a,b in zip(glue[400000:550000],airy[400000:550000]))
assert distinct>1500,("FDN Studio Glue and Airy Chamber matched exactly",distinct)
for name,variant in (("glue",musicGlue),("airy",musicAiry)):
    delta=sum(abs(int(a)-int(b)) for a,b in
              zip(variant[300000:540000],musicBase[300000:540000]))
    assert delta>1000,("Musical FDN did not alter original tones",name,delta)
musicDistinct=sum(abs(int(a)-int(b)) for a,b in
                  zip(musicGlue[300000:540000],musicAiry[300000:540000]))
assert musicDistinct>1000,("Musical presets sound identical",musicDistinct)
# +90° (second reference segment) has a distinct binaural response.
section=single[120000*2:120000*4]
left=sum(abs(section[i]) for i in range(0,len(section),2))
right=sum(abs(section[i]) for i in range(1,len(section),2))
assert right>left*1.2,("RIGHT HRTF channel swapped",left,right)
# No false silence between orbit buffer boundaries; source is continuous.
assert sum(abs(x) for x in orbit[220000:230000])>1000
# Verify there is no mute, edit or source reset at the 10s handoff.
# Stereo interleaved: 48000 samples = 1 second, 2 PCM ints per frame.
for audio in (orbit,v2orbit,squareOrbit,reverse,reverseRoom,reverseSquare,glue,airy):
    for second in (9,10,11,19):
        begin=(second*48000+4000)*2
        assert sum(abs(x) for x in audio[begin:begin+9600])>2000,(
            "silent loop seam",second)
assert "20s" in (root/"LISTENING-GUIDE.txt").read_text()
assert not (root/"sonku-orbit-cw.wav").exists(),(
    "stale 10s artifact must not appear alongside 2x master")
assert (root/"LISTENING-GUIDE.txt").exists()
key=(root/"BLIND-ANSWER-KEY.txt").read_text()
assert key.count("Trial ")==8 and key.count("FRONT")==2 and key.count("REAR")==2
print("PASS 8D-SonKu actual C++ offline WAV: four directions, balanced blind",
      "set, two continuous 360deg turns (20s), 48kHz stereo PCM, no clipping.")
