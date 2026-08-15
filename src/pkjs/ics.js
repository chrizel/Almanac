// Minimal ICS (RFC 5545) parser for the next-event complication.
// Pure functions, no Pebble dependencies — testable standalone with node.
//
// Scope (deliberate):
// - All-day events (VALUE=DATE) are skipped.
// - TZID datetimes are treated as phone-local time (no VTIMEZONE engine);
//   correct whenever the calendar's timezone matches the phone's.
// - Recurrence expansion covers FREQ=DAILY and FREQ=WEEKLY (with BYDAY,
//   INTERVAL, UNTIL, COUNT, EXDATE). Other frequencies fall back to the
//   literal DTSTART.

var DAY_MS = 86400000;
var BYDAY_CODES = { SU: 0, MO: 1, TU: 2, WE: 3, TH: 4, FR: 5, SA: 6 };

// "20260815T130000Z" / "20260815T150000" / "20260815" -> {allDay, date}
function parseIcsDate(value) {
  var m = /^(\d{4})(\d{2})(\d{2})(?:T(\d{2})(\d{2})(\d{2})?(Z)?)?$/.exec(value);
  if (!m) return null;
  if (!m[4]) return { allDay: true, date: new Date(+m[1], +m[2] - 1, +m[3]) };
  var y = +m[1], mo = +m[2] - 1, d = +m[3], h = +m[4], mi = +m[5], s = +(m[6] || 0);
  var date = m[7] ? new Date(Date.UTC(y, mo, d, h, mi, s)) : new Date(y, mo, d, h, mi, s);
  return { allDay: false, date: date };
}

function unescapeText(s) {
  return s.replace(/\\n/gi, ' ')
          .replace(/\\,/g, ',')
          .replace(/\\;/g, ';')
          .replace(/\\\\/g, '\\');
}

// Unfold continuation lines (CRLF or LF followed by space/tab), split into lines.
function unfoldLines(text) {
  return text.replace(/\r\n[ \t]/g, '').replace(/\n[ \t]/g, '').split(/\r?\n/);
}

