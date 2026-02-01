# Synth

```plantuml
@startsalt
scale 1.6
{
  {+
    [Home]
    [Music]
    [Minimize]
  } | {
    {#
      [ ] | . | [ ] | . | . | [ ] | . | [ ] | . | [ ] | .
    }
    {#
      [ ] | . | [ ] | . | [ ] | . | [ ] | . | [ ] | . | [ ] | . | [ ]
    }
    .
  }
}
@endsalt
```

Black keys overlay the white keys in a piano layout. Keys should be at least Action Size. Bottom row is empty.

## States

```plantuml
@startuml
[*] --> Synth

StartMenu --> Synth : Music
Synth --> StartMenu : Stop
@enduml
```
