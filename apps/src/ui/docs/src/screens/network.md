# Network

```plantuml
@startsalt
scale 1.6
{
  {+
    [Home]
    [Network]
    [Minimize]
  } | {
    {+
      [Stop]
    }
    .
    "Network"
    "WiFi: onionchan"
    .
    "Networks"
    {
      onionchan
      "connected | -40 dBm | wpa2"
      [Connected] [Forget]
    }
    .
    "WebSocket token"
    .
    "IP Address:"
    "wlan0: 192.168.1.143"
    .
    [Refresh]
  } | {
    .
    "LAN Web UI"
    [On]
    .
    "Incoming WebSockets"
    [On]
  }
}
@endsalt
```

Background is black. NetworkDiagnosticsPanel content spans the full screen (minus icon rail).

## States

```plantuml
@startuml
[*] --> StartMenu

StartMenu --> Network : Network
Network --> StartMenu : Stop
@enduml
```
