# zbroker - the ZeroMQ broker project

The zbroker project is a container for arbitrary ZeroMQ-based messaging services. The current list of messaging services is:

* ZPIPES - reliable, distributed named pipes

## General Operation

To build zbroker:

    git clone git://github.com/jedisct1/libsodium.git
    for project in libzmq czmq zyre zbroker; do
        git clone git://github.com/zeromq/$project.git
    done
    for project in libsodium libzmq czmq zyre zbroker; do
        cd $project
        ./autogen.sh
        ./configure && make check
        sudo make install
        sudo ldconfig
        cd ..
    done

To run zbroker:

    zbroker [broker-name]

Where 'broker-name' is a string that is unique on any given host. The default broker name is 'local'. To end the broker, send a TERM or INT signal (Ctrl-C).

## ZPIPES Overview

### The ZPIPES Protocol

The following ABNF grammar defines the ZPIPES protocol:

    ZPIPES = reader | writer

    reader = input-command *( read-command | ping-command )
             close-command
    input-command = c:input ( s:input-ok | s:input-failed )
    read-command = c:read ( s:read-ok | s:read-end
                          | s:read-timeout | s:read-failed )
    close-command = c:close ( s:close-ok | s:close-failed )

    writer = output-command *write-command close-command
    output-command = c:output ( s:output-ok | s:output-failed )
    write-command = c:write ( s:write-ok
                            | s:write-timeout | s:write-failed )
    ping-command = c:ping s:ping-ok

    ;         Create a new pipe for reading
    input           = signature %d1 pipename
    signature       = %xAA %xA0             ; two octets
    pipename        = string                ; Name of pipe

    ;         Input request was successful
    input_ok        = signature %d2

    ;         Input request failed
    input_failed    = signature %d3 reason
    reason          = string                ; Reason for failure

    ;         Create a new pipe for writing
    output          = signature %d4 pipename
    pipename        = string                ; Name of pipe

    ;         Output request was successful
    output_ok       = signature %d5

    ;         Output request failed
    output_failed   = signature %d6 reason
    reason          = string                ; Reason for failure

    ;         Read a chunk of data from pipe
    read            = signature %d7 size timeout
    size            = number-4              ; Number of bytes to read
    timeout         = number-4              ; Timeout, msecs, or zero

    ;         Read was successful
    read_ok         = signature %d8 chunk
    chunk           = chunk                 ; Chunk of data

    ;         Pipe is closed, no more data
    read_end        = signature %d9

    ;         Read ended with timeout
    read_timeout    = signature %d10

    ;         Read failed due to error
    read_failed     = signature %d11 reason
    reason          = string                ; Reason for failure

    ;         Write chunk of data to pipe
    write           = signature %d12 chunk timeout
    chunk           = chunk                 ; Chunk of data
    timeout         = number-4              ; Timeout, msecs, or zero

    ;         Write was successful
    write_ok        = signature %d13

    ;         Write ended with timeout
    write_timeout   = signature %d14

    ;         Read failed due to error
    write_failed    = signature %d15 reason
    reason          = string                ; Reason for failure

    ;         Close pipe
    close           = signature %d16

    ;         Close was successful
    close_ok        = signature %d17

    ;         Close failed due to error
    close_failed    = signature %d18 reason
    reason          = string                ; Reason for failure

    ;         Signal liveness
    ping            = signature %d19

    ;         Respond to ping
    ping_ok         = signature %d20

    ;         Command was invalid at this time
    invalid         = signature %d21

    ; A chunk has 4-octet length + binary contents
    chunk           = number-4 *OCTET

    ; Strings are always length + text contents
    string          = number-1 *VCHAR

    ; Numbers are unsigned integers in network byte order
    number-1        = 1OCTET
    number-4        = 4OCTET

### The ZPIPES API

The zpipes_client class provides the public API.
    
## Ownership and Contributing

The contributors are listed in AUTHORS. This project uses the MPL v2 license, see LICENSE.

The contribution policy is the standard ZeroMQ [C4.1 process](http://rfc.zeromq.org/spec:22). Please read this RFC if you have never contributed to a ZeroMQ project.
