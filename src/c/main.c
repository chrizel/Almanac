#include <pebble.h>

// Uncomment to inject fake data for screenshots (see the
// marketing-screenshots skill). Also disables AppMessage so the JS
// companion cannot overwrite the stubs.
// #define DEMO

#ifdef DEMO
#define DEMO_ACCENT GColorBlue
#define DEMO_TIME_TEXT "14:28"
#define DEMO_NOW_MIN 868          // 14:28
#define DEMO_DOW "SAT"
#define DEMO_DAY "15"
#define DEMO_TODAY 20260815
#define DEMO_TEMP 84
#define DEMO_HIGH 91
#define DEMO_LOW 67
#define DEMO_CONDITION COND_CLEAR
#define DEMO_SUNRISE_MIN 381      // 6:21
#define DEMO_SUNSET_MIN 1234      // 20:34
#define DEMO_EVENT_START_MIN 900  // 15:00
#define DEMO_EVENT_DAY 0          // 0 today · 1 tomorrow · -1 none
#define DEMO_EVENT_TITLE "Design Review"
#define DEMO_EVENT_LOCATION "Office HQ, Room B2"
#endif

extern uint32_t MESSAGE_KEY_TEMPERATURE;
extern uint32_t MESSAGE_KEY_TEMP_HIGH;
extern uint32_t MESSAGE_KEY_TEMP_LOW;
extern uint32_t MESSAGE_KEY_PrimaryColor;
extern uint32_t MESSAGE_KEY_CONDITION;
extern uint32_t MESSAGE_KEY_SUNRISE_MIN;
extern uint32_t MESSAGE_KEY_SUNSET_MIN;
extern uint32_t MESSAGE_KEY_EVENT_DAY;
extern uint32_t MESSAGE_KEY_EVENT_START_MIN;
extern uint32_t MESSAGE_KEY_EVENT_TITLE;
extern uint32_t MESSAGE_KEY_EVENT_LOCATION;

// Shared with conditionFromWmo() in src/pkjs/index.js
enum Condition {
  COND_CLEAR = 0,
  COND_PARTLY = 1,
  COND_OVERCAST = 2,
  COND_FOG = 3,
  COND_RAIN = 4,
  COND_SNOW = 5,
  COND_STORM = 6
};

#define WEATHER_POLL_MINUTES 30
#define SETTINGS_KEY 1
#define OLD_WEATHER_KEY 2  // v1.x string cache — deleted on first run
#define WEATHER_KEY 3
#define EVENT_KEY 4

static Window *s_main_window;
static Layer *s_window_layer;
static Layer *s_date_layer;
static Layer *s_time_layer;
static Layer *s_event_layer;
static Layer *s_weather_layer;
static Layer *s_bt_layer;
static Layer *s_qt_layer;
static bool s_bt_app_connected;
static bool s_bt_radio_connected;

static GFont s_font_14;
static GFont s_font_16;
static GFont s_font_18;
static GFont s_font_20;
static GFont s_font_40;
static GFont s_font_68;

static GPath *s_bolt_path;

static const GPathInfo BOLT_INFO = {
  .num_points = 7,
  .points = (GPoint[]) {{1, -3}, {-6, 8}, {-1, 8}, {-4, 18}, {8, 6}, {3, 6}, {7, -3}}
};

// New fields must be appended to the end — never insert or reorder.
// load_settings() reads stored bytes and zero-fills the rest, so
// existing users keep their settings when the struct grows.
// The v1.x complication/canvas fields are kept (ignored) so stored
// bytes still map correctly; primary_color is now the event accent.
typedef struct {
  GColor primary_color;
  uint8_t mini_comp_left;
  uint8_t mini_comp_middle;
  uint8_t mini_comp_right;
  uint8_t bottom_comp_left;
  uint8_t bottom_comp_primary;
  uint8_t bottom_comp_right;
  uint8_t canvas;
} Settings;

static Settings s_settings;

