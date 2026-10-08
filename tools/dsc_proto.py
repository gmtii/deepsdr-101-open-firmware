import numpy as np, sys
FS=12000.0; BAUD=100.0
def bits_de(fichero, flo, fhi):
    x=np.fromfile(fichero,dtype=np.float32).astype(float)
    t=np.arange(len(x))/FS; L=int(round(FS/BAUD)); k=np.ones(L)/L
    a=np.abs(np.convolve(x*np.exp(-2j*np.pi*fhi*t),k,'same'))
    b=np.abs(np.convolve(x*np.exp(-2j*np.pi*flo*t),k,'same'))
    d=a-b
    s=np.sign(d); ch=np.nonzero(np.diff(s))[0]
    if len(ch)<20: return None
    fase=np.median((ch % L))
    idx=np.arange(int(fase+L/2), len(d)-L, L).astype(int)
    # ALTA = B = cero binario ; BAJA = Y = uno binario   (M.493-16 §1)
    return (d[idx] < 0).astype(np.uint8), d[idx]
def caracteres(bits):
    """Devuelve lista (pos, simbolo, ok) para cada alineacion de 10 bits."""
    out=[]
    for i in range(0, len(bits)-10):
        inf=bits[i:i+7]; chk=bits[i+7:i+10]
        # 7 de informacion, BIT MENOS SIGNIFICATIVO PRIMERO
        sim=int(sum(int(inf[j])<<j for j in range(7)))
        # 3 de comprobacion, MAS SIGNIFICATIVO PRIMERO = cuenta de elementos B (ceros)
        c=int(chk[0])*4+int(chk[1])*2+int(chk[2])
        ceros=int(7-sum(int(v) for v in inf))
        out.append((i,sim,c==ceros))
    return out
def engancha(bits):
    """Busca la fase de 10 bits donde mas caracteres cuadran, y el DX=125."""
    cs=caracteres(bits)
    mejor=None
    for off in range(10):
        ok=sum(1 for (i,s,o) in cs if i%10==off and o)
        tot=sum(1 for (i,s,o) in cs if i%10==off)
        if tot and (mejor is None or ok/tot>mejor[1]): mejor=(off,ok/tot,tot,ok)
    return mejor, cs
if __name__=="__main__":
    f,flo,fhi=sys.argv[1],float(sys.argv[2]),float(sys.argv[3])
    r=bits_de(f,flo,fhi)
    if r is None: print("sin transiciones"); sys.exit()
    bits,d=r
    (off,frac,tot,ok),cs=engancha(bits)
    print("  %s: %d bits, fase %d -> %d de %d caracteres cuadran (%.0f%%)"%(f.split('/')[-1],len(bits),off,ok,tot,100*frac))
    sims=[(i,s,o) for (i,s,o) in cs if i%10==off]
    txt=" ".join(("%d"%s) if o else "·" for (i,s,o) in sims)
    print("   ", txt[:400])
