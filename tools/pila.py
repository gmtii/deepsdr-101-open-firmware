#!/usr/bin/env python3
"""
Peor caso de pila del firmware, medido sobre el binario ENLAZADO.

POR QUE EXISTE (24/09/2026). El firmware llevaba tiempo saliendose de la
pila sin que nadie lo notara: siete buffers de 4 kB declarados dentro de
funciones de spi_flash.c, anidandose, daban un peor caso de 10.752 bytes
cuando la pila real mide 4.456. La pila se salia por debajo y pisaba las
variables del TCM - sin fallo, sin aviso, sobrescribiendo y siguiendo.

Se colo porque el razonamiento fue "la pila vive en los 64 kB del TCM, un
buffer de 4 kB no molesta", mirando el TAMANO de la region en vez de lo
que quedaba LIBRE en ella (el DSP tiene ahi 61.076 bytes fijos).

Esto lo convierte en una comprobacion automatica que corre en cada
compilacion. Dos fuentes, ninguna de ellas una estimacion:

  - build/*.su, que escribe gcc con -fstack-usage: los bytes de marco de
    cada funcion, dichos por el propio compilador.
  - el grafo de llamadas sacado del .elf con objdump: quien llama a quien
    DE VERDAD en el binario final, ya pasado el --gc-sections.

Y el limite tampoco se escribe a mano: sale de los simbolos del propio
.elf (_estack por arriba, 'end' por abajo), asi que si algun dia se mueve
la pila o crece el TCM, esto se entera solo.

LO QUE NO VE, dicho claramente para que nadie le de mas credito del que
tiene:
  - Llamadas indirectas por puntero a funcion. objdump ve "blx r3" y no
    sabe a donde va. Se listan TODAS al final, y las que se sabe a donde
    van estan en GANCHOS, mas abajo.
  - Recursion. Se detecta y se avisa, pero no se puede acotar.
  - alloca() y arrays de tamano variable: gcc los marca "dynamic" en el
    .su y aqui salen senalados, porque su coste real no se sabe.
"""
import re, sys, os, glob, subprocess, collections

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ELF  = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RAIZ, 'build', 'firmware.elf')
SUD  = sys.argv[2] if len(sys.argv) > 2 else os.path.join(RAIZ, 'build')
GCC  = 'arm-none-eabi-'

# Margen para la interrupcion que caiga encima del peor caso: 32 bytes que
# apila el hardware al entrar + 72 mas si toca guardar los registros de coma
# flotante, redondeado a 104. Solo UNA interrupcion, porque en este proyecto
# todas tienen la misma prioridad y por tanto no se anidan entre ellas. Si
# alguna vez se sube la prioridad de una, este numero hay que multiplicarlo.
APILADO_HW = 104

#
# LOS GANCHOS - 25/09/2026.
#
# El camino MAS PROFUNDO del firmware es el de la interrupcion del audio, y
# hasta hoy esta herramienta no lo veia: la interrupcion no llama al
# demodulador por su nombre sino por un puntero (sdr_rx_set_block_hook), y
# objdump solo ve un "blx r3". Resultado: decia "peor interrupcion: 40
# bytes" cuando de verdad son 744.
#
# Cuarenta contra setecientos cuarenta no es un matiz. Es la diferencia entre
# una comprobacion que mide y una que da el visto bueno mirando a otro lado,
# que es EXACTAMENTE el fallo que esta herramienta ya tuvo una vez con el
# filtro de las interrupciones (ver mas abajo). Dos veces el mismo error en el
# mismo sitio: aqui no se puede suponer nada, hay que ir a mirar.
#
# Asi que los ganchos conocidos se escriben. Y como una lista escrita a mano
# se queda vieja, cada linea se COMPRUEBA: que el que llama exista, que de
# verdad tenga una llamada indirecta, y que el llamado exista. Si algo de eso
# deja de ser cierto, esto falla en vez de callarse.
#
GANCHOS = {
    # sdr_rx.c: s_block_hook(half). main.c lo pone con sdr_rx_set_block_hook()
    # a uno de los dos demoduladores segun el modo.
    'DMA0_Channel3_IRQHandler': ['demod_am_process_raw', 'demod_wfm_process_raw'],
}

def salir(msg, codigo=1):
    print(msg); sys.exit(codigo)

if not os.path.exists(ELF):
    salir("pila.py: no encuentro %s - compila primero" % ELF)

# --- 1) coste de marco de cada funcion, de los .su de gcc ---
coste, dinamicas = {}, set()
sus = glob.glob(os.path.join(SUD, '**', '*.su'), recursive=True)
if not sus:
    salir("pila.py: no hay ningun .su en %s.\n"
          "         Hace falta -fstack-usage en CFLAGS (esta en avisos.mk)." % SUD)
for f in sus:
    for ln in open(f, encoding='utf-8', errors='replace'):
        p = ln.rstrip('\n').split('\t')
        if len(p) < 3: continue
        nom = p[0].rsplit(':', 1)[-1]
        try: n = int(p[1])
        except ValueError: continue
        coste[nom] = max(coste.get(nom, 0), n)
        if 'dynamic' in p[2]: dinamicas.add(nom)

