/**
 * Auto-generated 1:1 from AdaptixClient/Resources/themes/*.json and themes/console/*.json
 * Contains all 10 Qlementine application themes + all 10 Console/Logs themes + Qt client prefs.
 */
export type QtThemeId = "adaptix_dark" | "adaptix_dracula" | "adaptix_light" | "dark" | "dark_ice" | "dracula" | "fallout" | "glass_morph" | "hacker_tech" | "light_arc"
export type QtConsoleThemeId = "console_adaptix_dark" | "console_adaptix_dracula" | "console_adaptix_light" | "console_dark_classic" | "console_dark_ice" | "console_dracula" | "console_fallout" | "console_glass_morph" | "console_hacker_tech" | "console_light_arc"

export type QtThemeTokens = {
  bg1: string; bg2: string; bg3: string; bg4: string
  workspace: string; tabbar: string
  border: string; borderHover: string; borderPress: string; borderDisabled: string
  focus: string
  neutral: string; neutralHover: string; neutralPress: string; neutralDisabled: string
  primary: string; primaryHover: string; primaryPress: string; primaryAlt: string; primaryFg: string
  text: string; textHover: string; textPress: string; textDisabled: string; textMuted: string
  semi1: string; semi2: string; semi3: string
  err: string; info: string; ok: string; warn: string
  radius: number
}

export type QtThemeMeta = {
  id: QtThemeId
  name: string
  mode: 'dark' | 'light'
  defaultConsole: QtConsoleThemeId
  swatches: [string, string, string, string]
  tokens: QtThemeTokens
}

export type QtConsoleTheme = {
  id: QtConsoleThemeId
  name: string
  bg: string
  dimming: number
  hasImage: boolean
  text: string
  debug: string
  success: string
  error: string
  info: string
  operator: string
  task: string
  agent: string
  command: string
  inputSymbol: string
  inputColor: string
  logDebug: string
  logConnect: string
  logDisconnect: string
  logAgentNew: string
  logTunnel: string
  logListener: string
}

