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

    reader = input-command *fetch-command close-command
    input-command = c:input ( s:ready | s:failed )
    fetch-command = c:fetch ( s:fetched | s:empty | s:timeout | s:failed )
    close-command = c:close ( s:closed | s:failed )

    writer = output-command *store-command close-command
    output = c:output ( s:ready | s:failed )
    store = c:store ( s:stored | s:failed )

    ;         Create a new pipe for reading
    C:input         = signature %d1 pipename
    signature       = %xAA %d0              ; two octets
    pipename        = string                ; Name of pipe

    ;         Create a new pipe for writing
    C:output        = signature %d2 pipename
    pipename        = string                ; Name of pipe

    ;         Input or output request was successful
    C:ready         = signature %d3

    ;         Input or output request failed
    C:failed        = signature %d4 reason
    reason          = string                ; Reason for failure

    ;         Read next chunk of data from pipe
    C:fetch         = signature %d5 timeout
    timeout         = number-4              ; Timeout, msecs, or zero

    ;         Have data from pipe
    C:fetched       = signature %d6 chunk
    chunk           = chunk                 ; Chunk of data

    ;         Pipe is closed, no more data
    C:end_of_pipe   = signature %d7

    ;         Get or put ended with timeout
    C:timeout       = signature %d8

    ;         Write chunk of data to pipe
    C:store         = signature %d9 chunk
    chunk           = chunk                 ; Chunk of data

    ;         Store was successful
    C:stored        = signature %d10

    ;         Close pipe
    C:close         = signature %d11

    ;         Close was successful
    C:closed        = signature %d12

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
