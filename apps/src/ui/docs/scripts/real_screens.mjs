export const screens = [
  {
    id: "start-menu",
    flowId: "start-menu",
    resetSystem: true,
    expect: {
      state: "StartMenu",
      selectedIcon: "COUNT",
      panelVisible: false
    },
    steps: [
      {
        kind: "NavigateToScreen",
        target: "StartMenu"
      },
      {
        args: ["ui", "IconSelect", "{\"id\":\"COUNT\"}"],
        allowDeselected: true,
        waitMs: 300
      }
    ]
  },
  {
    id: "start-menu-home",
    flowId: "start-menu",
    resetSystem: false,
    expect: {
      state: "StartMenu",
      selectedIcon: "CORE",
      panelVisible: true
    },
    steps: [
      {
        kind: "NavigateToScreen",
        target: "StartMenu"
      },
      {
        args: ["ui", "IconSelect", "{\"id\":\"COUNT\"}"],
        allowDeselected: true,
        waitMs: 200
      },
      {
        args: ["ui", "IconSelect", "{\"id\":\"CORE\"}"],
        waitMs: 400
      }
    ]
  },
  {
    id: "start-menu-network",
    flowId: "start-menu",
    resetSystem: false,
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
        args: ["ui", "IconSelect", "{\"id\":\"COUNT\"}"],
        allowDeselected: true,
        waitMs: 200
      },
      {
        args: ["ui", "IconSelect", "{\"id\":\"NETWORK\"}"],
        waitMs: 500
      }
    ]
  },
  {
    id: "training",
    resetSystem: false,
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
        args: ["ui", "IconSelect", "{\"id\":\"CORE\"}"],
        waitMs: 500,
        waitForState: "Training"
      }
    ]
  },
  {
    id: "training-config",
    flowId: "training-config",
    resetSystem: true,
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
      }
    ]
  },
  {
    id: "training-config-evolution",
    flowId: "training-config",
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
