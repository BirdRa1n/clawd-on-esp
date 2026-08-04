#pragma once
// Mirrors Serial output into a small ring buffer so the dashboard terminal can
// show the same lines that are printed to the serial port. Use console.logf()/
// console.log() in place of Serial.printf()/println() to capture a line.
#include <Arduino.h>

class Console {
public:
  void   begin(unsigned long baud);
  void   logf(const char *fmt, ...);     // printf-style; goes to Serial + ring
  void   log(const String &line);        // one line; Serial + ring
  String jsonSince(uint32_t seq);         // {"seq":N,"lines":[...]} for entries newer than seq

private:
  static const int CAP = 48;
  String   _lines[CAP];
  uint32_t _seqOf[CAP];
  int      _n = 0;
  uint32_t _next = 1;
  void     push(const String &s);
};

extern Console console;
