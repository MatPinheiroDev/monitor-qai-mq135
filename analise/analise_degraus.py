# Analise dos degraus de ocupacao nas salas I09 e K13: leitura dos dados, relogio pelas ancoras NTP,
# variacao (delta) por degrau e pseudo-degraus de controle.
import os
import csv, re, statistics as st, datetime as dt, sys
from collections import defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

def num(x):
    x = (x or '').strip()
    if x.count(',') == 1 and x.count('.') == 0: x = x.replace(',', '.')
    try: return float(x)
    except: return None

def carregar(arq, sala, boots, T0_manual=None):
    anc = defaultdict(list); seen = set(); R = []
    for r in csv.reader(open(arq, encoding='utf-8')):
        if len(r) < 5 or r[1] != sala: continue
        m = re.match(r'# ANCORA (\S+ \S+) em min=([\d.]+) boot=(\d+)', r[4])
        if m:
            t = dt.datetime.strptime(m.group(1), '%Y-%m-%d %H:%M:%S')
            anc[int(m.group(3))].append(t - dt.timedelta(minutes=float(m.group(2))))
            continue
        if r[4].startswith('#') or len(r) < 20: continue
        b, mi = num(r[4]), num(r[5])
        if b not in boots or mi is None or (b, mi) in seen: continue
        seen.add((b, mi))
        R.append(dict(b=int(b), m=mi, adc=num(r[6]), dp=num(r[7]), rs=num(r[14]), tc=num(r[15]), ur=num(r[16]), vcc=num(r[18])))
    T0 = {}
    for b, l in anc.items():
        ref = l[0]; T0[b] = ref + dt.timedelta(seconds=st.mean([(x - ref).total_seconds() for x in l]))
    T0.update(T0_manual or {})
    for x in R: x['t'] = T0[x['b']] + dt.timedelta(minutes=x['m'])
    R.sort(key=lambda x: x['t'])
    return R, T0, anc

def med(R, a, b, k='rs'):
    v = [x[k] for x in R if a <= x['t'] < b and x[k] is not None]
    return (st.median(v) if v else None), len(v)

def rolling_min(R, a, b, w=10):
    v = [x for x in R if a <= x['t'] < b and x['rs'] is not None]
    best = None
    for i in range(len(v)):
        s = [y['rs'] for y in v[i:i + 2 * w]]
        if len(s) < 2 * w: break
        mm = st.median(s)
        if best is None or mm < best[0]: best = (mm, v[i]['t'])
    return best

import io, contextlib
with contextlib.redirect_stdout(io.StringIO()):
    import contar_degraus as CD

def livre(sala, t):
    """True se a grade indica sala vazia no instante t (fora de 07:10-22:10 = vazia)."""
    wd = t.weekday()
    if wd >= 5: return False
    mm = t.hour * 60 + t.minute
    for k, s in enumerate(CD.slots):
        a = CD.mins(s)
        if a <= mm < a + 50: return CD.G[sala][wd][k] == '.'
    return True

def delta(R, t0):
    b, nb = med(R, t0 - dt.timedelta(minutes=30), t0)
    r, nr = med(R, t0 + dt.timedelta(minutes=30), t0 + dt.timedelta(minutes=90))
    tb, _ = med(R, t0 - dt.timedelta(minutes=30), t0, 'tc'); tr, _ = med(R, t0 + dt.timedelta(minutes=30), t0 + dt.timedelta(minutes=90), 'tc')
    ub, _ = med(R, t0 - dt.timedelta(minutes=30), t0, 'ur'); ur, _ = med(R, t0 + dt.timedelta(minutes=30), t0 + dt.timedelta(minutes=90), 'ur')
    if None in (b, r) or nb < 40 or nr < 80: return None
    ac = (tb is not None and tr is not None and abs(tr - tb) > 1.0) or (ub is not None and ur is not None and abs(ur - ub) > 5)
    return dict(base=b, resp=r, d=r / b - 1, dT=(tr - tb) if tb and tr else None, dU=(ur - ub) if ub and ur else None, ac=ac, nb=nb, nr=nr)

def analisar(sala, R, janelas):
    """janelas: lista de (inicio_valido, fim) por boot"""
    deg = []
    for ini, fim in janelas:
        o, f = CD.degraus(sala, ini - dt.timedelta(hours=6), fim)
        for dia, hh, vaz in o:
            d0 = dt.datetime.strptime(dia.split()[1] + '/2026 ' + hh, '%d/%m/%Y %H:%M')
            deg.append((d0, vaz))
    out = []
    for t0, vaz in deg:
        x = delta(R, t0)
        rm = rolling_min(R, t0, t0 + dt.timedelta(minutes=100))
        lat = None
        if x and rm:
            alvo = x['base'] - (x['base'] - rm[0]) / 2
            v = [y for y in R if t0 <= y['t'] < t0 + dt.timedelta(minutes=100)]
            for i in range(len(v) - 10):
                if st.median([z['rs'] for z in v[i:i + 10]]) <= alvo: lat = (v[i + 5]['t'] - t0).total_seconds() / 60; break
        out.append((t0, vaz, x, rm, lat))
    ctl = []
    for ini, fim in janelas:
        t = ini.replace(minute=0, second=0, microsecond=0) + dt.timedelta(hours=1)
        while t + dt.timedelta(minutes=90) <= fim:
            if t - dt.timedelta(minutes=100) >= ini and all(livre(sala, t + dt.timedelta(minutes=k)) for k in range(-100, 91, 10)):
                x = delta(R, t)
                if x: ctl.append((t, x))
            t += dt.timedelta(minutes=30)
    return out, ctl
