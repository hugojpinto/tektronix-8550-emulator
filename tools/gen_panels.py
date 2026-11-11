#!/usr/bin/env python3
# Generates emu/artwork/8301.lay and 8501.lay. Design grid (view units, window shows them at 0.75):
#   face 10..990; content margins 26 left/right, 18 top/bottom; left area x 36..568, right column x 592..964.
#   Right column = grille / control panel (112 tall) / grille, gaps 10, on both units.
#   Panel labels 12 tall (5x7 dots of 1.5), name strips 13, drive labels and inside-LED label 14.
import sys
OUT = sys.argv[1]

def f(v):
    s = ('%.2f' % v).rstrip('0').rstrip('.')
    return s if s not in ('-0', '') else '0'

def col(c):
    return '<color red="%s" green="%s" blue="%s"/>' % tuple(f(x) for x in c)

def bnd(x, y, w, h):
    return '<bounds x="%s" y="%s" width="%s" height="%s"/>' % (f(x), f(y), f(w), f(h))

INK = (0.20, 0.20, 0.22)
LIGHT_INK = (0.72, 0.72, 0.69)
BLUE = (0.20, 0.45, 0.75)
SLAT_BG = (0.14, 0.14, 0.15)
SLAT = (0.33, 0.33, 0.34)

COMMON = [
    '<element name="frame"><rect>%s</rect></element>' % col((0.66, 0.65, 0.60)),
    '<element name="face"><rect>%s</rect></element>' % col((0.86, 0.85, 0.80)),
    '<element name="recess"><rect>%s</rect></element>' % col((0.80, 0.79, 0.73)),
    '<element name="strip"><rect>%s</rect></element>' % col((0.90, 0.89, 0.85)),
    '<element name="panel"><rect>%s</rect><rect>%s<bounds x="0" y="0" width="100" height="1"/></rect></element>'
        % (col((0.88, 0.88, 0.85)), col((0.70, 0.70, 0.68))),
    '<element name="divider"><rect>%s</rect></element>' % col((0.62, 0.62, 0.60)),
    '<element name="led_red"><disk state="0">%s</disk><disk state="1">%s</disk></element>'
        % (col((0.30, 0.07, 0.05)), col((1.00, 0.20, 0.10))),
    '<element name="led_green"><disk state="0">%s</disk><disk state="1">%s</disk></element>'
        % (col((0.07, 0.22, 0.12)), col((0.30, 1.00, 0.45))),
    '<element name="toggle"><rect>%s<bounds x="0" y="0" width="10" height="12"/></rect>'
        '<rect state="0">%s<bounds x="2" y="1.5" width="6" height="4.5"/></rect>'
        '<rect state="1">%s<bounds x="2" y="6" width="6" height="4.5"/></rect>'
        '<rect state="0">%s<bounds x="3" y="2" width="4" height="1.2"/></rect>'
        '<rect state="1">%s<bounds x="3" y="9" width="4" height="1.2"/></rect></element>'
        % (col((0.08, 0.08, 0.09)), col((0.30, 0.30, 0.32)), col((0.30, 0.30, 0.32)),
           col((0.55, 0.55, 0.57)), col((0.55, 0.55, 0.57))),
    '<element name="rocker_v"><rect>%s<bounds x="0" y="0" width="10" height="17"/></rect>'
        '<rect>%s<bounds x="1.2" y="1.2" width="7.6" height="14.6"/></rect>'
        '<rect>%s<bounds x="1.2" y="1.2" width="7.6" height="6"/></rect></element>'
        % (col((0.05, 0.05, 0.06)), col((0.16, 0.16, 0.17)), col((0.24, 0.24, 0.25))),
    '<element name="inset"><rect>%s</rect></element>' % col((0.72, 0.72, 0.70)),
    '<element name="rocker"><rect>%s<bounds x="0" y="0" width="10" height="10"/></rect>'
        '<rect>%s<bounds x="1.2" y="1.2" width="7.6" height="7.6"/></rect>'
        '<rect>%s<bounds x="1.2" y="1.2" width="7.6" height="3"/></rect></element>'
        % (col((0.05, 0.05, 0.06)), col((0.16, 0.16, 0.17)), col((0.24, 0.24, 0.25))),
]

