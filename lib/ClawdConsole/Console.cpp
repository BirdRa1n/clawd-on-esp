#include "Console.h"
#include <stdarg.h>

Console console;

static String jsonEscape(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if (c >= 0 && c < 0x20) { /* drop other control chars */ }
    else o += c;
  }
  return o;
}

void Console::begin(unsigned long baud) { Serial.begin(baud); }

void Console::push(const String &s) {
  if (_n < CAP) {
    _lines[_n] = s;
    _seqOf[_n] = _next++;
    _n++;
  } else {
    for (int i = 1; i < CAP; i++) { _lines[i - 1] = _lines[i]; _seqOf[i - 1] = _seqOf[i]; }
    _lines[CAP - 1] = s;
    _seqOf[CAP - 1] = _next++;
  }
}

void Console::log(const String &line) {
  Serial.println(line);
  push(line);
}

void Console::logf(const char *fmt, ...) {
  char buf[168];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  String s(buf);
  s.trim();                 // strip trailing newline captured lines don't need
  Serial.println(s);
  push(s);
}

String Console::jsonSince(uint32_t seq) {
  String o = "{\"seq\":" + String(_next - 1) + ",\"lines\":[";
  bool first = true;
  for (int i = 0; i < _n; i++) {
    if (_seqOf[i] > seq) {
      if (!first) o += ',';
      o += '"';
      o += jsonEscape(_lines[i]);
      o += '"';
      first = false;
    }
  }
  o += "]}";
  return o;
}
