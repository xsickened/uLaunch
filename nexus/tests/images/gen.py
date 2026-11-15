# Erzeugt die Testbilder für decode_test.cpp (Pillow). Nur zum Nachvollziehen; die Bilder liegen im Repo.
from PIL import Image, ImageDraw
import io
def quad(w, h, mode='RGB'):
    # vier Farbflächen: oben links rot, oben rechts grün, unten links blau, unten rechts weiß
    im = Image.new(mode if mode != 'RGBA' else 'RGBA', (w, h))
    d = ImageDraw.Draw(im)
    c = [(255,0,0),(0,255,0),(0,0,255),(255,255,255)]
    if mode == 'RGBA': c = [x + (255,) for x in c]
    d.rectangle([0,0,w//2-1,h//2-1], fill=c[0]); d.rectangle([w//2,0,w-1,h//2-1], fill=c[1])
    d.rectangle([0,h//2,w//2-1,h-1], fill=c[2]); d.rectangle([w//2,h//2,w-1,h-1], fill=c[3])
    return im
quad(640, 480).save('rgb.jpg', quality=92)
quad(3000, 2000).save('prog.jpg', quality=85, progressive=True)
quad(6000, 4000).save('big.jpg', quality=70)
quad(400, 300).convert('CMYK').save('cmyk.jpg', quality=92)
quad(320, 240).convert('L').save('gray.jpg', quality=92)
# EXIF-Ausrichtung 6 (90° im Uhrzeigersinn anzeigen): gespeichert 400x200
ex = Image.Exif(); ex[0x0112] = 6
quad(400, 200).save('exif6.jpg', quality=92, exif=ex.tobytes())
a = quad(256, 256, 'RGBA'); ImageDraw.Draw(a).rectangle([0,0,63,63], fill=(0,0,0,0)); a.save('rgba.png')
p = quad(200, 100).convert('P', palette=Image.ADAPTIVE, colors=4); p.info['transparency'] = 0; p.save('pal.png', transparency=0)
Image.new('I;16', (64, 64), 40000).save('gray16.png')
la = Image.new('LA', (64, 64), (200, 128)); la.save('la.png')
quad(5000, 3000).save('big.png', optimize=True)
quad(800, 600).save('lossy.webp', quality=80)
quad(300, 300, 'RGBA').save('alpha.webp', lossless=True)
quad(120, 90).save('img.gif'); quad(120, 90).save('img.bmp'); quad(120, 90).save('img.tga')
data = open('rgb.jpg','rb').read(); open('truncated.jpg','wb').write(data[:len(data)//2])
open('fake.heic','wb').write(b'\x00\x00\x00\x18ftypheic\x00\x00\x00\x00mif1heic' + b'\x00'*64)
open('fake.avif','wb').write(b'\x00\x00\x00\x1cftypavif\x00\x00\x00\x00avifmif1miaf' + b'\x00'*64)
open('notimage.png','wb').write(b'Das ist kein Bild, nur Text mit falscher Endung.' * 4)
