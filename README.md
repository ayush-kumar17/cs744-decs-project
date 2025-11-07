# CS744-DECS Project – Phase 1
## Key-Value Store with Redis Cache and PostgreSQL Backend

### What This Project Does
This project is a simple key-value store built in C++ that demonstrates two different request paths:

- **Memory Path** — Data served directly from Redis (fast, in-memory cache)
- **Disk Path** — Data retrieved from PostgreSQL (persistent storage)

It exposes a small REST API (using `cpp-httplib`) and includes a basic load generator to test how the system performs under different workloads.

---

### Project Layout
```
CS744-DECS-Project/
├── include/
│   ├── httplib.h
│   └── thread_pool.hpp
├── server.cpp          # main key-value server (Redis + PostgreSQL)
├── loadgen.cpp         # load generator for testing
├── makefile
```
---

### Setting Up

Install the required dependencies:
```bash
sudo apt update
sudo apt install g++ make libpq-dev libhiredis-dev redis postgresql curl
```

Start Redis and PostgreSQL:
```bash
sudo systemctl start redis-server
sudo systemctl start postgresql
```

---

### Database Setup
Enter PostgreSQL:
```bash
sudo -u postgres psql
```

Then run:
```sql
CREATE DATABASE kvstore;
CREATE USER kvuser WITH PASSWORD 'kvpass';
GRANT ALL PRIVILEGES ON DATABASE kvstore TO kvuser;
\c kvstore
CREATE TABLE kv (k TEXT PRIMARY KEY, v TEXT);
\q
```

---

### Build
```bash
make clean && make
```

---

### Run the Server
```bash
./server
```

Expected output:
```
Connected to Redis
Connected to PostgreSQL
Server running on http://localhost:8080
```

---

### REST API Endpoints

| Method | Endpoint | Description |
|--------|---------|-------------|
| PUT    | `/kv/<key>` | Store key-value pair (writes to DB + cache) |
| GET    | `/kv/<key>` | Retrieve key (checks Redis first, then PostgreSQL) |
| DELETE | `/kv/<key>` | Delete key from both DB and cache |
| GET    | `/check_cache?key=<key>` | Check whether a key exists in Redis cache |

---

### Example Commands

```bash
# Insert a key
curl -X PUT -d "IIT Bombay" http://localhost:8080/kv/name

# Retrieve it (first → DB hit, next → cache hit)
curl http://localhost:8080/kv/name

# Delete it
curl -X DELETE http://localhost:8080/kv/name

# Check if it’s still in cache
curl "http://localhost:8080/check_cache?key=name"
```

---

### Load Generator (Optional)

Usage:
```bash
./loadgen <server-url> <mode> <threads> <duration-s> <popular-k>
# Modes: put-all | get-all | get-popular | mixed
```

Examples:
```bash
./loadgen http://localhost:8080 put-all 8 30 0
./loadgen http://localhost:8080 get-all 8 30 0
./loadgen http://localhost:8080 mixed 8 30 0
```

Typical output:
```
Success=12000 Fail=0 AvgLatency(ms)=1.3
```

---

### Phase 1 Checklist (You Have These)
- Redis (in-memory cache) + PostgreSQL (persistent backend)
- Clear separation of memory vs. disk paths
- REST API implemented using `cpp-httplib`
- Thread-safe access (mutex)
- `/check_cache` route for verification
- Load generator for quick testing

---

### Author
**Ayush Kumar**  
IIT Bombay — CS744: Distributed and Embedded Computing Systems