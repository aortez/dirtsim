# Training Active

```text
+------------------+  +------------------------------------------------------------------+  +------------------------+
| Stream           |  | Evolution                                                        |  | Long-Term Progress     |
| Interval (ms)    |  | Training: Running                                                |  | Mode: Long-Term        |
| [ - ] [ 16 ] [+] |  | Gen 12 / 100    Eval 23    Genomes 824 / 5000                    |  | Phase: Explore         |
| [Stop Training]  |  | Time: 1055.5s  Sim: 0.0s  Speed: 1362.4x  ETA: --  CPU: 54%      |  | Episodes: 824 / 5000   |
| [Pause]          |  | This Gen: 2.64  All Time: 2.64 (Gen 0)                            |  | Novelty: 0.48          |
|                  |  |                                                                  |  | Plateau: 12 generations|
| +------------+   |  |   Current                          Best                          |  | ETA: 2h 14m            |
| |  _CPU--    |   |  |  +----------------------+      +----------------------+          |  |                        |
| |--|    |____|   |  |  |                      |      |                      |          |  |                        |
| +------------+   |  |  |                      |      |                      |          |  |                        |
|                  |  |  +----------------------+      +----------------------+          |  |                        |
|                  |  |                                                                  |  | Last update: 0.5s ago  |
|                  |  |   Fitness Insights                                               |  |                        |
|                  |  |    Last Gen Distribution          Best                           |  |                        |
|                  |  |  +----------------------+       +----------------------+         |  |                        |
|                  |  |  |                      |       |                      |         |  |                        |
|                  |  |  |                      |       |                      |         |  |                        |
|                  |  |  +----------------------+       +----------------------+         |  |                        |
+------------------+  +------------------------------------------------------------------+  +------------------------+
```

Full-screen training view. Icon rail hidden. The left stream column includes controls plus a per-core CPU bar chart, while the center section mirrors current and best cards and the right panel is reserved for long-term progress stats.

## States

```plantuml
@startuml
[*] --> TrainingActive

TrainingActive --> StartMenu : Stop Training
@enduml
```
