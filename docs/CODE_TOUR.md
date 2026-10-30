# Code tour

A guided path through the codebase, followed by a glossary of the C++ features it uses. Read
the files in the order below; each step builds on the previous one.

## The big picture in one paragraph

A client sends bytes such as `*2\r\n$3\r\nGET\r\n$4\r\nuser\r\n`. The **net** layer reads them
from the socket. The **protocol** layer cuts them into arguments (`["GET", "user"]`). The
**command** layer finds the `GET` command and runs it against the **storage** layer, which looks
the key up in its hash map. The reply travels back up the same way and is written to the socket.
Each layer only talks to the one directly below it.

```
net  ->  protocol  ->  command  ->  storage  ->  ds
(sockets)  (bytes <-> args)  (GET, ZADD...)  (keys, LRU, TTL)  (data structures)
```

## Reading order

### 1. Data structures (`src/memkv/ds/`): plain algorithms, nothing Redis-specific

| File | What to look at |
| --- | --- |
| `intrusive_list.hpp` | A doubly linked list with a sentinel node. Look at `link_after` and `unlink`: four pointer writes each, with no `if`s. |
| `skiplist.hpp` | The public interface of the skip list: insert, erase, rank, and range queries. |
| `skiplist.cpp` | The algorithms. Start with `find`, then `link` (insert) and `unlink` (erase). `rank` shows how adding up spans gives a position. |
| `skiplist_node.hpp` | How a node and its links share one allocation. This is the most low-level code in the project; skip it on a first read. |
| `expiry_heap.hpp` | A min-heap of deadlines built on `std::push_heap`/`std::pop_heap`. |
| `random.hpp` | A tiny random number generator used to pick skip-list node heights. |

### 2. Storage engine (`src/memkv/storage/`): the database itself, no networking

| File | What to look at |
| --- | --- |
| `value.hpp` | A value is a string *or* a sorted set (`std::variant`). |
| `entry.hpp` | One key: name, value, expiry time, and its links in the LRU list. |
| `sorted_set.hpp/.cpp` | Hash map plus skip list, kept in sync by `add` and `remove`. |
| `keyspace.hpp/.cpp` | All keys. `find` (lookup plus "recently used"), `put` (insert plus eviction), and `expire_due` (background expiry). The most important file in the project. |
| `database.hpp/.cpp` | The API the commands call: `get`, `set`, `zadd`, and so on. Mostly delegates to `Keyspace` and `SortedSet`, adding type checks. |
| `database_types.hpp`, `errors.hpp` | Small option, result, and error types used by `Database`. |

### 3. Protocol (`src/memkv/protocol/`): bytes in, bytes out

| File | What to look at |
| --- | --- |
| `request_parser.cpp` | `parse` (step 1: find the start of a request) and `parse_bulk_strings` (step 2: read its arguments). |
| `reply.hpp/.cpp` | A reply value and `encode`, which writes it as RESP text. |

### 4. Commands (`src/memkv/command/`): one class per Redis command

This layer uses the **Command design pattern**:

```cpp
class GetCommand final : public Command {
public:
    GetCommand() : Command("get", Arity::exactly(1)) {}

    Reply execute(Database& db, const Arguments& args) const override {
        std::optional<std::string> value = db.get(args.text(0));
        return value ? Reply::bulk(std::move(*value)) : Reply::null();
    }
};
```

| File | What to look at |
| --- | --- |
| `command.hpp` | The `Command` base class and `Arity` (how many arguments a command takes). |
| `arguments.hpp/.cpp` | Typed, validating access to arguments: `args.integer(1)`, `args.score(2)`. |
| `commands/*.cpp` | The commands, grouped by family: server, key, string, sorted-set. |
| `command_registry.hpp/.cpp` | Finds a command by name, ignoring case. |
| `dispatcher.hpp/.cpp` | Looks up, checks arity, executes, and turns errors into replies. Read this first in this layer; it is short. |
| `replies.hpp/.cpp` | Reply builders shared by several commands. |

**To add a command** (for example `STRLEN`), write a class like `GetCommand` in
`commands/string_commands.cpp` and add `registry.add<StrlenCommand>();` to
`register_string_commands`. No other file changes. This is the *open/closed principle*: the
system is extended by adding code, not by editing existing code.

