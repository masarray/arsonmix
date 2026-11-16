#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Deterministic ImerSip concert PNG assets. No runtime external assets.
Original independent RGBA PA cabinetry, not a rasterised SVG/atlas.
Build prerequisite: pillow, numpy. Assets are generated at CMake configure time.
"""
import argparse, hashlib, json, random
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
ap=argparse.ArgumentParser()
ap.add_argument('--out',required=True,type=Path)
args=ap.parse_args()
R=args.out.resolve();A=R/'assets';A.mkdir(parents=True,exist_ok=True)
S=4
def rgba(size):return Image.new('RGBA',size,(0,0,0,0))
def rect(d,xy,fill,radius=0,outline=None,width=1):
    if radius:d.rounded_rectangle(tuple(map(int,xy)),radius=radius,fill=fill,outline=outline,width=width)
    else:d.rectangle(tuple(map(int,xy)),fill=fill,outline=outline,width=width)
def layer_shadow(img,blur=13,offset=(0,9),opacity=.55):
    a=img.getchannel('A').point(lambda t:int(t*opacity))
    sh=rgba(img.size);ink=Image.new('RGBA',img.size,(0,0,0,255));ink.putalpha(a)
    sh.alpha_composite(ink,offset);sh=sh.filter(ImageFilter.GaussianBlur(blur))
    sh.alpha_composite(img)
    return sh
def carbon(d,box,seed):
    rr=random.Random(seed);x0,y0,x1,y1=map(int,box)
    d.rounded_rectangle((x0,y0,x1,y1),radius=5*S,fill=(6,13,17,255),outline=(50,63,67,255),width=max(1,S//2))
    for yy in range(y0+4*S,y1-4*S,4*S):
        for xx in range(x0+4*S+(yy//(4*S)%2)*2*S,x1-4*S,4*S):
            c=rr.randint(14,34)
            d.ellipse((xx,yy,xx+int(1.6*S),yy+int(1.6*S)),fill=(c,c+7,c+10,255))
    for yy in range(y0+S,y1,8*S):
        d.line((x0+2*S,yy,x1-2*S,yy),fill=(30,45,50,60),width=1)
def chassis(size,kind,side):
    W,H=size;im=rgba(size);d=ImageDraw.Draw(im)
    pad=int(W*.10);top=int(H*.12);bot=int(H*.88)
    x0,y0,x1,y1=pad,top,W-pad,bot
    skew=int(W*.085)*(1 if side=='left' else -1)
    d.polygon([(x0,y0),(x1,y0+int(H*.025)),(x1-skew,y1-int(H*.035)),(x0-skew,y1-int(H*.02))],fill=(37,45,51,255))
    d.polygon([(x1,y0+int(H*.025)),(x1+skew//2,y0+int(H*.055)),(x1+skew//2,y1-int(H*.06)),(x1-skew,y1-int(H*.035))],fill=(13,20,26,255))
    x0+=max(-skew,0);x1-=max(skew,0)
    d.polygon([(x0,y0),(x1,y0+int(H*.024)),(x1,y1),(x0,y1-int(H*.022))],fill=(17,23,29,255),outline=(97,108,111,255),width=max(1,S//2))
    carbon(d,(x0+6*S,y0+7*S,x1-6*S,y1-6*S),1815 if kind=='subwoofer' else 1756)
    cx=(x0+x1)//2
    if kind=='subwoofer':
        cy=(y0+y1)//2;rw=(x1-x0)*.35
        for mult,col in [(1.23,(33,48,54,180)),(1.04,(2,8,12,155)),(.79,(25,37,43,160)),(.52,(1,5,8,170))]:
            r=rw*mult;d.ellipse((cx-r,cy-r,cx+r,cy+r),fill=col)
        for vx in [x0+8*S,x1-8*S]:
            for vy in [y0+10*S,y1-10*S]:
                d.ellipse((vx-3,vy-3,vx+3,vy+3),fill=(130,148,153,255))
        for xx in [x0+12*S,x1-18*S]:
            d.rounded_rectangle((xx,y1-13*S,xx+6*S,y1-8*S),radius=2*S,fill=(34,174,178,255))
    else:
        for cy,rw in [(int(y0+(y1-y0)*.34),(x1-x0)*.18),(int(y0+(y1-y0)*.69),(x1-x0)*.29)]:
            for k,col in [(1.22,(23,42,48,220)),(1.,(3,7,10,230)),(.74,(30,43,46,220)),(.40,(1,5,7,240))]:
                r=rw*k;d.ellipse((cx-r,cy-r,cx+r,cy+r),fill=col)
            d.arc((cx-rw,cy-rw,cx+rw,cy+rw),190,315,fill=(86,119,124,160),width=2*S)
        d.line((x0+6*S,y0+3*S,x1-6*S,y0+4*S),fill=(101,143,148,200),width=2*S)
    d.line((x0+2*S,y0+2*S,x0+2*S,y1-2*S),fill=(112,128,129,165),width=2*S)
    d.line((x1-2*S,y0+2*S,x1-2*S,y1-2*S),fill=(4,10,12,255),width=3*S)
    return layer_shadow(im,blur=3*S,offset=(0,3*S),opacity=.6)
def line_array(side):
    im=rgba((392,640));d=ImageDraw.Draw(im)
    cx=196;top=33
    d.line((cx-105,top+5,cx-97,top+72),fill=(116,136,144,240),width=5)
    d.line((cx+96,top+5,cx+96,top+72),fill=(98,115,126,240),width=5)
    d.ellipse((cx-105,top,cx-93,top+12),fill=(180,198,202,255))
    d.ellipse((cx+92,top,cx+104,top+12),fill=(180,198,202,255))
    for n in range(8):
        y=66+n*62
        x=int(57+(n/7)*18) if side=='left' else int(57+(1-n/7)*18)
        w=269-int(n*.7);cab=rgba((392,640));q=ImageDraw.Draw(cab)
        sh=(n-3)*2 if side=='left' else -(n-3)*2
        q.polygon([(x+13,y),(x+w-11,y+1),(x+w-2+sh,y+54),(x+sh,y+57)],fill=(23,31,36,255),outline=(89,104,107,255),width=3)
        carbon(q,(x+17,y+6,x+w-19,y+50),130+n)
        q.line((x+18,y+20,x+w-20,y+20),fill=(73,105,109,195),width=2)
        q.line((x+19,y+42,x+w-19,y+42),fill=(53,71,76,205),width=2)
        for xx in [x+9,x+w-11]:
            q.ellipse((xx-4,y+11,xx+4,y+19),fill=(142,157,160,255))
            q.ellipse((xx-4,y+37,xx+4,y+45),fill=(85,104,110,255))
        im.alpha_composite(cab)
    return layer_shadow(im,blur=12,offset=(0,10),opacity=.65)
def save_sprite(image,file):
    inside=image.resize((round(image.width*.88),round(image.height*.88)),Image.Resampling.LANCZOS)
    padded=rgba(image.size)
    padded.alpha_composite(inside,((image.width-inside.width)//2,(image.height-inside.height)//2))
    padded.save(A/file,optimize=True)
for kind,size in [('line_array',(392,640)),('subwoofer',(448,368)),('front_fill',(240,350)),('side_fill',(216,280))]:
    for side in ('left','right'):
        sprite=line_array(side) if kind=='line_array' else chassis(size,kind,side)
        save_sprite(sprite,f'{kind}_{side}.png')
def room():
    W,H=2200,922
    y=np.arange(H,dtype=np.float32)[:,None];x=np.arange(W,dtype=np.float32)[None,:]
    noise=np.random.default_rng(1666).normal(0,1.5,(H,W)).astype('float32')
    radial=np.clip(1-np.sqrt(((x-1100)/1400)**2+((y-455)/780)**2),0,1)
    glow=np.exp(-(((x-1100)/900)**2+((y-530)/360)**2))*10
    out=np.empty((H,W,4),dtype=np.uint8)
    for i,(base,mult) in enumerate([(8,7),(17,10),(23,12)]):
        out[:,:,i]=np.uint8(np.clip(base+radial*mult+glow*(.5 if i<2 else 1)+noise,0,255))
    out[:,:,3]=255
    im=Image.fromarray(out,'RGBA');d=ImageDraw.Draw(im,'RGBA')
    d.polygon([(45,184),(2155,184),(1880,285),(320,285)],fill=(4,13,17,180))
    for yy in [115,173,225]:
        d.line((0,yy,2200,yy),fill=(54,78,83,130),width=2)
        for xx in range(70,2210,104):
            d.line((xx,yy-12,xx+28,yy+12),fill=(58,93,99,95),width=2)
    for xx in range(60,2200,138):
        d.line((xx,164,xx,690),fill=(41,98,104,44),width=2)
    for yy in [230,310,385,465,548]:
        d.line((0,yy,2200,yy),fill=(48,112,117,55),width=2)
    d.polygon([(0,480),(2200,480),(2200,920),(0,920)],fill=(5,14,18,104))
    for bx in range(-3500,5700,285):
        d.line((1100,410,bx,H),fill=(44,149,154,75),width=2)
    for yy in [490,520,563,615,685,784,910]:
        d.line((0,yy,2200,yy),fill=(34,155,161,80 if yy<685 else 112),width=2)
    d.polygon([(300,530),(1900,530),(2160,800),(60,800)],fill=(11,24,27,95),outline=(56,145,146,95),width=2)
    for xx,yy in [(480,295),(1100,260),(1720,295)]:
        for rr,opa in [(115,3),(55,7),(10,35)]:
            d.ellipse((xx-rr,yy-rr*.6,xx+rr,yy+rr*.6),fill=(36,216,211,opa))
    d.line((280,541,1920,541),fill=(69,199,191,140),width=2)
    d.line((280,548,1920,548),fill=(30,74,77,95),width=1)
    im.save(A/'room_background.png',optimize=True)
room()
def panel():
    W,H=2200,438;Y=np.arange(H)[:,None]
    noise=np.random.default_rng(81).normal(0,1.5,(H,W))
    brushed=np.sin(Y*1.52)*1.6
    img=np.empty((H,W,4),np.uint8)
    for i,base in enumerate((25,34,40)):
        img[:,:,i]=np.uint8(np.clip(base+brushed+noise+(Y/H)*(-5),0,255))
    img[:,:,3]=255
    im=Image.fromarray(img,'RGBA');d=ImageDraw.Draw(im,'RGBA')
    d.rectangle((0,0,W-1,4),fill=(75,133,139,255))
    d.rectangle((0,5,W,14),fill=(13,25,31,155))
    for left,right in [(18,494),(510,1755),(1767,2182)]:
        rect(d,(left,30,right,425),(17,25,30,135),radius=18,outline=(92,117,120,135),width=2)
        d.line((left+25,37,right-25,37),fill=(111,158,155,100),width=2)
    for xx in (502,1762):
        d.line((xx,40,xx,426),fill=(6,10,14,255),width=5)
        d.line((xx+4,40,xx+4,426),fill=(104,133,139,90),width=2)
    im.save(A/'control_panel.png',optimize=True)
panel()
im=rgba((2200,146));d=ImageDraw.Draw(im,'RGBA')
for yy in range(146):
    a=int(230*(1-yy/146)**1.4)
    d.line((0,yy,2199,yy),fill=(6,14,19,a),width=1)
d.line((46,141,2154,141),fill=(65,138,143,75),width=2)
im.save(A/'header_overlay.png',optimize=True)
im=rgba((1100,116));d=ImageDraw.Draw(im)
rect(d,(10,44,1090,73),(2,8,12,245),radius=25,outline=(102,132,135,210),width=4)
rect(d,(18,51,1082,64),(14,40,43,250),radius=8)
d.line((28,46,1068,46),fill=(132,167,160,130),width=2)
im.save(A/'slider_track.png',optimize=True)
im=rgba((156,156));d=ImageDraw.Draw(im)
d.ellipse((23,22,139,138),fill=(2,7,12,180))
d.ellipse((18,12,138,132),fill=(20,38,43,255),outline=(126,160,165,255),width=5)
for r,c in [(48,(28,65,72,255)),(33,(36,118,120,255)),(20,(71,219,209,255))]:
    d.ellipse((78-r,72-r,78+r,72+r),fill=c)
d.ellipse((72,64,82,74),fill=(226,251,249,255))
im.save(A/'slider_thumb.png',optimize=True)
def knob(purple):
    N=480;im=rgba((N,N));d=ImageDraw.Draw(im);c=N//2
    d.ellipse((49,57,439,447),fill=(1,5,8,90))
    for r,col in [(187,(8,17,22,255)),(175,(122,143,150,255)),(167,(32,50,59,255)),(153,(8,16,23,255)),(139,(38,52,63,255)),(126,(23,34,42,255))]:
        d.ellipse((c-r,c-r-7,c+r,c+r-7),fill=col)
    hue=(183,107,220,255) if purple else (46,205,192,255)
    d.arc((75,68,405,399),200,342,fill=hue,width=8)
    d.arc((89,81,389,384),195,340,fill=(118,145,153,185),width=3)
    for r in range(118,18,-3):
        k=(r-18)/100
        d.ellipse((c-r,c-r-7,c+r,c+r-7),fill=(int(23+24*(1-k)),int(35+24*(1-k)),int(45+26*(1-k)),255))
    d.arc((125,100,355,326),190,320,fill=(134,172,177,100),width=3)
    return im
knob(False).save(A/'knob_teal.png',optimize=True)
knob(True).save(A/'knob_master_purple.png',optimize=True)
def ctrl(name,colour,border=(96,125,129,220),size=(640,112),radius=15):
    im=rgba(size);d=ImageDraw.Draw(im)
    rect(d,(7,7,size[0]-8,size[1]-10),colour,radius,outline=border,width=3)
    d.line((23,13,size[0]-25,13),fill=(150,188,187,70),width=2)
    im.save(A/name,optimize=True)
ctrl('dropdown_background.png',(13,28,33,245),size=(852,124),radius=14)
ctrl('button_normal.png',(31,48,56,250),size=(420,120),radius=16)
ctrl('button_hover.png',(38,76,81,255),border=(93,221,213,255),size=(420,120),radius=16)
ctrl('button_pressed.png',(19,94,95,255),border=(72,241,215,255),size=(420,120),radius=16)
DIM={
'room_background.png':(0,0,1100,461),'control_panel.png':(0,461,1100,219),
'header_overlay.png':(0,0,1100,73),
'line_array_left.png':(0,0,98,160),'line_array_right.png':(0,0,98,160),
'subwoofer_left.png':(0,0,112,92),'subwoofer_right.png':(0,0,112,92),
'front_fill_left.png':(0,0,60,87.5),'front_fill_right.png':(0,0,60,87.5),
'side_fill_left.png':(0,0,54,70),'side_fill_right.png':(0,0,54,70),
'slider_track.png':(0,0,135,14),'slider_thumb.png':(0,0,20,20),
'knob_teal.png':(0,0,90,90),'knob_master_purple.png':(0,0,90,90),
'dropdown_background.png':(0,0,213,31),'button_normal.png':(0,0,105,30),
'button_hover.png':(0,0,105,30),'button_pressed.png':(0,0,105,30)}
assets=[]
for file,b in DIM.items():
    with Image.open(A/file) as im:
        bb=im.getchannel('A').getbbox()
        pad=[bb[0],bb[1],im.width-bb[2],im.height-bb[3]] if bb else None
        assets.append({'file':file,'source_px':[im.width,im.height],
         'logical_default_rect':list(b),'anchor':[.5,1.] if 'subwoofer_' in file else [.5,.5] if ('fill_' in file or 'array_' in file) else [0,0],
         'alpha_bbox':list(bb) if bb else None,'alpha_padding_px':pad,
         'mirror':False,'perspective':'concert front three-quarter' if ('array_' in file or 'fill_' in file or 'subwoofer_' in file) else 'master scene',
         'safe_scale':[.65,1.] if ('array_' in file or 'fill_' in file or 'subwoofer_' in file) else [1,1],
         'z':0 if file=='room_background.png' else 10 if file=='control_panel.png' else 5,
         'sha256':hashlib.sha256((A/file).read_bytes()).hexdigest()})
manifest={'schema':'imersip-concert-asset-manifest-v1',
 'coordinates':{'editor':[1100,680],'source_master':[2200,1360],'room':[0,0,1100,461],'desk':[0,461,1100,219]},
 'stage_range':[30,180],'distance_range_m':[1.6,7.4],
 'asset_count':len(assets),'assets':assets,
 'notes':'Separate left/right PNG; no mirroring, no speaker in room background.'}
(R/'asset_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print('ImerSip: generated',len(assets),'independent RGBA PNGs into',A)
