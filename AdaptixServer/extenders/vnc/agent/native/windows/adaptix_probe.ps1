param(
    [int]$Port = 5900
)

$ErrorActionPreference = "Stop"
$client = $null

try {
    $client = [Net.Sockets.TcpClient]::new()
    $client.ReceiveTimeout = 3000
    $client.SendTimeout = 3000
    $client.Connect("127.0.0.1", $Port)
    $stream = $client.GetStream()

    function Read-Exact {
        param([int]$Count)

        $buffer = [byte[]]::new($Count)
        $offset = 0
        while ($offset -lt $Count) {
            $read = $stream.Read($buffer, $offset, $Count - $offset)
            if ($read -le 0) {
                throw "short read while waiting for $Count byte(s)"
            }
            $offset += $read
        }
        return $buffer
    }

    $bannerBytes = Read-Exact 12
    $banner = [Text.Encoding]::ASCII.GetString($bannerBytes)
    Write-Output "[adaptix-vnc] RFB banner: $($banner.Trim())"
    if (-not $banner.StartsWith("RFB ")) {
        throw "not an RFB service"
    }

    $minor = [int]$banner.Substring(8, 3)
    if ($minor -lt 7) {
        $reply = [Text.Encoding]::ASCII.GetBytes("RFB 003.003`n")
        $stream.Write($reply, 0, $reply.Length)

        $securityBytes = Read-Exact 4
        $securityType = ([int]$securityBytes[0] -shl 24) -bor ([int]$securityBytes[1] -shl 16) -bor ([int]$securityBytes[2] -shl 8) -bor [int]$securityBytes[3]
        Write-Output "[adaptix-vnc] RFB 3.3 security type: $securityType"
        if ($securityType -eq 1) {
            exit 0
        }
        throw "RFB security type $securityType does not include None"
    }

    $clientVersion = if ($minor -ge 8) { "RFB 003.008`n" } else { "RFB 003.007`n" }
    $reply = [Text.Encoding]::ASCII.GetBytes($clientVersion)
    $stream.Write($reply, 0, $reply.Length)

    $count = [int](Read-Exact 1)[0]
    if ($count -eq 0) {
        $lengthBytes = Read-Exact 4
        $reasonLength = ([int]$lengthBytes[0] -shl 24) -bor ([int]$lengthBytes[1] -shl 16) -bor ([int]$lengthBytes[2] -shl 8) -bor [int]$lengthBytes[3]
        $reason = ""
        if ($reasonLength -gt 0 -and $reasonLength -lt 4096) {
            $reason = [Text.Encoding]::UTF8.GetString((Read-Exact $reasonLength))
        }
        throw "RFB server rejected security negotiation: $reason"
    }

    $types = Read-Exact $count
    Write-Output "[adaptix-vnc] RFB security types: $($types -join ',')"
    if ($types -contains 1) {
        exit 0
    }

    throw "RFB security type None is not offered"
} catch {
    Write-Output "[adaptix-vnc] probe failed: $($_.Exception.Message)"
    exit 1
} finally {
    if ($client) {
        $client.Close()
    }
}
