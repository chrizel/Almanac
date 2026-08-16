module.exports = [
  {
    "type": "heading",
    "defaultValue": "Percival Settings"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "color",
        "messageKey": "PrimaryColor",
        "label": "Accent Color",
        "defaultValue": "0x0000FF"
      },
      {
        "type": "select",
        "messageKey": "TempUnit",
        "label": "Temperature Unit",
        "defaultValue": 0,
        "options": [
          {"label": "Fahrenheit (°F)", "value": 0},
          {"label": "Celsius (°C)", "value": 1}
        ]
      },
      {
        "type": "select",
        "messageKey": "TopLeft",
        "label": "Top Left Display",
        "defaultValue": 0,
        "options": [
          {"label": "None", "value": 0},
          {"label": "Steps", "value": 1},
          {"label": "Heart Rate", "value": 2},
          {"label": "Battery", "value": 3}
        ]
      },
      {
        "type": "select",
        "messageKey": "Language",
        "label": "Language",
        "defaultValue": 0,
        "options": [
          {"label": "English", "value": 0},
          {"label": "Deutsch", "value": 1},
          {"label": "Français", "value": 2},
          {"label": "Español", "value": 3},
          {"label": "Italiano", "value": 4},
          {"label": "Português", "value": 5},
          {"label": "Nederlands", "value": 6}
        ]
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Calendar"
      },
      {
        "type": "input",
        "messageKey": "IcsUrl",
        "label": "ICS Feed URL",
        "defaultValue": "",
        "description": "Paste your calendar's private ICS address (Google: Settings → your calendar → Secret address in iCal format). Leave empty to hide the event block.",
        "attributes": {
          "placeholder": "https://…/basic.ics",
          "type": "url"
        }
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