export const QT_THEMES: QtThemeMeta[] = [
  {
    "id": "adaptix_dark",
    "name": "Adaptix Dark",
    "mode": "dark",
    "defaultConsole": "console_adaptix_dark",
    "swatches": [
      "#121212",
      "#1E1E1E",
      "#3D9E6E",
      "#BEBEBE"
    ],
    "tokens": {
      "bg1": "#1E1E1E",
      "bg2": "#1A1A1A",
      "bg3": "#181818",
      "bg4": "#151515",
      "workspace": "#121212",
      "tabbar": "#1A1A1A",
      "border": "#3A3A3A",
      "borderHover": "#4A4A4A",
      "borderPress": "#2D7A5A",
      "borderDisabled": "#2A2A2A",
      "focus": "#2D7A5A99",
      "neutral": "#2A2A2A",
      "neutralHover": "#343434",
      "neutralPress": "#3E3E3E",
      "neutralDisabled": "#282828",
      "primary": "#3D9E6E",
      "primaryHover": "#4DAE7E",
      "primaryPress": "#5DBE8E",
      "primaryAlt": "#2D8B5A",
      "primaryFg": "#FFFFFF",
      "text": "#BEBEBE",
      "textHover": "#D0D0D0",
      "textPress": "#E0E0E0",
      "textDisabled": "#5A5A5A",
      "textMuted": "#909090",
      "semi1": "#FFFFFF08",
      "semi2": "#FFFFFF12",
      "semi3": "#FFFFFF1A",
      "err": "#E96B72",
      "info": "#1BA8D5",
      "ok": "#2D7A5A",
      "warn": "#FBC064",
      "radius": 4
    }
  },
  {
    "id": "adaptix_dracula",
    "name": "Adaptix Dracula",
    "mode": "dark",
    "defaultConsole": "console_adaptix_dracula",
    "swatches": [
      "#21222c",
      "#44475a",
      "#3D8B6A",
      "#f8f8f2"
    ],
    "tokens": {
      "bg1": "#44475a",
      "bg2": "#383a4a",
      "bg3": "#282a36",
      "bg4": "#21222c",
      "workspace": "#21222c",
      "tabbar": "#383a4a",
      "border": "#6272a4",
      "borderHover": "#7282b4",
      "borderPress": "#3D8B6A",
      "borderDisabled": "#4a4d64",
      "focus": "#3D8B6A99",
      "neutral": "#282a36",
      "neutralHover": "#383a4a",
      "neutralPress": "#44475a",
      "neutralDisabled": "#323442",
      "primary": "#3D8B6A",
      "primaryHover": "#4A9F7A",
      "primaryPress": "#5AAF8A",
      "primaryAlt": "#4A9F7A",
      "primaryFg": "#FFFFFF",
      "text": "#f8f8f2",
      "textHover": "#FFFFFF",
      "textPress": "#FFFFFF",
      "textDisabled": "#6272a4",
      "textMuted": "#c0c0c0",
      "semi1": "#FFFFFF08",
      "semi2": "#FFFFFF12",
      "semi3": "#FFFFFF1A",
      "err": "#ff5555",
      "info": "#8be9fd",
      "ok": "#50fa7b",
      "warn": "#ffb86c",
      "radius": 4
    }
  },
  {
    "id": "adaptix_light",
    "name": "Adaptix Light",
    "mode": "light",
    "defaultConsole": "console_adaptix_light",
    "swatches": [
      "#EBEBEB",
      "#E0E0E0",
      "#388E3C",
      "#2A2A2A"
    ],
    "tokens": {
      "bg1": "#E0E0E0",
      "bg2": "#D8D8D8",
      "bg3": "#D0D0D0",
      "bg4": "#C8C8C8",
      "workspace": "#EBEBEB",
      "tabbar": "#E8E8E8",
      "border": "#BFBFBF",
      "borderHover": "#A0A0A0",
      "borderPress": "#388E3C",
      "borderDisabled": "#D0D0D0",
      "focus": "#388E3C99",
      "neutral": "#B0B0B0",
      "neutralHover": "#A0A0A0",
      "neutralPress": "#909090",
      "neutralDisabled": "#D8D8D8",
      "primary": "#388E3C",
      "primaryHover": "#43A047",
      "primaryPress": "#4CAF50",
      "primaryAlt": "#2E7D32",
      "primaryFg": "#FFFFFF",
      "text": "#2A2A2A",
      "textHover": "#1A1A1A",
      "textPress": "#000000",
      "textDisabled": "#909090",
      "textMuted": "#4A4A4A",
      "semi1": "#00000008",
      "semi2": "#00000012",
      "semi3": "#0000001A",
      "err": "#DC3545",
      "info": "#17A2B8",
      "ok": "#388E3C",
      "warn": "#FFC107",
      "radius": 4
    }
  },
  {
    "id": "dark",
    "name": "Dark Classic",
    "mode": "dark",
    "defaultConsole": "console_dark_classic",
    "swatches": [
      "#111111",
      "#333333",
      "#5A5A5A",
      "#E0E0E0"
    ],
    "tokens": {
      "bg1": "#333333",
      "bg2": "#2A2A2A",
      "bg3": "#222222",
      "bg4": "#1A1A1A",
      "workspace": "#111111",
      "tabbar": "#2A2A2A",
      "border": "#4A4A4A",
      "borderHover": "#555555",
      "borderPress": "#666666",
      "borderDisabled": "#3A3A3A",
      "focus": "#66666699",
      "neutral": "#2A2A2A",
      "neutralHover": "#3A3A3A",
      "neutralPress": "#4A4A4A",
      "neutralDisabled": "#222222",
      "primary": "#5A5A5A",
      "primaryHover": "#666666",
      "primaryPress": "#727272",
      "primaryAlt": "#505050",
      "primaryFg": "#FFFFFF",
      "text": "#E0E0E0",
      "textHover": "#F0F0F0",
      "textPress": "#FFFFFF",
      "textDisabled": "#5A5A5A",
      "textMuted": "#909090",
      "semi1": "#FFFFFF08",
      "semi2": "#FFFFFF12",
      "semi3": "#FFFFFF1A",
      "err": "#E96B72",
      "info": "#1BA8D5",
      "ok": "#5A9A6A",
      "warn": "#FBC064",
      "radius": 4
    }
  },
  {
    "id": "dark_ice",
    "name": "Dark Ice",
    "mode": "dark",
    "defaultConsole": "console_dark_ice",
    "swatches": [
      "#000000",
      "#0F172A",
      "#20A0F0",
      "#E2E8F0"
    ],
    "tokens": {
      "bg1": "#0F172A",
      "bg2": "#0A0F1D",
      "bg3": "#05080A",
      "bg4": "#020405",
      "workspace": "#000000",
      "tabbar": "#0A0F1D",
      "border": "#1E293B",
      "borderHover": "#334155",
      "borderPress": "#00F0FF",
      "borderDisabled": "#0F172A",
      "focus": "#00F0FF99",
      "neutral": "#0F172A",
      "neutralHover": "#1E293B",
      "neutralPress": "#334155",
      "neutralDisabled": "#0A0F1D",
      "primary": "#20A0F0",
      "primaryHover": "#40B0FF",
      "primaryPress": "#60C0FF",
      "primaryAlt": "#40E0F0",
      "primaryFg": "#FFFFFF",
      "text": "#E2E8F0",
      "textHover": "#F1F5F9",
      "textPress": "#FFFFFF",
      "textDisabled": "#475569",
      "textMuted": "#1E293B",
      "semi1": "#FFFFFF08",
      "semi2": "#FFFFFF12",
      "semi3": "#FFFFFF1A",
      "err": "#EF4444",
      "info": "#00F0FF",
      "ok": "#22C55E",
      "warn": "#F59E0B",
      "radius": 4
    }
  },
  {
    "id": "dracula",
    "name": "Dracula",
    "mode": "dark",
    "defaultConsole": "console_dracula",
    "swatches": [
      "#1a1b26",
      "#44475a",
      "#bd93f9",
      "#f8f8f2"
    ],
    "tokens": {
      "bg1": "#44475a",
      "bg2": "#383a4a",
      "bg3": "#282a36",
      "bg4": "#21222c",
      "workspace": "#1a1b26",
      "tabbar": "#383a4a",
      "border": "#6272a4",
      "borderHover": "#7282b4",
      "borderPress": "#bd93f9",
      "borderDisabled": "#44475a",
      "focus": "#bd93f999",
      "neutral": "#282a36",
      "neutralHover": "#383a4a",
      "neutralPress": "#44475a",
      "neutralDisabled": "#21222c",
      "primary": "#bd93f9",
      "primaryHover": "#caa4fa",
      "primaryPress": "#d7b5fb",
      "primaryAlt": "#a67df0",
      "primaryFg": "#282a36",
      "text": "#f8f8f2",
      "textHover": "#FFFFFF",
      "textPress": "#FFFFFF",
      "textDisabled": "#6272a4",
      "textMuted": "#b0b0c0",
      "semi1": "#FFFFFF08",
      "semi2": "#FFFFFF12",
      "semi3": "#FFFFFF1A",
      "err": "#ff5555",
      "info": "#8be9fd",
      "ok": "#50fa7b",
      "warn": "#ffb86c",
      "radius": 4
    }
  },
  {
    "id": "fallout",
    "name": "Fallout Pip-Boy",
    "mode": "dark",
    "defaultConsole": "console_fallout",
    "swatches": [
      "#000000",
      "#1A1A1A",
      "#00AA00",
      "#00FF00"
    ],
    "tokens": {
      "bg1": "#1A1A1A",
      "bg2": "#111111",
      "bg3": "#0A0A0A",
      "bg4": "#050505",
      "workspace": "#000000",
      "tabbar": "#1A1A1A",
      "border": "#006600",
      "borderHover": "#00AA00",
      "borderPress": "#00FF00",
      "borderDisabled": "#003300",
      "focus": "#00FF0099",
      "neutral": "#1A1A1A",
      "neutralHover": "#2D4D2D",
      "neutralPress": "#3D6D3D",
      "neutralDisabled": "#0A0A0A",
      "primary": "#00AA00",
      "primaryHover": "#00CC00",
      "primaryPress": "#00FF00",
      "primaryAlt": "#006400",
      "primaryFg": "#000000",
      "text": "#00FF00",
      "textHover": "#40FF40",
      "textPress": "#80FF80",
      "textDisabled": "#006400",
      "textMuted": "#00CC00",
      "semi1": "#00FF0008",
      "semi2": "#00FF0012",
      "semi3": "#00FF001A",
      "err": "#FF0000",
      "info": "#00FFFF",
      "ok": "#00FF00",
      "warn": "#FFFF00",
      "radius": 2
    }
  },
  {
    "id": "glass_morph",
    "name": "Glass Morphism",
    "mode": "dark",
    "defaultConsole": "console_glass_morph",
    "swatches": [
      "#030508",
      "#1E293B",
      "#38BDF8",
      "#F8FAFC"
    ],
    "tokens": {
      "bg1": "#1E293B",
      "bg2": "#0F172A",
      "bg3": "#0A101D",
      "bg4": "#050810",
      "workspace": "#030508",
      "tabbar": "#1E293B",
      "border": "#FFFFFF1A",
      "borderHover": "#FFFFFF30",
      "borderPress": "#38BDF8",
      "borderDisabled": "#FFFFFF0A",
      "focus": "#38BDF899",
      "neutral": "#1E293BB0",
      "neutralHover": "#334155B0",
      "neutralPress": "#475569B0",
      "neutralDisabled": "#0F172A80",
      "primary": "#38BDF8",
      "primaryHover": "#7DD3FC",
      "primaryPress": "#BAE6FD",
      "primaryAlt": "#0EA5E9",
      "primaryFg": "#FFFFFF",
      "text": "#F8FAFC",
      "textHover": "#FFFFFF",
      "textPress": "#FFFFFF",
      "textDisabled": "#475569",
      "textMuted": "#94A3B8",
      "semi1": "#FFFFFF08",
      "semi2": "#FFFFFF12",
      "semi3": "#FFFFFF1A",
      "err": "#F87171",
      "info": "#38BDF8",
      "ok": "#4ADE80",
      "warn": "#FBBF24",
      "radius": 4
    }
  },
  {
    "id": "hacker_tech",
    "name": "Hacker Tech",
    "mode": "dark",
    "defaultConsole": "console_hacker_tech",
    "swatches": [
      "#000000",
      "#0F1722",
      "#00AA50",
      "#D7E1EC"
    ],
    "tokens": {
      "bg1": "#0F1722",
      "bg2": "#0A0F16",
      "bg3": "#05070A",
      "bg4": "#030406",
      "workspace": "#000000",
      "tabbar": "#0A0F16",
      "border": "#142033",
      "borderHover": "#1E3050",
      "borderPress": "#00FF6A",
      "borderDisabled": "#0A0F16",
      "focus": "#00FF6A99",
      "neutral": "#142033",
      "neutralHover": "#1E3050",
      "neutralPress": "#2A4070",
      "neutralDisabled": "#0A0F16",
      "primary": "#00AA50",
      "primaryHover": "#00CC60",
      "primaryPress": "#00EE70",
      "primaryAlt": "#008840",
      "primaryFg": "#FFFFFF",
      "text": "#D7E1EC",
      "textHover": "#E7F1FC",
      "textPress": "#FFFFFF",
      "textDisabled": "#607284",
      "textMuted": "#B6FFCC",
      "semi1": "#00FF6A08",
      "semi2": "#00FF6A12",
      "semi3": "#00FF6A1A",
      "err": "#FF4444",
      "info": "#00AAFF",
      "ok": "#00FF6A",
      "warn": "#FFAA00",
      "radius": 4
    }
  },
  {
    "id": "light_arc",
    "name": "Light Arc",
    "mode": "light",
    "defaultConsole": "console_light_arc",
    "swatches": [
      "#E8E8E8",
      "#F0F0F0",
      "#17A2B8",
      "#2E3440"
    ],
    "tokens": {
      "bg1": "#F0F0F0",
      "bg2": "#E8E8E8",
      "bg3": "#DCDCDC",
      "bg4": "#D0D0D0",
      "workspace": "#E8E8E8",
      "tabbar": "#D0D0D0",
      "border": "#D0D0D0",
      "borderHover": "#B0B0B0",
      "borderPress": "#17A2B8",
      "borderDisabled": "#E0E0E0",
      "focus": "#17A2B899",
      "neutral": "#B8B8B8",
      "neutralHover": "#A0A0A0",
      "neutralPress": "#909090",
      "neutralDisabled": "#D0D0D0",
      "primary": "#17A2B8",
      "primaryHover": "#138496",
      "primaryPress": "#117A8B",
      "primaryAlt": "#138496",
      "primaryFg": "#FFFFFF",
      "text": "#2E3440",
      "textHover": "#1E2430",
      "textPress": "#0E1420",
      "textDisabled": "#A0A0A0",
      "textMuted": "#4E5460",
      "semi1": "#00000008",
      "semi2": "#00000012",
      "semi3": "#0000001A",
      "err": "#DC3545",
      "info": "#17A2B8",
      "ok": "#28A745",
      "warn": "#FFC107",
      "radius": 4
    }
  }
]

