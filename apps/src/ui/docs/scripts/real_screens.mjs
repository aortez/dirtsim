export const screens = [
  {
    id: "start-menu",
    resetSystem: true,
    activityEnabled: false,
    ensureRailExpanded: true,
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
    resetSystem: true,
    activityEnabled: false,
    expect: {
      state: "Training",
      selectedIcon: "CORE",
      panelVisible: true
    },
    steps: [
      {
        args: ["ui", "SimStop"],
        waitMs: 300,
        waitForState: "StartMenu"
      },
      {
        args: ["ui", "IconSelect", "{\"id\":\"EVOLUTION\"}"],
        waitMs: 700,
        waitForState: "Training",
        retryOnUnselected: true,
        allowDeselected: true
      },
      {
        args: ["ui", "IconSelect", "{\"id\":\"CORE\"}"],
        waitMs: 600,
        retryOnUnselected: true
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
        waitMs: 100,
        allowFailure: true
      },
      {
        args: ["ui", "MouseDown", "{\"pixelX\":200,\"pixelY\":170}"],
        waitMs: 80
      },
      {
        args: ["ui", "MouseUp", "{\"pixelX\":200,\"pixelY\":170}"],
        waitMs: 500
      }
    ]
  }
];
