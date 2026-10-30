//! @file command_handler.h
//! @brief CommandHandler: reads serial command lines, decodes single-letter commands
//!        and their arguments, and calls each command's callback.
//!
//! This file holds the generic part of the serial command interface: the polling
//! state, the argument types, the format of a command definition and of the command
//! table, and the line buffer and decoder. The base station's actual commands (the
//! CommandType list, the command definitions, the letter -> command table and the
//! handlers) are in command_control.h, whose CommandControl class derives from
//! CommandHandler.

#pragma once
#include <Arduino.h>

constexpr uint8_t CMD_MAX_ARGS = 4;   //!< Max arguments any single command can carry.
constexpr uint8_t CMD_BUF_SIZE = 64;  //!< Max length of one serial command line (incl. NUL).

// ---------------------------------------------------------------------------
// Polling state for tick serial loop timings
// ---------------------------------------------------------------------------

//! @brief Whether the base station measures only on request or continuously.
//!
//! Held in CommandHandler::pollingState, which starts as POLLING. CommandHandler
//! itself never changes it: the command handlers do (in CommandControl, 'C'
//! selects CONTINUOUS and every other command POLLING), and loop() reads it.
enum class PollingState : uint8_t {
    CONTINUOUS, //!< Measure continuously: loop() keeps calling CommandControl::runContinuous().
    POLLING     //!< Measure only when a command asks for it (the state at start-up).
};

//! @brief Delay in milliseconds between two measurements in continuous mode.
//!
//! loop() calls CommandControl::runContinuous() only if at least this long has passed
//! since the previous call.
//! @note loop() keeps lastPulseTime between passes, so this delay is honoured, but
//!       it is shorter than the trigger period. The rate is set by the wait for the
//!       next Timer1 trigger inside
//!       CommandControl::handleUltrasonics(): one measurement per trigger period
//!       (10 per second at 10 Hz) while the sampling time, the trigger send guard
//!       (TRIGGER_SEND_GUARD_TICKS, 2 ms by default) and the time to print and
//!       send a sync packet fit in that period; otherwise one every two periods.
constexpr uint16_t continuousPollingDelay = 20;

// ---------------------------------------------------------------------------
// Argument types
// ---------------------------------------------------------------------------

//! @brief Supported argument types that the decoder can parse.
enum class ArgType : uint8_t {
    INT,    //!< Signed 32-bit integer, decimal (parsed with strtol).
    FLOAT,  //!< 32-bit float (parsed with strtod).
};

//! @brief Holds one decoded argument value — read the field matching the ArgType.
union CmdArg {
    int32_t i;  //!< Value of an ArgType::INT argument.
    float   f;  //!< Value of an ArgType::FLOAT argument.
};

// ---------------------------------------------------------------------------
// Command identity enum
// ---------------------------------------------------------------------------

//! @brief Enumerates every known command type.
//!
//! Only declared here (an opaque enum declaration) so CommandDef can name it; the
//! list of commands is defined in command_control.h.
enum class CommandType : uint8_t;

// ---------------------------------------------------------------------------
// Command definition and table
// ---------------------------------------------------------------------------

// Forward declaration: CommandCallback below takes a CommandHandler&.
class CommandHandler;

//! @brief Function called after decoding a command.
//!
//! It receives the CommandHandler that decoded the command and the decoded
//! arguments. Passing the handler lets the function forward to a member function
//! of a class derived from CommandHandler (see CommandControl).
//! @param handler The CommandHandler that decoded the command.
//! @param args    The decoded arguments: CMD_MAX_ARGS entries, of which the first
//!                CommandDef::numArgs hold the parsed values and the rest are 0.
//!                nullptr when the callback is called directly rather than by the
//!                decoder (CommandControl::runContinuous() does that).
typedef void (*CommandCallback)(CommandHandler& handler, const CmdArg* args);