export const QT_CONSOLE_THEMES: QtConsoleTheme[] = [
  {
    "id": "console_adaptix_dark",
    "name": "Adaptix Dark Console",
    "bg": "#000000",
    "dimming": 85,
    "hasImage": true,
    "text": "#E0E0E0",
    "debug": "#606060",
    "success": "#FFFF00",
    "error": "#E32227",
    "info": "#89CFF0",
    "operator": "#808080",
    "task": "#606060",
    "agent": "#808080",
    "command": "#E0E0E0",
    "inputSymbol": ">",
    "inputColor": "#808080",
    "logDebug": "#606060",
    "logConnect": "#E0E0E0",
    "logDisconnect": "#808080",
    "logAgentNew": "#39FF14",
    "logTunnel": "#FDFD96",
    "logListener": "#FFA500"
  },
  {
    "id": "console_adaptix_dracula",
    "name": "Adaptix Dracula Console",
    "bg": "#21222c",
    "dimming": 85,
    "hasImage": false,
    "text": "#f8f8f2",
    "debug": "#6272a4",
    "success": "#f1fa8c",
    "error": "#ff5555",
    "info": "#8be9fd",
    "operator": "#bd93f9",
    "task": "#6272a4",
    "agent": "#50fa7b",
    "command": "#f8f8f2",
    "inputSymbol": "»",
    "inputColor": "#ff79c6",
    "logDebug": "#6272a4",
    "logConnect": "#50fa7b",
    "logDisconnect": "#6272a4",
    "logAgentNew": "#50fa7b",
    "logTunnel": "#f1fa8c",
    "logListener": "#ffb86c"
  },
  {
    "id": "console_adaptix_light",
    "name": "Adaptix Light Console",
    "bg": "#F5F5F5",
    "dimming": 85,
    "hasImage": false,
    "text": "#2A2A2A",
    "debug": "#909090",
    "success": "#2E7D32",
    "error": "#C62828",
    "info": "#1565C0",
    "operator": "#607D8B",
    "task": "#909090",
    "agent": "#455A64",
    "command": "#212121",
    "inputSymbol": ">",
    "inputColor": "#607D8B",
    "logDebug": "#909090",
    "logConnect": "#2E7D32",
    "logDisconnect": "#909090",
    "logAgentNew": "#1B5E20",
    "logTunnel": "#E65100",
    "logListener": "#1565C0"
  },
  {
    "id": "console_dark_classic",
    "name": "Dark Classic Console",
    "bg": "#1A1A1A",
    "dimming": 85,
    "hasImage": false,
    "text": "#C0C0C0",
    "debug": "#4A4A4A",
    "success": "#AAAAAA",
    "error": "#CC4444",
    "info": "#8888AA",
    "operator": "#707070",
    "task": "#4A4A4A",
    "agent": "#909090",
    "command": "#D0D0D0",
    "inputSymbol": ">",
    "inputColor": "#707070",
    "logDebug": "#4A4A4A",
    "logConnect": "#C0C0C0",
    "logDisconnect": "#707070",
    "logAgentNew": "#AAAAAA",
    "logTunnel": "#CC9944",
    "logListener": "#8888AA"
  },
  {
    "id": "console_dark_ice",
    "name": "Dark Ice Console",
    "bg": "#020405",
    "dimming": 85,
    "hasImage": false,
    "text": "#C8D6E5",
    "debug": "#334155",
    "success": "#00F0FF",
    "error": "#FF4D6A",
    "info": "#40B0FF",
    "operator": "#5B7FA5",
    "task": "#334155",
    "agent": "#20A0F0",
    "command": "#E0F0FF",
    "inputSymbol": ">",
    "inputColor": "#5B7FA5",
    "logDebug": "#334155",
    "logConnect": "#40B0FF",
    "logDisconnect": "#5B7FA5",
    "logAgentNew": "#00F0FF",
    "logTunnel": "#FFD93D",
    "logListener": "#40B0FF"
  },
  {
    "id": "console_dracula",
    "name": "Dracula Console",
    "bg": "#1a1b26",
    "dimming": 85,
    "hasImage": false,
    "text": "#f8f8f2",
    "debug": "#6272a4",
    "success": "#f1fa8c",
    "error": "#ff5555",
    "info": "#bd93f9",
    "operator": "#8be9fd",
    "task": "#6272a4",
    "agent": "#bd93f9",
    "command": "#f8f8f2",
    "inputSymbol": "»",
    "inputColor": "#ff79c6",
    "logDebug": "#6272a4",
    "logConnect": "#50fa7b",
    "logDisconnect": "#6272a4",
    "logAgentNew": "#bd93f9",
    "logTunnel": "#f1fa8c",
    "logListener": "#ffb86c"
  },
  {
    "id": "console_fallout",
    "name": "Fallout Pip-Boy Console",
    "bg": "#050505",
    "dimming": 85,
    "hasImage": false,
    "text": "#00CC00",
    "debug": "#004400",
    "success": "#00FF00",
    "error": "#FF3300",
    "info": "#00AA00",
    "operator": "#008800",
    "task": "#004400",
    "agent": "#00AA00",
    "command": "#00FF00",
    "inputSymbol": ">",
    "inputColor": "#008800",
    "logDebug": "#004400",
    "logConnect": "#00CC00",
    "logDisconnect": "#006600",
    "logAgentNew": "#00FF00",
    "logTunnel": "#AAFF00",
    "logListener": "#00AA00"
  },
  {
    "id": "console_glass_morph",
    "name": "Glass Morphism Console",
    "bg": "#050810",
    "dimming": 85,
    "hasImage": false,
    "text": "#CBD5E1",
    "debug": "#334155",
    "success": "#38BDF8",
    "error": "#F87171",
    "info": "#7DD3FC",
    "operator": "#64748B",
    "task": "#475569",
    "agent": "#38BDF8",
    "command": "#E2E8F0",
    "inputSymbol": "›",
    "inputColor": "#64748B",
    "logDebug": "#334155",
    "logConnect": "#7DD3FC",
    "logDisconnect": "#64748B",
    "logAgentNew": "#38BDF8",
    "logTunnel": "#FBBF24",
    "logListener": "#7DD3FC"
  },
  {
    "id": "console_hacker_tech",
    "name": "Hacker Tech Console",
    "bg": "#030406",
    "dimming": 85,
    "hasImage": false,
    "text": "#B0C4B0",
    "debug": "#1E3050",
    "success": "#00FF6A",
    "error": "#FF3333",
    "info": "#00CC60",
    "operator": "#3A6A4A",
    "task": "#1E3050",
    "agent": "#00AA50",
    "command": "#C0E0C0",
    "inputSymbol": "$",
    "inputColor": "#00AA50",
    "logDebug": "#1E3050",
    "logConnect": "#00CC60",
    "logDisconnect": "#3A6A4A",
    "logAgentNew": "#00FF6A",
    "logTunnel": "#AAFF00",
    "logListener": "#00CC60"
  },
  {
    "id": "console_light_arc",
    "name": "Light Arc Console",
    "bg": "#EEEEEE",
    "dimming": 85,
    "hasImage": false,
    "text": "#333333",
    "debug": "#A0A0A0",
    "success": "#17A2B8",
    "error": "#DC3545",
    "info": "#138496",
    "operator": "#6C757D",
    "task": "#A0A0A0",
    "agent": "#17A2B8",
    "command": "#212529",
    "inputSymbol": ">",
    "inputColor": "#6C757D",
    "logDebug": "#A0A0A0",
    "logConnect": "#17A2B8",
    "logDisconnect": "#6C757D",
    "logAgentNew": "#138496",
    "logTunnel": "#E65100",
    "logListener": "#17A2B8"
  }
]

