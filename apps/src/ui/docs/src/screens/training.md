# Training Home

```plantuml
@startsalt
scale 1.6
{
  {+
    [Home]
    [Evolution]
    [Genome]
    [Results]
  } | {
    "Training Home"
    .
    [Quit]
  }
}
@endsalt
```

## States

```plantuml
@startuml
[*] --> Training

Training --> StartMenu : Quit
@enduml
```
