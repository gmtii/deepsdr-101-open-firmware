import numpy as np, sys
sys.path.insert(0,'/tmp/dscdec')
from proto import bits_de, caracteres, engancha
FMT={112:"socorro",116:"todas las estaciones",120:"individual",114:"grupo",102:"zona geografica",123:"individual automatico"}
CAT={100:"rutina",106:"ACS",108:"seguridad",110:"urgencia",112:"socorro"}
EOS={117:"acuse pedido (RQ)",122:"acuse dado (BQ)",127:"sin acuse"}
NAT={100:"incendio/explosion",101:"via de agua",102:"abordaje",103:"varada",104:"escora",105:"hundimiento",
     106:"a la deriva",107:"sin especificar",108:"abandono",109:"pirateria",110:"hombre al agua"}
def mmsi(c):
    d="".join("%02d"%v for v in c)
    return d[:9]
def ecc(info):
    """Paridad vertical par sobre los 7 bits de cada caracter de informacion."""
    r=0
    for s in info:
        r ^= (s & 0x7F)
    return r
def analiza(f,flo,fhi):
    r=bits_de(f,flo,fhi)
    if r is None: return
    bits,_=r
    (off,frac,tot,ok),cs=engancha(bits)
    sims=[(i,s,o) for (i,s,o) in cs if i%10==off]
    v=[(s if o else None) for (i,s,o) in sims]
    # localizar el final del enganche: el ultimo 104 en ranura RX
    try: p=max(i for i,s in enumerate(v) if s==104)
    except ValueError: print("  %-22s sin enganche"%f.split('/')[-1]); return
    # a partir de ahi: DX en las pares, RX (copia atrasada 4) en las impares
    dx=[v[i] for i in range(p-1, len(v), 2)]   # p-1 es el primer 120 en DX
    # rellenar huecos del DX con la copia RX de 4 despues
    rx=[v[i] for i in range(p, len(v), 2)]
    for i in range(len(dx)):
        if dx[i] is None and i+4 < len(rx)+0 and i+4-0 < len(rx):
            pass
    seq=[x for x in dx if x is not None]
    print("\n  === %s ==="%f.split('/')[-1])
    print("    secuencia DX:", " ".join(str(x) if x is not None else "·" for x in dx[:24]))
    s=[x for x in dx]
    if len(s)<14 or s[0] is None: print("    secuencia corta"); return
    fmt=s[0]; print("    formato        %s  (%s)"%(fmt, FMT.get(fmt,"?")))
    k=2 if (len(s)>1 and s[1]==fmt) else 1
    dirc=s[k:k+5]
    if all(x is not None for x in dirc): print("    dirigida a     MMSI %s"%mmsi(dirc))
    cat=s[k+5]; print("    categoria      %s  (%s)"%(cat, CAT.get(cat,"?")))
    prop=s[k+6:k+11]
    if all(x is not None for x in prop): print("    de             MMSI %s"%mmsi(prop))
    resto=[x for x in s[k+11:] if x is not None]
    print("    resto          %s"%(" ".join(str(x) for x in resto[:10])))
    for x in resto:
        if x in EOS: print("    fin de secuencia %s  (%s)"%(x,EOS[x])); break
for a in sys.argv[1:]:
    f,flo,fhi=a.split(',')
    analiza(f,float(flo),float(fhi))