export type QtFontFamily = 'JetBrains Mono' | 'Hack' | 'System'

export type QtClientSettings = {
  theme: QtThemeId
  consoleTheme: QtConsoleThemeId
  fontFamily: QtFontFamily
  fontSize: number
  consoleShowBg: boolean
  consoleAutoScroll: boolean
  consoleNoWrap: boolean
  consoleShowTime: boolean
  sessionsCols: Record<string, boolean>
}

export const SESSIONS_COL_META: { key: string; label: string }[] = [
  { key: 'id', label: 'ID' },
  { key: 'type', label: 'Type' },
  { key: 'external', label: 'External' },
  { key: 'listener', label: 'Listener' },
  { key: 'internal', label: 'Internal' },
  { key: 'domain', label: 'Domain' },
  { key: 'computer', label: 'Computer' },
  { key: 'user', label: 'User' },
  { key: 'os', label: 'OS' },
  { key: 'process', label: 'Process' },
  { key: 'pid', label: 'PID' },
  { key: 'tid', label: 'TID' },
  { key: 'tags', label: 'Tags' },
  { key: 'created', label: 'Created' },
  { key: 'last', label: 'Last' },
  { key: 'sleep', label: 'Sleep' },
  { key: 'geo', label: 'Geo' },
]

export const DEFAULT_SESSIONS_COLS: Record<string, boolean> = {
  id: true,
  type: true,
  external: true,
  listener: true,
  internal: true,
  domain: true,
  computer: true,
  user: true,
  os: true,
  process: true,
  pid: true,
  tid: false,
  tags: true,
  created: false,
  last: true,
  sleep: true,
  geo: true,
}

