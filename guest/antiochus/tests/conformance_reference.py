# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent pixel expectations for the cf-2 inputs. No OpenGL or worker code."""
import math
import operator

CLEAR = (64, 128, 191, 255)
WHITE = (255, 255, 255, 255)
RED, GREEN, BLUE = (255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255)
TEXELS = (RED, GREEN, BLUE, WHITE)
LOGIC = {
    'clear': lambda s, d: 0, 'set': lambda s, d: 255,
    'copy': lambda s, d: s, 'copy-inverted': lambda s, d: ~s,
    'noop': lambda s, d: d, 'invert': lambda s, d: ~d,
    'and': operator.and_, 'nand': lambda s, d: ~(s & d),
    'or': operator.or_, 'nor': lambda s, d: ~(s | d),
    'xor': operator.xor, 'equiv': lambda s, d: ~(s ^ d),
    'and-reverse': lambda s, d: s & ~d, 'and-inverted': lambda s, d: ~s & d,
    'or-reverse': lambda s, d: s | ~d, 'or-inverted': lambda s, d: ~s | d,
}
DEPTH = {'less': operator.lt, 'lequal': operator.le, 'equal': operator.eq,
         'notequal': operator.ne, 'greater': operator.gt, 'gequal': operator.ge,
         'never': lambda a,b: False, 'always': lambda a,b: True}


def byte(value):
    return max(0, min(255, int(value + 0.5)))


def blend(op):
    source, dest = (174, 97, 58, 83), (53, 96, 181, 117)
    def channel(i, s, d):
        a, b = source[3]/255, dest[3]/255
        factors = {
            'one-one': (1, 1), 'zero-src-color': (0, s/255),
            'dst-color-zero': (d/255, 0), 'src-alpha-one-minus-src-alpha': (a, 1-a),
            'dst-alpha-one-minus-dst-alpha': (b, 1-b), 'one-minus-dst-color-one': (1-d/255, 1),
            'one-one-minus-src-color': (1, 1-s/255),
            'src-alpha-saturate-one': (1 if i == 3 else min(a, 1-b), 1),
        }
        f, g = factors[op]
        return byte(s*f + d*g)
    return tuple(channel(i,s,d) for i,(s,d) in enumerate(zip(source,dest)))


def wrap(mode, x, y):
    # Normalized sample coordinates span [-0.5, 1.5] before wrapping.
    s, t = ((p+0.5)/16-0.5 for p in (x,y))
    if mode.endswith('-linear'):
        if mode == 'clamp-linear':
            s, t = (max(0, min(1, u)) for u in (s,t))
        elif mode != 'clamp-to-border-linear':
            raise ValueError('unknown wrap ' + mode)
        u, v = 2*s-0.5, 2*t-0.5
        left, bottom = math.floor(u), math.floor(v)
        rgba = [0.0]*4
        for j in (bottom,bottom+1):
            for i in (left,left+1):
                texel = TEXELS[2*j+i] if 0 <= i < 2 and 0 <= j < 2 else (0,255,255,0)
                weight = (1-abs(u-i))*(1-abs(v-j))
                for k, c in enumerate(texel):
                    rgba[k] += c*weight
        return tuple(map(byte, rgba))
    def index(u):
        i = math.floor(2*u)
        if mode == 'repeat': return i % 2
        if mode == 'mirrored-repeat': return (0,1,1,0)[i % 4]
        if mode == 'clamp-to-edge': return max(0,min(1,i))
        raise ValueError('unknown wrap ' + mode)
    return TEXELS[2*index(t)+index(s)]


