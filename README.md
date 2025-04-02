# GeneralIoTModules
General purpose kernel modules for sending and receiving network file transfers for IoT.

This project is a rework of my previous IPStore project using Windows copilot and ChatGPT 4o.
See [IPStore](https://github.com/ahidaka/IPStore).

This is a loadable module that I asked ChatGPT to recreate based on the operation specifications of the article "Measurement data accumulation technique using loadable kernel module" published in the November 2004 issue of the Japanese Interface magazine mentioned above.

The original driver was able to communicate at maximum performance because it sent and received data in kernel mode without handshake using RAW Socket. However, this version is forced to send and receive data using UDP because ChatGPT does not know about RAW Socket communication in kernel mode.
