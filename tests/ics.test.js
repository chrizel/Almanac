// Standalone tests for src/pkjs/ics.js — run with: node test_ics.js
var ics = require('../src/pkjs/ics.js');

var failures = 0;
function eq(name, actual, expected) {
  var a = JSON.stringify(actual), e = JSON.stringify(expected);
  if (a === e) { console.log('PASS ' + name); }
  else { console.log('FAIL ' + name + '\n  expected ' + e + '\n  actual   ' + a); failures++; }
}

// ---- Google Calendar style export ----
var GOOGLE = [
  'BEGIN:VCALENDAR',
  'PRODID:-//Google Inc//Google Calendar 70.9054//EN',
  'VERSION:2.0',
  'BEGIN:VTIMEZONE',
  'TZID:Europe/Berlin',
  'END:VTIMEZONE',
  'BEGIN:VEVENT',
  'DTSTART;TZID=Europe/Berlin:20260810T090000',
  'DTEND;TZID=Europe/Berlin:20260810T091500',
  'RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR',
  'EXDATE;TZID=Europe/Berlin:20260817T090000',
  'DTSTAMP:20260801T000000Z',
  'UID:standup-123@google.com',
  'SUMMARY:Daily Standup',
  'LOCATION:Zoom',
  'STATUS:CONFIRMED',
  'END:VEVENT',
  'BEGIN:VEVENT',
  'DTSTART;TZID=Europe/Berlin:20260815T150000',
  'DTEND;TZID=Europe/Berlin:20260815T160000',
  'UID:review-456@google.com',
  'SUMMARY:Design Review with a very long title that keeps going',
  'LOCATION:Office HQ\\, Room B2',
  'END:VEVENT',
  'BEGIN:VEVENT',
  'DTSTART;VALUE=DATE:20260815',
  'DTEND;VALUE=DATE:20260816',
  'UID:allday-789@google.com',
  'SUMMARY:Company Holiday',
  'END:VEVENT',
  'END:VCALENDAR'
].join('\r\n');

// Sat 2026-08-15 14:28 local — one-off event today at 15:00 wins
var r = ics.nextEvent(GOOGLE, new Date(2026, 7, 15, 14, 28));
eq('google: today one-off beats all-day', r, {
  day: 0, startMin: 900,
  title: 'Design Review with a very long title that keeps going',
  location: 'Office HQ, Room B2'
});

// Sat 2026-08-15 16:00 — today's event passed; Sunday has nothing (standup is MO-FR),
// so nothing today/tomorrow
r = ics.nextEvent(GOOGLE, new Date(2026, 7, 15, 16, 0));
eq('google: weekend after event -> none', r, null);

// Sun 2026-08-16 20:00 — tomorrow (Monday) 9:00 standup... but 8/17 is EXDATEd
r = ics.nextEvent(GOOGLE, new Date(2026, 7, 16, 20, 0));
eq('google: EXDATE suppresses Monday standup', r, null);

// Mon 2026-08-17 20:00 — tomorrow Tue 8/18 standup at 9:00
r = ics.nextEvent(GOOGLE, new Date(2026, 7, 17, 20, 0));
eq('google: tomorrow weekly standup', r, { day: 1, startMin: 540, title: 'Daily Standup', location: 'Zoom' });

// Tue 2026-08-18 08:58 — standup in 2 min, today
r = ics.nextEvent(GOOGLE, new Date(2026, 7, 18, 8, 58));
eq('google: standup today', r, { day: 0, startMin: 540, title: 'Daily Standup', location: 'Zoom' });

// Tue 2026-08-18 09:03 — started 3 min ago, still within 5-min grace
r = ics.nextEvent(GOOGLE, new Date(2026, 7, 18, 9, 3));
eq('google: grace window keeps just-started event', r, { day: 0, startMin: 540, title: 'Daily Standup', location: 'Zoom' });

// ---- Outlook style: folded lines, UTC times, DAILY with COUNT ----
var OUTLOOK = [
  'BEGIN:VCALENDAR',
  'PRODID:Microsoft Exchange Server 2010',
  'VERSION:2.0',
  'BEGIN:VEVENT',
  'DTSTART:20260814T120000Z',
  'DTEND:20260814T123000Z',
  'RRULE:FREQ=DAILY;COUNT=5',
  'UID:daily-outlook-1',
  'SUMMARY:Lunch sync with the platf',
  ' orm team',   // folded continuation line
  'LOCATION:Cafeteria',
  'END:VEVENT',
  'BEGIN:VEVENT',
  'DTSTART:20260820T070000Z',
  'DTEND:20260820T080000Z',
  'RRULE:FREQ=MONTHLY;BYMONTHDAY=20',
  'UID:monthly-1',
  'SUMMARY:Monthly Review',
  'END:VEVENT',
  'END:VCALENDAR'
].join('\r\n');

// COUNT=5 from Aug 14 -> occurrences Aug 14..18. On Aug 17 (UTC noon = local?)
// Use a now where local == whatever; check Aug 17 exists, Aug 19 does not.
var utcNoonLocal = new Date(Date.UTC(2026, 7, 17, 8, 0)); // before that day's occurrence
r = ics.nextEvent(OUTLOOK, utcNoonLocal);
var expStart = new Date(Date.UTC(2026, 7, 17, 12, 0));
eq('outlook: folded SUMMARY + daily COUNT day-of', r && r.title, 'Lunch sync with the platform team');
eq('outlook: daily COUNT occurrence time', r && r.startMin, expStart.getHours() * 60 + expStart.getMinutes());

