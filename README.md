# Chat + Discovery Assignment (C++)

## Build
```
make
```
`target/`:
- `discovery_server` - 5050
- `chat_server_thread`
- `chat_server_select`
- `chat_server_fork` - one of these runing on 6060
- `chat_client`

## Discovery Server
- Run: `./target/discovery_server <port> 5050`
- Persists to `discovery_db.txt` with 4 fields: `username password ip port`. IP is learned from the TCP peer on REGISTER; the port is the chat server's listening port (typically shared, e.g., 6060).
- Commands:
  - `REGISTER <user> <pass>` → `OK` or `ERR wrong-password|bad-command`
  - `LOOKUP <user>` → `OK <ip> <port>` or `ERR not-found`

## Chat Server Protocol (common to all variants)
- Transport: TCP, line-based; each message is one line ending with `\n`.
- Client → Server architecture based:
  - `AUTH <user> <pass>` → `OK` or `ERR auth-failed|already-logged-in|expected-AUTH`
  - `BCAST <text>` → `OK` (relays `BCAST <user sending> <text>` to others)
  - `MSG <target> <text>` → `OK` or `ERR no-such-user`
  - `WHO` → `OK <user1,user2,...>` considers the client asking as active only
  - `QUIT` → `OK` then close

- Credential source: all chat servers read `discovery_db.txt` and reload on every `AUTH`.

## Chat Servers
All verify credentials against `discovery_db.txt` (same file used by discovery) and reload credentials per `AUTH`.
- `chat_server_thread <port>`: one thread per client; shared map guarded by mutex.
- `chat_server_select <port>`: single-process select-based loop, non-blocking sockets, capped at 10 concurrent clients; rejects connection with `ERR server-full` when full.
- `chat_server_fork <port>`: now implemented with threads (same behavior as thread server, but kept as a separate target).

## Chat Client
- Default interactive menu if no args: allows REGISTER, LOGIN (via discovery), LOOKUP.
- Non-interactive form: `./target/chat_client <disc_host> <disc_port> <chat_host> <chat_port> <user> <pass>`
- Registration advertises the shared chat server port (the `chat_port` argument; defaults to 6060); you generally run one chat server and all users register that port.
- CLI commands after login:
  - `/all <text>` → broadcast
  - `/msg <user> <text>` → private message
  - `/who` → list online users (or `No` if none)
  - `/logout` or `/quit` → graceful exit

## Quick Manual Test (localhost example)
```
# Term 1
./target/discovery_server 5050
# Term 2
./target/chat_server_select 6060  # or chat_server_thread 6060
# Term 3
./target/chat_client
Then proceed with whatever required.
```

## Notes
- Discovery registration overwrites existing user only when password matches; otherwise `ERR wrong-password`.
- Persistence: discovery writes `discovery_db.txt` in the working directory; chat servers read that file for credentials.
