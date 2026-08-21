"""Contrarian boot animation.

The Konami Code entered on a black screen, then the logo, then the set snaps
on. The code is original artwork: an input sequence is not anyone's copyrighted
work, which is exactly why it is the right motif here -- it is instantly
readable to anyone who would install this and owes nothing to Konami's art.
The logo that resolves out of the flash is docs/logo.png.

Writes numbered frames, then encodes res/boot/contrarian-boot.mp4.
"""
import os, subprocess, math, sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops
sys.path.insert(0, os.path.dirname(__file__))
import pixfont

W, H, FPS = 1024, 768, 30
OUT = "/tmp/ctr_boot_frames"
AMBER, WHITE = (214,150,64), (240,238,230)

SEQ = [("UP",None),("UP",None),("DOWN",None),("DOWN",None),
       ("LEFT",None),("RIGHT",None),("LEFT",None),("RIGHT",None),
       (None,"B"),(None,"A")]

STEP   = 0.19            # seconds between inputs
T_CODE = 0.45 + STEP*len(SEQ)
T_START= T_CODE + 0.40
T_FLASH= T_START + 0.34
T_TITLE= T_FLASH + 1.15
T_ON   = T_TITLE + 0.55
TOTAL  = T_ON + 0.30

LOGO_SRC  = "docs/logo.png"
LOGO_W    = 860              # of 1024: wide, but with air either side
_logo_cache = {}

def logo(scale):
    """The wordmark at a given scale, RGB on black, cached per size.

    Kept on its black field on purpose: the frame it lands on is either black
    or the tail of the white flash, and it is composited with `lighter`, so
    the black simply has no effect and the letters emerge as the flash falls
    away. Compositing it any other way would punch a black box in the flash."""
    key = round(scale, 3)
    if key not in _logo_cache:
        src = Image.open(LOGO_SRC).convert("RGB")
        w = max(1, int(LOGO_W * scale))
        h = max(1, round(w * src.height / src.width))
        _logo_cache[key] = src.resize((w, h), Image.LANCZOS)
    return _logo_cache[key]

def frame(t):
    im = Image.new("RGB", (W, H), (0,0,0))
    d  = ImageDraw.Draw(im)

    # --- the code, entered one input at a time --------------------------
    # int() truncates toward zero, so guard the pre-roll explicitly or
    # the first input starts with a negative age (and a negative radius)
    n_in = 0 if t < 0.45 else max(0, min(len(SEQ), int((t - 0.45) / STEP) + 1))
    if 0 < n_in and t < T_FLASH:
        px, gap = 6, 26
        widths = [7*px if a else 5*px for a,b in SEQ]
        total  = sum(widths) + gap*(len(SEQ)-1)
        x = (W - total)//2
        y = H//2 - 4*px
        for i,(arrow,btn) in enumerate(SEQ):
            w = widths[i]
            if i < n_in:
                age = max(0.0, (t - 0.45) - i*STEP)
                # each input lands bright then settles to amber
                k = max(0.0, min(1.0, age/0.13))
                col = tuple(int(WHITE[c] + (AMBER[c]-WHITE[c])*k) for c in range(3))
                if arrow:
                    pixfont.draw_glyph(d, pixfont.ARROW[arrow], x, y, px, col)
                else:
                    pixfont.draw(d, btn, x, y, px, col)
                if age < 0.10:                      # brief impact ring
                    r = 16 + age*260
                    d.ellipse([x+w//2-r, y+3.5*px-r, x+w//2+r, y+3.5*px+r],
                              outline=tuple(int(c*0.5) for c in AMBER), width=2)
            x += w + gap

    # --- START ----------------------------------------------------------
    if T_START <= t < T_FLASH:
        s = "START"
        pixfont.draw(d, s, (W - pixfont.width(s,5))//2, H//2 + 70, 5, WHITE)

    # --- the flash the title resolves out of ----------------------------
    if T_FLASH <= t < T_FLASH + 0.22:
        k = 1.0 - (t - T_FLASH)/0.22
        v = int(255*k*k)
        im = Image.new("RGB",(W,H),(v,v,v)); d = ImageDraw.Draw(im)

    # --- logo -----------------------------------------------------------
    # The code resolves into the wordmark: it settles down out of a slightly
    # oversized frame while it brightens, so it arrives rather than appears.
    if T_FLASH <= t < T_ON:
        k = min(1.0, (t - T_FLASH)/0.45)
        e = 1.0 - (1.0 - k)**3                 # ease out
        lg = logo(1.055 - 0.055*e)
        if k < 1.0:                            # brighten into place
            lg = lg.point(lambda v, _k=e: int(v*_k))
        lay = Image.new("RGB", (W, H), (0,0,0))
        lay.paste(lg, ((W - lg.width)//2, (H - lg.height)//2))
        im = ImageChops.lighter(im, lay)
        d  = ImageDraw.Draw(im)

    # --- the set snapping on --------------------------------------------
    if t >= T_ON:
        k = (t - T_ON)/0.30
        im = Image.new("RGB",(W,H),(0,0,0)); d = ImageDraw.Draw(im)
        hh = max(2, int((1-k)*4 + k*2))
        ww = int(W*0.9*(0.25 + 0.75*min(1.0,k*2)))
        d.rectangle([(W-ww)//2, H//2-hh, (W+ww)//2, H//2+hh],
                    fill=(int(235*(1-k*0.4)),)*3)
    return im

def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT): os.remove(os.path.join(OUT,f))
    n = int(TOTAL*FPS)
    for i in range(n):
        frame(i/FPS).save(f"{OUT}/f{i:04d}.png")
    os.makedirs("res/boot", exist_ok=True)
    subprocess.run(["ffmpeg","-y","-hide_banner","-loglevel","error",
                    "-framerate",str(FPS),"-i",f"{OUT}/f%04d.png",
                    "-c:v","libx264","-pix_fmt","yuv420p","-crf","20",
                    "-movflags","+faststart","res/boot/contrarian-boot.mp4"],
                   check=True)
    print(f"{n} frames, {TOTAL:.2f}s -> res/boot/contrarian-boot.mp4 "
          f"({os.path.getsize('res/boot/contrarian-boot.mp4')} bytes)")
    # First frame is black; the u-boot splash and the loading splash are both
    # that frame, so the handoff into the animation has no visible seam.
    black = frame(0).convert("RGB")
    black.save("res/boot/bootlogo.bmp")
    black.save("res/boot/splash.png")

main()
