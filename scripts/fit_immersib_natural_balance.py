# SPDX-License-Identifier: AGPL-3.0-only
"""Fit bounded broad common EQ to actual Fixed PA PCM. No audio-dependent EQ."""
import csv, cmath, json, math, statistics, sys
from pathlib import Path
CENTERS=(194,560,1250,2800,6300)
QS=(.55,.65,.65,.75,.70)
BOUNDS=((-2.5,2.5),(-2.5,2.5),(-2.5,2.5),(-2.5,1.),(-2.5,1.))
ROOT=Path("engine/builtin_fx/sonku")
HEADER=ROOT/"SonKuNaturalBalanceTable.generated.h"
def load(p):
    groups={}
    for r in csv.DictReader(open(p,encoding="utf-8")):
        key=tuple(int(r[k]) for k in ("rate","scene","mode"))
        v=tuple(float(r[k]) for k in ("db","lre","lim","rre","rim"))
        assert all(math.isfinite(x) for x in v)
        groups.setdefault(key,{})[int(r["hz"])]=v
    assert len(groups)==54 and all(len(v)==15 for v in groups.values())
    return groups
def dbresponse(rate,f,gains):
    z=cmath.exp(-2j*math.pi*f/rate)
    h=1+0j
    for fc,q,db in zip(CENTERS,QS,gains):
        w=2*math.pi*fc/rate
        a=math.sin(w)/(2*q); A=10**(db/40); c=-2*math.cos(w)
        h*=((1+a*A)+c*z+(1-a*A)*z*z)/((1+a/A)+c*z+(1-a/A)*z*z)
    return 20*math.log10(abs(h))
def profile(groups):
    # Fit shared broad shape, not individual narrow room notches.
    # Remove overall gain using 400/560/800Hz anchor. Each source topology
    # and scene has equal weight; distance extremes are held out.
    shapes={}
    for (rate,scene,mode),v in groups.items():
        anchor=statistics.mean(v[i][0] for i in (400,560,800))
        for f,row in v.items():
            if scene<20:shapes.setdefault((rate,f),[]).append(row[0]-anchor)
    return {k:statistics.median(v) for k,v in shapes.items()}
def objective(shape,g):
    cost=0.
    for (rate,f),v in shape.items():
        # Partial restoration only. Bass and upper pinna cues are protected.
        w=.35 if f<140 or f>=8000 else 1.
        desired=-.55*max(-6.,min(6.,v))
        off=statistics.mean(dbresponse(rate,n,g) for n in (400,560,800))
        err=dbresponse(rate,f,g)-off-desired
        cost+=w*err*err
    return cost+.8*sum(x*x for x in g)
def allowed(g):
    # Bound combined response, not merely individual bell gains.
    return all(-3.0<=dbresponse(rate,30*2**(i/12),g)<= (1.5 if 30*2**(i/12)>=2300 else 2.5)
               for rate in (44100,48000) for i in range(111))
def fit(path):
    base=load(path); target=profile(base); g=[0.]*5
    for step in (.5,.25,.125):
        for _ in range(8):
            changed=False
            for i in range(5):
                best=objective(target,g); selected=g
                for sign in (-1,1):
                    trial=g.copy();trial[i]+=sign*step
                    if not BOUNDS[i][0]<=trial[i]<=BOUNDS[i][1] or not allowed(trial):continue
                    score=objective(target,trial)
                    if score<best-1e-8:best,selected=score,trial
                if selected is not g:g=selected;changed=True
            if not changed:break
    assert objective(target,g)<objective(target,[0]*5)
    HEADER.write_text("#pragma once\n#include <array>\nnamespace armix::sonku::p101 {\ninline constexpr std::array<double,5> calibratedGainDb{"+",".join(str(x) for x in g)+"};\n}\n",encoding="utf-8")
    out={"baseline":"6399c0f52dc7b9f72cc5b53462f107578343e0fb","centers_hz":CENTERS,"q":QS,"gain_db":g,"target":"partial broad digital input shape; not ear or headphone calibration","baseline_shape_db":{str(k):round(v,4) for k,v in target.items()}}
    Path("natural_balance_calibration.json").write_text(json.dumps(out,indent=2)+"\n")
    print("P101_FIT",json.dumps(out))
def verify(a,b):
    base,after=load(a),load(b)
    p,q=profile(base),profile(after)
    rms=lambda t: math.sqrt(statistics.mean(v*v for (r,f),v in t.items() if 140<=f<=8000))
    before,now=rms(p),rms(q)
    print("P101_MEDIAN_SHAPE_RMS_DB",before,now)
    g=json.loads(Path("natural_balance_calibration.json").read_text())["gain_db"]
    deviations=[]; phase=[]; ild=[]
    for key,old in base.items():
        for f,(db,lr,li,rr,ri) in old.items():
            x=after[key][f]; expected=dbresponse(key[0],f,g)
            # Same measured low-level input, no limiter: compare full PCM transfer.
            deviations.append(abs(x[0]-db-expected))
            l,r=complex(lr,li),complex(rr,ri)
            ll,rrr=complex(x[1],x[2]),complex(x[3],x[4])
            if min(abs(l),abs(r),abs(ll),abs(rrr))>2.e-4:
                phase.append(abs(cmath.phase((ll/rrr)/(l/r))))
                ild.append(abs(20*math.log10(abs((ll/rrr)/(l/r)))))
    print("P101_TRANSFER_MAX_DB",max(deviations),"IPD_RAD",max(phase),"ILD_DB",max(ild))
    assert now<before and max(deviations)<.4 and max(phase)<.12 and max(ild)<.6
    print("P101_MEASURED_BALANCE_PASS")
if __name__=="__main__":
    if sys.argv[1]=="fit":fit(sys.argv[2])
    else:verify(sys.argv[2],sys.argv[3])
