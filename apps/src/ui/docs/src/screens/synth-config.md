# Synth Config

```plantuml
@startsalt
scale 1.6
{
  {+
    [Duck]
    [Music]
    [Minimize]
  } | {
    {+
      Volume
      [-] | 50 | [+]
    } | {
      Keyboard partially obscured by panel.
      .
      .
    }
  }
}
@endsalt
```

Volume stepper ranges 0-100. The expanded panel obscures part of the keyboard.

## States

```plantuml
@startuml
[*] --> SynthConfig

Synth --> SynthConfig : Music
SynthConfig --> Synth : Music
SynthConfig --> StartMenu : Duck
@enduml
```