const SETTINGS_KEY = 'adaptix.qt.settings.v2'

export function loadQtSettings(): QtClientSettings {
  const fallback: QtClientSettings = {
    theme: 'adaptix_dark',
    consoleTheme: 'console_adaptix_dark',
    fontFamily: 'JetBrains Mono',
    fontSize: 12,
    consoleShowBg: true,
    consoleAutoScroll: true,
    consoleNoWrap: false,
    consoleShowTime: true,
    sessionsCols: { ...DEFAULT_SESSIONS_COLS },
  }
  try {
    const raw = localStorage.getItem(SETTINGS_KEY)
    if (!raw) return fallback
    const p = JSON.parse(raw)
    const themeValid = QT_THEMES.some((t) => t.id === p.theme) ? p.theme : fallback.theme
    const cValid = QT_CONSOLE_THEMES.some((c) => c.id === p.consoleTheme) ? p.consoleTheme : fallback.consoleTheme
    return {
      theme: themeValid,
      consoleTheme: cValid,
      fontFamily: (p.fontFamily === 'Hack' || p.fontFamily === 'System' || p.fontFamily === 'JetBrains Mono') ? p.fontFamily : fallback.fontFamily,
      fontSize: Number(p.fontSize) >= 10 && Number(p.fontSize) <= 16 ? Number(p.fontSize) : fallback.fontSize,
      consoleShowBg: typeof p.consoleShowBg === 'boolean' ? p.consoleShowBg : true,
      consoleAutoScroll: typeof p.consoleAutoScroll === 'boolean' ? p.consoleAutoScroll : true,
      consoleNoWrap: typeof p.consoleNoWrap === 'boolean' ? p.consoleNoWrap : false,
      consoleShowTime: typeof p.consoleShowTime === 'boolean' ? p.consoleShowTime : true,
      sessionsCols: { ...DEFAULT_SESSIONS_COLS, ...(p.sessionsCols || {}) },
    }
  } catch {
    return fallback
  }
}

