<#
    LA VUELTA DEL BUCLE, MEDIDA POR EL CABLE Y NO POR UNA FOTO.

    Seis versiones persiguiendo por que la cascada se frena al girar el
    mando, y todas las medidas han llegado leyendo numeros diminutos de
    una foto de la pantalla de Informacion. Ademas de incomodo, es la
    medida equivocada: esa fila enseña una media larga o un record, nunca
    la vuelta a vuelta, y no se puede mirar MIENTRAS se gira porque en esa
    pagina el mando mueve el cursor.

    Esto lo arregla. La radio lleva desde la V6.41 una orden de CAT, "DG;",
    que devuelve la media de cada tramo del bucle desde la consulta
    anterior, cuantas vueltas fueron, cuanto se movio el dial y la carga de
    audio. Este guion pregunta dos veces por segundo, separa las tandas en
    las que el dial se movio de las que no, y dice QUE TRAMO CRECE.

    Y DESDE LA V6.54, tambien "DD;": el desglose de DENTRO del dibujo. Hizo
    falta porque la primera tanda con el dedo dijo que la vuelta pasa de
    14-22 ms quieto a 38-45 ms arrastrando, y que de los veintitantos
    milisegundos que crece, trece se los lleva el tramo `dib` -el tactil
    son 2,7 y repintar la frecuencia 2,9-. Y `dib` es UN hueco del reparto,
    asi que decir "es el dibujo" no resuelve nada: el dibujo son la
    transformada, el marco, la traza, el empuje al panel y la cascada.

    "DD;" los da por separado, y con ellos el estado del atajo del
    repintado parcial -bandas saltadas de bandas totales, y cuantas veces
    se tiro-, cuantas veces se bajo de verdad al panel tactil (775 us cada
    una) y cuantas veces se repinto el numero del dial, la regla y la
    chapa. Las dos ordenes se preguntan en la misma tanda y salen en la
    misma fila: una tabla, no dos que haya que cuadrar a mano.

    USO:
        1. En la radio: Informacion -> pagina 4 -> "Puerto serie USB",
           un toque. La fila tiene que poner "abierto N".
        2. Enchufa el cable.
        3. powershell -ExecutionPolicy Bypass -File tools\cat_vuelta.ps1
        4. Mientras corre, basta con esto, y en este orden:

             - QUINCE SEGUNDOS SIN TOCAR NADA;
             - QUINCE SEGUNDOS ARRASTRANDO EL DEDO por el espectro, de
               lado a lado, sin soltar;
             - quince segundos quieto otra vez;
             - y, si da tiempo, quince GIRANDO EL MANDO, para tener las
               dos maneras de mover el dial en la misma tanda.

           No hace falta mirar la pantalla ni apuntar nada. La radio dice
           en cada tanda si el reductor de ruido estaba puesto, y el
           arrastre con el dedo cuenta igual que girar el mando: la linea
           viva pone MOVIENDO y el resumen lo manda a la columna "mov".

        5. Al final sale TODO: el reparto de la vuelta en dieciocho
           tramos, el desglose de dentro del dibujo, el estado del atajo
           del repintado parcial, cuantas veces se bajo al panel tactil y
           cuantas veces se repinto cada cosa. Y un CSV al lado con todas
           las muestras.

        powershell -ExecutionPolicy Bypass -File tools\cat_vuelta.ps1 COM5 60

    Deja un CSV con todas las muestras al lado, por si hace falta mirarlo
    con calma, y escribe el resumen por pantalla.

    PowerShell trae de serie System.IO.Ports.SerialPort: no hace falta
    instalar nada.
#>
param([string]$Puerto = "", [int]$Segundos = 90)

$ErrorActionPreference = "Stop"

# El orden es el de k_tramo_nombre en User/main.c. Si alli se añade uno,
# aqui tambien: la respuesta trae CAT_DIAG_TRAMOS huecos y ni uno mas.
$TRAMOS = @("cat","smt","syn","cab","bus","emi","fra","mod","tc3",
            "dib","tac","man","frq","rgl","lot","dec","pol","res")

# Los huecos de "DD;", en el orden de cat_dibujo_t. El primero es el numero
# de cuadros y los catorce siguientes van de cinco cifras. Si alli se añade
# uno, aqui tambien.
$DIBUJO = @("cuadros","fft","chr","esp","emp","cas",
            "salta","bandas","inval","lect",
            "frqn","rgln","chrn","lotd","lotn","chapan","arrn","arru",
            "wflin","estado")