# --- 2) grafo de llamadas del binario ya enlazado ---
dis = subprocess.run([GCC + 'objdump', '-d', ELF], capture_output=True, text=True).stdout
llama = collections.defaultdict(set)
marco = {}
indirectas = collections.Counter()
act = None
for ln in dis.splitlines():
    m = re.match(r'^[0-9a-f]+ <(.+)>:$', ln)
    if m:
        act = m.group(1); marco.setdefault(act, 0); continue
    if act is None: continue
    m = re.search(r'\sblx?\s+[0-9a-f]+ <([^+>]+)', ln)
    if m:
        llama[act].add(m.group(1)); continue
    if re.search(r'\sblx?\s+r\d', ln):
        indirectas[act] += 1
    m = re.search(r'\ssub(?:\.w)?\s+sp, (?:sp, )?#(\d+)', ln)
    if m:
        marco[act] = marco.get(act, 0) + int(m.group(1))
    m = re.search(r'\s(?:push|stmdb\s+sp!)\s*.*\{([^}]*)\}', ln)
    if m:
        marco[act] = marco.get(act, 0) + 4 * len([x for x in m.group(1).split(',') if x.strip()])

def c(f):
    return coste[f] if f in coste else marco.get(f, 0)

# --- 2b) los ganchos, comprobados uno a uno antes de creerselos ---
for quien, adonde in GANCHOS.items():
    if quien not in marco:
        salir("pila.py: GANCHOS nombra '%s' y no esta en el binario.\n"
              "         O se ha renombrado o ya no se enlaza: arregla la tabla."
              % quien)
    if indirectas.get(quien, 0) == 0:
        salir("pila.py: GANCHOS dice que '%s' llama por puntero, y en el\n"
              "         binario no hay ninguna llamada indirecta ahi. La tabla\n"
              "         se ha quedado vieja." % quien)
    for destino in adonde:
        if destino not in marco:
            salir("pila.py: el gancho de '%s' apunta a '%s', que no esta en el\n"
                  "         binario. Arregla la tabla." % (quien, destino))
        llama[quien].add(destino)

# --- 3) profundidad maxima por camino ---
memo, enpila, ciclos, siguiente = {}, set(), set(), {}
def prof(f):
    if f in memo: return memo[f]
    if f in enpila:
        ciclos.add(f); return 0
    enpila.add(f)
    peor, hijo = 0, None
    for g in sorted(llama.get(f, ())):
        d = prof(g)
        if d > peor: peor, hijo = d, g
    enpila.discard(f)
    siguiente[f] = hijo
    memo[f] = c(f) + peor
    return memo[f]

# OJO con el filtro: "DMA0_Channel3_IRQHandler" NO acaba en "_Handler" (acaba
# en "QHandler"), asi que un endswith('_Handler') se deja fuera justo la
# interrupcion que mas trabajo hace. Lo escribi asi la primera vez y la
# herramienta dijo tranquilamente "peor interrupcion: 0". Una comprobacion
# que mira donde no es, no avisa: da el visto bueno. De ahi el endswith
# pelado, y Reset_Handler fuera porque ese ES el camino de main, no una
# interrupcion que caiga encima.
entradas = sorted(f for f in marco
                  if f == 'main' or (f.endswith('Handler') and f != 'Reset_Handler'))
if 'main' not in entradas:
    salir("pila.py: no encuentro main() en %s" % ELF)
res = sorted(((prof(f), f) for f in entradas), reverse=True)

principal = next(d for d, f in res if f == 'main')
isr_d, isr_n = max([(d, f) for d, f in res if f != 'main'] + [(0, '-')])
total = principal + isr_d + APILADO_HW

# --- 4) cuanta pila hay DE VERDAD, sacada del propio .elf ---
sim = {}
for ln in subprocess.run([GCC + 'nm', ELF], capture_output=True, text=True).stdout.splitlines():
    p = ln.split()
    if len(p) == 3: sim[p[2]] = int(p[0], 16)
if '_estack' not in sim or 'end' not in sim:
    salir("pila.py: al .elf le faltan _estack o end - no se cual es el limite")
# El monton tampoco se escribe aqui: el enlazador deja _Min_Heap_Size en la
# tabla de simbolos, asi que se lee de ahi. Si un dia cambia en el script del
# enlazador, esto se entera solo en vez de mentir por 1 kB.
MONTON = sim.get('_Min_Heap_Size', 0)
sitio = sim['_estack'] - sim['end'] - MONTON

print("PILA (peor caso, sobre el binario enlazado)")
print("  camino de main .......... %6d" % principal)
print("  peor interrupcion ....... %6d  (%s)" % (isr_d, isr_n))
print("  apilado del hardware .... %6d" % APILADO_HW)
print("  TOTAL ................... %6d bytes" % total)
print("  sitio real .............. %6d bytes  (_estack - end - %d de monton)" % (sitio, MONTON))
if sitio > 0:
    print("  margen .................. %6d bytes  (%.0f%%)" % (sitio - total, 100.0 * (sitio - total) / sitio))
print("\n  ruta del peor caso:")
f = 'main'
while f:
    print("    %6d  %s%s" % (c(f), f, '   <- DINAMICO, coste real desconocido' if f in dinamicas else ''))
    f = siguiente.get(f)
if ciclos:
    print("\n  AVISO: recursion en %s - estos caminos no quedan acotados." % ', '.join(sorted(ciclos)))
if indirectas:
    print("\n  AVISO: llamadas por puntero a funcion (invisibles para esto) en:")
    # TODAS, no las cinco primeras. Cuando solo salian cinco, la unica que
    # importaba -la de la interrupcion del audio- quedaba fuera de la lista
    # por orden alfabetico del azar, y eso convertia el aviso en un adorno.
    for f, n in indirectas.most_common():
        print("    %-32s (%d)%s" % (f, n, "   <- resuelta en GANCHOS"
                                    if f in GANCHOS else ""))
if total > sitio:
    salir("\nFALLA: la pila se sale por %d bytes.\n"
          "Ver el comentario de los borradores en User/spi_flash.c: esto ya paso una vez."
          % (total - sitio))
print("\nOK: cabe.")