export function saveQtSettings(s: QtClientSettings) {
  try { localStorage.setItem(SETTINGS_KEY, JSON.stringify(s)) } catch {}
  applyQtSettings(s)
  window.dispatchEvent(new CustomEvent('adaptix-qt-settings', { detail: s }))
}

export function loadQtTheme(): QtThemeId {
  return loadQtSettings().theme
}

export function applyQtTheme(id: QtThemeId, syncConsole = true) {
  const cur = loadQtSettings()
  const meta = QT_THEMES.find((t) => t.id === id) || QT_THEMES[0]
  const next: QtClientSettings = {
    ...cur,
    theme: meta.id,
    consoleTheme: syncConsole ? meta.defaultConsole : cur.consoleTheme,
  }
  saveQtSettings(next)
}

/** Resolve static Qt asset URL cleanly under any Vite VITE_BASE (e.g. / or /<secret-path>/) */
export function qtAsset(rel: string): string {
  const b = (import.meta.env.BASE_URL || '/').replace(/\/?$/, '/')
  return `${b}qt/${rel.replace(/^\/+/, '')}`
}

function ensureQtFonts() {
  if (typeof document === 'undefined' || document.getElementById('qt-font-faces')) return
  const st = document.createElement('style')
  st.id = 'qt-font-faces'
  st.textContent = `
@font-face { font-family: 'QtHack'; src: url('${qtAsset('fonts/Hack-Regular.ttf')}') format('truetype'); font-weight: 400; font-style: normal; font-display: swap; }
@font-face { font-family: 'QtHack'; src: url('${qtAsset('fonts/Hack-Bold.ttf')}') format('truetype'); font-weight: 700; font-style: normal; font-display: swap; }
@font-face { font-family: 'QtJetBrainsMono'; src: url('${qtAsset('fonts/JetBrainsMono-Regular.ttf')}') format('truetype'); font-weight: 400; font-style: normal; font-display: swap; }
@font-face { font-family: 'QtJetBrainsMono'; src: url('${qtAsset('fonts/JetBrainsMono-Bold.ttf')}') format('truetype'); font-weight: 700; font-style: normal; font-display: swap; }
`
  document.head.appendChild(st)
}

