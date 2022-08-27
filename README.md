# Xmodem-Protocol

Implemented a server that uses Xmodem Protocol to transfer files between client and server. 

The server supports both 128 bytes or 1024 bytes packets for client compatibility. The server uses CRC16 checksum to ensure the packet does not manipulate between client and server.