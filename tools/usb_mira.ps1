<#
    QUE HA HECHO WINDOWS CON LA RADIO.

    El puerto serie sale y la entrada de audio no. Las dos van por el mismo
    cable y el mismo descriptor, asi que Windows ha leido la funcion de
    audio y ha decidido no darle driver - o ni siquiera la ha separado.

    Esto lo dice sin instalar nada: Get-PnpDevice viene de serie en
    Windows 10 y 11.

    USO:
        powershell -ExecutionPolicy Bypass -File tools\usb_mira.ps1
#>
$ErrorActionPreference = "Continue"

# 0x28E9 / 0x018B es lo que declara esta radio. Ver usb_cdc_desc.c.
$VIDPID = "VID_28E9&PID_018B"

Write-Host "`n=== lo que Windows tiene de la radio ($VIDPID) ===`n"

$todos = Get-PnpDevice | Where-Object { $_.InstanceId -like "*$VIDPID*" }

if (-not $todos) {
    Write-Host "No hay NADA con ese identificador."
    Write-Host "  - ¿esta encendido el puerto en la radio? (abierto N)"
    Write-Host "  - ¿esta el cable puesto?"
} else {
    foreach ($d in $todos) {
        "{0,-10} {1}" -f $d.Status, $d.FriendlyName
        "           clase: {0}   servicio: {1}" -f $d.Class, $d.Service
        "           {0}" -f $d.InstanceId
        if ($d.Status -ne "OK") {
            $p = (Get-PnpDeviceProperty -InstanceId $d.InstanceId `
                  -KeyName "DEVPKEY_Device_ProblemCode" -ErrorAction SilentlyContinue).Data
            "           PROBLEMA: $p"
        }
        ""
    }
}

Write-Host "=== y las interfaces que ha separado el padre compuesto ===`n"
$hijos = Get-PnpDevice | Where-Object { $_.InstanceId -like "*$VIDPID&MI_*" }
if (-not $hijos) {
    Write-Host "NINGUNA. Windows no ha partido el equipo en funciones."
    Write-Host "  Si el COM va, es que lo esta tratando como un equipo de UNA"
    Write-Host "  sola funcion y se esta saltando el audio entero."
} else {
    foreach ($h in $hijos) {
        $mi = ($h.InstanceId -split "MI_")[1].Substring(0,2)
        "  MI_{0}  {1,-10} {2}" -f $mi, $h.Status, $h.FriendlyName
    }
    Write-Host "`n  MI_00/MI_01 son el puerto serie. MI_02/MI_03 el audio."
}

Write-Host "`n=== dispositivos de grabacion ===`n"
Get-PnpDevice -Class "AudioEndpoint","MEDIA" -ErrorAction SilentlyContinue |
    Where-Object { $_.Status -eq "OK" } |
    ForEach-Object { "  {0,-10} {1}" -f $_.Status, $_.FriendlyName }