export function applyQtSettings(s: QtClientSettings) {
  ensureQtFonts()
  const meta = QT_THEMES.find((t) => t.id === s.theme) || QT_THEMES[0]
  const cMeta = QT_CONSOLE_THEMES.find((c) => c.id === s.consoleTheme) || QT_CONSOLE_THEMES[0]
  const root = document.documentElement
  const tk = meta.tokens

  root.dataset.qtTheme = meta.id
  root.dataset.qtMode = meta.mode
  root.dataset.theme = meta.mode
  root.dataset.qtConsoleBg = s.consoleShowBg ? 'on' : 'off'

  const vars: Record<string, string> = {
    '--qt-bg1': tk.bg1,
    '--qt-bg2': tk.bg2,
    '--qt-bg3': tk.bg3,
    '--qt-bg4': tk.bg4,
    '--qt-workspace': tk.workspace,
    '--qt-tabbar': tk.tabbar,
    '--qt-border': tk.border,
    '--qt-border-hover': tk.borderHover,
    '--qt-border-press': tk.borderPress,
    '--qt-border-disabled': tk.borderDisabled,
    '--qt-focus': tk.focus,
    '--qt-neutral': tk.neutral,
    '--qt-neutral-hover': tk.neutralHover,
    '--qt-neutral-press': tk.neutralPress,
    '--qt-neutral-disabled': tk.neutralDisabled,
    '--qt-primary': tk.primary,
    '--qt-primary-hover': tk.primaryHover,
    '--qt-primary-press': tk.primaryPress,
    '--qt-primary-alt': tk.primaryAlt,
    '--qt-primary-fg': tk.primaryFg,
    '--qt-text': tk.text,
    '--qt-text-hover': tk.textHover,
    '--qt-text-press': tk.textPress,
    '--qt-text-disabled': tk.textDisabled,
    '--qt-text-muted': tk.textMuted,
    '--qt-semi-1': tk.semi1,
    '--qt-semi-2': tk.semi2,
    '--qt-semi-3': tk.semi3,
    '--qt-err': tk.err,
    '--qt-info': tk.info,
    '--qt-ok': tk.ok,
    '--qt-warn': tk.warn,
    '--qt-radius': tk.radius + 'px',
    '--qt-font-size': s.fontSize + 'px',
    '--qt-ui-font': s.fontFamily === 'System'
      ? "'Segoe UI', -apple-system, 'Microsoft YaHei UI', sans-serif"
      : s.fontFamily === 'Hack'
        ? "'QtHack', 'Consolas', monospace"
        : "'QtJetBrainsMono', 'Segoe UI', 'Microsoft YaHei UI', monospace",
    '--qt-mono-font': "'QtHack', 'QtJetBrainsMono', 'Consolas', monospace",
    '--qtc-bg': cMeta.bg,
    '--qt-console-bg-url': `url('${qtAsset('adaptix_background.png')}')`,
    '--qtc-dim': String(Math.min(0.96, Math.max(0.25, cMeta.dimming / 100))),
    '--qtc-text': cMeta.text,
    '--qtc-debug': cMeta.debug,
    '--qtc-success': cMeta.success,
    '--qtc-error': cMeta.error,
    '--qtc-info': cMeta.info,
    '--qtc-operator': cMeta.operator,
    '--qtc-task': cMeta.task,
    '--qtc-agent': cMeta.agent,
    '--qtc-command': cMeta.command,
    '--qtc-input': cMeta.inputColor,
    '--qtc-log-agent': cMeta.logAgentNew,
    '--qtc-log-listener': cMeta.logListener,
    '--qtc-log-tunnel': cMeta.logTunnel,
    '--qtc-log-connect': cMeta.logConnect,
    '--qtc-log-disconnect': cMeta.logDisconnect,
    '--bg': tk.workspace,
    '--paper': tk.bg1,
    '--surface': tk.bg3,
    '--surface-2': tk.bg2,
    '--surface-3': tk.bg4,
    '--ink': tk.text,
    '--ink-2': tk.textMuted,
    '--ink-3': tk.textDisabled,
    '--muted': tk.textMuted,
    '--line': tk.border,
    '--line-2': tk.borderHover,
    '--accent': tk.primary,
    '--accent-2': tk.primaryHover,
    '--accent-fg': tk.primaryFg,
    '--ok': tk.ok,
    '--warn': tk.warn,
    '--err': tk.err,
    '--bad': tk.err,
    '--info': tk.info,
    '--mono': "'QtHack', 'QtJetBrainsMono', 'Consolas', monospace",
  }
  for (const [k, v] of Object.entries(vars)) root.style.setProperty(k, v)
}