$QUE2 = @{
    "fft"    = "la transformada"
    "chr"    = "la regla, el marco y las etiquetas"
    "esp"    = "LA TRAZA del espectro"
    "emp"    = "empujar al panel"
    "cas"    = "la cascada"
    "salta"  = "bandas que el atajo se salto (por cuadro)"
    "bandas" = "bandas que habia (por cuadro)"
    "inval"  = "veces que se tiro el atajo del repintado parcial"
    "lect"   = "bajadas de verdad al panel tactil (775 us cada una)"
    "frqn"   = "repintados del numero del dial"
    "rgln"   = "repintados de la fila de etiquetas"
    "chrn"   = "repintados de la regla del marco"
    "lotd"   = "reprogramar la sintonia DESDE EL ARRASTRE (us por cuadro)"
    "lotn"   = "cuantas veces lo reprograma el arrastre"
    "chapan" = "repintados de la chapa"
    "arrn"   = "pasos de arrastre con el dedo"
    "arru"   = "lo que cuesta el arrastre entero (us por cuadro)"
    "wflin"  = "lineas volcadas a la cascada"
    "estado" = "bit0 NCO, bit1 menu, bit2 dedo, bit3 panel entero, bits4-7 zoom, bit8 rotulos, bit9 NR"
}

$QUE = @{
    "cat" = "puerto serie y CAT"
    "smt" = "el medidor de S"
    "syn" = "rellenar la ficha de la cabecera"
    "cab" = "componer la cabecera"
    "bus" = "empujar la cabecera al panel"
    "emi" = "el cartel de quien emite"
    "fra" = "el resto de la franja de arriba"
    "mod" = "repintados de cambio de modo"
    "tc3" = "el tactil que despacha"
    "dib" = "EL DIBUJO (espectro y cascada)"
    "tac" = "las otras miradas al tactil"
    "man" = "el mando, sin los tres de abajo"
    "frq" = "repintar el numero de la frecuencia"
    "rgl" = "la fila de etiquetas de la regla"
    "lot" = "reprogramar la sintonia (oscilador, mezclador, espejo)"
    "dec" = "los decodificadores"
    "pol" = "captura, volcado, fax, SSTV, RDS"
    "res" = "lo que no cae en ningun tramo"
}