//! @brief Defines the structure and handler for a single command.
//!
//! The decoder parses numArgs arguments, each as its entry in argTypes says, then
//! calls callback; it does not read type.
struct CommandDef {
    CommandType     type;                      //!< Which command this describes.
    uint8_t         numArgs;                   //!< Number of arguments expected (0–CMD_MAX_ARGS).
    ArgType         argTypes[CMD_MAX_ARGS];    //!< Type of each argument, in order.
    CommandCallback callback;                  //!< Function to call after decoding.
};

//! @brief One row of a command table — maps an ASCII letter to a CommandDef.
//!
//! A command table is an array of these, handed to the CommandHandler constructor
//! (CommandControl builds its own in its constructor).
struct CommandEntry {
    uint8_t           letter;   //!< ASCII value of the single-character command key.
    const CommandDef* def;      //!< Pointer to the command's definition.
};

// ---------------------------------------------------------------------------
// CommandHandler class
// ---------------------------------------------------------------------------

//! @brief Reads command lines from Serial, decodes them and calls their callbacks.
//!
//! A command line is one command letter, looked up in the command table given to
//! the constructor, followed by that command's arguments (see CommandDef), and ends
//! with a newline or carriage return. Letters are case-sensitive. A line that cannot
//! be decoded is ignored without any reply. Derive from this class to define the
//! commands (as CommandControl does) and call poll() from loop().
class CommandHandler {
public:
    //! Current polling state (see PollingState); starts as POLLING.
    PollingState pollingState = PollingState::POLLING;

    //! @brief Construct with a pointer to the command table and the number of entries.
    //!
    //! Only the pointer is stored, so the table and the CommandDefs it points to must
    //! outlive the handler. The table is not read here, so a derived class may pass a
    //! table that its own constructor fills in afterwards (CommandControl does).
    //! @param table     The command table: one CommandEntry per command letter.
    //! @param tableSize Number of entries in @p table.
    CommandHandler(const CommandEntry* table, uint8_t tableSize);

    //! @brief Call from loop() — drains Serial, buffers input, and dispatches complete
    //!        commands.
    //!
    //! Reads every byte Serial has available. A newline or carriage return ends a
    //! line (empty lines are ignored, so CR LF line endings work too), and a partial
    //! line stays buffered until a later call completes it. Each complete line is
    //! decoded and its callback runs before poll() returns, so poll() blocks for as
    //! long as the command takes (the 'D' sweep, for example).
    //! @note A line longer than CMD_BUF_SIZE - 1 characters is never decoded whole: its
    //!       first CMD_BUF_SIZE characters are discarded, and any characters after
    //!       them are treated as the start of a new line (subject to the same limit),
    //!       which is decoded when the line ends.
    void poll();

private:
    const CommandEntry* _table;      //!< Command table given to the constructor.
    uint8_t             _tableSize;  //!< Number of entries in _table.

    char    _buf[CMD_BUF_SIZE];  //!< The line being received; NUL-terminated before decoding.
    uint8_t _bufLen = 0;         //!< Number of characters in _buf.

    //! @brief Find the CommandDef for an ASCII command letter.
    //!
    //! Searches the table in order and uses the first entry whose letter matches
    //! exactly (case-sensitive).
    //! @param letter ASCII value of the command letter (the first character of a line).
    //! @return The CommandDef for @p letter, or nullptr if not found.
    const CommandDef* lookup(uint8_t letter) const;

    //! @brief Parse a NUL-terminated command line and dispatch the callback.
    //!
    //! The first character selects the command (see lookup()). Each of its
    //! CommandDef::numArgs arguments is then parsed as its ArgType says; spaces and
    //! tabs before an argument are skipped, so "H80" and "H 80" are the same.
    //! Anything after the last argument is ignored.
    //! @param line The command line, without its line ending.
    //! @return true if the line was decoded and its callback called; false (and
    //!         nothing is done) on any decoding failure: empty line, unknown letter,
    //!         missing argument, or an argument that cannot be parsed as its ArgType.
    bool decode(const char* line);
};
