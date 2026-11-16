#!/usr/bin/env python3
"""P9.8 proof: approved P9.7 Venue is PCM-bit-exact; only typed scenes voiced."""
from pathlib import Path
import sys

def load(path):
    rows={}
    for line in Path(path).read_text().splitlines():
        if not line.startswith("P98_SCENE,"): continue
        p=line.split(",")
        assert len(p)==10,line
        key=(int(p[1]),int(p[2]),int(p[3]),p[4])
        assert key not in rows
        rows[key]=(tuple(float(x) for x in p[5:9]),p[9])
    assert len(rows)==24,(path,len(rows))
    return rows

def main():
    assert len(sys.argv)==3
    old=load(sys.argv[1]);new=load(sys.argv[2])
    assert old.keys()==new.keys()
    for key,(base,oldHash) in old.items():
        cand,newHash=new[key]
        rate,az,index,scene=key
        if index in (0,4,5):
            assert oldHash==newHash, f"APPROVED_VENUE_P97_PCM_CHANGED {key}"
            print("P98_APPROVED_VENUE_BIT_EXACT",key)
        else:
            ratios=tuple(cand[i]/base[i] for i in range(3))
            print("P98_SCENE_RESPONSE",key,*[round(x,4) for x in ratios])
            # Intended subtle vs meaningful scene-specific LF signature,
            # while a lower instrument/voice band may never disappear.
            assert all(.80<r<2.0 for r in ratios),(key,ratios)
            if index==1:  # Signature flagship presence
                assert ratios[0]>1.04 and ratios[1]>1.03,(key,ratios)
            if index==2:  # Studio small adjustment
                assert ratios[0]>1.005,(key,ratios)
            if index==3:  # Cinema bass meaningful
                assert ratios[0]>1.045,(key,ratios)
    print("P98_APPROVED_VENUE_PCM_BIT_EXACT_AND_SCENE_GAINS_PASSED")

if __name__=="__main__":
    main()