function Lista-Puertos {
    [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
}

if ($Puerto -eq "") {
    $todos = @(Lista-Puertos)
    if ($todos.Count -eq 0) {
        Write-Host "No hay ningun puerto serie."
        Write-Host "  - Enciende el puerto en la radio: Informacion -> pagina 4"
        Write-Host "    -> 'Puerto serie USB', un toque."
        Write-Host "  - Enchufa el cable."
        exit 1
    }
    if ($todos.Count -gt 1) {
        Write-Host "Hay varios puertos. Dime cual:"
        $todos | ForEach-Object { Write-Host "    $_" }
        Write-Host "`n  powershell -ExecutionPolicy Bypass -File tools\cat_vuelta.ps1 COM5"
        exit 1
    }
    $Puerto = $todos[0]
}

$csv = "vuelta_{0}.csv" -f (Get-Date -Format "yyyyMMdd_HHmmss")

Write-Host "`nPuerto: $Puerto   -   $Segundos segundos   -   sale en $csv"
Write-Host "Quieto / girando con el NR puesto, luego lo mismo sin el,"
Write-Host "y al final enciendelo y apagalo varias veces.`n"

$p = New-Object System.IO.Ports.SerialPort $Puerto,9600,None,8,one
$p.ReadTimeout  = 1000
$p.WriteTimeout = 1000

$muestras = @()
$cab = $false

#
# EL CSV SE ESCRIBE SEGUN SE TOMA, NO AL FINAL - 03/11/2026.
#
# Estaba al final del todo, detras del resumen, y el usuario corta el guion
# en cuanto tiene las lineas que queria: tres tandas seguidas y ni un CSV en
# el disco. Un fichero que solo existe si el programa termina bien no es un
# registro, es una promesa.
#
# Ahora cada muestra se añade en cuanto se toma. Si se corta, lo tomado
# hasta ese momento esta en el disco.
#
function Apunta($m) {
    $fila = New-Object psobject -Property $m
    if (-not $script:cab) {
        $fila | Export-Csv -Path $script:csv -NoTypeInformation -Encoding UTF8
        $script:cab = $true
    } else {
        $fila | Export-Csv -Path $script:csv -NoTypeInformation -Encoding UTF8 -Append
    }
    return $fila
}

try {
    $p.Open()
    $p.DiscardInBuffer()

    # La primera consulta solo sirve para poner los contadores a cero.
    $p.Write("DG;"); Start-Sleep -Milliseconds 200; $p.ReadExisting() | Out-Null
    $p.Write("DD;"); Start-Sleep -Milliseconds 200; $p.ReadExisting() | Out-Null

    $fin = (Get-Date).AddSeconds($Segundos)
    while ((Get-Date) -lt $fin) {
        Start-Sleep -Milliseconds 500

        # --- el reparto de la vuelta ---
        $p.DiscardInBuffer()
        $p.Write("DG;")
        Start-Sleep -Milliseconds 120
        $r = ($p.ReadExisting() -replace "[^0-9A-Za-z;]","")
        # El ancho se saca de la propia lista de tramos: si un dia se
        # parte otro, aqui no hay nada que tocar salvo $TRAMOS.
        if ($r -notmatch ("^DG(\d{" + (17 + 5*$TRAMOS.Count) + "});$")) { continue }
        $d = $Matches[1]

        $m = [ordered]@{}
        $m["vueltas"]  = [int]$d.Substring(0,4)
        $m["detentes"] = [int]$d.Substring(4,4)
        $m["nr"]       = [int]$d.Substring(8,1)
        $m["carga"]    = [int]$d.Substring(9,3)
        $m["vuelta"]   = [int]$d.Substring(12,5)
        for ($k = 0; $k -lt $TRAMOS.Count; $k++) {
            $m[$TRAMOS[$k]] = [int]$d.Substring(17 + 5*$k, 5)
        }
        if ($m["vueltas"] -eq 0) { continue }

        # --- y lo de DENTRO del dibujo, que es donde se va el tiempo ---
        #
        # Se pregunta en la misma tanda y se mete en la misma fila: una
        # tabla con las dos mitades y no dos tablas que hay que cuadrar a
        # mano. Si la radio es vieja y no conoce "DD;" contesta "?;" y los
        # huecos se quedan a cero, sin romper nada.
        $p.DiscardInBuffer()
        $p.Write("DD;")
        Start-Sleep -Milliseconds 120
        $r2 = ($p.ReadExisting() -replace "[^0-9A-Za-z;]","")
        foreach ($c in $DIBUJO) { $m[$c] = 0 }
        # 4 cifras de cuadros y cinco por cada uno de los OTROS huecos:
        # "cuadros" es el primero de la lista y no lleva cinco.
        if ($r2 -match ("^DD(\d{" + (4 + 5*($DIBUJO.Count - 1)) + "});$")) {
            $d2 = $Matches[1]
            $m["cuadros"] = [int]$d2.Substring(0,4)
            for ($k = 1; $k -lt $DIBUJO.Count; $k++) {
                $m[$DIBUJO[$k]] = [int]$d2.Substring(4 + 5*($k-1), 5)
            }
        }

        $muestras += ,(Apunta $m)

        $marca = if ($m["detentes"] -gt 0) { "MOVIENDO" } else { "quieto  " }
        $marca = $marca + $(if ($m["nr"] -eq 1) { " NR " } else { " -- " })
        # El estado, en letras: es lo que luego hace falta para saber si
        # dos tandas se pueden comparar.
        $e = [int]$m["estado"]
        $est = $(if ($e -band 1) { "NCO" } else { "---" })
        $est = $est + $(if ($e -band 8) { " PE" } else { "   " })
        $est = $est + (" z{0}" -f (($e -shr 4) -band 15))
        $est = $est + $(if ($e -band 256) { " ROT" } else { " ---" })

        "{0} v{1,4}/c{2,4} vuelta {3,6} dib {4,6} | fft {5,5} chr {6,5} esp {7,5} emp {8,5} cas {9,5} | salta {10,3}/{11,3} lect {12,4} | tac {13,5} arr {14,5} lot {15,4}x{16,3} frq {17,5} | chrn {18,3} frqn {19,3} pasos {20,4} | {21}" -f `
            $marca, $m["vueltas"], $m["cuadros"], $m["vuelta"], $m["dib"], `
            $m["fft"], $m["chr"], $m["esp"], $m["emp"], $m["cas"], `
            $m["salta"], $m["bandas"], $m["lect"], `
            $m["tac"], $m["arru"], $m["lotd"], $m["lotn"], $m["frq"], `
            $m["chrn"], $m["frqn"], $m["detentes"], $est
    }
}
catch [System.IO.IOException], [System.UnauthorizedAccessException] {
    Write-Host "No se pudo: $($_.Exception.Message)"
    Write-Host "  Si dice que esta ocupado, cierra lo que tenga el puerto abierto."
    exit 1
}
catch {
    # Cualquier otra cosa -Ctrl-C incluido- no se come las muestras: se sale
    # del bucle y se saca el resumen con lo que haya.
    Write-Host ""
    Write-Host "  (cortado: resumen con las $($muestras.Count) muestras tomadas)"
}
finally {
    if ($p.IsOpen) { $p.Close() }
}

if ($muestras.Count -eq 0) {
    Write-Host "`nNo contesto nada con la forma esperada."
    Write-Host "  Comprueba que la radio es V6.41 o mas nueva y que el puerto"
    Write-Host "  serie esta encendido (Informacion -> pagina 4)."
    exit 1
}

#
# ======================================================================
# EL RESUMEN VA PRIMERO, Y SIN MORIRSE AL PRIMER TROPIEZO - 02/11/2026.
# ======================================================================
#
# Dos tandas seguidas salieron sin tabla: los renglones de cada medio
# segundo se veian y el resumen no. Y la segunda vez ni siquiera quedo el
# CSV, o sea que la cosa se moria ANTES de guardarlo, con
# $ErrorActionPreference = "Stop" llevandose el guion entero por delante.
#
# Lo que da igual cual fuera el tropiezo: un resumen que depende de que
# todo lo demas haya ido bien no es un resumen, es un adorno. Asi que
# ahora el aviso de error pasa a "Continue" para esta parte, el CSV se
# guarda AL FINAL y en su propio try -si falla, se pierde el fichero pero
# no la tabla-, y cualquier estacazo se imprime en vez de callarse.
#
$ErrorActionPreference = "Continue"
trap { Write-Host ("  *** tropiezo en el resumen: {0}" -f $_.Exception.Message); continue }

function Media($lista, $campo) {
    if ($lista.Count -eq 0) { return 0 }
    $t = 0
    foreach ($x in $lista) { $t += [int]$x.$campo }
    return [int]($t / $lista.Count)
}

#
# CUATRO CASILLAS Y NO DOS: quieto/moviendo POR reductor de ruido si/no.
#
# Y "moviendo" es el dial movido DE CUALQUIER MANERA: los clics del mando
# y los pasos del arrastre con el dedo cuentan en el mismo contador desde
# el 03/11/2026. Antes solo contaba el mando, y un arrastre con el dedo se
# promediaba con las vueltas en las que no pasa nada: justo la casilla
# equivocada cuando lo que se persigue es el dedo. Ver cat.h.
#
# La carga de audio es el divisor del cuadro -el dibujo se pinta con lo
# que queda despues del audio- y el reductor son ocho puntos de esa carga.
# Comparar una tanda con el NR puesto contra otra sin el, sin saber cual
# era cual, es como no medir.
#
# Escrito con bucles a pelo y sin scriptblocks guardados en una tabla: la
# primera version los usaba, PowerShell se fue sin decir nada y la tabla
# no salio. Un resumen que no se imprime es el banco que no existe.
#
$qs = @(); $qn = @(); $gs = @(); $gn = @()
foreach ($x in $muestras) {
    if ($x.detentes -gt 0) {
        if ($x.nr -eq 1) { $gn += $x } else { $gs += $x }
    } else {
        if ($x.nr -eq 1) { $qn += $x } else { $qs += $x }
    }
}

Write-Host ""
Write-Host "--------------------------------------------------------------"
Write-Host ("  tandas:  quieto sin NR {0}   quieto con NR {1}   moviendo sin NR {2}   moviendo con NR {3}" -f $qs.Count, $qn.Count, $gs.Count, $gn.Count)
Write-Host ""
Write-Host ("  {0,-8} {1,9} {2,9} {3,9} {4,9}" -f "tramo", "qui/--", "qui/NR", "mov/--", "mov/NR")
Write-Host "  --------------------------------------------------"
foreach ($t in (@("vuelta","carga","detentes") + $TRAMOS)) {
    $a = Media $qs $t; $b = Media $qn $t; $c = Media $gs $t; $d = Media $gn $t
    if (($a + $b + $c + $d) -eq 0) { continue }
    Write-Host ("  {0,-8} {1,9} {2,9} {3,9} {4,9}" -f $t, $a, $b, $c, $d)
}

#
# Y LO DE DENTRO DEL DIBUJO, en la misma tabla y con las mismas casillas.
# `dib` es UN hueco del reparto de arriba y la primera tanda con el dedo
# dijo que es donde se va el tiempo; esto lo abre.
#
Write-Host ""
Write-Host "  --- dentro del dibujo (us por cuadro, y cuentas por tanda) ---"
Write-Host ("  {0,-8} {1,9} {2,9} {3,9} {4,9}" -f "", "qui/--", "qui/NR", "mov/--", "mov/NR")
Write-Host "  --------------------------------------------------"
foreach ($t in $DIBUJO) {
    $a = Media $qs $t; $b = Media $qn $t; $c = Media $gs $t; $d = Media $gn $t
    if (($a + $b + $c + $d) -eq 0) { continue }
    Write-Host ("  {0,-8} {1,9} {2,9} {3,9} {4,9}" -f $t, $a, $b, $c, $d)
}

function Veredicto($nom, $q, $g) {
    if ($q.Count -eq 0 -or $g.Count -eq 0) { return }
    $peor = ""
    $peorD = -999999
    foreach ($t in $TRAMOS) {
        $dif = (Media $g $t) - (Media $q $t)
        if ($dif -gt $peorD) { $peorD = $dif; $peor = $t }
    }
    Write-Host ""
    Write-Host ("  {0}: la vuelta pasa de {1} a {2} us" -f $nom, (Media $q "vuelta"), (Media $g "vuelta"))
    Write-Host ("      y el que mas crece es {0} ({1}), +{2} us" -f $peor, $QUE[$peor], $peorD)

    # Y si el que mas crece es el dibujo, se abre: decir "es el dibujo" no
    # sirve de nada, el dibujo son cinco cosas.
    if ($peor -eq "dib") {
        $peor2 = ""
        $peorD2 = -999999
        foreach ($t in @("fft","chr","esp","emp","cas")) {
            $dif = (Media $g $t) - (Media $q $t)
            if ($dif -gt $peorD2) { $peorD2 = $dif; $peor2 = $t }
        }
        if ($peor2 -ne "") {
            Write-Host ("      y DENTRO del dibujo, {0} ({1}), +{2} us" -f $peor2, $QUE2[$peor2], $peorD2)
        }
        $sq = Media $q "salta"; $sg = Media $g "salta"
        $bq = Media $q "bandas"
        if ($bq -gt 0) {
            Write-Host ("      atajo parcial: {0} de {1} bandas saltadas quieto, {2} moviendo" -f $sq, $bq, $sg)
            if ($sg * 2 -lt $sq) {
                Write-Host "      *** EL ATAJO SE MUERE AL MOVER: ahi esta el grueso ***"
            }
        }
        $lq = Media $q "lect"; $lg = Media $g "lect"
        Write-Host ("      panel tactil: {0} bajadas por tanda quieto, {1} moviendo (775 us cada una)" -f $lq, $lg)
        Write-Host ("      repintados por tanda moviendo: marco {0}, dial {1}, etiquetas {2}" -f `
                    (Media $g "chrn"), (Media $g "frqn"), (Media $g "rgln"))
        $ld = Media $g "lotd"; $ln = Media $g "lotn"
        Write-Host ("      reprogramar la sintonia desde el arrastre: {0} us por cuadro, {1} veces por tanda" -f $ld, $ln)
        if ($ln -gt 0) {
            Write-Host ("      (o sea unos {0} us cada vez; el MS5351 por I2C son 1.763)" -f [int]($ld * (Media $g "cuadros") / $ln))
        }
    }
}

Veredicto "sin NR" $qs $gs
Veredicto "con NR" $qn $gn

try {
    Write-Host ""
    Write-Host ("  Guardado en {0} ({1} muestras), segun se tomaban." -f $csv, $muestras.Count)
}
catch {
    Write-Host ""
    Write-Host ("  (no se pudo guardar el CSV: {0})" -f $_.Exception.Message)
}
Write-Host "--------------------------------------------------------------"
Write-Host ""
