# zpipes

zpipes is an elastic pipes proof of concept.

To start, run "zpipes servicename".

To talk to the zpipes service you open a ZeroMQ DEALER socket and send commands in the form of multipart messages, then read replies. Supports two commands:

* PUT | pipename | opaque-data
* GET | pipename | size

Where size is an ascii integer, or empty/zero meaning "as much data as available".

## Ownership and Contributing

**zpipes** is owned by all its authors and contributors. This is an open source project licensed under the XYZ. To contribute to zpipes please read the [C4.1 process](http://rfc.zeromq.org/spec:22) that we use.
