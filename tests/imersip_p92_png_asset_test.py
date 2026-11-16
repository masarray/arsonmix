# SPDX-License-Identifier: AGPL-3.0-only
"""Fail-closed ImerSip 19-PNG alpha, manifest and concert-geometry gate."""
import hashlib
import json
import math
import pathlib
import subprocess
import sys
import tempfile
import unittest
from PIL import Image
ROOT=pathlib.Path(__file__).resolve().parents[1]
OUT=pathlib.Path(sys.argv[sys.argv.index('--out')+1]).resolve() if '--out' in sys.argv else None
if OUT is not None:
    idx=sys.argv.index('--out');del sys.argv[idx:idx+2]
    OUT.mkdir(parents=True,exist_ok=True)
else:
    OUT=pathlib.Path(tempfile.mkdtemp(prefix='imersip-rgba-'))
subprocess.run([sys.executable,str(ROOT/'scripts/generate_imersip_concert_png.py'),'--out',str(OUT)],check=True)
manifest=json.loads((OUT/'asset_manifest.json').read_text(encoding='utf-8'))
def boxes(stage,distance):
    stage=max(30,min(180,stage))
    far=max(0,min(1,(distance-1.6)/5.8))
    span=170+182*(stage-30)/150
    aw=98-28*far; ah=aw*260/160
    sw=112-32*far; sh=sw*150/180
    fw=60-6*far; fh=fw*160/112
    sidew=40-5*far; sideh=52-5*far
    def b(cx,y,w,h):return (cx-w*.5,y,w,h)
    return {
        'line_array_left':b(550-span,132+34*far,aw,ah),
        'line_array_right':b(550+span,132+34*far,aw,ah),
        'subwoofer_left':b(550-span,320+16*far,sw,sh),
        'subwoofer_right':b(550+span,320+16*far,sw,sh),
        'front_fill_left':b(466,279+10*far,fw,fh),
        'front_fill_right':b(634,279+10*far,fw,fh),
        'side_fill_left':b(550-span-100,270,sidew,sideh),
        'side_fill_right':b(550+span+100,270,sidew,sideh)}
class AssetGate(unittest.TestCase):
    def test_19_independent_rgba_and_manifest(self):
        self.assertEqual(manifest['schema'],'imersip-concert-asset-manifest-v1')
        self.assertEqual(manifest['coordinates']['editor'],[1100,680])
        self.assertEqual(manifest['asset_count'],19)
        self.assertEqual(len(set(a['file'] for a in manifest['assets'])),19)
        for a in manifest['assets']:
            with self.subTest(a=a['file']):
                p=OUT/'assets'/a['file']
                with Image.open(p) as im:
                    self.assertEqual(im.mode,'RGBA')
                    self.assertEqual(list(im.size),a['source_px'])
                    self.assertEqual(hashlib.sha256(p.read_bytes()).hexdigest(),a['sha256'])
                    if any(k in a['file'] for k in ('_array_','subwoofer_','_fill_')):
                        alpha=im.getchannel('A');bb=alpha.getbbox()
                        self.assertIsNotNone(bb)
                        self.assertGreaterEqual(min(bb[0],bb[1],im.width-bb[2],im.height-bb[3]),5)
                        self.assertLessEqual(max(im.width,im.height),2048)
    def test_all_stage_distance_extremes_and_no_major_collision(self):
        for stage in [30,31,70,120,150,179,180]:
            for dist in [1.6,2.2,4.5,7.4]:
                b=boxes(stage,dist)
                for key,(x,y,w,h) in b.items():
                    with self.subTest(stage=stage,dist=dist,key=key):
                        self.assertGreaterEqual(x,16)
                        self.assertGreaterEqual(y,100)
                        self.assertLessEqual(x+w,1084)
                        self.assertLessEqual(y+h,444)
                for a,c in [('subwoofer_left','front_fill_left'),
                            ('subwoofer_right','front_fill_right'),
                            ('line_array_left','front_fill_left'),
                            ('line_array_right','front_fill_right'),
                            ('side_fill_left','subwoofer_left'),
                            ('side_fill_right','subwoofer_right')]:
                    x,y,w,h=b[a];X,Y,W,H=b[c]
                    overlap=max(0,min(x+w,X+W)-max(x,X))*max(0,min(y+h,Y+H)-max(y,Y))
                    self.assertEqual(overlap,0,(stage,dist,a,c))
    def test_individual_speaker_scene_render(self):
        # PNG composition proof intentionally reuses the very same generated
        # assets as BinaryData; no fake HTML/screenshot replacement.
        for stage,dist,label in [(150,2.2,'live_concert'),(30,2.2,'stage_min'),
                                  (180,2.2,'stage_max'),(150,1.6,'distance_min'),
                                  (150,7.4,'distance_max'),(180,1.6,'all_speakers')]:
            scene=Image.new('RGBA',(2200,1360),(5,11,17,255))
            def place(name,box):
                x,y,w,h=box
                with Image.open(OUT/'assets'/(name+'.png')) as im:
                    resized=im.resize((round(w*2),round(h*2)),Image.Resampling.LANCZOS)
                scene.alpha_composite(resized,(round(x*2),round(y*2)))
            place('room_background',(0,0,1100,461))
            place('header_overlay',(0,0,1100,73))
            for key,box in boxes(stage,dist).items():place(key,box)
            place('control_panel',(0,461,1100,219))
            scene.convert('RGB').save(OUT/(label+'_composition.png'),optimize=True)
if __name__=='__main__':
    unittest.main()
