# Remote control for the LED cube.
#   .\cubectl.ps1 status
#   .\cubectl.ps1 scene=plasma     (pin one scene; names are listed by "status")
#   .\cubectl.ps1 auto | next | off | on | toggle | reload
#   .\cubectl.ps1 brightness=30
param(
    [Parameter(Position = 0)][string]$Command = "status",
    [string]$Address = "192.168.50.66",
    [int]$Port = 7777
)

function Send-CubeCommand([string]$cmd) {
    $udp = New-Object System.Net.Sockets.UdpClient
    try {
        $udp.Client.ReceiveTimeout = 1500
        $bytes = [Text.Encoding]::ASCII.GetBytes("C $cmd")
        [void]$udp.Send($bytes, $bytes.Length, $Address, $Port)
        $from = New-Object System.Net.IPEndPoint ([System.Net.IPAddress]::Any, 0)
        [Text.Encoding]::ASCII.GetString($udp.Receive([ref]$from))
    } catch {
        throw "no reply from ${Address}:$Port - is the ledcube service running?"
    } finally {
        $udp.Close()
    }
}

try {
    if ($Command -eq "toggle") {
        # toggle is done here on the PC: read the power state, then send the opposite
        $Command = if ((Send-CubeCommand "status") -match "power=on") { "off" } else { "on" }
    }
    Send-CubeCommand $Command
} catch {
    Write-Error $_
}
