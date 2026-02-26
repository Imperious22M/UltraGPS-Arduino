#ifndef DEBUG_MACROS_H
#define DEBUG_MACROS_H

#include <Arduino.h>

//! Enable debug mode
#define DEBUG

// ---------------------------------------------------------------
// Usage:
//   #define INFO_DEBUG   enables DEBUG_INFO_* macros only
//   #define DEBUG        enables both DEBUG_INFO_* and DEBUG_* macros
//
// DEBUG_INFO_* — status/error messages (radio init, sync complete, etc.)
//   DEBUG_INFO_PRINT("msg")          prints a string literal to Serial
//   DEBUG_INFO_PRINTLN("msg")        prints a string literal + newline
//   DEBUG_INFO_PRINTF("fmt", ...)    printf-style, e.g. DEBUG_INFO_PRINTF("val=%d\n", x)
//
// DEBUG_* — verbose diagnostic messages (tick counts, packet types, etc.)
//   DEBUG_PRINT("msg")
//   DEBUG_PRINTLN("msg")
//   DEBUG_PRINTF("fmt", ...)
//
// String macros wrap literals in F() automatically to save SRAM.
// Printf macros write into a stack buffer via snprintf — keep
// format strings short on AVR (buffer is capped at 80 chars).
// ---------------------------------------------------------------

// ---------------------------------------------------------------
// DEBUG_INFO_* macros — enabled by INFO_DEBUG or DEBUG
// ---------------------------------------------------------------
#if defined(INFO_DEBUG) || defined(DEBUG)
  #define DEBUG_INFO_PRINT(msg)        Serial.print(F(msg))
  #define DEBUG_INFO_PRINTLN(msg)      Serial.println(F(msg))
  #define DEBUG_INFO_PRINTF(fmt, ...)  do {                              \
      char _buf[80];                                                      \
      snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__);                  \
      Serial.print(_buf);                                                 \
  } while(0)
#else
  #define DEBUG_INFO_PRINT(msg)
  #define DEBUG_INFO_PRINTLN(msg)
  #define DEBUG_INFO_PRINTF(fmt, ...)
#endif

// ---------------------------------------------------------------
// DEBUG_* macros — enabled by DEBUG only
// ---------------------------------------------------------------
#ifdef DEBUG
  #define DEBUG_PRINT(msg)         Serial.print(F(msg))
  #define DEBUG_PRINTLN(msg)       Serial.println(F(msg))
  #define DEBUG_PRINTF(fmt, ...)   do {                              \
      char _buf[80];                                                  \
      snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__);              \
      Serial.print(_buf);                                             \
  } while(0)
#else
  #define DEBUG_PRINT(msg)
  #define DEBUG_PRINTLN(msg)
  #define DEBUG_PRINTF(fmt, ...)
#endif

#endif // DEBUG_MACROS_H