// Aug 19: COUNT exhausted (last occurrence Aug 18) -> next is tomorrow's Monthly Review
r = ics.nextEvent(OUTLOOK, new Date(Date.UTC(2026, 7, 19, 8, 0)));
var monthlyLocal = new Date(Date.UTC(2026, 7, 20, 7, 0));
eq('outlook: daily COUNT exhausted -> monthly tomorrow', r,
   { day: 1, startMin: monthlyLocal.getHours() * 60 + monthlyLocal.getMinutes(),
     title: 'Monthly Review', location: '' });

// Aug 20 monthly: unsupported freq falls back to literal DTSTART (Aug 20) — visible day-of
r = ics.nextEvent(OUTLOOK, new Date(Date.UTC(2026, 7, 20, 5, 0)));
eq('outlook: monthly literal DTSTART', r && r.title, 'Monthly Review');

// ---- UNTIL, INTERVAL=2 weekly, RECURRENCE-ID override, CANCELLED ----
var EDGE = [
  'BEGIN:VCALENDAR',
  'BEGIN:VEVENT',
  'DTSTART:20260803T100000',      // Mon, floating local time
  'DTEND:20260803T110000',
  'RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=MO;UNTIL=20260831T235959',
  'UID:biweekly-1',
  'SUMMARY:Biweekly 1:1',
  'END:VEVENT',
  'BEGIN:VEVENT',
  'DTSTART:20260818T140000',
  'UID:cancelled-1',
  'SUMMARY:Cancelled Meeting',
  'STATUS:CANCELLED',
  'END:VEVENT',
  'BEGIN:VEVENT',
  'DTSTART:20260817T090000',
  'RRULE:FREQ=DAILY',
  'UID:moved-1',
  'SUMMARY:Morning Check',
  'END:VEVENT',
  'BEGIN:VEVENT',
  'DTSTART:20260818T110000',
  'RECURRENCE-ID:20260818T090000',
  'UID:moved-1',
  'SUMMARY:Morning Check (moved)',
  'END:VEVENT',
  'END:VCALENDAR'
].join('\r\n');

// Mon 2026-08-17 is an ON week (Aug 3 + 2 weeks). At 8:00 that day: biweekly at 10:00
// vs Morning Check daily at 9:00 -> Morning Check wins (earlier).
r = ics.nextEvent(EDGE, new Date(2026, 7, 17, 8, 0));
eq('edge: earliest of multiple wins', r, { day: 0, startMin: 540, title: 'Morning Check', location: '' });

// Mon 2026-08-17 09:30: Morning Check passed -> biweekly 10:00
r = ics.nextEvent(EDGE, new Date(2026, 7, 17, 9, 30));
eq('edge: biweekly ON week', r, { day: 0, startMin: 600, title: 'Biweekly 1:1', location: '' });

// Mon 2026-08-24 09:30 is an OFF week: no biweekly; next is daily tomorrow 9:00... wait,
// daily Morning Check occurs every day, so 8/24 09:30 -> passed today (>5min), tomorrow 9:00.
r = ics.nextEvent(EDGE, new Date(2026, 7, 24, 9, 30));
eq('edge: biweekly OFF week -> daily tomorrow', r, { day: 1, startMin: 540, title: 'Morning Check', location: '' });

// Tue 2026-08-18: daily occurrence at 9:00 was MOVED to 11:00 via RECURRENCE-ID.
// At 08:00 the 9:00 slot must NOT appear; next is the override at 11:00.
r = ics.nextEvent(EDGE, new Date(2026, 7, 18, 8, 0));
eq('edge: RECURRENCE-ID override replaces original', r, { day: 0, startMin: 660, title: 'Morning Check (moved)', location: '' });

// Cancelled event at 14:00 Aug 18 must never appear. At 12:00 -> next is tomorrow's daily 9:00.
r = ics.nextEvent(EDGE, new Date(2026, 7, 18, 12, 0));
eq('edge: STATUS:CANCELLED skipped', r, { day: 1, startMin: 540, title: 'Morning Check', location: '' });

// UNTIL: Mon 2026-09-14 would be an ON week but is past UNTIL=Aug 31.
r = ics.nextEvent([
  'BEGIN:VCALENDAR',
  'BEGIN:VEVENT',
  'DTSTART:20260803T100000',
  'RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=MO;UNTIL=20260831T235959',
  'UID:u1',
  'SUMMARY:Biweekly 1:1',
  'END:VEVENT',
  'END:VCALENDAR'
].join('\r\n'), new Date(2026, 8, 14, 8, 0));
eq('edge: UNTIL expires rule', r, null);

// Old daily standup (started 2020) still found without COUNT — window jump works.
r = ics.nextEvent([
  'BEGIN:VCALENDAR',
  'BEGIN:VEVENT',
  'DTSTART:20200106T093000',
  'RRULE:FREQ=DAILY',
  'UID:old1',
  'SUMMARY:Old Daily',
  'END:VEVENT',
  'END:VCALENDAR'
].join('\r\n'), new Date(2026, 7, 15, 8, 0));
eq('edge: 6-year-old daily rule', r, { day: 0, startMin: 570, title: 'Old Daily', location: '' });

// Old weekly rule (started 2019) — weekly window jump works.
r = ics.nextEvent([
  'BEGIN:VCALENDAR',
  'BEGIN:VEVENT',
  'DTSTART:20190107T140000',
  'RRULE:FREQ=WEEKLY;BYDAY=MO,TH',
  'UID:old2',
  'SUMMARY:Old Weekly',
  'END:VEVENT',
  'END:VCALENDAR'
].join('\r\n'), new Date(2026, 7, 16, 12, 0)); // Sunday -> tomorrow Monday 14:00
eq('edge: 7-year-old weekly rule', r, { day: 1, startMin: 840, title: 'Old Weekly', location: '' });

process.exit(failures ? 1 : 0);
