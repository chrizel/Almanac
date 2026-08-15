var WEATHER_POLL_MINUTES = 30;

var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);
var ics = require('./ics');

var xhrRequest = function (url, type, callback) {
  var xhr = new XMLHttpRequest();
  xhr.timeout = 15000;
  xhr.onload = function () {
    callback(xhr.status === 200 ? this.responseText : null);
  };
  xhr.onerror = function () { callback(null); };
  xhr.ontimeout = function () { callback(null); };
  xhr.open(type, url);
  xhr.send();
};

// Serialize outgoing AppMessages — weather and calendar sends can otherwise
// race when both fetches resolve at the same time.
var sendQueue = [];
var sending = false;

function drainQueue() {
  if (sending || !sendQueue.length) return;
  sending = true;
  var item = sendQueue.shift();
  Pebble.sendAppMessage(item.msg,
    function () {
      console.log(item.label + ' sent successfully');
      sending = false;
      drainQueue();
    },
    function (e) {
      console.log('Error sending ' + item.label + ': ' + JSON.stringify(e));
      sending = false;
      drainQueue();
    });
}

function enqueueSend(msg, label) {
  sendQueue.push({ msg: msg, label: label });
  drainQueue();
}

function getClaySetting(key) {
  try {
    var settings = JSON.parse(localStorage.getItem('clay-settings'));
    if (settings && settings[key] != null) return settings[key];
  } catch (e) {}
  return null;
}

function getTempUnit() {
  var unit = getClaySetting('TempUnit');
  return unit != null && parseInt(unit, 10) === 1 ? 'celsius' : 'fahrenheit';
}

// WMO weather code -> condition enum shared with main.c:
// 0 clear · 1 partly · 2 overcast · 3 fog · 4 rain · 5 snow · 6 storm
function conditionFromWmo(code) {
  if (code === 0) return 0;
  if (code === 1 || code === 2) return 1;
  if (code === 3) return 2;
  if (code === 45 || code === 48) return 3;
  if (code >= 51 && code <= 67) return 4;   // drizzle + rain + freezing rain
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return 5;
  if (code >= 80 && code <= 82) return 4;   // rain showers
  if (code >= 95) return 6;
  return 2;
}

// "2026-08-15T06:21" (local, timezone=auto) -> minutes since midnight
function minutesFromIso(isoString) {
  var parts = isoString.split('T')[1].split(':');
  return parseInt(parts[0], 10) * 60 + parseInt(parts[1], 10);
}

function locationSuccess(pos) {
  var lat = pos.coords.latitude;
  var lon = pos.coords.longitude;

  var weatherUrl = 'https://api.open-meteo.com/v1/forecast?' +
    'latitude=' + lat + '&longitude=' + lon +
    '&current=temperature_2m,weather_code' +
    '&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset' +
    '&temperature_unit=' + getTempUnit() +
    '&timezone=auto' +
    '&forecast_days=1';

  xhrRequest(weatherUrl, 'GET', function (weatherResp) {
    if (!weatherResp) {
      console.log('Weather request failed');
      return;
    }
    var w;
    try {
      w = JSON.parse(weatherResp);
      enqueueSend({
        'TEMPERATURE': Math.round(w.current.temperature_2m),
        'TEMP_HIGH': Math.round(w.daily.temperature_2m_max[0]),
        'TEMP_LOW': Math.round(w.daily.temperature_2m_min[0]),
        'CONDITION': conditionFromWmo(w.current.weather_code),
        'SUNRISE_MIN': minutesFromIso(w.daily.sunrise[0]),
        'SUNSET_MIN': minutesFromIso(w.daily.sunset[0])
      }, 'weather');
    } catch (e) {
      console.log('Weather parse error: ' + e);
    }
  });
}

function locationError(err) {
  console.log('Error requesting location: ' + err);
}

function getWeather() {
  navigator.geolocation.getCurrentPosition(
    locationSuccess,
    locationError,
    { timeout: 15000, maximumAge: WEATHER_POLL_MINUTES * 60 * 1000 }
  );
}

// ---- Calendar (ICS) ----

function utf8ByteLength(s) {
  var bytes = 0;
  for (var i = 0; i < s.length; i++) {
    var c = s.charCodeAt(i);
    if (c < 0x80) bytes += 1;
    else if (c < 0x800) bytes += 2;
    else if (c >= 0xd800 && c <= 0xdbff) { bytes += 4; i++; }  // surrogate pair
    else bytes += 3;
  }
  return bytes;
}

// Truncate by characters AND UTF-8 bytes: the C-side buffers are 28 bytes,
// and cutting mid-character would produce invalid UTF-8 the watch won't draw.
function truncate(s, n, maxBytes) {
  s = String(s || '');
  if (s.length > n) s = s.slice(0, n);
  while (utf8ByteLength(s) > maxBytes) s = s.slice(0, -1);
  return s;
}

function sendEvent(ev) {
  enqueueSend({
    'EVENT_DAY': ev ? ev.day : -1,
    'EVENT_START_MIN': ev ? ev.startMin : 0,
    'EVENT_TITLE': ev ? truncate(ev.title, 24, 27) : '',
    'EVENT_LOCATION': ev ? truncate(ev.location, 24, 27) : ''
  }, 'event');
}

function getCalendar() {
  var url = getClaySetting('IcsUrl');
  url = url ? String(url).trim() : '';
  if (!url) {
    sendEvent(null);
    return;
  }
  url = url.replace(/^webcal:\/\//i, 'https://');

  xhrRequest(url, 'GET', function (resp) {
    if (!resp) {
      console.log('ICS request failed');
      sendEvent(null);
      return;
    }
    var ev = null;
    try {
      ev = ics.nextEvent(resp, new Date());
    } catch (e) {
      console.log('ICS parse error: ' + e);
    }
    sendEvent(ev);
  });
}

// Weather and calendar fire from the same triggers but run independently —
// a slow or failing ICS fetch never blocks the weather send.
function refreshAll() {
  getWeather();
  getCalendar();
}

Pebble.addEventListener('ready', function (e) {
  console.log('PebbleKit JS ready');
  refreshAll();
});

Pebble.addEventListener('appmessage', function (e) {
  console.log('AppMessage received');
  refreshAll();
});

// Clay's own webviewclosed handler runs first and persists the new
// settings, so this fetch picks up changed unit / ICS URL immediately.
Pebble.addEventListener('webviewclosed', function (e) {
  if (e && e.response) {
    refreshAll();
  }
});
