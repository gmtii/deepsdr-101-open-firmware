<#
    PRUEBA DEL CAT SIN INSTALAR NADA.

    Pedido por el usuario, 09/10/2026: no tiene rigctl en Windows.

    PowerShell trae de serie System.IO.Ports.SerialPort, asi que esto abre
    el puerto que monta la radio y le hace las cinco preguntas que importan.
    No hace falta hamlib, ni WSJT-X, ni drivers.

    USO:
        powershell -ExecutionPolicy Bypass -File tools\cat_prueba.ps1
        powershell -ExecutionPolicy Bypass -File tools\cat_prueba.ps1 COM5

    Sin argumento busca el puerto solo. Si hay varios, los lista y para:
    adivinar cual es seria abrir un puerto que puede ser de otra cosa.
#>
param([string]$Puerto = "")

$ErrorActionPreference = "Stop"

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
        Write-Host "  - La fila tiene que poner 'abierto N'."
        exit 1
    }
    if ($todos.Count -gt 1) {
        Write-Host "Hay varios puertos. Dime cual:"
        $todos | ForEach-Object { Write-Host "    $_" }
        Write-Host "`n  powershell -ExecutionPolicy Bypass -File tools\cat_prueba.ps1 COM5"
        exit 1
    }
    $Puerto = $todos[0]
}

Write-Host "`nPuerto: $Puerto`n"

# Los baudios dan igual: detras no hay un UART, va por USB a 12 Mbit/s pase
# lo que pase. Se ponen porque el objeto los pide.
$p = New-Object System.IO.Ports.SerialPort $Puerto,9600,None,8,one
$p.ReadTimeout  = 1000
$p.WriteTimeout = 1000
$p.NewLine = ";"

try {
    $p.Open()
    $p.DiscardInBuffer()

    $preguntas = @(
        @("ID;",  "quien eres"),
        @("FA;",  "frecuencia"),
        @("MD;",  "modo"),
        @("SM0;", "el S"),
        @("IF;",  "el bloque de estado entero")
    )

    $bien = 0
    foreach ($q in $preguntas) {
        $p.DiscardInBuffer()
        $p.Write($q[0])
        Start-Sleep -Milliseconds 250
        $r = $p.ReadExisting()
        $r = $r -replace "`r","" -replace "`n",""
        if ($r -ne "") { $bien++ }
        "{0,-6} {1,-28} -> {2}" -f $q[0], $q[1], $(if ($r -eq "") { "(nada)" } else { $r })
    }

    Write-Host ""
    if ($bien -eq $preguntas.Count) {
        Write-Host "TODO CONTESTADO. La radio es un TS-2000 para cualquier"
        Write-Host "programa que use hamlib: WSJT-X, JTDX, fldigi, CQRLOG."
    } elseif ($bien -eq 0) {
        Write-Host "EL PUERTO ABRE PERO NO CONTESTA NADA."
        Write-Host "  Mira la fila de la radio: si el contador de 'abierto N'"
        Write-Host "  sigue subiendo, el cable y el USB van bien y lo que falla"
        Write-Host "  es de aqui para dentro. Si no sube, es el cable."
    } else {
        Write-Host "CONTESTA A $bien de $($preguntas.Count). Pega esto en el chat."
    }

    # Y la prueba de que ESCRIBE, no solo lee: mover el dial desde aqui.
    Write-Host "`n--- y ahora escribiendo: pongo 7.110,00 kHz ---"
    $p.DiscardInBuffer()
    $p.Write("FA00007110000;")
    Start-Sleep -Milliseconds 400
    $p.DiscardInBuffer()
    $p.Write("FA;")
    Start-Sleep -Milliseconds 250
    $r = ($p.ReadExisting() -replace "`r","" -replace "`n","")
    Write-Host "la radio dice ahora: $r"
    if ($r -like "*00007110000*") {
        Write-Host "MIRA LA PANTALLA DE LA RADIO: tiene que estar en 7.110,00"
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
