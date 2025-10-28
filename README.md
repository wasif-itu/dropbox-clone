# Dropbox Clone - Multi-threaded File Server

A multi-threaded file storage server implementing concurrent client handling, worker thread pools, and per-file locking for safe concurrent operations.

## Architecture

- **Main Thread**: Accepts TCP connections and queues client sockets
- **Client Thread Pool**: Handles authentication and command parsing (4 threads)
- **Worker Thread Pool**: Executes file I/O operations (4 threads)
- **Communication**: Client threads wait on condition variables; workers signal completion

### Thread-Safe Queues
- **Client Queue**: Capacity 256 (accepts incoming connections)
- **Task Queue**: Capacity 1024 (file operation requests)
- Both use mutex + condition variables (no busy-waiting)

### Synchronization Features
- Per-session result delivery via condition variables
- Per-file mutex locks with reference counting
- Global user authentication mutex
- Clean shutdown with queue closure and thread joining

---

## Build Instructions

### Prerequisites
- GCC or Clang compiler
- POSIX-compliant system (Linux/macOS)
- pthread library

### Compilation

```bash
# Build server and client
make all

# Build with ThreadSanitizer (for race detection)
make tsan

# Clean build artifacts
make clean
```

### Build Outputs
- `server` - Main file server
- `client_app` - Interactive test client
- `server_tsan` - Server with ThreadSanitizer instrumentation (via `make tsan`)

---

## Running the Server

```bash
./server
```

**Default Configuration:**
- Port: `8080`
- Storage Directory: `server_storage/` (created automatically)
- User Database: `server_storage/users.txt`

The server will print:
```
Server listening on 8080
```

Press `Ctrl+C` to gracefully shutdown.

---

## Running the Client

```bash
./client_app
```

The client connects to `127.0.0.1:8080` and provides an interactive prompt.

### Available Commands

#### 1. **SIGNUP** - Create new account
```
> SIGNUP alice password123
OK signup
```

#### 2. **LOGIN** - Authenticate
```
> LOGIN alice password123
OK login
```

#### 3. **UPLOAD** - Upload a file
```
> UPLOAD testfile.txt
# Client will read local file and send contents
OK upload
```
- File must exist locally
- Client auto-detects file size
- Server stores in `server_storage/<username>/`

#### 4. **DOWNLOAD** - Download a file
```
> DOWNLOAD testfile.txt
Downloaded testfile.txt (1234 bytes)
```
- Saves to current directory
- Overwrites existing file

#### 5. **LIST** - List all files
```
> LIST
OK list 128
testfile.txt 1234
photo.jpg 56789
```
- Shows filename and size in bytes

#### 6. **DELETE** - Remove a file
```
> DELETE testfile.txt
OK delete
```

#### 7. **QUIT** - Disconnect
```
> QUIT
OK bye
```

---

## Testing

### Manual Testing

**Terminal 1** (Server):
```bash
./server
```

**Terminal 2** (Client):
```bash
./client_app
# Run command sequence
```

### Automated Tests

```bash
# Single client flow test
cd tests
./test_single_client.sh

# Concurrent clients test
./test_concurrent.sh
```

### Memory Leak Check (Valgrind)

```bash
# Run server under Valgrind
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./server

# In another terminal, run client operations then Ctrl+C server
```

**Expected output**: 
```
All heap blocks were freed -- no leaks are possible
```

### Race Condition Check (ThreadSanitizer)

```bash
# Build with TSAN
make tsan

# Run instrumented server
./server_tsan

# In another terminal, run concurrent clients
# Check for race warnings in server_tsan output
```

**Expected output**: No race condition warnings

---

## Protocol Specification

### Text-Based Protocol
Commands are newline-terminated (`\n`). Binary data follows specific commands.

#### Authentication Phase
```
C: SIGNUP <username> <password>\n
S: OK signup\n  OR  ERR userexists\n

C: LOGIN <username> <password>\n
S: OK login\n  OR  ERR badcreds\n
```

#### File Operations (After Login)

**UPLOAD:**
```
C: UPLOAD <filename> <size_in_bytes>\n
S: READY\n
C: <raw binary data of exact size>
S: OK upload\n  OR  ERR upload <reason>\n
```

**DOWNLOAD:**
```
C: DOWNLOAD <filename>\n
S: OK download <size>\n<raw binary data>
   OR  ERR download notfound\n
```

**LIST:**
```
C: LIST\n
S: OK list <payload_size>\n<filename1 size1\nfilename2 size2\n...>
   OR  ERR list <reason>\n
```

**DELETE:**
```
C: DELETE <filename>\n
S: OK delete\n  OR  ERR delete <reason>\n
```

---

## File Storage Structure

```
server_storage/
├── users.txt              # User credentials (username password pairs)
└── <username>/            # Per-user directory
    ├── file1.txt
    ├── file2.jpg
    └── ...
```

### Atomic Writes
Files are written to `.tmp` files and atomically renamed to prevent corruption on crashes.

---

## Configuration

Edit `include/dropbox.h` to modify:

```c
#define SERVER_PORT 8080           // TCP port
#define CLIENT_QUEUE_CAP 256       // Max pending connections
#define TASK_QUEUE_CAP 1024        // Max pending tasks
#define CLIENT_POOL_SIZE 4         // Client thread count
#define WORKER_POOL_SIZE 4         // Worker thread count
```

Recompile after changes: `make clean && make`

---

## Concurrency Design

### Worker → Client Communication
- **Method**: Per-session result slot + condition variable
- **Rationale**: 
  - Direct pointer to session (no lookups)
  - Only client threads perform socket I/O (worker-safe)
  - Low contention (per-session locks, not global)
  - Deterministic task routing

### File Locking Strategy
- Per-file mutex (not global lock)
- Reference-counted lock entries
- Lock acquired order: `file_map_mutex` → `file_lock` → perform I/O
- Allows concurrent operations on different files

### Trade-offs
- **Current**: Single result slot per session (sequential task processing)
- **Alternative**: Per-session result queue (multiple outstanding tasks)
  - Would increase memory usage
  - Added complexity in result matching
  - Current design sufficient for Phase 1

---

## Error Handling

- All `pthread_*` return values checked
- Socket I/O uses robust helpers (`read_n_bytes`, `send_all`)
- Partial reads/writes handled with loops
- Client disconnect detection (session `alive` flag)
- Worker checks session validity before result delivery

---

## Known Limitations

- No user quota enforcement (can be added in Phase 2)
- Plain-text password storage (use hashing in production)
- No TLS/SSL encryption
- Single server instance (no horizontal scaling)

---

## Troubleshooting

### Port Already in Use
```bash
# Find process using port 8080
lsof -i :8080
# Kill it or change SERVER_PORT in dropbox.h
```

### Permission Denied on server_storage/
```bash
chmod 755 server_storage
```

### Client Connection Refused
- Ensure server is running: `ps aux | grep server`
- Check firewall settings
- Verify port in client matches server

---

## Deliverables

- ✅ Source code with proper file structure
- ✅ Makefile with all targets
- ✅ README (this file)
- ✅ Automated test scripts
- ✅ Valgrind clean run
- ✅ ThreadSanitizer clean run

---

## Authors

Muhammad Wasif BSCS23020
Mobeen Qamar BSCS23050
Saifullah Arshad BSCS23065
 
Operating Systems Lab - Phase 1

## License

Educational use only.
