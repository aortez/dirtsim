# Start Menu

```plantuml
@startsalt
scale 1.6
{
  {+
    [Home]
    [Graphs]
    [Scenes]
    [Wireless]
    [Settings]
  } | {
    {+
      [Quit]
      [Next Fractal]
    } | "Touch: ..."
    .
    "                               "
  }
}
@endsalt
```

## States

```plantuml
@startuml
[*] --> StartMenu

StartMenu --> SimRunning : Start
StartMenu --> Training : Training
StartMenu --> Exit : Quit

SimRunning --> Paused : Pause
Paused --> SimRunning : Resume
SimRunning --> StartMenu : Stop

Training --> StartMenu : Complete
@enduml
```
