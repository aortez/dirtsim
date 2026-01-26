export const screens = [
  {
    id: "start-menu",
    resetUi: true,
    activityEnabled: true,
    steps: [
      {
        args: ["ui", "SimStop"],
        waitMs: 300,
        allowFailure: true
      }
    ]
  },
  {
    id: "training",
    resetUi: true,
    activityEnabled: false,
    skipClearTraining: true,
    steps: [
      // Replace with a real TrainingStart payload once you decide defaults.
      {
        args: ["ui", "SimStop"],
        waitMs: 300,
        allowFailure: true
      },
      {
        args: [
          "ui",
          "TrainingStart",
          "{\"evolution\":{\"energyReference\":100.0,\"maxGenerations\":1,\"maxSimulationTime\":10.0,\"populationSize\":10,\"tournamentSize\":3,\"waterReference\":100.0},\"mutation\":{\"rate\":0.015,\"resetRate\":0.0005,\"sigma\":0.05},\"training\":{\"organismType\":\"TREE\",\"population\":[],\"scenarioId\":8}}"
        ],
        waitMs: 700
      }
    ]
  }
];
