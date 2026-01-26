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
      {
        args: ["ui", "SimStop"],
        waitMs: 300,
        allowFailure: true
      },
      {
        args: ["ui", "TrainingStart"],
        waitMs: 700
      }
    ]
  },
  {
    id: "training-config",
    resetUi: true,
    activityEnabled: false,
    skipClearTraining: true,
    steps: [
      {
        args: ["ui", "SimStop"],
        waitMs: 300,
        allowFailure: true
      },
      {
        args: ["ui", "IconSelect", "{\"id\":\"EVOLUTION\"}"],
        waitMs: 400,
        waitForState: "Training"
      },
      {
        args: ["ui", "IconSelect", "{\"id\":\"EVOLUTION\"}"],
        waitMs: 600
      }
    ]
  }
];
