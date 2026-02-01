# Training Genome Browser

## List View

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
    "Genome Browser"
    "Scenario" | [Tree Germination] [>]
    .
    {
      "Sel" | "Genome" | "Fitness/Gen" | "Selected 2/8"
      "" | "" | "" | [Add to Training]
      [ ] | "Seed-01" | "2.34 / 17" | [x]
      [x] | "Seed-02" | "1.98 / 15" | [ ]
      [ ] | "Seed-03" | "1.20 / 8" | [x]
    }
    "Pool indicators are read-only."
    "List scroll: clamp at ends."
  }
}
@endsalt
```

## Scenario Selector (Replaces List Column)

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
    [Back]
    "Scenario"
    [Tree Germination]
    [Tree Growth]
    [Riverbed]
    [Sandbox]
  }
}
@endsalt
```