### 5. Networking (`src/memkv/net/`): the event loop

| File | What to look at |
| --- | --- |
| `server.cpp` | `Server::run`: wait for events, then accept or serve clients, then run the cron. This is the whole server. |
| `connection.cpp` | One client: `receive`, then `process_requests`, then `flush`. |
| `poller.hpp` | One interface with two implementations: `poller_epoll.cpp` (Linux) and `poller_poll.cpp` (portable). |
| `socket.hpp/.cpp` | `UniqueFd` closes a file descriptor automatically, plus helpers for listening and accepting. |

### 6. Application (`apps/`)

`memkv_server.cpp` creates the clock, database, and server, and runs it. `server_options.cpp`
parses `--port` and the other flags.

## Design principles you can point to

| Principle | Where it shows up |
| --- | --- |
| Single responsibility | Each file has one job: the parser parses, commands validate and execute, the keyspace manages keys. |
| Open/closed | New commands are new classes; the dispatcher never changes. |
| Dependency inversion | `Keyspace` depends on the abstract `Clock`, not on the system clock, so tests can control time. |
| Polymorphism | `Command::execute` is virtual. `Poller` hides epoll vs poll behind one interface (the pimpl idiom). |
| Encapsulation | Only `Keyspace` can change an entry's expiry (`friend class Keyspace`); everyone else gets read-only access. |
| RAII | `UniqueFd`, `std::unique_ptr`, and containers release resources automatically. No manual `close` or `delete` outside the two low-level data structures. |
| Layering | CMake libraries enforce that lower layers never include upper ones. |

## C++ features used, and why

### Why `std::` everywhere instead of `using namespace std;`

`using namespace std;` makes every name from the standard library usable without the `std::`
prefix. It's common in tutorials and competitive programming because it saves typing. Most
professional codebases avoid it, and the Google, LLVM, and C++ Core Guidelines style guides say
not to use it (Core Guidelines rule SF.7), for three reasons:

1. **Name collisions.** `std` contains hundreds of names (`count`, `distance`, `data`, `size`,
   `byte`, `hash`, ...). Pulling them all in can clash with your own names, sometimes silently
   picking the wrong function.
2. **Headers spread it.** A `using namespace std;` in a header applies to every file that
   includes that header, so the collisions spread through the project.
3. **Readability.** `std::string` tells the reader exactly where a name comes from.

memkv therefore writes `std::` explicitly. Two narrower forms are fine and do appear:

```cpp
using namespace std::chrono_literals;  // in tests only: enables 100ms, 5s literals
using protocol::Reply;                 // a using-declaration: imports ONE name, inside a .cpp
```

If you were taught `using namespace std;`, it's fine in small single-file programs and
contests. In an interview, writing `std::` shows you know the convention.

### Other features

| Feature | Example | Why it's used |
| --- | --- | --- |
| `namespace memkv::storage { ... }` | every file | Groups code by layer; avoids clashes with other libraries. |
| Anonymous namespace `namespace { ... }` | top of most `.cpp` files | Helpers visible only inside that file (like `static` functions in C). |
| `std::string_view` | `find(std::string_view key)` | A non-owning view of text: reads a string without copying it. |
| `std::unique_ptr<T>` | `std::unique_ptr<Entry>` | Single owner; deletes automatically. |
| `std::optional<T>` | `std::optional<std::string> get(...)` | "A value or nothing", instead of sentinel values or null pointers. |
| `std::variant<A, B>` | `Value` | Holds exactly one of several types, type-safely. |
| `std::span<T>` | `std::span<const std::string_view>` | A view over a contiguous array, such as the request arguments. |
| `enum class` | `ValueType::kString` | Scoped, type-safe enumerations. |
| `[[nodiscard]]` | most getters | The compiler warns if a result is accidentally ignored. |
| `const` and `noexcept` | everywhere | Document and enforce "doesn't modify" and "doesn't throw". |
| `= delete` | copy constructors | Forbids copying objects that must not be copied (sockets, lists). |
| `final` and `override` | command classes | The compiler checks the method really overrides a base-class method. |
| Designated initializers | `{.ttl = 100ms}` | Name the fields you set; the rest keep their defaults. |
