# Design notes

These notes explain the main design decisions in memkv, the alternatives considered, and how each
choice compares with real Redis.

## 1. Single-threaded event loop (reactor)

**Decision:** one thread multiplexes every client through a level-triggered `Poller` and runs
commands one at a time.

**Why:** an in-memory store is usually limited by memory latency and syscalls rather than by CPU.
With one thread, the storage engine needs no locks, every command is atomic for free, and
behavior is easy to reason about and test. Redis made the same choice (it later added optional I/O
threads, but commands still execute on one thread).

**Alternatives:**
- *Thread per connection:* simple, but needs locks on shared state and stops scaling around
  thousands of connections because of stack memory and context switches.
- *Global lock with a thread pool:* the lock serializes commands anyway, so it adds cost without
  adding throughput.
- *Shared-nothing sharding:* N event loops, each owning `hash(key) % N` of the keyspace. This is
  how memkv would scale past one core. Each shard has its own LRU list and expiry heap, which makes
  eviction approximate across the whole dataset. Multi-key commands that span shards would need
  coordination.

**Fairness and backpressure:** a client can deliver at most 1 MB per readiness event, so one fast
client can't starve the others. Output is buffered per client, and the socket is watched for
writability only while output is pending; watching it otherwise would make a level-triggered
poller wake on every iteration. A client whose queued output exceeds 64 MB is disconnected,
similar to Redis's `client-output-buffer-limit`.

## 2. Layering and testability

`net -> command -> {storage, protocol} -> ds -> common`. Each arrow is a CMake link dependency, so
a forbidden include fails to build.

- The **storage engine has no I/O.** It returns values and throws typed domain errors such as
  `WrongTypeError`. All RESP formatting happens in the command layer, so the same engine could sit
  behind an HTTP or gRPC front end.
- **Time is injected** through a `Clock` interface. Production uses `steady_clock`, which wall-clock
  changes can't affect. Tests use `ManualClock`, so TTL tests run instantly and deterministically.
- **Commands are objects.** Each Redis command is a subclass of `Command` that declares its
  name and `Arity` and implements `execute()` (the Command pattern). A `CommandRegistry` maps
  names to commands. Adding a command means adding a class, without touching the dispatcher (the
  open/closed principle), and each command can be read and tested on its own. The cost is one
  virtual call per request, which is negligible next to a hash lookup.
- **Errors cross one boundary.** `Dispatcher::dispatch` is the only place that turns exceptions
  into error replies. Argument validation happens before any mutation, so a malformed `ZADD` leaves
  the set untouched.

## 3. LRU eviction: exact, with an intrusive list

A hash map gives O(1) lookup but has no order. A linked list gives order but O(N) lookup. Combining
them gives O(1) for both: the map finds the entry, and the entry's embedded `prev`/`next` links let
it be moved to the front or evicted from the back in O(1).

- **Intrusive rather than `std::list`:** the links live inside `Entry`, so there is no separate
  list-node allocation and no iterator stored next to the map entry.
- **Sentinel node:** the list is circular around a dummy node, so each real node always has two
  valid neighbors. Link and unlink are each four pointer writes with no branches.
- **`string_view` map keys:** the index's keys view each entry's own `const std::string key_`. Each
  key is stored once, and a lookup by `string_view` never builds a temporary `std::string`.

**How Redis differs:** Redis does not keep an exact LRU list. Each object stores a 24-bit
last-access clock, and on eviction Redis samples a few random keys (`maxmemory-samples`, default
5), evicting the oldest from a small pool. That saves 16 bytes per key and avoids the two extra
cache misses that relinking neighbors costs on every read, in exchange for approximate
eviction. memkv's exact LRU makes eviction deterministic and easy to test; switching to sampled
LRU would only change `Keyspace`.

## 4. TTL expiration: lazy plus a heap-driven active pass

A pure lazy scheme (check on access) never frees keys that are never read again. Scanning every
key periodically costs O(N).

memkv uses both of the following:
1. **Lazy:** every lookup checks the deadline and deletes an expired key on the spot. Correctness
   never depends on the active pass.
2. **Active:** a min-heap of `{deadline, ticket, key}`. Each cron tick (every 100 ms) pops due
   items, up to a **budget** (1000 items by default). The budget matters: if a million keys expire
   at the same moment, the work is spread over many ticks instead of freezing the server for one
   long pause.

**Lazy heap invalidation with tickets:** deleting an arbitrary item from a binary heap needs a
position index. Instead, each `set_expiry` issues a fresh, globally unique ticket, stores it in the
entry, and pushes it with the heap item. A popped item is acted on only if its key still exists
and holds the same ticket. Tickets are better than comparing deadlines: if a key is deleted and
re-created, or toggled between two TTLs, an old item can carry an identical deadline but never an
identical ticket.

**Bounded memory:** stale items accumulate when TTLs change. Each key with a TTL has exactly one
current item, so when the heap grows past twice the number of keys with a TTL, one O(n) pass
removes the stale items and re-heapifies. At least n/2 pushes happen between passes, so the
amortized cost per push is O(1).

