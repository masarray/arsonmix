#!/usr/bin/env python3
"""Research only: copy real headers into a temporary tree, never retune product.
Equal-RMS listening clips are preliminary, not LUFS or listening proof.
"""
import array
import hashlib
import json
import math
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile
import wave

ROOT=Path(__file__).resolve().parents[1]
FX=ROOT/"engine/builtin_fx"
FLAGS = [
    "SONKU_P7_MEASURED_ELEVATION_RESEARCH",
    "SONKU_P71_DIRECT_XYZ_RESEARCH",
    "SONKU_P72_NATIVE_XYZ_RT",
    "SONKU_P75_MUSIC_SAFE_RESEARCH",
    "SONKU_P80_TRANSPARENT_BASS_RESEARCH",
    "SONKU_P82_MULTIBAND_ROOM_RESEARCH",
    "SONKU_P83_PHYSICAL_SPEAKER_RESEARCH",
    "SONKU_P84_SINGLE_CORE_FUSION_RESEARCH",
    "SONKU_P85_WORLD_COHERENCE_RESEARCH",
    "SONKU_P86_HORIZONTAL_FOCUS_RESEARCH",
    "SONKU_P87_MUSIC_BODY_RESEARCH",
    "SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH",
    "SONKU_P89_DEPTH_COHERENCE_RESEARCH",
    "SONKU_P90_TRUE_EXTERNAL_RESEARCH",
    "SONKU_P94_FRONT_BACK_DEPTH_RESEARCH",
    "SONKU_P95_MUSICAL_PRESENCE_RESEARCH",
    "SONKU_P96_DISTANCE_REINFORCEMENT_RESEARCH",
    "SONKU_P97_STAGE_IMPACT_RESEARCH",
    "SONKU_P98_SCENE_VOICING_RESEARCH",
    "SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH",
    "ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING",
    "SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH",
    "ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH",
    "ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH",
]

def replace_once(text, old, new):
    if text.count(old)!=1:
        raise ValueError("candidate source anchor changed: "+old[:90])
    return text.replace(old,new)

