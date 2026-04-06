$ErrorActionPreference = 'Stop'

$logPath = Join-Path $PSScriptRoot 'restore-seagate-disk.log'
Start-Transcript -Path $logPath -Force | Out-Null

try {
    $disk = Get-Disk -Number 0
    if ($disk.IsSystem -or $disk.IsBoot) {
        throw 'Disk 0 is marked as system or boot. Aborting.'
    }

    Clear-Disk -Number 0 -RemoveData -Confirm:$false
    Initialize-Disk -Number 0 -PartitionStyle GPT
    $partition = New-Partition -DiskNumber 0 -UseMaximumSize -AssignDriveLetter
    Format-Volume -Partition $partition -FileSystem NTFS -NewFileSystemLabel 'Seagate8TB' -Confirm:$false | Out-Null

    Get-Disk -Number 0 | Format-List Number,FriendlyName,Size,PartitionStyle,IsOffline,IsReadOnly
    Get-Partition -DiskNumber 0 | Select-Object DiskNumber,PartitionNumber,DriveLetter,Offset,Size,Type | Format-Table -AutoSize
    Get-Volume -DriveLetter $partition.DriveLetter | Select-Object DriveLetter,FileSystemLabel,FileSystem,HealthStatus,SizeRemaining,Size | Format-List
}
finally {
    Stop-Transcript | Out-Null
}