def expected(case, x, y):
    name = case[3:]
    quadrant = 2*(y >= 16)+(x >= 16)
    inside = 2 <= x < 30 and 3 <= y < 29
    if name.startswith('fbo-'):
        alpha = 255 if x >= 16 or name.endswith(('-rgb8','-rgb5')) else 0
        return TEXELS[quadrant][:3]+(alpha,)
    if name.startswith('logic-'):
        s, d = (180,103,40,203), (83,150,225,53)
        op = LOGIC[name[6:]]
        return tuple(op(a,b)&255 for a,b in zip(s,d)) if inside else d
    if name.startswith('blend-'):
        return blend(name[6:]) if inside else (53,96,181,117)
    if name.startswith('texsub-stride-'):
        if 5 <= x < 13 and 9 <= y < 17:
            skip = int(name.rsplit('-',1)[1])
            u, v = x-5+skip, y-9+skip
            return (13*u,17*v,u^v,255)
        return (0,0,0,0)
    if name == 'elements-short-vbo':
        return WHITE if x%2 == (y//4)%2 else CLEAR
    if name.startswith('elements-'):
        # Triangle (2,4),(30,4),(2,25). Evaluate at pixel centers.
        return WHITE if x >= 2 and y >= 4 and 3*(x+0.5-2)+4*(y+0.5-4) < 84 else CLEAR
    if name == 's3tc-subimage-dxt1':
        return GREEN if x >= 16 and y < 16 else RED
    if name == 'tri-cull-both': return CLEAR
    if name == 'tri-flat':
        return GREEN if x < 28 and abs(y+0.5-16) < (x+0.5-4)/2 else CLEAR
    if name.startswith('glsl-color-'): return RED if x < 16 else GREEN
    if name.startswith('depth-'):
        previous = (0,1,2,4)[x//10]  # quarter units
        return (25,215,40,255) if DEPTH[name[6:]](1,previous) else (WHITE if x < 30 else CLEAR)
    if name.startswith('stencil-'):
        return GREEN if x < 16 or not name.endswith('-wrap') else CLEAR
    if name.startswith('fog-'):
        z = (x//8)/4
        f = {'linear': 1-z, 'exp': math.exp(-2*z), 'exp2': math.exp(-(2*z)**2)}[name[4:]]
        return tuple(byte(255*(f+(1-f)*c)) for c in (.125,.25,.5))+(255,)
    if name.startswith('wrap-'): return wrap(name[5:],x,y)
    if name.startswith('combine-'):
        tile = ((20,70,190),(100,140,210),(230,180,150),(70,240,45))[quadrant]
        def rgb(a,b,w):
            return byte({'modulate': a*b/255, 'replace': a, 'add': a+b, 'add-signed': a+b-127.5,
                         'interpolate': a*w+b*(1-w), 'subtract': a-b}[name[8:]])
        return tuple(rgb(*p) for p in zip(tile,(45,90,135),(.25,.5,.75)))+(255,)
    if name in ('offset-negative','offset-positive'):
        if name.endswith('negative') and 4 <= x < 24 and 6 <= y < 22: return (200,35,85,255)
        return (20,160,40,255)
    if name == 'clip-four-planes': return WHITE if 4 <= x < 25 and 7 <= y < 22 else CLEAR
    raise ValueError('no independent rule for ' + case)


def check_reference(case, data, probe='primary'):
    if len(data) != 32*32*4:
        raise ValueError('invalid reference extent')
    if case == 'cf-ci-invert-controls':
        # DeviceRGB and Core Image color management are platform-dependent.
        if (data[0]-data[124] <= 16 or data[125]-data[1] <= 16 or
                any(a != 255 for a in data[3::4]) or data[:128]*32 != data):
            raise ValueError('CI reference failed opaque, row or inverted ramp invariants')
        return
    tolerance = (0 if case.startswith('cf-logic-') else 4 if case.startswith('cf-fog-') else
                 2 if case.startswith(('cf-blend-','cf-combine-')) or case.endswith('-linear') else 1)
    bad = 0
    first = None
    for y in range(32):
        for x in range(32):
            rgba = expected(case, x, y)
            if probe == 'same_fbo_rewrite' and 7 <= x < 19 and 3 <= y < 8:
                rgba = (255,0,255,255)
            elif probe == 'post_read_progress':
                rgba = (220,175,45,255) if 7 <= x < 19 and 3 <= y < 8 else (30,70,130,255)
            k = 4*(y*32+x)
            actual = tuple(data[k:k+4])
            if any(abs(a-b)>tolerance for a,b in zip(actual,rgba)):
                if first is None: first = (x,y,rgba,actual)
                bad += 1
    if bad:
        raise ValueError(f'{probe} independent reference: {bad} pixels, first (x,y,expected,got)={first}')
