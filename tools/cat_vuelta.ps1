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
    anterior, cuantas vueltas fueron, cuantos clics de mando hubo y la
    carga de audio. Este guion pregunta dos veces por segundo, separa las
    tandas en las que el mando se movio de las que no, y dice QUE TRAMO
    CRECE.

    USO:
        1. En la radio: Informacion -> pagina 4 -> "Puerto serie USB",
           un toque. La fila tiene que poner "abierto N".
        2. Enchufa el cable.
        3. powershell -ExecutionPolicy Bypass -File tools\cat_vuelta.ps1
        4. Mientras corre, la tanda que de verdad contesta la pregunta:

             - un rato quieto y un rato GIRANDO, con el reductor de ruido
               ENCENDIDO;
             - lo mismo con el reductor APAGADO;
             - y al final, encenderlo y apagarlo varias veces.

           No hace falta mirar la pantalla: la radio dice en cada tanda si
           el reductor estaba puesto, y la tabla del final sale en cuatro
           casillas -quieto/girando por NR si/no-.

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

try {
    $p.Open()
    $p.DiscardInBuffer()

    # La primera consulta solo sirve para poner los contadores a cero.
    $p.Write("DG;"); Start-Sleep -Milliseconds 300; $p.ReadExisting() | Out-Null

    $fin = (Get-Date).AddSeconds($Segundos)
    while ((Get-Date) -lt $fin) {
        Start-Sleep -Milliseconds 500
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
        $muestras += ,(New-Object psobject -Property $m)

        $marca = if ($m["detentes"] -gt 0) { "GIRANDO" } else { "quieto " }
        $marca = $marca + $(if ($m["nr"] -eq 1) { " NR " } else { " -- " })
        "{0}  vuelta {1,6}   dib {2,6}   frq {3,5}   rgl {4,5}   lot {5,5}   man {6,5}   carga {7,3} %   clics {8,3}" -f `
            $marca, $m["vuelta"], $m["dib"], $m["frq"], $m["rgl"], $m["lot"], $m["man"], $m["carga"], $m["detentes"]
    }
}
catch {
    Write-Host "No se pudo: $($_.Exception.Message)"
    Write-Host "  Si dice que esta ocupado, cierra lo que tenga el puerto abierto."
    exit 1
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
# CUATRO CASILLAS Y NO DOS: quieto/girando POR reductor de ruido si/no.
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
Write-Host ("  tandas:  quieto sin NR {0}   quieto con NR {1}   girando sin NR {2}   girando con NR {3}" -f $qs.Count, $qn.Count, $gs.Count, $gn.Count)
Write-Host ""
Write-Host ("  {0,-8} {1,9} {2,9} {3,9} {4,9}" -f "tramo", "qui/--", "qui/NR", "gir/--", "gir/NR")
Write-Host "  --------------------------------------------------"
foreach ($t in (@("vuelta","carga","clics") + $TRAMOS)) {
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
}

Veredicto "sin NR" $qs $gs
Veredicto "con NR" $qn $gn

try {
    $muestras | Export-Csv -Path $csv -NoTypeInformation -Encoding UTF8
    Write-Host ""
    Write-Host "  Guardado en $csv"
}
catch {
    Write-Host ""
    Write-Host ("  (no se pudo guardar el CSV: {0})" -f $_.Exception.Message)
}
Write-Host "--------------------------------------------------------------"
Write-Host ""