**How Redis differs:** Redis keeps a separate `expires` hash table and, about 10 times per second,
samples 20 random keys with a TTL, deleting the expired ones. It keeps sampling while a large
share of each sample turns out to be expired, within a CPU time budget. Sampling needs no ordered structure but expires keys in
no particular order. The heap expires them in exact deadline order at O(log E) per insert. A
*timing wheel* is another option, with O(1) insert at the cost of bucketed precision.

## 5. Sorted sets: hash map plus skip list

No single structure answers all the sorted-set queries efficiently:

| Query | Hash map | Skip list |
| --- | --- | --- |
| `ZSCORE member` | O(1) | O(N) without a member index |
| Is this member already present? | O(1) | O(N) |
| `ZRANGEBYSCORE` | O(N) | O(log N + K) |
| `ZRANK`, `ZRANGE` by rank | O(N) | O(log N) with spans |

So `SortedSet` keeps both, like Redis's `zset`, which pairs a `dict` with a `zskiplist`. Redis also
stores small sets, 128 members or fewer by default, in a compact array called a listpack; memkv
always uses the two-structure form. Each
member string is owned once, by its skip-list node, and the hash map's keys are views into it. A
score update re-links the same node, which keeps those views valid. The node is not freed and
reallocated.

**Why a skip list and not a balanced tree (`std::map`)?** Both give O(log N) search, insert, and
delete. Redis's author gave these reasons for choosing skip lists, and they apply here too:
- They are **simpler to implement and debug** than a red-black tree's rotation cases.
- **Range scans** are a plain walk along level 0.
- **Rank queries are easy to add.** Storing a span on each link makes rank an O(log N) sum. A tree
  would need subtree sizes maintained through every rotation, and `std::map` cannot be augmented
  at all.
- **Memory is tunable** through the promotion probability. With p = 1/4, a node averages 1.33
  links, versus 3 pointers plus a color in a red-black tree.

Concurrency is *not* the reason in Redis's case. Lock-free skip lists exist, as in Java's
`ConcurrentSkipListMap`, but Redis runs commands on a single thread.

**Implementation details:**
- Each node and its tower of links share one allocation: `[Node | Level 0 | ... | Level h-1]`, as
  Redis's `zskiplistNode` does with a flexible array member. An earlier version used a
  `std::vector` per node, which added a dependent cache miss on every hop. Switching to the single
  allocation made `ZADD` and `ZRANGEBYSCORE` roughly 1.5x faster in the in-process benchmark.
- Random levels come from splitmix64, with 8 bytes of state per set. `std::mt19937` would cost
  2.5 KB per sorted set. One 64-bit draw supplies all 32 two-bit promotion decisions.
- A randomized differential test checks spans, ranks, and ranges against `std::set` after
  thousands of random operations.

## 6. Protocol parsing: incremental and zero-copy

- **Incremental:** TCP is a byte stream, so a request can arrive split anywhere. The parser
  records its progress (arguments expected, offset reached, pending bulk length) and resumes there.
  Waiting for the rest of a 100 MB value is an O(1) length check per read, with no re-scanning.
- **Zero-copy:** completed arguments are `string_view`s into the connection's input buffer, so
  parsing a command performs no heap allocation. The trade-off is that an incomplete request's
  bytes aren't consumed until it completes, because the views have to point at them. That uses
  the same memory the copies would have.
- **Defensive limits** like Redis's: a 64 KB inline or header line, 512 MB bulk strings, and 1M
  arguments. A malformed request gets an `ERR Protocol error` reply and the connection is closed,
  because the stream can't be resynchronized.
- Both **multibulk** (sent by client libraries) and **inline** commands (typed into telnet or
  `nc`) are supported, as in Redis.

## 7. Performance work, driven by measurement

Profiling with callgrind under `redis-benchmark` load found four improvements:
1. Single-allocation skip-list nodes (see section 5).
2. Zero-copy arguments instead of one `std::string` per argument, which removed a malloc and free
   per argument longer than 15 bytes.
3. Breaking out of the read loop after a short `recv()`, which saves one syscall per event that
   would only have returned `EAGAIN`. Level-triggered polling makes this safe.
4. ASCII-only case folding for command names instead of the locale-aware `std::tolower`.

With pipelining, the remaining gap to Redis is spread across parsing, hashing, and allocation. The
largest single factor is probably the allocator: Redis uses jemalloc and memkv uses glibc malloc.

## 8. Known gaps and how they would be addressed

| Gap | Approach |
| --- | --- |
| Persistence | An append-only command log with `fsync` every second, plus snapshotting via `fork()` and copy-on-write, as Redis does. |
| Memory-based limit | Track approximate bytes per entry and sorted set, and evict until under `maxmemory`. |
| Rehash latency spikes | `std::unordered_map` rehashes every key at once when it grows. Redis rehashes incrementally, moving a few buckets per operation between two tables; this would need a custom hash table. |
| Multi-core scaling | Shared-nothing shards with one event loop per core (see section 1). |
| Larger command set | `ZREVRANGE`, `ZRANGE ... LIMIT`, `INCR`, and hashes and lists as new `Value` alternatives. |