def candidate_copy(root):
    shutil.copytree(FX/"sonku",root/"sonku")
    shutil.copy2(FX/"ImerSipP92Scene.h",root)
    p=root/"sonku/SonKuP90StereoDecoder.h"
    t=p.read_text()
    t=replace_once(t,"    void compose(","    float compose(")
    t=replace_once(t,"    }\nprivate:\n    float evidenceAlpha_","        return c;\n    }\nprivate:\n    float evidenceAlpha_")
    p.write_text(t)
    p=root/"sonku/SonKuP94FrontBackDepthCue.h"
    t=p.read_text()
    t=replace_once(t,"target_=0.f;weight_=0.f;enabled_=false;",
        "target_=0.f;weight_=0.f;enabled_=false;research_=false;"
        "coherentTarget_=coherentWeight_=transition_=0.f;")
    t=replace_once(t,"        enabled_=roomWithEarly;","        research_=false;\n        enabled_=roomWithEarly;")
    t=replace_once(t,"    Gains next() noexcept {",'''    // Experimental blend of phantom center and real emitter geometry.
    // c is the EXACT blend already used by P90 compose, not a second update.
    void retargetCoherentStereo(float center,float left,float right,float c,
                                bool roomWithEarly) noexcept {
        retarget(center,roomWithEarly);
        const float centerWeight=target_;
        retargetStereo(left,right,roomWithEarly);
        coherentTarget_=c*centerWeight+(1.f-c)*target_;
        target_=centerWeight; // original P94 baseline evolves independently
        research_=roomWithEarly;
    }
    Gains next() noexcept {''')
    t=replace_once(t,"        if(!enabled_)return {};",
        "        if(!enabled_){coherentWeight_=transition_=0.f;return {};}")
    t=replace_once(t,"        return {1.f-.045f*w,1.f+.28f*w};",'''        const Gains baseline{1.f-.045f*w,1.f+.28f*w};
        // Preserve the original base weight even during Signature.
        // A separate, 45ms envelope fades only the additional cue in/out.
        if(!research_ && transition_==0.f)return baseline;
        const float targetBlend=research_?1.f:0.f;
        // A one-pole float can reach a representational fixed point before
        // the epsilon test. Snap only the candidate state when that happens;
        // the original approved baseline coefficient path is untouched.
        const auto smooth=[this](float& value,float target) noexcept {
            const float before=value;
            value+=slew_*(target-value);
            if(value==before||std::abs(value-target)<1.e-6f)value=target;
        };
        if(research_)smooth(coherentWeight_,coherentTarget_);
        smooth(transition_,targetBlend);
        if(transition_==0.f){coherentWeight_=0.f;return baseline;}
        const float cw=std::clamp(coherentWeight_,0.f,1.f);
        const Gains candidate{1.f-.065f*cw,1.f+.42f*cw};
        if(transition_==1.f)return candidate;
        return {baseline.direct+transition_*(candidate.direct-baseline.direct),
                baseline.early+transition_*(candidate.early-baseline.early)};''')
    t=replace_once(t,"    bool enabled_=false;",
        "    bool enabled_=false,research_=false;\n"
        "    float coherentTarget_=0.f,coherentWeight_=0.f,transition_=0.f;")
    p.write_text(t)
    p=root/"sonku/SonKuSpatialCore.h"
    t=p.read_text()
    t=replace_once(t,"p90Decoder_.compose(centerTarget_,targets_[0],targets_[1]);",
        "const float coherence=p90Decoder_.compose(centerTarget_,targets_[0],targets_[1]);")
    t=replace_once(t,"""                p94Depth_.retarget(p83::toHead(static_cast<float>(degrees_),p83HeadYaw_),
                                  settings_.spacePreset!=0 && settings_.roomAmount>0.f);""", """                if(p99ForSonKuInsert_ && settings_.sceneVoicing==1 && settings_.spatialMix>0.f) {
                    p94Depth_.retargetCoherentStereo(
                        p83::toHead(static_cast<float>(degrees_),p83HeadYaw_),
                        positions.leftHead,positions.rightHead,coherence,
                        settings_.spacePreset!=0 && settings_.roomAmount>0.f);
                }else
                    p94Depth_.retarget(p83::toHead(static_cast<float>(degrees_),p83HeadYaw_),
                                      settings_.spacePreset!=0 && settings_.roomAmount>0.f);""")
    p.write_text(t)

def read_pcm(path):
    b=path.read_bytes()
    a=array.array("f");a.frombytes(b)
    if sys.byteorder!="little":raise RuntimeError("little-endian offline qualification only")
    if not a or len(a)%2 or not all(math.isfinite(v) for v in a):
        raise ValueError("invalid PCM "+str(path))
    return b,a

def metrics(a):
    n=len(a)//2
    el=sum(float(x)*x for x in a[::2])
    er=sum(float(x)*x for x in a[1::2])
    cross=sum(float(x)*y for x,y in zip(a[::2],a[1::2]))
    return {"rms":math.sqrt((el+er)/(2*n)), "peak":max(map(abs,a)),
            "correlation":cross/math.sqrt(max(el*er,1e-30)),
            "ildDb":10*math.log10(max(el,1e-30)/max(er,1e-30))}

def wav(path,a,rate,gain):
    pcm=array.array("h",(max(-32767,min(32767,round(x*gain*32767))) for x in a))
    with wave.open(str(path),"wb") as w:
        w.setnchannels(2);w.setsampwidth(2);w.setframerate(rate);w.writeframes(pcm.tobytes())
    # Check actual quantized output; no clipping or silently unmatched files.
    return math.sqrt(sum(float(x)*x for x in pcm)/len(pcm))/32767

