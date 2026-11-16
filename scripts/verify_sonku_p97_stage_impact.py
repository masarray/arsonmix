#!/usr/bin/env python3
"""P9.7 must not overwrite selected P9.6 checkpoint source sound."""
from pathlib import Path
import sys

def parse(path):
    rows={}
    for line in Path(path).read_text().splitlines():
        if not line.startswith("P97_FULL,"): continue
        p=line.split(",")
        assert len(p)==10, line
        k=(int(p[1]),int(p[2]),int(p[3]),float(p[4]),float(p[5]))
        assert k not in rows
        rows[k]=(tuple(float(v) for v in p[6:9]),p[9])
    assert len(rows)==20,(path,len(rows))
    return rows

def main():
    assert len(sys.argv)==3
    base,new=parse(sys.argv[1]),parse(sys.argv[2])
    assert base.keys()==new.keys()
    for k,(old,old_hash) in base.items():
        current,current_hash=new[k]
        rate,az,room,distance,mix=k
        if room==0 or distance<=2. or mix==0.:
            assert old_hash==current_hash, f"P96_CHECKPOINT_BIT_EXACT_FAILED {k}"
        else:
            ratios=[b/a for a,b in zip(old,current)]
            print("P97_STAGE_REAL_PCM",k,"bass/vocal/air",*[round(v,3) for v in ratios])
            if distance>=10.:
                assert ratios[0]>1.06, (k,ratios)
                assert ratios[1]>1.04, (k,ratios)
                # P9.7 MUST NOT sacrifice the air P9.6 already restored.
                assert ratios[2]>.98, (k,ratios)
                assert ratios[0]<1.7 and ratios[1]<1.7 and ratios[2]<1.20, (k,ratios)
            else:
                # Signature at 3.1m must remain subtle, not overwritten.
                assert .92<ratios[0]<1.4 and .92<ratios[1]<1.4, (k,ratios)
    print("SONKU_P97_CHECKPOINT_BIT_EXACT_AND_REAL_PCM_PASSED")

if __name__=="__main__":
    main()