static void default_settings() {
  memset(&s_settings, 0, sizeof(s_settings));
  s_settings.primary_color = GColorBlue;
}

static void load_settings() {
  default_settings();
#ifdef DEMO
  s_settings.primary_color = DEMO_ACCENT;
#else
  if (persist_exists(SETTINGS_KEY)) {
    int stored = persist_get_size(SETTINGS_KEY);
    if (stored > 0 && (size_t)stored <= sizeof(s_settings)) {
      persist_read_data(SETTINGS_KEY, &s_settings, stored);
    }
  }
#endif
}

static void save_settings() {
  persist_write_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
}

typedef struct {
  int16_t temp;
  int16_t high;
  int16_t low;
  int16_t condition;
  int16_t sunrise_min;
  int16_t sunset_min;
  bool loaded;
} WeatherCache;

static WeatherCache s_weather;

typedef struct {
  int32_t civil_date;  // YYYYMMDD of the event's day; 0 = no event
  int16_t start_min;   // minutes since midnight
  char title[28];
  char location[28];
} EventCache;

static EventCache s_event;

static void load_weather() {
  memset(&s_weather, 0, sizeof(s_weather));
#ifdef DEMO
  s_weather.temp = DEMO_TEMP;
  s_weather.high = DEMO_HIGH;
  s_weather.low = DEMO_LOW;
  s_weather.condition = DEMO_CONDITION;
  s_weather.sunrise_min = DEMO_SUNRISE_MIN;
  s_weather.sunset_min = DEMO_SUNSET_MIN;
  s_weather.loaded = true;
#else
  if (persist_exists(WEATHER_KEY)) {
    int stored = persist_get_size(WEATHER_KEY);
    if (stored > 0 && (size_t)stored <= sizeof(s_weather)) {
      persist_read_data(WEATHER_KEY, &s_weather, stored);
    }
  }
#endif
}

static void save_weather() {
  persist_write_data(WEATHER_KEY, &s_weather, sizeof(s_weather));
}

static void load_event() {
  memset(&s_event, 0, sizeof(s_event));
#ifdef DEMO
  #if DEMO_EVENT_DAY >= 0
  s_event.civil_date = DEMO_TODAY + DEMO_EVENT_DAY;
  s_event.start_min = DEMO_EVENT_START_MIN;
  snprintf(s_event.title, sizeof(s_event.title), DEMO_EVENT_TITLE);
  snprintf(s_event.location, sizeof(s_event.location), DEMO_EVENT_LOCATION);
  #endif
#else
  if (persist_exists(EVENT_KEY)) {
    int stored = persist_get_size(EVENT_KEY);
    if (stored > 0 && (size_t)stored <= sizeof(s_event)) {
      persist_read_data(EVENT_KEY, &s_event, stored);
    }
  }
#endif
}

static void save_event() {
  persist_write_data(EVENT_KEY, &s_event, sizeof(s_event));
}

static char s_dow_buffer[6];
static char s_day_buffer[4];

static struct tm *get_time(struct tm *t) {
  if (t) return t;
  time_t now = time(NULL);
  return localtime(&now);
}

static int now_minutes() {
#ifdef DEMO
  return DEMO_NOW_MIN;
#else
  struct tm *t = get_time(NULL);
  return t->tm_hour * 60 + t->tm_min;
#endif
}

static int32_t yyyymmdd(struct tm *t) {
  return (t->tm_year + 1900) * 10000 + (t->tm_mon + 1) * 100 + t->tm_mday;
}

static int32_t today_civil_date() {
#ifdef DEMO
  return DEMO_TODAY;
#else
  return yyyymmdd(get_time(NULL));
#endif
}

static int32_t tomorrow_civil_date() {
#ifdef DEMO
  return DEMO_TODAY + 1;
#else
  time_t n = time(NULL) + 86400;
  return yyyymmdd(localtime(&n));
#endif
}

