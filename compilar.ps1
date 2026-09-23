# =============================================================================
#  Compila el firmware del DEEPSDR 101 SIN necesidad de make.
#
#  Uso, desde la raiz del repositorio:
#
#      .\compilar.ps1                 el FIRMWARE (el receptor de verdad)
#      .\compilar.ps1 -Tarjeta        la TARJETA DE PRUEBA de pantalla
#      .\compilar.ps1 -Uart           anade la salida por el UART de diagnostico
#      .\compilar.ps1 -Limpiar        borra lo compilado y sale
#
#  Lo unico que hace falta instalado es el compilador ARM (arm-none-eabi-gcc).
#  Si PowerShell se niega a ejecutar el script:
#
#      powershell -ExecutionPolicy Bypass -File .\compilar.ps1
#
#  Genera, dentro de la carpeta de compilacion correspondiente:
#      firmware.elf / testcard.elf   con simbolos, para depurar
#      firmware.bin / testcard.bin   imagen cruda, se graba en 0x08020000
#      update4.bin                   para el bootloader, listo para copiar
#                                    tal cual, sin renombrar nada
# =============================================================================

param(
    [switch]$Tarjeta,
    [switch]$Uart,
    [switch]$Limpiar
)

$ErrorActionPreference = 'Stop'
$raiz = $PSScriptRoot

if ($Tarjeta) {
    $build  = Join-Path $raiz 'build_testcard'
    $nombre = 'testcard'
} else {
    $build  = Join-Path $raiz 'build'
    $nombre = 'firmware'
}

if ($Limpiar) {
    foreach ($d in @('build','build_testcard')) {
        $p = Join-Path $raiz $d
        if (Test-Path $p) { Remove-Item -Recurse -Force $p }
    }
    Write-Host "Limpiado." -ForegroundColor Green
    exit 0
}

