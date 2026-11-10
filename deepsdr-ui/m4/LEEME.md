# El banco del Cortex-M4: lo que cuesta de verdad, en instrucciones

*Puesto el 02/11/2026, y el motivo es una equivocacion mia repetida seis
veces.*

Los bancos de `sim/` miden en el PC y despues se escala a la radio "a ojo".
Eso no vale: el PC tiene cache y el microcontrolador no, el relleno escribe
en vertical y el bus del panel aqui es un `store` a RAM y alli son flancos
de EXMC. Durante seis versiones estuve persiguiendo microsegundos del PC
creyendo que sabia cuantos milisegundos eran en la radio, y no lo sabia.

Esto compila el **mismo codigo del firmware, con las opciones del firmware,
para Cortex-M4**, lo corre sobre un M4 emulado y **cuenta instrucciones**.
Lo que sale no depende de la maquina: es el codigo que produce el
compilador, igual que `tools/espectro_coste_check.py`, pero dinamico en vez
de leido del desensamblado.

## Como se usa

    make m4

Hace falta `qemu-system-arm`. Si no esta, el objetivo lo dice y se salta,
no falla: en la maquina del usuario no tiene por que estar instalado.

## Que bancos hay

La lista, con una linea por banco, esta en `deepsdr-ui/local.mk`, justo
encima de las reglas `out/m4_*.elf` — **ahi y no aqui**, para que no haya
dos listas que puedan decir cosas distintas. `make m4todo` los corre todos.

Dos de ellos merecen una nota porque no miden una pieza sino una pregunta:

- **`m4_arbitro.c`** existe porque dos bancos daban dos numeros distintos
  para la misma funcion. Le da **un segundo exacto** de audio al detector de
  STANAG y cuenta la tanda entera, que es lo unico que no admite
  interpretacion. (Los dos tenian razon: el detector cuesta el triple con la
  banda vacia.)
- **`m4_piezas.c`** desmonta una ventana de ese detector y cronometra cada
  trozo. Se trae `stanag_det.c` con un `#include` para ver las funciones
  internas, asi que lleva **copiado** el preambulo de `stanag_det_paso()`;
  contra eso tiene una red, que es comprobar que las piezas sumen la ventana
  entera medida por la puerta de siempre.

## Como se lee el numero

El reloj es el SysTick del M4 emulado con `-icount shift=0`, o sea que el
tiempo virtual avanza una unidad por instruccion. Calibrado: **un tic del
SysTick son 40 instrucciones**. De ahi a microsegundos, dividir por 200,
que son los MHz del GD32.

## Lo que NO dice

- **El bus del panel.** `panel.c` escribe en RAM; en la radio son flancos
  de EXMC con su tiempo de dato programable. O sea que los numeros son un
  SUELO: lo real es mas.
- **Las esperas de flash y la cache.** QEMU no las modela.
- **El DMA.** `gfx_blit_arranca()` aqui no cuesta nada, que es el caso
  mejor para el espectro.

Calibrado contra la radio del usuario: la traza sale 13,1 ms aqui y su
fila "Fotograma" marca unos 22, o sea **lo real es ~1,7 veces esto**. Para
comparar dos versiones del mismo codigo el factor se va y el numero vale
tal cual, que es para lo que existe.
