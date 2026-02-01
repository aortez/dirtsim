# Training

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
    .
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