def main():
    out=Path(sys.argv[1] if len(sys.argv)>1 else "sonku-p104-listening")
    out.mkdir(parents=True,exist_ok=True)
    rng=random.Random(1042026);rows=[];key=[]
    with tempfile.TemporaryDirectory(prefix="sonku-depth-") as tmp:
        tmp=Path(tmp);candidate_copy(tmp/"candidate")
        # Focused coefficient test on the exact isolated candidate used by
        # the full native renderer, before generating listening artifacts.
        transition=tmp/"transition-test"
        subprocess.run(["g++","-std=c++20","-O2","-ffp-contract=off",
            "-I",str(tmp/"candidate"/"sonku"),
            str(ROOT/"tests/sonku_p106_depth_transition.cpp"),"-o",str(transition)],check=True)
        subprocess.run([str(transition)],check=True)
        for name,src in (("reference",FX),("candidate",tmp/"candidate")):
            exe=tmp/(name+"-render")
            subprocess.run(["g++","-std=c++20","-O2","-ffp-contract=off",
                "-I",str(src/"sonku"),"-I",str(src),*["-D"+f+"=1" for f in FLAGS],
                str(ROOT/"tests/sonku_p104_externalization.cpp"),"-o",str(exe)],check=True)
            subprocess.run([str(exe),str(tmp/name/"pcm")],check=True)
        for rate in (44100,48000):
            for case in range(13):
                name=f"{rate}-{case}.f32"
                old,a=read_pcm(tmp/"reference"/"pcm"/name)
                new,b=read_pcm(tmp/"candidate"/"pcm"/name)
                assert len(a)==len(b)==2*rate*(12 if case==8 else 2)
                ma,mb=metrics(a),metrics(b)
                assert max(ma["peak"],mb["peak"])<=.981,(rate,case,ma,mb)
                if case>=9:
                    assert old==new,("protected PCM changed",rate,case)
                delta=math.sqrt(sum((float(x)-y)**2 for x,y in zip(a,b))/len(a))
                row={"rate":rate,"case":case,"baseline":ma,"candidate":mb,
                    "deltaRms":delta,"identical":old==new,
                    "referenceSha256":hashlib.sha256(old).hexdigest(),
                    "candidateSha256":hashlib.sha256(new).hexdigest()}
                if case<9:
                    assert ma["rms"]>1e-5 and mb["rms"]>1e-5
                    target=min(.075,ma["rms"],mb["rms"],
                        .9*ma["rms"]/max(ma["peak"],1e-9),
                        .9*mb["rms"]/max(mb["peak"],1e-9))
                    order=[("reference",a,ma),("candidate",b,mb)];rng.shuffle(order)
                    pair=f"{rate}-pair-{case:02}"
                    levels=[]
                    for label,(variant,pcm,m) in zip("AB",order):
                        levels.append(wav(out/(pair+"-"+label+".wav"),pcm,rate,target/m["rms"]))
                        key.append({"file":pair+"-"+label+".wav","variant":variant,"case":case})
                    row["matchedRmsErrorDb"]=abs(20*math.log10(levels[0]/levels[1]))
                    assert row["matchedRmsErrorDb"]<.02,row
                rows.append(row)
    assert any(not r["identical"] for r in rows if r["case"]<9),"candidate has no measured effect"
    (out/"metrics.json").write_text(json.dumps(rows,indent=2)+"\n")
    (out/"blind-answer-key.json").write_text(json.dumps(key,indent=2)+"\n")
    (out/"README.txt").write_text(
        "Research only. A/B order randomized per pair; do not open key before scoring.\n"
        "Cases 0-3 coherent mono at 0/90/180/270; 4-7 stereo same angles; 8 full 12s orbit.\n"
        "Score outside-head distance, front/back, vocal body, clarity, transient smear and preference.\n"
        "Whole-excerpt RMS matched, NOT integrated LUFS or personalized headphone calibration.\n"
        "Synthetic signal, not music listening approval. Use same headphone and fixed volume.\n")
    print("P104_RESEARCH_PASS",len(rows),"cases;",sum(r["identical"] for r in rows if r["case"]>=9),
          "protected exact PCM cases; no perceptual claim")
if __name__=="__main__":main()
