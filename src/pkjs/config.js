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
