//! @file debug_macros.h
//! @brief Serial debug printing macros that compile away unless enabled.
//!
//! Usage:
//! - `#define INFO_DEBUG` enables the `DEBUG_INFO_*` macros only.
//! - `#define DEBUG` enables both the `DEBUG_INFO_*` and the `DEBUG_*` macros.
//!
//! With neither defined, every macro expands to nothing (its arguments are not
//! evaluated).
//!
//! - `DEBUG_INFO_*` print status/error messages (settings loaded, trigger and
//!   reset packets, commands received, etc.).
//! - `DEBUG_*` print verbose diagnostic messages (radio init, tick counts, PINC
//!   values, etc.).
//!
//! @code
//! DEBUG_INFO_PRINT("msg");           // prints a string literal to Serial
//! DEBUG_INFO_PRINTLN("msg");         // prints a string literal + newline
//! DEBUG_INFO_PRINTF("val=%d\n", x);  // printf-style
//! DEBUG_PRINT("msg");                // the DEBUG_* macros work the same way
//! DEBUG_PRINTLN("msg");
//! DEBUG_PRINTF("val=%d\n", x);
//! @endcode
//!
//! The string macros wrap literals in F() automatically to save SRAM. The printf
//! macros write into a stack buffer via snprintf(): keep format strings short on
//! AVR (the buffer is capped at 80 chars).

#ifndef DEBUG_MACROS_H
#define DEBUG_MACROS_H

#include <Arduino.h>

// Enable debug mode (uncomment the line below)
//#define DEBUG

// ---------------------------------------------------------------------------
// DEBUG_INFO_* macros — enabled by INFO_DEBUG or DEBUG
// ---------------------------------------------------------------------------

//! @def DEBUG_INFO_PRINT(msg)
//! @brief Print a string literal to Serial, kept in flash with F().
//!
//! Enabled by INFO_DEBUG or DEBUG; otherwise expands to nothing.
//! @param msg String literal to print.

//! @def DEBUG_INFO_PRINTLN(msg)
//! @brief Print a string literal and a newline to Serial, kept in flash with F().
//!
//! Enabled by INFO_DEBUG or DEBUG; otherwise expands to nothing.
//! @param msg String literal to print.

//! @def DEBUG_INFO_PRINTF(fmt, ...)
//! @brief printf-style print to Serial.
//!
//! Formats into an 80-char stack buffer with snprintf(), so output longer than
//! 79 characters is cut short. The format string is not wrapped in F(), so it
//! takes SRAM. Enabled by INFO_DEBUG or DEBUG; otherwise expands to nothing.
//! @param fmt printf format string.
//! @param ... Values for the conversions in @p fmt.

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

// ---------------------------------------------------------------------------
// DEBUG_* macros — enabled by DEBUG only
// ---------------------------------------------------------------------------

//! @def DEBUG_PRINT(msg)
//! @brief Print a string literal to Serial, kept in flash with F().
//!
//! Enabled by DEBUG only; otherwise expands to nothing.
//! @param msg String literal to print.

//! @def DEBUG_PRINTLN(msg)
//! @brief Print a string literal and a newline to Serial, kept in flash with F().
//!
//! Enabled by DEBUG only; otherwise expands to nothing.
//! @param msg String literal to print.

//! @def DEBUG_PRINTF(fmt, ...)
//! @brief printf-style print to Serial.
//!
//! Formats into an 80-char stack buffer with snprintf(), so output longer than
//! 79 characters is cut short. The format string is not wrapped in F(), so it
//! takes SRAM. Enabled by DEBUG only; otherwise expands to nothing.
//! @param fmt printf format string.
//! @param ... Values for the conversions in @p fmt.

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