// "DTSTART;TZID=Europe/Berlin:20260815T150000" -> {name, params:{TZID:...}, value}
function parseLine(line) {
  var colon = -1, inQuotes = false;
  for (var i = 0; i < line.length; i++) {
    var c = line.charAt(i);
    if (c === '"') inQuotes = !inQuotes;
    else if (c === ':' && !inQuotes) { colon = i; break; }
  }
  if (colon < 0) return null;
  var head = line.slice(0, colon).split(';');
  var params = {};
  for (var j = 1; j < head.length; j++) {
    var eq = head[j].indexOf('=');
    if (eq > 0) params[head[j].slice(0, eq).toUpperCase()] = head[j].slice(eq + 1).replace(/"/g, '');
  }
  return { name: head[0].toUpperCase(), params: params, value: line.slice(colon + 1) };
}

function parseRrule(value) {
  var rule = {};
  value.split(';').forEach(function (part) {
    var eq = part.indexOf('=');
    if (eq > 0) rule[part.slice(0, eq).toUpperCase()] = part.slice(eq + 1);
  });
  return rule;
}

// Parse all VEVENTs out of the ICS text.
function parseEvents(text) {
  var lines = unfoldLines(text);
  var events = [];
  var cur = null;
  for (var i = 0; i < lines.length; i++) {
    var line = lines[i];
    if (line === 'BEGIN:VEVENT') { cur = { exdates: [] }; continue; }
    if (line === 'END:VEVENT') { if (cur) events.push(cur); cur = null; continue; }
    if (!cur) continue;
    var p = parseLine(line);
    if (!p) continue;
    switch (p.name) {
      case 'DTSTART': {
        var parsed = parseIcsDate(p.value);
        if (parsed) {
          cur.allDay = parsed.allDay || p.params.VALUE === 'DATE';
          cur.start = parsed.date;
        }
        break;
      }
      case 'DTEND': {
        var pe = parseIcsDate(p.value);
        if (pe) cur.end = pe.date;
        break;
      }
      case 'SUMMARY': cur.summary = unescapeText(p.value); break;
      case 'LOCATION': cur.location = unescapeText(p.value); break;
      case 'RRULE': cur.rrule = parseRrule(p.value); break;
      case 'UID': cur.uid = p.value; break;
      case 'STATUS': cur.cancelled = p.value.toUpperCase() === 'CANCELLED'; break;
      case 'RECURRENCE-ID': {
        var pr = parseIcsDate(p.value);
        if (pr) cur.recurrenceId = pr.date.getTime();
        break;
      }
      case 'EXDATE': {
        p.value.split(',').forEach(function (v) {
          var px = parseIcsDate(v);
          if (px) cur.exdates.push(px.date.getTime());
        });
        break;
      }
    }
  }

  // A moved instance (RECURRENCE-ID override) suppresses the master's
  // original occurrence at that time; the override itself is a normal event.
  var byUid = {};
  events.forEach(function (ev) {
    if (ev.uid && !ev.recurrenceId) byUid[ev.uid] = ev;
  });
  events.forEach(function (ev) {
    if (ev.uid && ev.recurrenceId && byUid[ev.uid]) {
      byUid[ev.uid].exdates.push(ev.recurrenceId);
    }
  });
  return events;
}

// Local-date arithmetic (DST-safe: keeps wall-clock time).
function addDays(date, n) {
  return new Date(date.getFullYear(), date.getMonth(), date.getDate() + n,
                  date.getHours(), date.getMinutes(), date.getSeconds());
}

function startOfWeek(date) { // Monday-based (RFC 5545 default WKST=MO)
  var dow = (date.getDay() + 6) % 7;
  return new Date(date.getFullYear(), date.getMonth(), date.getDate() - dow);
}

function weeksBetween(a, b) {
  return Math.round((startOfWeek(b) - startOfWeek(a)) / (7 * DAY_MS));
}

function isExcluded(ev, t) {
  var ms = t.getTime();
  for (var i = 0; i < ev.exdates.length; i++) {
    if (ev.exdates[i] === ms) return true;
  }
  return false;
}

function ruleUntilMs(rule) {
  if (!rule.UNTIL) return null;
  var p = parseIcsDate(rule.UNTIL);
  if (!p) return null;
  // date-only UNTIL means "through the end of that day"
  return p.allDay ? p.date.getTime() + DAY_MS - 1 : p.date.getTime();
}

function bydaySet(rule, start) {
  if (!rule.BYDAY) return [start.getDay()];
  var days = [];
  rule.BYDAY.split(',').forEach(function (code) {
    var d = BYDAY_CODES[code.replace(/^[+-]?\d+/, '')];
    if (d !== undefined) days.push(d);
  });
  return days.length ? days : [start.getDay()];
}

// All occurrence start times of ev inside [windowStart, windowEnd] (ms).
function expandOccurrences(ev, windowStart, windowEnd) {
  var results = [];
  function push(t) {
    var ms = t.getTime();
    if (ms >= windowStart && ms <= windowEnd && !isExcluded(ev, t)) results.push(t);
  }

  if (!ev.rrule) {
    push(ev.start);
    return results;
  }
  var rule = ev.rrule;
  var freq = (rule.FREQ || '').toUpperCase();
  if (freq !== 'DAILY' && freq !== 'WEEKLY') {
    push(ev.start); // unsupported frequency: literal DTSTART only
    return results;
  }

  var interval = parseInt(rule.INTERVAL || '1', 10) || 1;
  var count = rule.COUNT ? parseInt(rule.COUNT, 10) : null;
  var untilMs = ruleUntilMs(rule);
  var occurrenceIndex = 0; // counts ALL occurrences from DTSTART (for COUNT)
  var MAX_STEPS = 4000;    // ~11 years of daily steps; older COUNT rules are expired anyway

  if (freq === 'DAILY') {
    var n0 = 0;
    if (count === null) { // no COUNT: jump straight to the window
      n0 = Math.max(0, Math.floor((windowStart - ev.start.getTime()) / (interval * DAY_MS)) - 1);
      if (n0 < 0) n0 = 0;
    }
    for (var n = n0; n - n0 < MAX_STEPS; n++) {
      if (count !== null && n >= count) break;
      var t = addDays(ev.start, n * interval);
      if (untilMs !== null && t.getTime() > untilMs) break;
      if (t.getTime() > windowEnd) break;
      push(t);
    }
  } else { // WEEKLY
    var days = bydaySet(rule, ev.start);
    var d0 = 0;
    if (count === null) {
      d0 = Math.max(0, Math.floor((windowStart - ev.start.getTime()) / DAY_MS) - 8);
    }
    for (var d = d0; d - d0 < MAX_STEPS; d++) {
      var td = addDays(ev.start, d);
      if (td.getTime() > windowEnd) break;
      if (untilMs !== null && td.getTime() > untilMs) break;
      if (days.indexOf(td.getDay()) < 0) continue;
      if (weeksBetween(ev.start, td) % interval !== 0) continue;
      if (count !== null) {
        occurrenceIndex++;
        if (occurrenceIndex > count) break;
      }
      push(td);
    }
  }
  return results;
}

// The next timed event starting >= now - 5min, today or tomorrow.
// Returns {day: 0|1, startMin, title, location} or null.
function nextEvent(icsText, now) {
  var events = parseEvents(icsText);
  var windowStart = now.getTime() - 5 * 60000;
  var endOfTomorrow = new Date(now.getFullYear(), now.getMonth(), now.getDate() + 2).getTime() - 1;

  var best = null;
  events.forEach(function (ev) {
    if (!ev.start || ev.allDay || ev.cancelled) return;
    expandOccurrences(ev, windowStart, endOfTomorrow).forEach(function (t) {
      if (!best || t.getTime() < best.time) best = { time: t.getTime(), date: t, ev: ev };
    });
  });
  if (!best) return null;

  var d = best.date;
  var today = new Date(now.getFullYear(), now.getMonth(), now.getDate());
  var evDay = new Date(d.getFullYear(), d.getMonth(), d.getDate());
  var dayIdx = Math.round((evDay - today) / DAY_MS);
  if (dayIdx < 0) dayIdx = 0; // started within the 5-minute grace window
  if (dayIdx > 1) return null;

  return {
    day: dayIdx,
    startMin: d.getHours() * 60 + d.getMinutes(),
    title: best.ev.summary || '',
    location: best.ev.location || ''
  };
}

module.exports = {
  nextEvent: nextEvent,
  parseEvents: parseEvents,
  expandOccurrences: expandOccurrences,
  parseIcsDate: parseIcsDate
};
