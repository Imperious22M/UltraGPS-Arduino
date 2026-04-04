#pragma once
#include <Arduino.h>

constexpr uint8_t CMD_MAX_ARGS = 4;   //! Max arguments any single command can carry
constexpr uint8_t CMD_BUF_SIZE = 64;  //! Max length of one serial command line (incl. NUL)

// ---------------------------------------------------------------------------
// Polling state for tick serial loop timings
// ---------------------------------------------------------------------------

enum class PollingState : uint8_t { 
    CONTINOUS, 
    POLLING 
};
constexpr uint16_t continuousPollingDelay = 20; //! Millisecond delay between checking for pulses in continuous mode

// ---------------------------------------------------------------------------
// Argument types
// ---------------------------------------------------------------------------

//! Supported argument types that the decoder can parse
enum class ArgType : uint8_t {
    INT,    //! Signed 32-bit integer  (parsed with strtol)
    FLOAT,  //! 32-bit float           (parsed with strtod)
};

//! Holds one decoded argument value — read the field matching the ArgType
union CmdArg {
    int32_t i;
    float   f;
};

// ---------------------------------------------------------------------------
// Command identity enum — extend this as new commands are added
// ---------------------------------------------------------------------------

//! Enumerates every known command type
enum class CommandType : uint8_t {
    PULSE,      //! 'P' — trigger an ultrasonic pulse
    SIMULATE,   //! 'S' — run in simulation mode
    CONTINOUS,  //! 'C' — run in continuous mode 
    UNKNOWN,
};

// ---------------------------------------------------------------------------
// Command definition and table
// ---------------------------------------------------------------------------

//! Defines the structure and handler for a single command
struct CommandDef {
    CommandType  type;                      //! Which command this describes
    uint8_t      numArgs;                   //! Number of arguments expected (0–CMD_MAX_ARGS)
    ArgType      argTypes[CMD_MAX_ARGS];    //! Type of each argument, in order
    void       (*callback)(const CmdArg* args); //! Function to call after decoding
};

//! One row in the global command table — maps an ASCII letter to a CommandDef
struct CommandEntry {
    uint8_t           letter;   //! ASCII value of the single-character command key
    const CommandDef* def;      //! Pointer to the command's definition
};

// ---------------------------------------------------------------------------
// CommandControl class
// ---------------------------------------------------------------------------

class CommandControl {
public:
    //! Current polling state
    PollingState pollingState = PollingState::POLLING;
    
    //! Construct with a pointer to the command table and the number of entries
    CommandControl(const CommandEntry* table, uint8_t tableSize);

    //! Call from loop() — drains Serial, buffers input, and dispatches complete commands
    void poll();

private:
    const CommandEntry* _table;
    uint8_t             _tableSize;

    char    _buf[CMD_BUF_SIZE];
    uint8_t _bufLen = 0;

    //! Returns the CommandDef for the given ASCII letter, or nullptr if not found
    const CommandDef* lookup(uint8_t letter) const;

    //! Parses a NUL-terminated command line and dispatches the callback.
    //! Returns false (and does nothing) on any decoding failure.
    bool decode(const char* line);
};
