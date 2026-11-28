import numpy as np, sys
from PIL import Image
from psd_tools import PSDImage
from psd_tools.api.layers import PixelLayer, Group
from psd_tools.constants import Compression
out = sys.argv[1]
W,H=48,32
def blob(w,h,col,soft=True):
    y,x=np.mgrid[0:h,0:w]
    d=np.sqrt(((x-w/2)/(w/2))**2+((y-h/2)/(h/2))**2)
    a=np.clip((1-d)*2,0,1)*255
    arr=np.zeros((h,w,4),np.uint8); arr[...,0]=col[0];arr[...,1]=col[1];arr[...,2]=col[2];arr[...,3]=a.astype(np.uint8)
    return Image.fromarray(arr,'RGBA')
for name,comp in (('rle',Compression.RLE),('raw',Compression.RAW),('zip',Compression.ZIP)):
    psd=PSDImage.new('RGBA',(W,H))
    a=PixelLayer.frompil(blob(30,20,(255,40,40)),psd,'Red blob',top=2,left=3,compression=comp)
    b=PixelLayer.frompil(blob(24,24,(40,90,255)),psd,'Blue blob',top=6,left=20,compression=comp)
    c=PixelLayer.frompil(blob(20,20,(40,255,40)),psd,'Hidden green',top=10,left=10,compression=comp)
    c.visible=False
    b.opacity=128
    psd.append(a); psd.append(b); psd.append(c)
    try:
        g=Group.group_layers([psd[0]], name='Group') if False else None
    except Exception as e: print(e)
    psd.save(f'{out}/layers-{name}.psd')
    p=PSDImage.open(f'{out}/layers-{name}.psd')
    im=p.composite(force=True)
    im.convert('RGBA').save(f'{out}/layers-{name}.expect.png')
    print(name, p.has_preview() if hasattr(p,'has_preview') else '', [ (l.name,l.visible,l.opacity) for l in p])
    p.topil().convert('RGBA').save(f'{out}/layers-{name}.merged.png')
