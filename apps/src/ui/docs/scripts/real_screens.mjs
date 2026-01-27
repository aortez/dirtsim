export const screens = [
  {
    id: "start-menu",
    resetUi: true,
    activityEnabled: true,
    expect: {
      state: "StartMenu"
    },
    steps: [
      {
        args: ["ui", "SimStop"],
        waitMs: 300
      }
    ]
  },
  {
    id: "training",
    resetUi: true,
    activityEnabled: false,
    skipClearTraining: true,
    expect: {
      state: "Training"
    },
    steps: [
      {
        args: ["ui", "SimStop"],
        waitMs: 300
      },
      {
        args: ["ui", "TrainingStart"],
        waitMs: 700
      }
    ]
  },
  {
    id: "network",
    resetUi: true,
    activityEnabled: false,
    expect: {
      state: "StartMenu",
      selectedIcon: "NETWORK",
      panelVisible: true
    },
    steps: [
      {
        args: ["ui", "SimStop"],
        waitMs: 300,
        waitForState: "StartMenu"
      },
      {
        args: ["ui", "IconSelect", "{\"id\":\"NETWORK\"}"],
        waitMs: 800,
        allowFailure: true
      },
      {
        args: ["ui", "IconSelect", "{\"id\":\"NETWORK\"}"],
        waitMs: 500
      }
    ]
  },
  {
    id: "training-config",
    resetUi: true,
    activityEnabled: false,
    skipClearTraining: true,
    expect: {
      state: "Training",
      selectedIcon: "EVOLUTION",
      panelVisible: true
    },
    steps: [
      {
        args: ["ui", "SimStop"],
        waitMs: 300
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
