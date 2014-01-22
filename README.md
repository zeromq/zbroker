# zpipes

A reliable named pipes service.

## Overview

The zpipes broker provides a reliable named pipes service to application programs.

A pipe is a one-directional stream of data "chunks" from one application to another.

* Currently, all applications must be on the same machine instance.
* Currently, a pipe accepts multiple writers, and a single reader.
* Pipes are named using any convention that suits the application.
* The application is responsible for properly closing pipes, thus releasing resources.

## IPC Command Interface

The zpipes broker accepts ZeroMQ ipc:// connections on the "ipc://@/zpipes/local" endpoint, which is an abstract IPC endpoint. The sole interface between applications and the broker is across such IPC connections. The command interface has these commands, consisting of multiframe data:

* "OPEN" / pipename -- opens a named pipe, for reading or writing.
* "CLOSE" / pipename -- closes a named pipe.
* "READ" / pipename -- reads next chunk of data from the specified pipe.
* "WRITE" / pipename / chunk -- writes a chunk of data to the specified pipe.

The broker replies to valid OPEN, CLOSE, and WRITE commands with a single frame containing "OK".

The broker replies to a valid READ command with a single frame containing the chunk of data. If there is no data available, the READ command will block until data is available.

The application can pipeline commands, e.g. sending multiple WRITE commands, before reading the responses.

## Reliability

The zpipes broker does not store chunks on disk, so if the broker process crashes or is killed, data may be lost. However pipes and the chunks they contain will survive the stop/restart of application processes. Thus one process may write to a pipe, then exit, and then another process may start up, and read from the pipe.

## End of Stream

If the application wants to signal the end of the stream it must send a recognizable chunk to indicate that. The zpipes model treats pipes as infinite streams.

## Multiple Writers

Multiple applications can write to the same pipe. Chunks will then be fair-queued from writers, so that heavy writers do not block lighter ones.

## Ownership and Contributing

To be defined.
