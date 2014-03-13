# zbroker - a ZeroMQ Broker

The zbroker offers a broker container for multiple ZeroMQ-based messaging services. The current list of messaging services is:

* zpipes - reliable, distributed named pipes

Other planned/potential services are:

* Majordomo - service-oriented RPC.
* Clone - distributed key-value storage.

## zpipes Overview

The zpipes service provides a reliable named pipes service. A pipe is a one-directional stream of data "chunks" between applications.

* Currently, all applications must be on the same machine instance.
* Currently, a pipe accepts multiple writers, and a single reader.
* Pipes are named using any convention that suits the application.
* The application is responsible for properly closing pipes, thus releasing resources.

### Command Line Syntax

When run without arguments, the zpipes broker uses the name "default". You may run at most one broker with the same name, on a single machine instance. If you wish to use multiple brokers, e.g. for testing, you must use a different name for each.

To end a zpipes broker, send a TERM or INT signal (Ctrl-C).

### IPC Command Interface

The zpipes broker accepts ZeroMQ ipc:// connections on the "ipc://@/zpipes/local" endpoint, which is an abstract IPC endpoint. The sole interface between applications and the broker is across such IPC connections. The command interface has these commands, consisting of multiframe data:

* "OPEN" / pipename -- opens a named pipe, for reading or writing.
* "CLOSE" / pipename -- closes a named pipe.
* "READ" / pipename -- reads next chunk of data from the specified pipe.
* "WRITE" / pipename / chunk -- writes a chunk of data to the specified pipe.

The broker replies to valid OPEN, CLOSE, and WRITE commands with a single frame containing "OK".

The broker replies to a valid READ command with a single frame containing the chunk of data. If there is no data available, the READ command will block until data is available.

The application can pipeline commands, e.g. sending multiple WRITE commands, before reading the responses.

### Reliability

The zpipes broker does not store chunks on disk, so if the broker process crashes or is killed, data may be lost. However pipes and the chunks they contain will survive the stop/restart of application processes. Thus one process may write to a pipe, then exit, and then another process may start up, and read from the pipe.

### End of Stream

If the application wants to signal the end of the stream it must send a recognizable chunk to indicate that. The zpipes model treats pipes as infinite streams.

### Multiple Writers

Multiple applications can write to the same pipe. Chunks will then be fair-queued from writers, so that heavy writers do not block lighter ones.

## Ownership and Contributing

The contributors are listed in AUTHORS. The zbroker uses the standard ZeroMQ license (LGPLv3 with static link exception), see COPYING and COPYING.LESSER.

The contribution policy is the standard ZeroMQ [C4.1 process](http://rfc.zeromq.org/spec:22). Please read this RFC if you have never contributed to a ZeroMQ project.