// "6:21" / "20:34" (24h) or "6:21" / "8:34" (12h, no am/pm marker)
static void format_clock_min(int minutes, char *buf, size_t size) {
  int h = minutes / 60;
  int m = minutes % 60;
  if (!clock_is_24h_style()) {
    h = h % 12;
    if (h == 0) h = 12;
  }
  snprintf(buf, size, "%d:%02d", h, m);
}

// ---- Time ----

static char s_time_buffer[8];

static void update_time(struct tm *tick_time) {
#ifdef DEMO
  snprintf(s_time_buffer, sizeof(s_time_buffer), DEMO_TIME_TEXT);
#else
  struct tm *t = get_time(tick_time);
  char raw[8];
  strftime(raw, sizeof(raw), clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
  char *display = raw;
  if (display[0] == '0') display++;
  snprintf(s_time_buffer, sizeof(s_time_buffer), "%s", display);
#endif
  if (s_time_layer) {
    layer_mark_dirty(s_time_layer);
  }
}

// Drawn glyph by glyph with negative tracking — wide 24h times ("20:34")
// don't fit at this size with the font's natural spacing, and Pebble has
// no letter-spacing equivalent.
#define TIME_TRACKING -5

static void time_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int n = strlen(s_time_buffer);
  if (n == 0 || n > 7) return;

  GRect probe = GRect(0, 0, b.size.w, b.size.h);
  char glyph[2] = {0, 0};
  int widths[8];
  int total = TIME_TRACKING * (n - 1);
  for (int i = 0; i < n; i++) {
    glyph[0] = s_time_buffer[i];
    widths[i] = graphics_text_layout_get_content_size(
        glyph, s_font_68, probe, GTextOverflowModeWordWrap, GTextAlignmentLeft).w;
    total += widths[i];
  }

  int x = b.size.w - 10 - total;
  graphics_context_set_text_color(ctx, GColorBlack);
  for (int i = 0; i < n; i++) {
    glyph[0] = s_time_buffer[i];
    graphics_draw_text(ctx, glyph, s_font_68, GRect(x, 0, widths[i] + 4, b.size.h),
                       GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
    x += widths[i] + TIME_TRACKING;
  }
}

// ---- Date label ("SAT 15", top right above the time) ----

static void update_date_label(struct tm *tick_time) {
  struct tm *t = get_time(tick_time);
  static const char *days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  snprintf(s_dow_buffer, sizeof(s_dow_buffer), "%s", days[t->tm_wday]);
  snprintf(s_day_buffer, sizeof(s_day_buffer), "%d", t->tm_mday);
#ifdef DEMO
  snprintf(s_dow_buffer, sizeof(s_dow_buffer), DEMO_DOW);
  snprintf(s_day_buffer, sizeof(s_day_buffer), DEMO_DAY);
#endif
  if (s_date_layer) {
    layer_mark_dirty(s_date_layer);
  }
}

static void date_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GRect probe = GRect(0, 0, b.size.w, 24);
  GSize day_size = graphics_text_layout_get_content_size(
      s_day_buffer, s_font_20, probe,
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  GSize dow_size = graphics_text_layout_get_content_size(
      s_dow_buffer, s_font_20, probe,
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  int day_x = b.size.w - 10 - day_size.w;
  int dow_x = day_x - 5 - dow_size.w;

  graphics_context_set_text_color(ctx, s_settings.primary_color);
  graphics_draw_text(ctx, s_dow_buffer, s_font_20, GRect(dow_x, 0, dow_size.w + 2, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_day_buffer, s_font_20, GRect(day_x, 0, day_size.w + 2, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

// ---- Event block ----

static void event_update_proc(Layer *layer, GContext *ctx) {
  if (s_event.civil_date == 0) return;

  int day;
  if (s_event.civil_date == today_civil_date()) {
    day = 0;
  } else if (s_event.civil_date == tomorrow_civil_date()) {
    day = 1;
  } else {
    return;  // stale cache (e.g. after midnight rollover past the event)
  }

  char timebuf[8];
  format_clock_min(s_event.start_min, timebuf, sizeof(timebuf));

  char when[40];
  if (day == 0) {
    int d = s_event.start_min - now_minutes();
    if (d < -5) return;  // passed; hidden until the next JS refresh replaces it
    if (d <= 0) {
      snprintf(when, sizeof(when), "%s \xC2\xB7 NOW", timebuf);
    } else if (d < 60) {
      snprintf(when, sizeof(when), "%s \xC2\xB7 IN %d MIN", timebuf, d);
    } else if (d % 60 == 0) {
      snprintf(when, sizeof(when), "%s \xC2\xB7 IN %d H", timebuf, d / 60);
    } else {
      snprintf(when, sizeof(when), "%s \xC2\xB7 IN %d H %d MIN", timebuf, d / 60, d % 60);
    }
  } else {
    snprintf(when, sizeof(when), "TOMORROW \xC2\xB7 %s", timebuf);
  }

  GRect b = layer_get_bounds(layer);
  const int when_h = 17;
  const int title_h = 22;
  const int loc_h = s_event.location[0] ? 17 : 0;
  const int tx = 23;
  const int tw = b.size.w - tx - 6;

  // Accent bar spanning the text block
  graphics_context_set_fill_color(ctx, s_settings.primary_color);
  graphics_fill_rect(ctx, GRect(10, 3, 5, when_h + title_h + loc_h - 1), 0, GCornerNone);

  graphics_context_set_text_color(ctx, s_settings.primary_color);
  graphics_draw_text(ctx, when, s_font_16, GRect(tx, 0, tw, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_event.title, s_font_20, GRect(tx, when_h, tw, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  if (loc_h) {
    graphics_draw_text(ctx, s_event.location, s_font_16, GRect(tx, when_h + title_h, tw, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

// ---- Weather (big icon · current temp · stacked hi/lo) ----

static void draw_cloud(GContext *ctx, int cx, int cy) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(cx - 6, cy + 1), 7);
  graphics_fill_circle(ctx, GPoint(cx + 3, cy - 3), 9);
  graphics_fill_rect(ctx, GRect(cx - 13, cy + 1, 27, 9), 4, GCornersAll);
}

static void draw_sun_rays(GContext *ctx, int cx, int cy, int r1, int r2) {
  static const int dirs[8][2] = {
    {10, 0}, {7, 7}, {0, 10}, {-7, 7}, {-10, 0}, {-7, -7}, {0, -10}, {7, -7}
  };
  for (int i = 0; i < 8; i++) {
    graphics_draw_line(ctx,
        GPoint(cx + dirs[i][0] * r1 / 10, cy + dirs[i][1] * r1 / 10),
        GPoint(cx + dirs[i][0] * r2 / 10, cy + dirs[i][1] * r2 / 10));
  }
}

static void draw_condition_icon(GContext *ctx, int cx, int cy, int cond, bool night) {
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);

  switch (cond) {
    case COND_CLEAR:
      if (night) {
        // Crescent: filled disc with a background-colored punch-out
        graphics_fill_circle(ctx, GPoint(cx, cy), 11);
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_circle(ctx, GPoint(cx + 7, cy - 7), 10);
      } else {
        graphics_fill_circle(ctx, GPoint(cx, cy), 6);
        draw_sun_rays(ctx, cx, cy, 10, 16);
      }
      break;
    case COND_PARTLY:
      // Small sun peeking out top-right, clearly separated from the cloud
      graphics_fill_circle(ctx, GPoint(cx + 8, cy - 11), 4);
      graphics_draw_line(ctx, GPoint(cx + 8, cy - 19), GPoint(cx + 8, cy - 17));
      graphics_draw_line(ctx, GPoint(cx + 14, cy - 17), GPoint(cx + 15, cy - 18));
      graphics_draw_line(ctx, GPoint(cx + 15, cy - 11), GPoint(cx + 16, cy - 11));
      draw_cloud(ctx, cx - 3, cy + 5);
      break;
    case COND_OVERCAST:
      draw_cloud(ctx, cx, cy);
      break;
    case COND_FOG:
      draw_cloud(ctx, cx, cy - 5);
      graphics_draw_line(ctx, GPoint(cx - 12, cy + 8), GPoint(cx + 12, cy + 8));
      graphics_draw_line(ctx, GPoint(cx - 9, cy + 13), GPoint(cx + 9, cy + 13));
      break;
    case COND_RAIN:
      draw_cloud(ctx, cx, cy - 4);
      for (int i = -1; i <= 1; i++) {
        graphics_draw_line(ctx, GPoint(cx + i * 7, cy + 8), GPoint(cx + i * 7 - 3, cy + 15));
      }
      break;
    case COND_SNOW:
      draw_cloud(ctx, cx, cy - 4);
      graphics_fill_circle(ctx, GPoint(cx - 7, cy + 11), 2);
      graphics_fill_circle(ctx, GPoint(cx, cy + 15), 2);
      graphics_fill_circle(ctx, GPoint(cx + 7, cy + 11), 2);
      break;
    case COND_STORM:
      draw_cloud(ctx, cx, cy - 6);
      gpath_move_to(s_bolt_path, GPoint(cx, cy));
      gpath_draw_filled(ctx, s_bolt_path);
      break;
  }
}

static void weather_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  const int center_y = b.size.h / 2;

  bool loaded = s_weather.loaded;
  int now_min = now_minutes();
  bool night = loaded && (now_min < s_weather.sunrise_min || now_min > s_weather.sunset_min);

  // \xE2\x86\x91 = ↑, \xE2\x86\x93 = ↓, \xC2\xB0 = °
  char cur[8], hi[12], lo[12];
  if (loaded) {
    snprintf(cur, sizeof(cur), "%d\xC2\xB0", s_weather.temp);
    snprintf(hi, sizeof(hi), "\xE2\x86\x91 %d\xC2\xB0", s_weather.high);
    snprintf(lo, sizeof(lo), "\xE2\x86\x93 %d\xC2\xB0", s_weather.low);
  } else {
    snprintf(cur, sizeof(cur), "--");
    snprintf(hi, sizeof(hi), "\xE2\x86\x91 --");
    snprintf(lo, sizeof(lo), "\xE2\x86\x93 --");
  }

  GRect probe = GRect(0, 0, b.size.w, 50);
  GSize cur_size = graphics_text_layout_get_content_size(
      cur, s_font_40, probe, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  GSize hi_size = graphics_text_layout_get_content_size(
      hi, s_font_18, probe, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  GSize lo_size = graphics_text_layout_get_content_size(
      lo, s_font_18, probe, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  int hilo_w = hi_size.w > lo_size.w ? hi_size.w : lo_size.w;

  const int icon_w = 36;
  const int gap = 9;
  int total = icon_w + gap + cur_size.w + gap + hilo_w;
  int x = (b.size.w - total) / 2;

  if (loaded) {
    draw_condition_icon(ctx, x + icon_w / 2, center_y, s_weather.condition, night);
  }

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, cur, s_font_40,
                     GRect(x + icon_w + gap, center_y - 26, cur_size.w + 2, 50),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  int hx = x + icon_w + gap + cur_size.w + gap;
  graphics_draw_text(ctx, hi, s_font_18, GRect(hx, center_y - 22, hilo_w + 2, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, lo, s_font_18, GRect(hx, center_y - 1, hilo_w + 2, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

// ---- BT / QT indicators ----

static void update_bt_visibility() {
  if (s_bt_layer) {
    layer_set_hidden(s_bt_layer, s_bt_app_connected && s_bt_radio_connected);
  }
}

static void bt_app_callback(bool connected) {
  s_bt_app_connected = connected;
  update_bt_visibility();
}

static void bt_radio_callback(bool connected) {
  s_bt_radio_connected = connected;
  update_bt_visibility();
}

static void bt_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_text_color(ctx, GColorRed);
  graphics_draw_text(ctx, "BT", s_font_14, bounds,
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void qt_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, "QT", s_font_14, bounds,
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void update_quiet_time() {
  if (s_qt_layer) {
    layer_set_hidden(s_qt_layer, !quiet_time_is_active());
  }
}

// ---- Messaging ----

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  // Weather
  Tuple *temp_t = dict_find(iterator, MESSAGE_KEY_TEMPERATURE);
  Tuple *high_t = dict_find(iterator, MESSAGE_KEY_TEMP_HIGH);
  Tuple *low_t = dict_find(iterator, MESSAGE_KEY_TEMP_LOW);
  Tuple *cond_t = dict_find(iterator, MESSAGE_KEY_CONDITION);
  Tuple *rise_t = dict_find(iterator, MESSAGE_KEY_SUNRISE_MIN);
  Tuple *set_t = dict_find(iterator, MESSAGE_KEY_SUNSET_MIN);

  if (temp_t && high_t && low_t) {
    s_weather.temp = (int16_t)temp_t->value->int32;
    s_weather.high = (int16_t)high_t->value->int32;
    s_weather.low = (int16_t)low_t->value->int32;
    if (cond_t) s_weather.condition = (int16_t)cond_t->value->int32;
    if (rise_t) s_weather.sunrise_min = (int16_t)rise_t->value->int32;
    if (set_t) s_weather.sunset_min = (int16_t)set_t->value->int32;
    s_weather.loaded = true;
    save_weather();
    if (s_weather_layer) {
      layer_mark_dirty(s_weather_layer);
    }
  }

  // Calendar event
  Tuple *ev_day_t = dict_find(iterator, MESSAGE_KEY_EVENT_DAY);
  if (ev_day_t) {
    int day = (int)ev_day_t->value->int32;
    memset(&s_event, 0, sizeof(s_event));
    if (day == 0 || day == 1) {
      s_event.civil_date = day == 0 ? today_civil_date() : tomorrow_civil_date();
      Tuple *start_t = dict_find(iterator, MESSAGE_KEY_EVENT_START_MIN);
      Tuple *title_t = dict_find(iterator, MESSAGE_KEY_EVENT_TITLE);
      Tuple *loc_t = dict_find(iterator, MESSAGE_KEY_EVENT_LOCATION);
      if (start_t) s_event.start_min = (int16_t)start_t->value->int32;
      if (title_t) snprintf(s_event.title, sizeof(s_event.title), "%s", title_t->value->cstring);
      if (loc_t) snprintf(s_event.location, sizeof(s_event.location), "%s", loc_t->value->cstring);
    }
    save_event();
    if (s_event_layer) {
      layer_mark_dirty(s_event_layer);
    }
  }

  // Settings
  Tuple *color_t = dict_find(iterator, MESSAGE_KEY_PrimaryColor);
  if (color_t) {
    s_settings.primary_color = GColorFromHEX(color_t->value->int32);
    save_settings();
    if (s_event_layer) {
      layer_mark_dirty(s_event_layer);
    }
    if (s_date_layer) {
      layer_mark_dirty(s_date_layer);
    }
  }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

// ---- Ticks ----

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time(tick_time);
  update_date_label(tick_time);
  update_quiet_time();
  if (s_event_layer) {
    layer_mark_dirty(s_event_layer);  // countdown text
  }
  if (s_weather_layer) {
    layer_mark_dirty(s_weather_layer);  // day/night icon swap
  }

  // Request weather + calendar refresh every 30 minutes (if phone is connected)
  if (tick_time->tm_min % WEATHER_POLL_MINUTES == 0 &&
      connection_service_peek_pebble_app_connection()) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
      dict_write_uint8(iter, 0, 0);
      app_message_outbox_send();
    }
  }
}

// ---- Layout ----

static void update_layout() {
  GRect full = layer_get_bounds(s_window_layer);
  GRect unob = layer_get_unobstructed_bounds(s_window_layer);
  // Timeline Quick View: hide the weather block while obstructed
  layer_set_hidden(s_weather_layer, unob.size.h < full.size.h);
}

static void unobstructed_change(AnimationProgress progress, void *context) {
  update_layout();
}

static void unobstructed_did_change(void *context) {
  update_layout();
}

static void main_window_load(Window *window) {
  s_window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(s_window_layer);

  s_font_14 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_INTER_SEMIBOLD_14));
  s_font_16 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_INTER_SEMIBOLD_16));
  s_font_18 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_INTER_SEMIBOLD_18));
  s_font_20 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_INTER_EXTRABOLD_20));
  s_font_40 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_INTER_EXTRABOLD_40));
  s_font_68 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_INTER_EXTRABOLD_68));

  s_bolt_path = gpath_create(&BOLT_INFO);

  // Date label: "SAT 15" top right above the time
  s_date_layer = layer_create(GRect(0, 2, bounds.size.w, 26));
  layer_set_update_proc(s_date_layer, date_update_proc);

  // Time: right-aligned to the same 10px edge as the date label
  s_time_layer = layer_create(GRect(0, 16, bounds.size.w, 76));
  layer_set_update_proc(s_time_layer, time_update_proc);

  // Event block: centered between the time and the weather block
  s_event_layer = layer_create(GRect(0, 99, bounds.size.w, 60));
  layer_set_update_proc(s_event_layer, event_update_proc);

  // Weather block: bottom third
  s_weather_layer = layer_create(GRect(0, bounds.size.h - 76, bounds.size.w, 76));
  layer_set_update_proc(s_weather_layer, weather_update_proc);

  // BT / QT corner overlays in the (empty) top-left corner
  s_qt_layer = layer_create(GRect(9, 2, 50, 16));
  layer_set_update_proc(s_qt_layer, qt_update_proc);
  layer_set_hidden(s_qt_layer, true);

  s_bt_layer = layer_create(GRect(9, 18, 45, 16));
  layer_set_update_proc(s_bt_layer, bt_update_proc);
  layer_set_hidden(s_bt_layer, true);

  layer_add_child(s_window_layer, s_date_layer);
  layer_add_child(s_window_layer, s_time_layer);
  layer_add_child(s_window_layer, s_event_layer);
  layer_add_child(s_window_layer, s_weather_layer);
  layer_add_child(s_window_layer, s_bt_layer);
  layer_add_child(s_window_layer, s_qt_layer);

  UnobstructedAreaHandlers ua_handlers = {
    .change = unobstructed_change,
    .did_change = unobstructed_did_change
  };
  unobstructed_area_service_subscribe(ua_handlers, NULL);
  update_layout();
}

static void main_window_unload(Window *window) {
  unobstructed_area_service_unsubscribe();
  layer_destroy(s_date_layer);
  layer_destroy(s_time_layer);
  layer_destroy(s_event_layer);
  layer_destroy(s_weather_layer);
  layer_destroy(s_bt_layer);
  layer_destroy(s_qt_layer);
  gpath_destroy(s_bolt_path);
  fonts_unload_custom_font(s_font_14);
  fonts_unload_custom_font(s_font_16);
  fonts_unload_custom_font(s_font_18);
  fonts_unload_custom_font(s_font_20);
  fonts_unload_custom_font(s_font_40);
  fonts_unload_custom_font(s_font_68);
}

static void init() {
  // v1.x cached weather (string format) is obsolete — drop it once
  if (persist_exists(OLD_WEATHER_KEY)) {
    persist_delete(OLD_WEATHER_KEY);
  }

  load_settings();
  load_weather();
  load_event();

  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorWhite);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });
  window_stack_push(s_main_window, true);

  update_time(NULL);
  update_date_label(NULL);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = bt_app_callback
  });
  s_bt_app_connected = connection_service_peek_pebble_app_connection();
  bluetooth_connection_service_subscribe(bt_radio_callback);
  s_bt_radio_connected = bluetooth_connection_service_peek();
  update_bt_visibility();
  update_quiet_time();

#ifndef DEMO
  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  app_message_open(256, 256);
#endif
}

static void deinit() {
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
