# Training Active

```plantuml
@startsalt
scale 1.6
{
  {
    "Stream"
    "Interval (ms)" | [ - ] [ 500 ] [ + ]
    [Stop Training]
    [Pause/Resume]
  } | {
    "Training"
    "Status: Running"
    "Gen 12 / 100"
    "Eval 23 / 50"
  }
}
@endsalt
```

Full-screen modal. Icon rail hidden. Only training actions are available.

## States

```plantuml
@startuml
[*] --> Training

Training --> StartMenu : Stop Training
@enduml
```