# --- localizar el compilador --------------------------------------------------
function Buscar-Gcc {
    $c = Get-Command 'arm-none-eabi-gcc.exe' -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    $candidatos = @(
        "$env:ProgramFiles\Arm GNU Toolchain*\*\bin\arm-none-eabi-gcc.exe",
        "${env:ProgramFiles(x86)}\Arm GNU Toolchain*\*\bin\arm-none-eabi-gcc.exe",
        "${env:ProgramFiles(x86)}\GNU Arm Embedded Toolchain\*\bin\arm-none-eabi-gcc.exe",
        "$env:ProgramFiles\GNU Arm Embedded Toolchain\*\bin\arm-none-eabi-gcc.exe",
        "$env:USERPROFILE\scoop\apps\gcc-arm-none-eabi\current\bin\arm-none-eabi-gcc.exe",
        "C:\msys64\mingw64\bin\arm-none-eabi-gcc.exe",
        "C:\ProgramData\chocolatey\bin\arm-none-eabi-gcc.exe"
    )
    foreach ($patron in $candidatos) {
        $hit = Get-ChildItem -Path $patron -ErrorAction SilentlyContinue |
               Sort-Object FullName -Descending | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

$gcc = Buscar-Gcc
if (-not $gcc) {
    Write-Host ""
    Write-Host "No encuentro arm-none-eabi-gcc." -ForegroundColor Red
    Write-Host ""
    Write-Host "Descargalo de la pagina de Arm (busca 'Arm GNU Toolchain downloads'),"
    Write-Host "coge el instalador de Windows de la variante 'arm-none-eabi', y MARCA"
    Write-Host "la casilla de anadirlo al PATH durante la instalacion."
    Write-Host ""
    exit 1
}

$binDir  = Split-Path $gcc
$objcopy = Join-Path $binDir 'arm-none-eabi-objcopy.exe'
$size    = Join-Path $binDir 'arm-none-eabi-size.exe'

Write-Host "Compilador: $gcc" -ForegroundColor Cyan
& $gcc --version | Select-Object -First 1 | Write-Host
Write-Host ("Objetivo:   {0}" -f $(if ($Tarjeta) {'tarjeta de prueba'} else {'firmware'})) -ForegroundColor Cyan

# --- opciones, calcadas del Makefile -----------------------------------------
$mcu = @('-mcpu=cortex-m4','-mthumb','-mfpu=fpv4-sp-d16','-mfloat-abi=hard')

# HXTAL_VALUE tiene que ser el cristal real de la placa: 12,288 MHz, no los
# 25 MHz que asume la rama de reloj de serie de GigaDevice. Si esto no cuadra,
# SystemCoreClock sale mal y el SysTick de 1 ms no mide 1 ms.
$uartOn = if ($Uart) { '1' } else { '0' }
$defs = @('-DGD32F450','-DUSE_STDPERIPH_DRIVER','-DHXTAL_VALUE=12288000U',
          "-DDEBUG_UART_ENABLED=$uartOn")

$inc = @('-ICMSIS/Include','-ICMSIS/GD/GD32F4xx/Include','-IFirmware/Include','-IUser')

if ($Tarjeta) {
    $inc  += '-Itestcard'
} else {
    # ARM_MATH_DSP: el Cortex-M4 tiene la extension DSP de verdad, asi que
    # CMSIS-DSP usa los intrinsecos reales en vez de su emulacion por software.
    $defs += @('-DARM_MATH_DSP=1','-DARM_MATH_CM4')
    $inc  += @('-ICMSIS/DSP/Include','-ICMSIS/DSP/PrivateInclude')
}

$cflags = $mcu + $defs + $inc + @('-Wall','-O2','-g3',
                                  '-ffunction-sections','-fdata-sections')

# --- lista de fuentes ---------------------------------------------------------
if ($Tarjeta) {
    $fuentes = @(
        'testcard/testcard_main.c',
        'testcard/gfx2.c',
        'User/rm68120_exmc.c',   # solo los accesos al bus; su init NO se llama
        'User/gfx.c',            # lo usan touch.c y la pantalla de tipografia
        'User/touch.c',
        'User/touch_calib.c',
        'User/encoder.c',
        'User/backlight.c',
        'User/debug_uart.c',
        'CMSIS/GD/GD32F4xx/Source/system_gd32f4xx.c'
    )
} else {
    $fuentes = @(Get-ChildItem (Join-Path $raiz 'User/*.c') |
                 ForEach-Object { "User/$($_.Name)" })
    $fuentes += 'CMSIS/GD/GD32F4xx/Source/system_gd32f4xx.c'
    # solo las funciones de CMSIS-DSP que se usan, no la libreria entera
    $fuentes += @(
        'CMSIS/DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_f32.c',
        'CMSIS/DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f32.c',
        'CMSIS/DSP/Source/ComplexMathFunctions/arm_cmplx_mag_f32.c',
        'CMSIS/DSP/Source/FilteringFunctions/arm_fir_f32.c',
        'CMSIS/DSP/Source/FilteringFunctions/arm_fir_init_f32.c',
        'CMSIS/DSP/Source/FilteringFunctions/arm_fir_decimate_f32.c',
        'CMSIS/DSP/Source/FilteringFunctions/arm_fir_decimate_init_f32.c',
        'CMSIS/DSP/Source/FilteringFunctions/arm_fir_interpolate_f32.c',
        'CMSIS/DSP/Source/FilteringFunctions/arm_fir_interpolate_init_f32.c'
    )
}
$fuentes += (Get-ChildItem (Join-Path $raiz 'Firmware/Source/*.c') |
             ForEach-Object { "Firmware/Source/$($_.Name)" })

$asm = 'CMSIS/GD/GD32F4xx/Source/GCC/startup_gd32f450_470.S'

New-Item -ItemType Directory -Force -Path $build | Out-Null

# --- compilar -----------------------------------------------------------------
$objs = @()
$avisos = 0
$n = 0
foreach ($f in $fuentes) {
    $ruta = Join-Path $raiz $f
    if (-not (Test-Path $ruta)) { Write-Host "FALTA: $f" -ForegroundColor Red; exit 1 }
    $obj = Join-Path $build ([IO.Path]::GetFileNameWithoutExtension($f) + '.o')
    $objs += $obj
    $n++
    Write-Progress -Activity "Compilando" -Status $f -PercentComplete (100*$n/($fuentes.Count+1))
    $salida = & $gcc -c @cflags $ruta -o $obj 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Progress -Activity "Compilando" -Completed
        Write-Host "Error compilando $f" -ForegroundColor Red
        $salida | Write-Host
        exit 1
    }
    if ($salida) {
        $salida | Write-Host -ForegroundColor Yellow
        $avisos += ($salida | Select-String 'warning:').Count
    }
}

$objStartup = Join-Path $build 'startup_gd32f450_470.o'
& $gcc -x assembler-with-cpp -c @mcu -g3 (Join-Path $raiz $asm) -o $objStartup
if ($LASTEXITCODE -ne 0) { Write-Host "Error compilando el arranque" -ForegroundColor Red; exit 1 }
$objs += $objStartup
Write-Progress -Activity "Compilando" -Completed

# --- enlazar ------------------------------------------------------------------
$elf = Join-Path $build "$nombre.elf"
$ld  = Join-Path $raiz 'GD32F450VE_FLASH.ld'
$map = Join-Path $build "$nombre.map"

# -lm: lo necesita atan2f() del discriminador de FM en demod_am.c
$ldflags = $mcu + @('-specs=nano.specs','-specs=nosys.specs',"-T$ld",
                    "-Wl,-Map=$map,--cref",'-Wl,--gc-sections')
if (-not $Tarjeta) { $ldflags += '-lm' }

& $gcc @objs @ldflags -o $elf
if ($LASTEXITCODE -ne 0) { Write-Host "Error enlazando" -ForegroundColor Red; exit 1 }

$binOut = Join-Path $build "$nombre.bin"
& $objcopy -O binary -S $elf $binOut
& $objcopy -O ihex $elf (Join-Path $build "$nombre.hex")

Write-Host ""
& $size $elf

if ($avisos -gt 0) {
    Write-Host ""
    Write-Host "$avisos avisos del compilador." -ForegroundColor Yellow
} else {
    Write-Host ""
    Write-Host "Sin avisos del compilador." -ForegroundColor Green
}

# --- update4.bin --------------------------------------------------------------
# Mismo formato que el objetivo 'update4' del Makefile: el binario relleno
# hasta 0x40000 (256 KB) mas la firma de 8 bytes que comprueba el bootloader.
# Se deja con ese nombre exacto y dentro de la carpeta de compilacion, para
# poder copiarlo tal cual sin renombrar y sin pisar el update4.bin que viene
# en el repositorio.
$fw = [IO.File]::ReadAllBytes($binOut)
if ($fw.Length -gt 0x40000) {
    Write-Host "La imagen pasa de 256 KB: no cabe en update4.bin." -ForegroundColor Red
    Write-Host "Por ST-Link si se puede grabar; por el bootloader no." -ForegroundColor Red
    exit 1
}
$magic  = [byte[]](0x8f,0x25,0xc8,0x65,0x59,0x9c,0x55,0x31)
$salida = New-Object byte[] (0x40000 + 8)
[Array]::Copy($fw, $salida, $fw.Length)              # el resto queda a cero
[Array]::Copy($magic, 0, $salida, 0x40000, 8)
$u4 = Join-Path $build 'update4.bin'
[IO.File]::WriteAllBytes($u4, $salida)

# --- verificacion de la imagen ------------------------------------------------
$sp  = [BitConverter]::ToUInt32($fw, 0)
$rst = [BitConverter]::ToUInt32($fw, 4)

# La app vive entre 0x08020000 (justo tras los 128 KB del bootloader) y el
# final de la flash. El Reset_Handler cae donde lo ponga el enlazador dentro
# de ese rango, NO necesariamente en los primeros 64 KB: el firmware completo
# lo pone en 0x0803xxxx. Comprobar solo el prefijo 0x0802 daba un falso fallo.
$appLo = 0x08020000
$appHi = 0x08080000
$okSp  = ($sp -eq 0x10010000)              # pila en TCMRAM: lo exige el bootloader
$okRst = ((($rst -band 0xFFFFFFFE) -ge $appLo) -and
          (($rst -band 0xFFFFFFFE) -lt $appHi) -and
          (($rst -band 1) -eq 1))          # bit thumb
$okU4  = ($salida.Length -eq 0x40008)
$okMag = ([BitConverter]::ToString($salida[0x40000..0x40007]) -replace '-','') -ieq '8F25C865599C5531'

Write-Host ""
Write-Host "Verificacion de la imagen" -ForegroundColor Cyan
Write-Host ("  SP inicial     0x{0:X8}          {1}" -f $sp,  $(if($okSp) {"OK"} else {"FALLO"}))
Write-Host ("  Reset_Handler  0x{0:X8}          {1}" -f $rst, $(if($okRst){"OK"} else {"FALLO"}))
Write-Host ("  update4.bin    {0} bytes       {1}" -f $salida.Length, $(if($okU4){"OK"} else {"FALLO"}))
Write-Host ("  firma en 0x40000                  {0}" -f $(if($okMag){"OK"} else {"FALLO"}))
Write-Host ("  imagen         {0} bytes" -f $fw.Length)
Write-Host ""

if ($okSp -and $okRst -and $okU4 -and $okMag) {
    Write-Host "Listo para grabar:" -ForegroundColor Green
    Write-Host "   $u4"
} else {
    Write-Host "La imagen NO pasa la verificacion: no la grabes." -ForegroundColor Red
    exit 1
}