class Lay:
    def __init__(self):
        self.elements = list(COMMON)
        self.items = []
        self.texts = {}
        self.grilles = {}

    def item(self, ref, x, y, w, h, extra='', rot=None):
        o = '<orientation rotate="%d"/>' % rot if rot else ''
        self.items.append('<element ref="%s"%s>%s%s</element>' % (ref, extra, bnd(x, y, w, h), o))

    def text_el(self, s, c):
        key = (s, c)
        if key not in self.texts:
            name = 't_' + ''.join(ch for ch in s.lower().replace(' ', '_') if ch.isalnum() or ch == '_')
            self.texts[key] = name
            self.elements.append('<element name="%s"><text string="%s">%s</text></element>' % (name, s, col(c)))
        return self.texts[key]

    # text boxes exactly 6*len dots wide, so nothing shrinks; ink spans 6*len-1 dots
    def text_c(self, s, cx, y, h, c=INK):        # centred on cx
        w = 6 * len(s) * h / 8
        self.item(self.text_el(s, c), cx - w / 2, y, w, h)

    def text_l(self, s, x, y, h, c=INK):         # ink starts at x
        w = 6 * len(s) * h / 8
        self.item(self.text_el(s, c), x - h / 16, y, w, h)

    def text_r(self, s, x, y, h, c=INK):         # ink ends at x
        w = 6 * len(s) * h / 8
        self.item(self.text_el(s, c), x - w + h / 16, y, w, h)

    def grille(self, x, y, w, h, pitch=22, slat=9, border=11):
        """vertical slats at a fixed pitch in view units, centred, equal-ish borders"""
        key = (w, h)
        if key not in self.grilles:
            name = 'grille_%dx%d' % (w, h)
            self.grilles[key] = name
            n = int((w - 2 * border + (pitch - slat)) // pitch)
            span = n * pitch - (pitch - slat)
            x0 = (w - span) / 2
            parts = ['<rect>%s%s</rect>' % (col(SLAT_BG), bnd(0, 0, w, h))]
            for i in range(n):
                parts.append('<rect>%s%s</rect>' % (col(SLAT), bnd(x0 + i * pitch, border, slat, h - 2 * border)))
            self.elements.append('<element name="%s">%s</element>' % (name, ''.join(parts)))
        self.item(self.grilles[key], x, y, w, h)

    def write(self, path, header, vname, vw, vh):
        with open(path, 'w') as fh:
            fh.write('<?xml version="1.0"?>\n' + header + '\n<mamelayout version="2">\n')
            for e in self.elements:
                fh.write(e + '\n')
            fh.write('<view name="%s">%s\n' % (vname, bnd(0, 0, vw, vh)))
            for i in self.items:
                fh.write(i + '\n')
            fh.write('</view></mamelayout>\n')

LBL = 12   # panel label height
LAMP = 12  # panel lamp diameter

def box(L, fw, fh):
    L.item('frame', 0, 0, fw, fh)
    L.item('face', 10, 10, fw - 20, fh - 20)

def diag(L, y0, text, names):
    # below the box, on the window background: label, then the LEDs, on one centre line
    h = 14
    L.text_l(text, 36, y0 - h / 2, h, LIGHT_INK)
    x = 36 + 6 * len(text) * h / 8 + 24
    for n in names:
        L.item('led_red', x, y0 - 7, 14, 14, ' name="%s"' % n)
        x += 26

def divider(L, x, y0, y1):
    L.item('divider', x - 1, y0 + 10, 2, y1 - y0 - 20)

# ---------------------------------------------------------------- 8501
def make_8501():
    L = Lay()
    L.elements.append(
        '<element name="drive">'
        '<rect>%s<bounds x="0" y="0" width="512" height="213"/></rect>'      # cream drive face
        '<rect>%s<bounds x="6" y="46" width="500" height="50"/></rect>'      # slot band
        '<rect>%s<bounds x="282" y="94" width="56" height="10"/></rect>'     # tab below the band, right of centre
        '<rect>%s<bounds x="214" y="143" width="84" height="14"/></rect>'    # latch bar, centred, on the sticker centre line
        '</element>' % (col((0.84, 0.83, 0.78)), col(SLAT_BG), col((0.05, 0.05, 0.06)), col((0.05, 0.05, 0.06))))
    FW, FH = 1000, 560
    box(L, FW, FH)
    L.item('recess', 36, 28, 532, 504)
    # drives: 46..558 x; drive 213 tall; label strips 20 tall; gaps 6
    for d, y in ((0, 38), (1, 283)):
        L.item('drive', 46, y, 512, 213, ' drive="%d" inputtag="drive%d:eject"' % (d, d))
        # DRIVE n: in the band, ink right edge on the caption sticker's right edge (97% of 512)
        L.text_r('DRIVE %d' % d, 46 + 0.97 * 512, y + 71 - 7, 14, (0.90, 0.90, 0.88))
        # activity lamp: lower left, on the sticker / latch centre line (70.5%)
        L.item('led_red', 46 + 28, y + 150 - 7, 14, 14, ' name="drive%d:active"' % d)
    L.item('strip', 46, 257, 512, 20)
    L.item('strip', 46, 502, 512, 20)
    L.text_l('Tektronix', 46 + 0.03 * 512, 502 + 3.5, 13, BLUE)
    L.text_r('8501 DATA MANAGEMENT UNIT', 46 + 0.97 * 512, 502 + 3.5, 13)
    # right column
    L.grille(592, 28, 372, 186)
    L.item('panel', 592, 224, 372, 112)
    L.grille(592, 346, 372, 186)
    P0, P1 = 224, 336
    d1, d2 = 702, 798
    divider(L, d1, P0, P1); divider(L, d2, P0, P1)
    ly = 238            # label row
    dy = 256            # devices: 48 tall, centre 280
    # section 1: ON lamp | DC rocker
    L.item('inset', 606, 262, 24, 36)
    L.text_c('ON', 618, ly, LBL); L.item('led_green', 618 - LAMP / 2, 280 - LAMP / 2, LAMP, LAMP, ' name="8501:on"')
    L.text_c('DC', 670, ly, LBL); L.item('rocker', 646, dy, 48, 48)
    # section 2: AC ON, PROCESSOR BUSY (stacked)
    cx = (d1 + d2) / 2
    L.text_c('AC ON', cx, ly, LBL); L.item('led_red', cx - LAMP / 2, 254, LAMP, LAMP, ' name="8501:acon"')
    L.text_c('PROCESSOR', cx, 280, LBL); L.text_c('BUSY', cx, 293, LBL)
    L.item('led_red', cx - LAMP / 2, 309, LAMP, LAMP, ' name="8501:busy"')
    # section 3: RESTART | HALT/RUN
    c1, c2 = 840, 922
    L.text_c('RESTART', c1, ly, LBL); L.item('toggle', c1 - 20, dy, 40, 48, ' inputtag="8501:restart"')
    L.text_c('HALT', c2, ly, LBL); L.item('toggle_run', c2 - 20, dy, 40, 48)
    L.text_c('RUN', c2, 310, LBL)
    L.elements.append(
        '<element name="toggle_run"><rect>%s<bounds x="0" y="0" width="10" height="12"/></rect>'
        '<rect>%s<bounds x="2" y="6" width="6" height="4.5"/></rect>'
        '<rect>%s<bounds x="3" y="9" width="4" height="1.2"/></rect></element>'
        % (col((0.08, 0.08, 0.09)), col((0.30, 0.30, 0.32)), col((0.55, 0.55, 0.57))))
    diag(L, 590, 'UTILITY BOARD LEDS (INSIDE)', ['8501:led%d' % i for i in (4, 3, 2, 1, 0)])
    L.write(OUT + '/8501.lay', HDR_8501, 'Tektronix 8501 Data Management Unit', 1000, 620)

# ---------------------------------------------------------------- 8301
def make_8301():
    L = Lay()
    FW, FH = 1000, 560                # same box height as the 8501 (catalogue photo)
    box(L, FW, FH)
    # left: grille / plain name panel / grille with the name strip; rows match the right column
    L.grille(36, 28, 532, 186)
    L.item('recess', 36, 224, 532, 112)
    L.grille(36, 346, 532, 186)
    L.item('strip', 46, 502, 512, 20)
    L.text_l('Tektronix', 46 + 0.03 * 512, 502 + 3.5, 13, BLUE)
    L.text_r('8301 MICROPROCESSOR DEVELOPMENT UNIT', 46 + 0.97 * 512, 502 + 3.5, 13)
    # right column
    L.grille(592, 28, 372, 186)
    L.item('panel', 592, 224, 372, 112)
    L.grille(592, 346, 372, 186)
    P0, P1 = 224, 336                 # the 8501's panel row
    d1, d2 = 730, 856
    divider(L, d1, P0, P1); divider(L, d2, P0, P1)
    rows = [247, 269, 291, 313]   # lamp rows, pitch 22, centred on the panel
    ly = rows[0] - LBL / 2
    dy = 265                      # devices 48 tall, centre 289
    # section 1: POWER rocker
    RW, RH = 40, 68                                           # portrait rocker, as on the photo
    gw = RW + 8 + 6 * 3 * LBL / 8                             # rocker + ON/OFF marks: centred as a group
    cx = (592 + d1) / 2 - gw / 2 + RW / 2
    ry = P1 - 10 - RH
    L.text_c('POWER', cx, ry - 8 - LBL, LBL)
    L.item('rocker_v', cx - RW / 2, ry, RW, RH)
    L.text_l('ON', cx + RW / 2 + 8, ry + 2, LBL)
    L.text_l('OFF', cx + RW / 2 + 8, ry + RH - 2 - LBL, LBL)
    # section 2: four lamps in a column, labels to the right
    lx = (d1 + d2) / 2 - (LAMP + 8 + 6 * 9 * LBL / 8) / 2   # lamp + widest label (SELF TEST), centred
    for (lab, n), cy in zip((('SYSTEM', 'system'), ('PROGRAM', 'program'), ('DMA', 'dma'), ('SELF TEST', 'selftest')), rows):
        L.item('led_red', lx, cy - LAMP / 2, LAMP, LAMP, ' name="8301:%s"' % n)
        L.text_l(lab, lx + LAMP + 8, cy - LBL / 2, LBL)
    # section 3: RESTART toggle
    cx = (d2 + 964) / 2
    L.text_c('RESTART', cx, ly, LBL)
    L.item('toggle', cx - 20, dy, 40, 48, ' inputtag="8301:restart"')
    diag(L, 590, 'SYSTEM CONTROLLER LEDS (INSIDE)', ['8301:led%d' % i for i in (4, 3, 2, 1, 0)])
    L.write(OUT + '/8301.lay', HDR_8301, 'Tektronix 8301 Microprocessor Development Unit', 1000, 620)

HDR_8501 = open(OUT + '/8501.lay').read().split('\n', 1)[1].split('<mamelayout')[0].strip()
HDR_8301 = '''<!-- Tektronix 8301 Microprocessor Development Unit, front panel. After the 8550 catalogue photo
     (sources/tekwiki/8550/8550_1.jpg; sharper: sources/photos/8550/8550_catalogue_hires.png): on the
     left a grille, a plain name panel and a grille with the name strip along its bottom; POWER is a
     tall portrait rocker with ON/OFF marks to its right; on the right a grille, the control panel and a grille, the same arrangement as the
     8501. The control panel is the 8540's control strip (photo from the VCFed thread "Tektronix 85xx
     MDL system", a unit standing on its side) turned to lie flat, as in the brochure photo: POWER rocker |
     SYSTEM, PROGRAM, DMA, SELF TEST lamps | RESTART toggle (070-2976 Fig. 1-3). The System Controller
     board LEDs (port ED) are inside: shown on a strip below. -->'''
make_8501()
make_8301()
