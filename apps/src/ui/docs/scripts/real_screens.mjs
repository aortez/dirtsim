export const screens = [
  {
    id: "start-menu",
    resetSystem: true,
    activityEnabled: false,
    expect: {
      state: "StartMenu"
    },
    steps: [
      {
        kind: "NavigateToScreen",
        target: "StartMenu"
      }
    ]
  },
  {
    id: "training",
    resetSystem: false,
    activityEnabled: false,
    expect: {
      state: "Training",
      selectedIcon: "CORE",
      panelVisible: true
    },
    steps: [
      {
        kind: "NavigateToScreen",
        target: "Training"
      },
      {
        args: ["ui", "IconSelect", "{\"id\":\"EVOLUTION\"}"],
        waitMs: 500,
        waitForState: "Training"
      }
    ]
  },
  {
    id: "network",
    resetSystem: true,
    activityEnabled: false,
    expect: {
      state: "StartMenu",
      selectedIcon: "NETWORK",
      panelVisible: true
    },
    steps: [
      {
        kind: "NavigateToScreen",
        target: "StartMenu"
      },
      {
        args: ["ui", "IconSelect", "{\"id\":\"NETWORK\"}"],
        waitMs: 800
      },
      {
        args: ["ui", "IconSelect", "{\"id\":\"NETWORK\"}"],
        waitMs: 500
      }
    ]
  },
  {
    id: "training-config",
    flowId: "training-config",
    resetSystem: true,
    activityEnabled: false,
    skipClearTraining: true,
    expect: {
      state: "Training",
      selectedIcon: "EVOLUTION",
      panelVisible: true
    },
    steps: [
      {
        kind: "NavigateToScreen",
        target: "Training"
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
  },
  {
    id: "training-config-evolution",
    flowId: "training-config",
    activityEnabled: false,
    expect: {
      state: "Training",
      selectedIcon: "EVOLUTION",
      panelVisible: true
    },
    steps: [
      {
        args: ["ui", "MouseMove", "{\"pixelX\":200,\"pixelY\":170}"],
        waitMs: 500
      }
    ]
  }
];
