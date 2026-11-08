# 2026-11-07 — Protected Branch Pull Request Workflow, Atomic Versioning, and Post-Merge Branch Lifecycle

Decision: Adopt a branch-protection pull request workflow with atomic version increments and post-merge branch pruning:
- Development occurs on isolated feature branches (`feat/<topic>`). Direct pushes to `main` are prohibited under branch protection rules.
- Atomic Version Bump: Semantic versions (`AKAOS_VERSION`, `XSCORE_VERSION`) in `sdk/include/akaos/version.h` are updated in a single atomic commit at the end of the feature branch before opening the PR, avoiding intermediate version thrashing.
- Post-Merge Pruning: Once merged to `main`, feature branches are pruned locally and remotely to prevent repository sprawl while preserving full history in the linear commit log of `main`.
- Windows Host Git Isolation: All `git` interactions are performed strictly from the Windows host environment (`cmd.exe` or PowerShell) using Windows Credential Manager.

Reason: Protects repository stability, eliminates merge conflicts, keeps commit history linear and clean, and prevents credentials issues with WSL.

# akaOS Architectural Decisions

# 2026-11-03 — Job control specification, multi-job bg, and built-in kill %jobspec

Decision: Implement POSIX/Bash-compatible job control extensions in akash:
- Support `bg [job_id...]` with multiple job arguments and default fallback to the shell's current job.
- Track current (`+`) and previous (`-`) jobs dynamically using a monotonically increasing sequence counter, prioritizing stopped jobs over running background jobs for current job selection.
- Implement shell built-in `kill` in akash supporting `%jobspec` (`%n`, `%%`, `%+`, `%-`) target resolution and group signaling.
- Enhance `help bg` to provide comprehensive syntax documentation and examples.

Reason: Standard job control workflows (backgrounding stopped processes with `bg`, switching foreground tasks with `fg`, inspecting status with `jobs`, and terminating background jobs with `kill %jobspec`) are essential interactive shell primitives.

# 2026-10-27 — Stamp X11 events per receiver, and keep unmentioned configure values

Decision: the native X11 display server writes the sequence number of the
receiving client into every event it queues, and writes the window of the
delivery into the first window field of every event whose protocol record
starts with a window.  The delivery window is the window itself for a
selection on the window and the parent for a selection on the children; the
keyboard, button, motion and crossing events keep their timestamp first field,
and the mapping notifications carry no window.  `ConfigureWindow` starts from
the current position, size and border, and a value the request does not
mention keeps its current value; an absent stack mode leaves the stack alone.

Reason: XCB advances its request-completion tracking from the sequence number
of every packet it reads.  An event that carried the sequence number of the
window owner, which is zero for a root window, wrapped the tracker to the next
16-bit epoch after request 164, completed every pending request and dropped
the stored `InternAtom` reply; libX11 then reported a `_XReply` I/O error and
Openbox exited.  The first window field is what a client library filters a
typed window event on, so a `ReparentNotify` built with the parent in it never
matched the client's window.  A configure request that only moved or
restacked a window without a size previously overwrote the size with zero and
destroyed the window's backing store.  The unreconciled limits remain: the
RENDER and input extensions, the selection event layout, and the rest of the
core protocol are still absent.


# 2026-09-24 — Implement bounded AF_UNIX SCM_RIGHTS before D-Bus fd lists

Decision: add libc/kernel `sendmsg()` and `recvmsg()` with a native-width
`msghdr` ABI, and initially support reference-counted `SCM_RIGHTS` transfer on
connected AF_UNIX streams. Queue records are associated with stream byte
positions; install all delivered descriptors atomically; retain records on
`EMFILE`; report `MSG_CTRUNC` for insufficient control space; discard
ancillary rights consumed by plain reads; and release undelivered references
when sockets close. Hold the active socket file reference across these new
message operations so descriptor close/reuse cannot invalidate the socket.
Reject passing socket descriptions until the kernel has Unix-socket reference
cycle collection.

Reason: D-Bus signature `h` denotes a Unix descriptor, and native desktop
service communication needs true descriptor transport, not just integer body
values. A bounded kernel transport is a prerequisite for a later D-Bus fd-list
implementation and enables real pipe/resource transfer between processes.
Cross-architecture build and QEMU tests exercise that transfer on all six
ports. This decision does not claim full socket ABI or upstream desktop
compatibility; descriptor limits, unsupported flags, and missing D-Bus fd-list
integration remain explicit follow-up work.

# 2026-09-24 — Normalize D-Bus frames to the existing little-endian dispatch ABI

Decision: accept both standard D-Bus `l` and `B` markers at message boundaries,
validate protocol version, frame sizes, typed header fields, signatures, and
body layout, then normalize numeric fields and body values to little-endian
before handing frames to the current bounded client and broker handlers. Keep
existing little-endian public APIs and dispatch internals stable. Encode every
`as` array element at its required 4-byte boundary, including `ListNames`.

Reason: byte order is a property of each D-Bus message, not of the host ABI;
hardcoding the frame header while correctly supporting body scalars still
rejects valid peers. A centralized normalizer avoids duplicating subtle
header/body conversion rules through sync, async, and broker paths. Live
big-endian broker calls plus the full six-port compositor matrix validate
this bounded contract. It does not imply full D-Bus method/policy coverage or
binary compatibility with unmodified KDE/GNOME/LXDE/LXQt components.

# 2026-09-24 — Keep legacy D-Bus body APIs little-endian and expose explicit order

Decision: preserve the existing body codec entry points as little-endian
wrappers and add explicit-byte-order APIs for D-Bus `l` and `B` bodies. Apply
the order consistently to scalar values and array byte lengths, leaving
alignment and byte-string fields unchanged.

Reason: frame byte order belongs to each D-Bus message, while existing akaOS
callers and broker methods use the little-endian ABI. An explicit codec
parameter allows correct body handling without silently changing those
callers. Exact big-endian scalar, double-bit, and array vectors now pass on
all six architecture guests. At this codec-only stage, frame parsing was not
included; the subsequent frame-normalization decision above records that
integration. Full D-Bus semantics remain a separate requirement.

# 2026-09-24 — Enforce D-Bus UTF-8 and signature wire limits

Decision: cap D-Bus signatures at 255 bytes and validate every `s` body value
as strict UTF-8 during encoding, body validation, and decoding. Reject
embedded NULs, overlong/truncated sequences, surrogate code points, and
values above U+10FFFF; keep object-path grammar validation in addition to its
ASCII UTF-8 validity.

Reason: malformed strings accepted by a shared codec can reach D-Bus name,
path, property, or activation logic with different interpretations across
services, and accepting an overlength signature violates the D-Bus wire
contract. The regression exercises valid multibyte text plus malformed
outbound and inbound vectors on all six architectures. This hardens one codec
boundary; it does not replace complete D-Bus message semantics, general
broker dispatch, or full upstream D-Bus compatibility.

# 2026-09-24 — Reparent exiting processes' children to the active init reaper

Decision: register the actual kernel-launched `/bin/init` process as the
orphan reaper in every architecture's boot path instead of assuming PID 1.
When a process exits, under the process-table lock, move all visible children
to the registered live reaper before publishing the exiting process as
`EXITED`; if init is itself exiting or unavailable, detach them with PPID 0
until its replacement registers and adopts them. `waitpid` must snapshot
parent ownership and state while holding the process-table lock followed by
the candidate's process lock. Init polls its login child so it can reap other
adopted zombies without waiting for the session to end.

Reason: exposing `getppid()` without updating parentage leaves children with
a stale PID after parent exit and can make the former parent appear to own
children it can no longer wait for. Registration must be explicit because
the native direct-kernel architecture entrypoints do not use the shared x86
startup loop, and the reaper PID must not be guessed from process allocation
order. The six-port regression waits/reaps an intermediate parent, verifies
the grandchild observes a new live PPID, and checks the old parent gets
`ECHILD`. This is bounded kernel-native behavior, not complete POSIX reaper,
signal, or job-control semantics.

# 2026-09-24 — Expose the process record's parent PID through getppid

Decision: implement syscall 73 as a read-only accessor for the current XSCore
process's recorded `parent_pid`, protected by that process's lock, and make
libc `getppid()` call it. Verify with a real fork where the child checks that
its parent PID equals the PID captured in the caller before fork. Keep orphan
reparenting and process-slot reuse as explicit follow-up requirements rather
than returning a guessed parent.

Reason: returning zero for every process violates a basic POSIX process
identity contract used by supervision and session services. The kernel
already records parentage for fork/spawn, so exposing that state avoids a
userspace guess and works through the common dispatcher on all six CPU ports.
The guest matrix verifies a live parent-child relationship; it does not yet
prove init adoption after parent death or complete process teardown semantics.

# 2026-09-24 — Snapshot and repair bounded pthread state around fork

Decision: implement a fixed 32-entry `pthread_atfork()` registry in libc.
Snapshot handlers before invoking them; run prepare callbacks in reverse
registration order and parent/child callbacks in registration order. Hold the
pthread/TSD-record lock across the kernel fork, release it in the parent, and
repair the fork child's copied state before invoking child callbacks: retain
and renumber the caller, clear vanished workers' records and TSD, unmap their
copied pthread stacks, and reset copied lock/cleanup/cancellation state.
Before i386 ring-3 entry, validate and prefault the `user_sp + 4` ABI argument
slot because the assembly shim writes it while at CPL0.

Reason: after fork only the calling thread survives, so copied libc records
for other workers and a possibly owned lock would leave child `pthread_create`
and synchronization broken. The stable handler snapshot permits callbacks to
run outside the registry lock while preserving POSIX ordering. The i386
prefault prevents the kernel transition shim from page-faulting while writing
to a nonresident user stack slot. A multithreaded-fork regression checks
callback ordering and child thread creation; the full native QEMU matrix passes
on all six architectures. This is bounded child-state recovery, not complete
POSIX fork/exec or NPTL semantics.

# 2026-09-24 — Make local peer identity a kernel socket property

Decision: capture effective PID/UID/GID at AF_UNIX `listen()` and `connect()`
time and expose the opposite endpoint's snapshot through `SO_PEERCRED`.
Authenticate D-Bus EXTERNAL claims against that kernel data and serve standard
connection identity queries from credentials retained on the live bus client.
Do not treat TCP or greeting-only sessions as authenticated D-Bus transports
when the kernel cannot supply peer identity.

Reason: a daemon cannot safely establish identity from the UID claimed during
protocol negotiation, and a process ID must not be synthesized from a bus
unique-name counter. Kernel capture binds the identity to the process that
established the local socket and gives future session services a real primitive
shared across the six supported ABIs. The QEMU matrix verifies root and
unprivileged peer identities and standard broker replies. Multi-user bus
authorization, credential delegation, and full upstream D-Bus behavior remain
separate work.

# 2026-09-24 — Reuse static-TLS offsets after DSO unload

Decision: allocate each static `PT_TLS` image from an aligned first-fit range
within the bounded per-thread area, considering only live loader objects.
Releasing the final object reference frees that range and decrements the live
static-TLS module count; a later DSO may reuse the offset, but receives a new
generation so existing threads copy its fresh template.

Reason: a monotonic offset cursor leaked capacity across ordinary
`dlopen`/`dlclose` cycles, and a stale nonzero module count caused new threads
to allocate static-TLS areas even when no static-TLS DSO remained. A 64-byte
aligned 8 KiB probe plus 12 reload cycles exercises range reuse and template
reinitialization; the loader and compositor/client matrices pass on all six
architecture targets. The fixed-size region is still a bounded static-TLS
implementation, not the complete TLS ABI of upstream runtimes.

# 2026-09-23 — Follow POSIX pthread-key destructor re-arming rules

Decision: remove the current thread's key value before calling its destructor,
permit the destructor to install a replacement for another bounded
`PTHREAD_DESTRUCTOR_ITERATIONS` pass, and reclaim any value left after the last
pass. Setting a key to null and deleting a key both return their fixed-table
slots to the allocator.

Reason: a destructor must observe its key as unset and may republish a value;
clearing only after the callback both exposes stale state and erases a valid
replacement. The previous behavior also leaked fixed TLS-value slots across
thread exits. The six-port regression checks four re-arms and reuses one key
across 65 sequential workers, exceeding the 64-entry per-key thread table.
This remains a bounded pthread implementation, not full NPTL compatibility.

# 2026-09-23 — Preserve successful D-Bus routes across peer teardown

Decision: once a pending route has received and forwarded its method reply,
connection teardown must not change its completion result. Disconnect cleanup
only fails routes that are still pending. Resident desktop-role services also
remain registered while their compositor surface is unavailable, until the
role is explicitly stopped or its service connection fails.

Reason: activated D-Bus providers are allowed to be short-lived, and their
reply can arrive immediately before they close. Treating the close as route
failure made a successfully forwarded reply race with cleanup. Display
availability is likewise independent of role discovery and service ownership;
keeping those lifetimes separate permits headless/recovery service queries.
Standard local EXTERNAL authentication now compares the UID claim with
`SO_PEERCRED`. The legacy same-UID greeting is restricted to local UNIX
connections; this remains a bounded policy, not complete multi-user D-Bus
authorization or credential delegation.

# 2026-09-20 — Keep dlfcn scope handles ABI-compatible and bounded

Decision: represent `RTLD_DEFAULT` as the null handle and `RTLD_NEXT` as the
all-ones handle, matching the conventional POSIX ABI. Default lookups walk
the bounded loaded-object table and libc built-ins; `RTLD_NEXT` identifies the
mapped caller DSO from the return address when possible and begins after it,
otherwise using a bounded global fallback. Versioned lookups reuse the same
provider-version filtering as relocation and explicit-handle paths. Honor
`RTLD_LOCAL`/`RTLD_GLOBAL` for process-wide visibility, promoting an existing
object if it is reopened globally while keeping explicit handles valid.

Reason: GLib/GIO and Qt plugin code routinely probes optional symbols through
these scopes. Matching their handle values avoids source-level shims while
preserving akaOS's fixed object-table and bounded metadata model. Explicit
handles recurse through `DT_NEEDED` dependencies using the same fixed visited
set, so a plugin can resolve a symbol exported by a transitive provider
without allowing dependency cycles to recurse forever. This does not imply a
complete glibc symbol namespace or complete upstream ABI.

The loader may reclaim a DSO mapping and its bounded per-thread TLS blocks
only after the final reference is closed; a fresh `dlopen` must copy the
template again. `dltest` exercises this close/reload boundary on all ports.
Static TP-area allocation and key cleanup remain separate decisions.

# 2026-09-20 — Match bounded ELF symbol versions explicitly

Decision: parse `DT_VERSYM`, `DT_VERDEF`, and `DT_VERNEED` only within the
mapped DSO ranges and a fixed record bound. For an unversioned lookup, accept
the provider's non-hidden default version; for relocations, honor the
requester's exact version requirement; expose `dlvsym()` for callers that
choose a version directly. Reject malformed tables before dereferencing them.

Reason: Qt/GLib-family DSOs commonly carry symbol-version metadata even when
the caller uses the default symbol name. This adds the selection semantics
without coupling akaOS to glibc's loader or making the metadata parser
unbounded; the dependency-bearing probe verifies relocation-time selection,
not just an explicit lookup. The bounded static PT_TLS/TP foundation was
added in later work; the remainder of the upstream ABI remains a separate
follow-up layer.

# 2026-09-20 — Keep dynamic TLS ABI bounded but compiler-correct

Decision: resolve the compiler-generated general-dynamic TLS forms used by
akaOS's six target toolchains without pretending to provide static TLS. For
AArch64, register each `R_AARCH64_TLSDESC` descriptor and dispatch it through
a TP-relative resolver whose shim preserves x8. For RISC-V, recognize the
LLVM-defined DTPMOD32/64 (6/7) and DTPREL32/64 (8/9) relocations. Continue to
reject TPREL/static TLS until XSCore and pthreads own a real per-thread TLS
area.

Reason: this keeps the native loader independent of Linux while making real
PIC DSO TLS access reliable on every supported architecture. The strict
six-port live test now proves template initialization and worker isolation;
the remaining static-TLS and full upstream ABI layers stay explicit follow-up
work.

# 2026-09-19 — Emit a bounded standard ELF startup vector

Decision: put the common ELF startup metadata on every kernel-created user
stack, including program-header location/count/entry size, page size, entry,
interpreter base, credentials, secure mode, `AT_EXECFN`, and `AT_NULL`. Derive
the program-header pointer from `PT_PHDR` or from the containing `PT_LOAD`.
Keep the vector fixed-size and omit entropy/platform/HWCAP values until their
kernel sources exist.

Reason: a native dynamic interpreter and future libc/toolkit startup code need
the conventional ELF contract even when they do not use Linux. Calculating
PHDR from the mapped segment keeps the contract correct for akaOS's fixed
32-bit link addresses while preserving the small cross-architecture kernel
boundary. This is deliberately a bounded subset, not complete upstream auxv.

# 2026-09-19 — Run bounded DSO lifecycle callbacks in userspace

Decision: keep `DT_INIT`/`DT_FINI` and bounded init/fini-array dispatch inside
the native userspace loader. Run constructors only after all declared
dependencies are relocated, and run finalizers in reverse order immediately
before the last mapping is released. Reject malformed table sizes and bound
callback counts to preserve the fixed-resource model.

Reason: real desktop toolkits rely on shared-object constructors for type
registration, allocator setup, and other startup state. Keeping the callbacks
in `dlopen` preserves XSCore's small kernel boundary and keeps behavior the
same across all six ports. This is a lifecycle tranche, not a claim of full
TLS, symbol-version, or upstream desktop ABI compatibility.

# 2026-09-19 — Stage a native interpreter for PT_INTERP binaries

Decision: keep the kernel's ELF loader bounded and stage a packaged native
interpreter when an executable declares `PT_INTERP`. Preserve the executable
path through a small `AT_EXECFN` auxiliary-vector pair, let the userspace
interpreter reuse the existing `dlopen`/DT_NEEDED/relocation engine, and expose
only an akaOS-specific `akaos_dl_entry()` accessor for the relocated entry.
Use separate dynamic linker scripts so ordinary static images never acquire an
empty `PT_DYNAMIC` segment.

Reason: real desktop toolchains require a dynamic execution boundary, but
moving the complete loader into kernel space would duplicate policy and make
the six ports harder to keep identical. A staged native interpreter gives
XSCore a clear handoff while preserving its independent VFS, mmap, and process
model. This is deliberately a first ABI tranche; static TLS/TP, constructors,
full auxv and complete upstream ABI compatibility remain later
work.

# 2026-09-19 — Back GIO enumeration with native VFS records

Decision: implement the file-manager-facing GIO enumeration tranche directly
over XSCore `getdents` and `stat`, with fixed-size batches and opaque
reference-counted `GFileEnumerator`/`GFileInfo` objects. Keep the public
surface limited to stable metadata accessors and deterministic close/error
semantics.

Reason: a desktop file manager must discover directory contents and metadata;
file-content APIs alone cannot support a real session. Native VFS records
preserve akaOS independence and cross-architecture behavior while keeping
resource bounds explicit. Full GIO monitoring, async enumeration, and the
upstream ABI remain later work.

# 2026-09-19 — Keep desktop file I/O native and 32-bit freestanding

Decision: expose only the bounded GIO file-content operations needed by the
native desktop substrate, route them through akaOS VFS syscalls, and map
packaged compatibility-library aliases to one canonical runtime DSO before
loading. Keep runtime objects size-optimized for x86_32 rather than expanding
the kernel's fixed 128-page per-process mapping table. Implement userspace
u64 decimal formatting with restoring division where a compiler helper would
introduce an unavailable ABI symbol.

Reason: desktop startup needs real file/config reads and writes, but a full
GLib/GIO port is far beyond the bounded runtime contract. Canonical aliases
avoid duplicate DSO mappings, while the freestanding arithmetic keeps all six
ports buildable without host runtime dependencies. The resulting probes are
meaningful native compatibility checks, not a claim of complete upstream
toolkit ABI coverage.

# 2026-09-17 — Make the D-Bus compatibility tranche observable and bounded

Decision: implement the standard broker calls needed by native desktop
clients (`GetNameOwner`, `GetConnectionUnixUser`, `GetMachineId`,
`Introspect`, `StartServiceByName`, and `Ping`) with explicit D-Bus error
frames for invalid or unsupported calls. Keep resident akaOS desktop roles
answering their own status/capabilities/introspection/ping methods, and use a
bounded 4 KiB wire buffer plus a 2 KiB standard-probe response buffer for the
broker's complete XML.

Reason: returning an empty successful reply masks integration failures and
does not let a client distinguish an absent service from a broker bug. The
standard calls are a useful, testable substrate for session startup while the
bounded buffers preserve akaOS's fixed-resource design. This is deliberately
not a claim to implement the complete D-Bus specification or every upstream
desktop service interface.

# 2026-09-17 — Keep translated poll results out of ARM32 r14 across SVC

Decision: copy kernel-written `pollfd.revents` from the translated ABI array
through a `noinline` helper after the blocking syscall returns. Keep the
canonical userspace TTY input buffer at 1024 bytes.

Reason: ARM32 uses r14 as the link register. A blocking SVC can yield to the
scheduler, so retaining a translated stack pointer in r14 across that call is
not safe; the kernel correctly reported timer readiness while userspace read
stale/corrupted revents. A separate no-inline call gives the compiler a fresh
post-SVC frame without changing the kernel ABI or shrinking interactive input.

# 2026-09-17 — Scope D-Bus activation ownership to resident desktop roles

Decision: register one bounded native session-bus activation definition and
claim the matching well-known name per resident
`desktop-compat --component` role, using stable `org.akaos.*` names and
canonical alias paths; do not publish activation metadata from `--once`
probes, and only release/unregister definitions owned by the current process.

Reason: composed desktop sessions need independently addressable window
manager, panel, file-manager, and session roles, and a real resident role must
hold its bus name for routed calls. An immediately exiting probe must not
leave a broker entry racing its own teardown. The bounded registration and
claim/release APIs provide this native lifecycle without pretending to
implement the complete upstream D-Bus service-owner protocol. Duplicate
registrations remain non-fatal so a second session does not delete the first
session's service definition.

# 2026-09-16 — Keep GTK container removal explicit and ownership-safe

Decision: expose a bounded remove operation that only detaches a child from
the exact parent recorded in its fixed-layout runtime object, compacts the
parent array, and clears the child's parent link. Reparenting requires a
successful remove before a new add.

Reason: source ports rebuild containers and move widgets between boxes during
session startup. Leaving stale links makes later add operations fail or lets
destroy paths reference the wrong owner. A strict parent check and fixed-array
shift preserve deterministic ownership without dynamic container metadata.

# 2026-09-16 — Treat GLib source destroy notifications as owned lifecycle

Decision: store one optional destroy notifier on each fixed runtime source and
invoke it exactly once when a callback returns false or a source is removed;
defer invocation until a callback finishes if removal occurs during dispatch.
Priority arguments on the `*_add_full` wrappers remain source-compatible but
are not used by the bounded scheduler.

Reason: desktop event loops routinely attach owned state to timers and idle
sources. Dropping that callback leaks ownership, while freeing it before a
currently executing callback returns is unsafe. A single notifier pointer in
the existing source slot provides the normal lifecycle contract without
introducing an unbounded allocator or scheduler queue.

# 2026-09-16 — Make GTK show-all tree behavior recursive but bounded

Decision: implement `gtk_widget_show_all()` as a depth-guarded traversal of
the fixed runtime parent/child links, setting visible and clearing minimized /
closed bits for each reachable widget.

Reason: GTK callers normally use one root show-all operation to expose an
entire constructed tree. Showing only the root leaves labels and controls
hidden, while unrestricted recursion could loop if a malformed source-port
tree creates a cycle. The fixed window capacity supplies a deterministic safe
bound without adding dynamic graph metadata.

# 2026-09-16 — Keep GObject convenience properties in the shared object ABI

Decision: expose a small variadic `g_object_set/get` surface for the common
string, boolean, and integer property names used by desktop toolkits, and
apply recognized window properties directly to the shared fixed-layout runtime
object as well as the local property table.

Reason: GLib and GTK compatibility code is loaded as separate DSOs. Updating
only one DSO's private metadata makes a property appear to succeed while the
widget observed by the other DSO remains stale. A bounded name/type switch and
the existing shared handle layout preserve cross-DSO behavior without adding
an unrestricted GObject type system or allocator ABI.

# 2026-09-16 — Re-size the x86_32 runtime arena after GObject growth

Decision: use a 52 KiB fixed runtime arena on x86_32 and keep
`USER_MAX_MMAP_PAGES` at 128; 64-bit and native ports retain the 64 KiB arena.

Reason: the GApplication and cross-DSO GObject additions grew the 32-bit DSO's
resident read/write segment. The smaller arena keeps GLib, GTK, and Qt
compatibility DSOs mappable together without changing the kernel's proven
address-space bookkeeping or reintroducing the i386 fault.

# 2026-09-16 — Schedule runtime sources by due time, not callback count

Decision: maintain a wrap-safe logical millisecond clock in each compatibility
runtime DSO. A timeout source becomes eligible at `due_ms`, repeating sources
are rescheduled after dispatch, and each dispatch pass snapshots due sources.

Reason: dispatching every source on every pass breaks animation, debounce, and
session timeout behavior; processing sources added mid-pass can also starve
existing work. A small deterministic clock keeps the freestanding runtime
bounded while matching the ordering and delay contract expected by toolkit
event loops.

# 2026-09-16 — Construct GLib loops before dispatching them

Decision: `g_main_loop_new()` must return a usable loop handle immediately;
the optional `running` argument initializes the state only, while
`g_main_loop_run()` owns dispatch entry.

Reason: entering dispatch inside the constructor prevents callers from adding
the sources that terminate or drive the loop and diverges from normal GLib
usage. The explicit two-step lifecycle is safe across the separately loaded
compatibility DSOs and is now dynamically tested on all six ports.

# 2026-09-16 — Use one explicit quit-driven loop for GLib and GTK entry points

Decision: back `GMainLoop`, `GMainContext`, and `gtk_main()` on the same
reference-counted akaOS runtime loop. Dispatch all sources present at the
start of each pass, and leave the loop only when a caller invokes quit (with a
bounded empty-pass guard for malformed tests).

Reason: a fixed callback-count loop silently stops real desktop sessions and
starves sources added during dispatch. One shared loop keeps cross-DSO handles
safe and gives GTK/GLib-shaped callers deterministic lifecycle semantics while
remaining small enough for freestanding targets.

# 2026-09-16 — Keep x86_32 runtime storage below the proven address-space cap

Decision: use a 52 KiB fixed runtime arena on 32-bit x86 while keeping the
kernel's 128-page per-process mmap budget unchanged; 64-bit ports retain a
64 KiB arena.

Reason: the persistent-loop code increased DSO resident data enough to round
the i386 map beyond the safe three-DSO layout after the GObject additions. A
52 KiB arena keeps the map within the proven limit, avoiding a kernel layout
expansion that previously caused early i386 faults.

# 2026-09-15 — Preserve the i386 mmap budget with bounded runtime metadata

Decision: keep `USER_MAX_MMAP_PAGES` at 128 on x86_32 and reduce the shared
runtime's fixed property table to 24 entries so the GLib, GTK, and Qt
compatibility DSOs fit concurrently. Do not enlarge the kernel process
bookkeeping table just to accommodate compatibility-library metadata.

Reason: raising the i386 mmap table changed the resident process layout and
caused an early page fault during the kernel self-tests. Right-sizing a
bounded user-space table removes the address-space exhaustion while keeping
the proven kernel memory layout and all six architecture ports stable.

# 2026-09-15 — Keep Qt source-port coverage bounded and cross-DSO safe

Decision: expose a fixed C-facing Qt-shaped bridge for application loops,
QObject properties/signals, and QWidget state/geometry, backed by the same
pointer-keyed runtime tables used by the GTK bridge. Validate handles at every
boundary and keep widget storage fixed-capacity.

Reason: KDE/LXQt source ports need these common calls, but a complete Qt ABI
would require unrestricted meta-object marshalling and allocator ownership
across independent freestanding DSOs. The bounded bridge exercises real
lifecycle behavior deterministically on every architecture.

# 2026-09-15 — Keep GTK window state explicit and bounded

Decision: store resizable, maximized, minimized, and closed bits directly in
the fixed-layout runtime window and expose explicit GTK-shaped transitions and
queries. `present` restores a visible, non-minimized window; `close` hides it
without reclaiming the object until the normal destroy path.

Reason: desktop ports rely on these calls for taskbar and window-manager
behavior. A deterministic state machine is safer across separately loaded
compatibility DSOs than no-op stubs, while geometry negotiation remains in
the compositor configure/ack protocol.

# 2026-09-15 — Require an explicit native resize acknowledgement

Decision: every toolkit resize carries a monotonic configure serial. Lumen
publishes the serial with the exact replacement surface geometry; the client
must observe that geometry, submit `AKAOS_DISPLAY_MSG_CONFIGURE_ACK`, and wait
for the compositor to record the acknowledgement before continuing its
damage/destroy sequence.

Reason: replacing a shared buffer without an acknowledgement lets clients
race compositor mapping and leaves stale or scaled pixels. A fixed serial and
bounded polling preserve deterministic ownership across all architectures
without exposing an unbounded upstream X11/Wayland event protocol.

# 2026-09-15 — Supervise composed profile companion lifetimes

Decision: the native desktop launcher records each companion PID, forwards
SIGTERM/SIGINT on session shutdown, waits for every child, and exposes a
deterministic `--once` completion marker. Insert a brief yield between spawns
so concurrent child image loading cannot race the compositor/runtime startup.

Reason: a desktop session is a process graph, not a single surface. Explicit
ownership and cleanup make exit/restart behavior testable across all ports and
avoid leaking panel/window-manager processes, while the bounded handoff keeps
startup deterministic without pretending to implement an upstream service
manager.

# 2026-09-15 — Keep application actions bounded and signal-driven

Decision: implement GSimpleAction as a fixed-capacity, reference-counted
named object. Enabled actions emit through the existing fixed callback
registry, and each application pointer owns a bounded action-map table with
explicit add/lookup/remove operations.

Reason: GNOME-style sessions need named actions, but exposing GLib's complete
GAction/GMenu variadic marshalling ABI would cross the freestanding DSO
boundary unsafely. The bounded map exercises the real activation path while
keeping ownership deterministic on every architecture.

# 2026-09-15 — Keep GApplication lifecycle explicit across DSOs

Decision: represent applications as tagged fixed-layout runtime objects
created by libgtk; GLib/GIO lifecycle calls validate that tag directly on the
passed object instead of searching a private per-DSO object table.

Reason: akaOS loads GTK and GLib compatibility objects as separate shared
objects. A local-table lookup rejects valid application pointers at the ABI
boundary, while the tagged layout preserves deterministic ownership without
pretending to implement the complete upstream GApplication type system.

# 2026-09-15 — Require exact native buffers for compositor resize

Decision: a resize request must name a new shared-memory surface whose header
width, height, stride, pixel offset, and byte size exactly satisfy the slot;
Lumen invalidates both rectangles and remaps the new buffer before presenting
it. The toolkit exposes this as `akaos_toolkit_surface_resize()`.

Reason: scaling an old client buffer hides geometry bugs and can read beyond a
freestanding mapping. Requiring an explicitly sized replacement matches the
buffer ownership model used by GTK/Qt-style clients and keeps every port
deterministic.

# 2026-09-15 — Compose desktop profiles behind one supervised session

Decision: expose one native `desktop` session entry point that starts a
profile's bounded companion roles through `/bin/<profile>` compatibility
executables, propagates `--once` for lifecycle tests, and waits for the main
profile process to exit.

Reason: KDE, GNOME, LXDE, and LXQt all depend on cooperating shell,
window-manager, panel, and session processes. A composed graph exercises the
real D-Bus/display/process boundaries without claiming that a single surface
is an upstream desktop implementation.

# 2026-09-14 — Guard resume helpers as well as initial user entry
# 2026-09-14 — Guard resume helpers as well as initial user entry

Decision: mask timer interrupts at the first instruction of each ARM32,
RISC-V32, and RISC-V64 `xscore_user_resume` helper, matching the existing
AArch64 guard; restore the saved user interrupt state only through the normal
exception return instruction.

Reason: a timer-driven switch during fork-frame copying can execute the helper
with a different TCB and fault at its own stack accesses. Keeping the resume
critical section atomic removes that race without disabling interrupts in the
resumed user context.

# 2026-09-14 — Keep initial user-mode handoffs interrupt-atomic

Decision: mask timer interrupts while AArch64, ARM32, RISC-V64, and RISC-V32
assemble their EL0 return state; the status restored by `eret`/`sret`/ARM
exception return re-enables interrupts in user mode. Size the user-image
self-test to the complete bounded loader reservation table.

Reason: a timer-driven context switch during the handoff can leave stale
transition state and report an apparent user fault inside the entry helper;
the fixed probe also prevents image growth from hiding a writable segment.

# 2026-09-14 — Keep GIO task completion deterministic

Decision: represent GCancellable and GTask with fixed reference-counted
objects; completion callbacks run synchronously from g_task_return_* and can
be deferred explicitly through the existing main-loop source API.

Reason: this provides a usable GIO async contract in a freestanding runtime
without hiding scheduler or allocator ownership across DSOs. Full worker
thread dispatch and unrestricted GIO error objects remain separate work.

# 2026-09-14 — Bound property synchronization explicitly

Decision: implement GObject property bindings as a fixed 16-entry table with
one-way/bidirectional flags, optional sync-on-create, reference counting, and
an update guard to prevent recursive loops.

Reason: GTK-style widgets commonly derive state from another object property;
this adds real synchronization while keeping the freestanding cross-DSO ABI
bounded and deterministic until a complete upstream GObject type system is
available.

# 2026-09-14 — Keep GValue property storage fixed-layout and copy-in

Decision: expose only bounded scalar GValue kinds (string, boolean, signed
int, unsigned int) and copy them into a fixed property slot keyed by object
pointer and property name.

Reason: GTK/GObject source ports need ordinary property semantics, but the
compatibility DSOs do not share a host allocator or a complete variadic
GValue ABI. Fixed storage keeps ownership deterministic and safe on every
akaOS architecture while leaving the full upstream ABI explicitly open.

# 2026-09-14 — Keep GFile path objects pure and bounded

Decision: represent GFile as a reference-counted normalized path or file URI
value and expose only deterministic path operations; returned strings use the
runtime allocator and parent objects carry independent references.

Reason: desktop source ports need path identity and URI conversion even before
full asynchronous filesystem I/O exists. A pure bounded object avoids coupling
the compatibility DSO to an incomplete GIO backend while keeping ownership
explicit.

# 2026-09-14 — Copy scalar GSettings values across DSO boundaries

Decision: store supported GSettings scalar values in fixed per-object slots and
recreate GVariants on get, rather than retaining caller-owned variants across
the GLib/GIO compatibility DSOs.

Reason: each freestanding compatibility DSO has an independent bounded arena;
copy-in scalar storage gives deterministic lifetime and prevents a GIO object
from attempting to free a GLib-owned allocation. Full schemas, persistence,
and arbitrary variant values remain future work.

# 2026-09-14 — Clean signal handlers by ID, function, and object destruction

Decision: maintain handler IDs in the process-wide signal registry, support
explicit ID/function removal, and clear all handlers targeting an object when
that object is destroyed.

Reason: separate GLib/GTK DSOs must share callback ownership, and stale
handlers after widget destruction are unsafe. A bounded registry preserves
deterministic behavior while matching the common lifecycle semantics.

# 2026-09-14 — Represent a{sv} values with explicit GVariant v wrappers

Decision: implement v as a one-child, reference-counted GVariant container and
require a{sv}-style builder callers to wrap values explicitly; lookup can
validate the requested outer type before unwrapping.

Reason: D-Bus and GIO settings payloads commonly use variant-valued
dictionaries. An explicit wrapper preserves ownership and type boundaries
without pretending that the freestanding runtime supports every recursive
GLib variant signature.

# 2026-09-14 — Keep GObject metadata process-wide across compatibility DSOs

Decision: store g_object_set_data/get_data/steal_data associations in a
bounded process-wide pointer-keyed registry instead of each DSO's private
widget table; clear entries when the runtime object is destroyed.

Reason: GTK objects and GLib metadata functions are loaded from different
soname compatibility objects, so per-DSO state cannot resolve a shared object
pointer. A bounded registry preserves cross-DSO behavior without exposing
private widget layouts or unbounded allocation.

# 2026-09-14 — Keep GVariant builders bounded and ownership-explicit

Decision: represent dictionary entries as two retained child variants, expose
string-key lookup over bounded array containers, and provide a builder capped
at eight children with explicit add-value/add-format/end/clear ownership.

Reason: GIO/D-Bus payloads commonly use a{sv} dictionaries, but unbounded
recursive builders, floating references, and arbitrary format language would
violate the freestanding runtime's deterministic arena limits. Explicit
retention and cleanup are auditable on every akaOS architecture.

# 2026-09-14 — Bound the GVariant format bridge to explicit signatures

Decision: implement `g_variant_new()` only for the common scalar signatures
`s`, `b`, `u`, `i`, `t`, and `x`, optional `@` child
references, and small tuple forms with at most eight children. Reject
unsupported or trailing format data and keep the existing explicit tuple/array
APIs.

Reason: common GIO/D-Bus call sites benefit from a familiar constructor, but an
unrestricted GLib format language, recursive builders, dictionaries, and
floating-reference semantics would exceed the deterministic freestanding
runtime. The bounded parser is auditable on every akaOS architecture and does
not imply full upstream GLib compatibility.

# 2026-09-14 — Retain children explicitly in composite GVariants

Decision: construct bounded tuple/array variants from an explicit child
vector, retain each child reference, expose count and child-value access, and
release children recursively when the composite reaches zero references.
Represent type queries as `(*)` and `a*` rather than claiming recursive format
string support.

Reason: GIO/D-Bus payloads need grouped values and stable child lifetimes, but
full GLib varargs builders and arbitrary recursive types would exceed the
current freestanding ABI.  Explicit vectors keep ownership auditable and
safe across all six ports.

# 2026-09-14 — Keep GVariant scalar support typed and explicit

Decision: expose `GVariant` scalar constructors/getters for the common string,
boolean, signed/unsigned 32-bit, and signed/unsigned 64-bit forms, with
reference counting and type queries.  Copy strings into the runtime arena and
return neutral values for type mismatches.

Reason: GIO/D-Bus code needs stable typed values, but full GLib format-string
varargs, tuple builders, and recursive ownership would be a much larger ABI
surface.  An explicit scalar subset is safe across all ports and clearly
bounded while leaving the remaining tuple/builder work visible.

# 2026-09-14 — Make GBytes immutable with explicit references

Decision: implement `GBytes` as an arena-backed copy of caller data with a
reference count, explicit `ref`/`unref`, size/data accessors, and bytewise
equality.  Do not expose mutable storage or transfer ownership of the source
buffer.

Reason: GIO/D-Bus payloads rely on stable immutable byte sequences, while
copy-in and explicit references prevent a source-port buffer from becoming a
use-after-free.  The bounded arena keeps the behavior deterministic on i386
and the other native ports.

# 2026-09-14 — Bound GLib registries and lists to the runtime arena

Decision: represent `GHashTable` with fixed-capacity open addressing and
tombstones, and represent `GList`/`GSList` with arena-backed nodes guarded by
a finite traversal bound.  Keep key/value and list-data ownership with the
caller; only container storage is reclaimed by the runtime.

Reason: real GLib/GIO source-port state needs registries and ordered object
lists, but unbounded growth or implicit destructors would exceed i386's
deterministic resident-memory budget and make malformed guest state unsafe.
The bounded ABI adds useful semantics while preserving cross-architecture
predictability.

# 2026-09-14 — Use bounded linked lists for GLib source-port state

Decision: expose opaque `GList`/`GSList` prepend, append, remove, length,
indexed-data, and free operations, with a 256-node traversal guard and node
storage in the existing runtime arena.  Do not take ownership of user data.

Reason: GLib/GIO widget and service code commonly passes linked lists of
opaque objects; bounded node traversal prevents malformed source-port state
from wedging a freestanding service while preserving normal list ordering and
explicit data ownership.

# 2026-09-14 — Keep GLib hash tables fixed-capacity and callback-selectable

Decision: expose the common `GHashTable` operations and string hash/equality
helpers while keeping the table layout private.  Use bounded open addressing
with tombstones, replace values on duplicate keys, and default to pointer
identity when callers do not provide callbacks.

Reason: GLib/GIO service registries need key/value lookup semantics, but
unbounded rehashing and destructor callbacks would exceed the deterministic
64 KiB per-DSO arena and complicate cross-architecture safety.  The bounded
ABI gives source ports a real registry primitive while preserving explicit
ownership and predictable failure.

# 2026-09-13 — Keep GLib collections opaque and arena-bounded

Decision: expose the common `GArray`, `GPtrArray`, and `GString` function
shapes while keeping concrete layouts private to each runtime DSO.  Back their
storage with the existing deterministic 64 KiB arena, return NULL on overflow
or exhaustion, and use explicit `free_segment` ownership just like the
source-port contract.

Reason: GTK/GIO/Qt source ports need structured collections and mutable strings
before they can build realistic object trees, but an unrestricted allocator
would exhaust i386's resident mmap budget and undermine cross-architecture
determinism.  This improves the ABI without pretending to be the complete
upstream GLib implementation.

# 2026-09-13 — Size the x86_32 dual-firmware EFI image for both loaders

Decision: allocate a 64 MiB FAT EFI image when building the x86_32 ISO with
`--firmware both`, allowing both self-contained IA32 and x86_64 loaders to be
copied before xorriso assembly.

Reason: the old 16 MiB image overflowed (`Disk full`) even though the host had
free storage; loader sizes and FAT metadata legitimately exceeded that fixed
limit.  The larger image keeps the requested UEFI+BIOS ISO contract intact.

# 2026-09-13 — Deliver pthread cancellation at safe points

Decision: implement cancellation requests in the shared pthread record table,
with enable/disable and deferred/asynchronous mode state. Check requests at
explicit `pthread_testcancel()` calls and scheduler-yield checkpoints, run
TLS destructors, and use the existing native thread-exit ABI to return
`PTHREAD_CANCELED` to `pthread_join`.

Reason: desktop worker pools need a way to stop cleanly, but XSCore does not
yet have an interruptible user-thread cancellation syscall. Treating
asynchronous mode as a safe-point request preserves memory/lifetime safety
across all six ports and avoids arbitrary instruction interruption.

# 2026-09-13 — Layer reusable pthread barriers over cooperative scheduling

Decision: represent a barrier with an atomic arrival count, a short
high-bit update lock, and a generation counter. Accept private/shared
attributes for source compatibility, return the POSIX serial-thread value for
the final arrival, and yield while waiting.

Reason: GLib/Qt worker pools commonly rendezvous with barriers, while adding a
new kernel wait-object ABI would expand the current XSCore risk surface. The
pointer-free state works in the native shared address space on all six ports;
full cross-process futex semantics and cancellation remain separate work.

# 2026-09-13 — Bound desktop runtime memory per compatibility DSO

Decision: implement GLib-shaped allocation/string/object helpers with a
deterministic 64 KiB arena and allocation table inside each native runtime
object. Keep the same limit on every architecture and exercise it through
`hello --desktop-runtime-test`.

Reason: the i386 user address-space model permits only 128 mmap pages for the
resident desktop set. A larger per-DSO arena caused three concurrently loaded
GLib/GTK/Qt objects to exhaust that budget; 64 KiB preserves the ABI while
keeping the six-architecture images runnable. This is intentionally bounded
compatibility infrastructure, not an unrestricted host allocator or a claim
of complete upstream desktop support.

# 2026-09-13 — Layer semaphores over cooperative scheduling

Decision: provide a compact pointer-free `sem_t` with atomic count updates;
blocking waiters yield through XSCore's scheduler and timed wait uses the
existing freestanding realtime clock/nanosleep path. Accept `pshared` values
for source compatibility while retaining the current address-space scope.

Reason: GLib/GIO and Qt-adjacent code frequently uses POSIX semaphores, but
introducing a new kernel wait-object ABI would expand the risk surface. The
bounded bridge is portable across all six native ports and makes the missing
primitive explicit without claiming unrestricted Linux semaphore semantics.

# 2026-09-13 — Keep pthread synchronization portable and bounded

Decision: extend the freestanding pthread ABI with explicit attributes,
ownership-aware mutex types, timed condition waits, and atomic reader/writer
locks while retaining fixed-size, pointer-free synchronization objects.  Use
validated stack-size limits and the existing XSCore scheduler-yield path for
waiters; repeat TLS destructors for a bounded POSIX-compatible number of
passes.

Reason: GLib/GIO and Qt-style runtimes depend on these core pthread semantics,
but akaOS cannot import a host libc or pthread implementation.  A compact
cross-architecture ABI keeps the six native ports deterministic and avoids
pretending to provide unsupported cancellation or unbounded process-shared
attributes.

At this stage the pshared attr setters were retained for source compatibility
but did not imply cross-process synchronization. This was superseded on
2026-09-23 by XSCore futex WAIT/WAKE support and a cross-address-space shared
mapping test. Direct process-shared semantics for libc pthread objects remain
unvalidated and must not be inferred from the raw futex test.

# 2026-09-13 — Keep timer workers inside the process lifetime

Decision: implement timerfd readiness with a bounded pipe and a joinable
native worker; `close()` joins the worker and libc `_exit()` closes any open
timerfds before issuing the kernel exit syscall.  Use a shift/subtract 64-bit
division helper for 32-bit freestanding ports.

Reason: a detached timer worker can execute after XSCore has marked the main
thread's process exited, causing an ARM32 prefetch abort.  Joining and exit
cleanup preserve the current process/thread lifetime contract while keeping
the event-loop ABI useful to GLib/GIO/Qt-shaped ports.

# 2026-09-13 — Size the optical UEFI payload for the runtime

Decision: use a 32 MiB FAT image for the El-Torito UEFI entry in the dual
firmware ISO.

Reason: the embedded kernel/initramfs grew beyond the old 16 MiB image; the
larger image remains below El-Torito's sector limit and prevents valid builds
from failing with `Disk full`.

# 2026-09-12 — Protect the final AArch64 user transition

Decision: mask the timer IRQ across the tiny process-thread handoff to
`xscore_user_resume`, and keep the context/transition pointers in
callee-saved registers inside the resume assembly.  Store the architectural
frame and image metadata in resident process storage.

Reason: a scheduler switch between argument setup and the AArch64 `eret`
sequence could restore caller-saved registers from another thread and turn a
valid fork context into a null/low pointer.  The protected handoff is now
stable in repeated native runs and the full six-architecture matrix passes.

# 2026-09-12 — Reconcile stale D-Bus activation PIDs on unregister

Decision: `UnregisterService` probes a recorded activation PID with
`waitpid(WNOHANG)` and clears it when the child has already exited/reaped;
return `EBUSY` only while the service is actually running.

Reason: activation supervision and process reaping can observe exit in
different orders.  Treating a stale positive PID as busy made the public
registration API flaky even though the service was gone.

# 2026-09-12 — Layer eventfd over the audited pipe wait path

Decision: expose a libc eventfd API using virtual descriptors and native pipe
backing, then translate those descriptors in `poll()` and `epoll()` while
handling eventfd counter/semaphore reads and writes in userspace.

Reason: GLib/GIO, D-Bus workers, and Qt event dispatchers commonly require
eventfd readiness, and the existing XSCore descriptor table is intentionally
bounded.  Reusing the tested pipe/poll path gives all six ports a functional
notification primitive without inventing an untested kernel wait-object ABI.
The implementation is explicitly bounded; complete Linux flag inheritance,
close-on-exec, and unrestricted event-object counts remain open.

# 2026-09-12 — Add bounded stateful X11 and Wayland xdg-shell protocol edges

Decision: retain explicit, bounded display-protocol state in the local bridge.
X11 clients can create/map/configure a window and query its geometry; Wayland
clients can bind compositor and xdg-wm-base globals, complete ping/pong and
surface/toplevel configure acknowledgement, set a title, and commit a surface.
Keep rendering, policy, and unsupported protocol requests out of this ABI.

Reason: toolkit startup needs a real wire-level window/configure handshake on
each architecture, while the current kernel/compositor has no general X11 or
Wayland object server.  A small state machine is testable and honest about the
remaining work needed for unmodified KDE, GNOME, LXDE, and LXQt.

# 2026-09-12 — Add a bounded GTK-shaped widget and activation boundary

Decision: represent a small widget tree in the packaged runtime objects with
explicit parent/child links, bounded text/name fields, requested dimensions,
visibility, and a fixed two-argument activation callback.  Export familiar
GTK-shaped constructors/tree operations, but do not emulate GLib's variadic
signal marshalling or GTK's private object layout.

Reason: desktop source ports need to create a window hierarchy and connect
basic actions before they can reach a display bridge.  The fixed ABI is safe
for freestanding six-architecture guests and testable through `dlsym`, while
keeping the distinction between akaOS compatibility scaffolding and the full
upstream GTK/GLib/GIO/Qt6 implementation explicit.

# 2026-09-12 — Provide a deterministic C/C.UTF-8 locale boundary

Decision: expose the common ctype, locale, and UTF-8 conversion entry points
from freestanding libc, with C/C.UTF-8 as the supported locale family and
strict Unicode scalar-value validation.  Keep conversion buffers bounded by
the caller and avoid hidden host locale state.

Reason: GTK/Qt/session code needs predictable classification, locale
selection, and multibyte conversion during startup.  A deterministic native
boundary is useful on every architecture while locale catalogs, collation,
iconv, full Unicode properties, and complete upstream internationalization are
separate work.

# 2026-09-12 — Add a native SPAWNVE path for explicit environments

Decision: add syscall 68 with a fixed-width `akaos_spawnve_args` structure and
bounded kernel copies of path, argv, and envp, then load the child image with
the supplied environment.  Route libc `posix_spawn()`/`posix_spawnp()` through
this ABI when `envp` is supplied; retain syscall 13 for inherited-environment
spawns.

Reason: fork/exec is an awkward address-space transition in the current
freestanding kernel and produced architecture-dependent child failures.  A
single native creation path reuses the already audited ELF stack setup on all
six ports and keeps environment semantics testable.  Carrying only bounded
close/dup2 file actions gives desktop launchers useful descriptor setup while
leaving open-file actions, attributes, and the full dynamic-loader contract
explicitly out of scope.

# 2026-09-12 — Layer POSIX readiness and IPv4 address resolution over native ABIs

Decision: implement `select()` as a descriptor-set translation to the audited
XSCore `poll()` syscall, and expose a bounded IPv4 `getaddrinfo()` family in
libc using the existing literal/localhost/DNS resolver.  Keep the native
descriptor wait set capped at 16 and return explicit errors for unsupported
families, socket types, services, or oversized sets.

Reason: GLib/GIO and desktop session code commonly use these POSIX entry
points, while a new kernel wait-object ABI would be a larger independent
change.  The translation reuses existing readiness and signal-interruption
semantics on every architecture without pretending to provide IPv6 or the
complete Linux resolver ABI.

# 2026-09-12 — Expose bounded per-user D-Bus registration

Decision: let session-bus clients register activation definitions through the
public libc API instead of requiring edits to the read-only image registry.
Persist the definition at `/run/user/0/dbus-services.conf`, accept executable,
argument, restart, and timeout metadata in a bounded `sssuu` frame, retain the
legacy `sss` frame for existing clients, and reject duplicates or removal of a
running provider.  The broker supervisor reaps activated children and retries
restart-enabled providers only a bounded number of times.

Reason: desktop session components need a registration lifecycle they can use
at runtime, while akaOS still has fixed-size process/connection tables and no
general D-Bus policy engine.  The explicit limits preserve deterministic
behavior on every native port and make clear that this is compatibility
scaffolding, not the complete upstream broker contract.

# 2026-09-12 — Reset AArch64 exception-return state on TCB reuse

Reused AArch64 thread-control blocks now initialize the saved `ELR_EL1`,
`SPSR_EL1`, and `SP_EL0` scheduler fields before first dispatch. These fields
are CPU exception-return state rather than ordinary callee-saved registers;
leaving them stale could let a prior process's privilege-return context leak
into a new task. The change is architecture-local and preserves the existing
bounded desktop runtime design.

## Bounded desktop runtime ABI and soname aliases (2026-09-11)

Decision: provide a small dependency-free event-loop, source scheduling,
object-lifetime, and window-lifecycle ABI in the packaged shared runtime
objects, and expose conventional GLib/GTK/Qt soname aliases to the same
implementation. Keep the opt-in akaos_ symbols explicit and test resolution
through `dlopen`/`dlsym` on every architecture.

Reason: the previous objects only reported a family marker, which was not a
useful boundary for a real source port. The new ABI exercises callback
dispatch, bounded loop behavior, reference counting, and window lifecycle
without importing host libraries or pretending that the complete upstream
toolkits are present. The loader now reads objects in 4096-byte chunks to
match XSCore's syscall limit. Full GLib/GTK/Qt symbol sets, C++ ABI, and
unmodified upstream desktop execution remain separate work.

## Capability-driven Aurora shell and protocol-v3 events (2026-09-09)

The desktop may display only state backed by an XSCore or registered-userspace
interface. Aurora therefore reads network existence/state from `/proc/net/dev`
and `/proc/net/route`, audio existence from `/proc/asound/cards`, and wall-clock
time from `/proc/realtime`. XSCore has no battery-class interface, so absence is
represented by no battery UI—not an unknown or hardcoded percentage.

Lumen remains the sole input owner. Protocol v3 adds a bounded compositor-to-
shell event queue and a bounded notification queue instead of allowing Aurora
to race `/dev/input/event0`. Applications register a stable icon resource name
on attach, and launchable applications are data-driven by `/etc/aurora-apps`.
Transient launcher, context-menu, and notification surfaces are allocated only
while visible so i386 retains a bounded process mapping table. Window maximize
and resize are deliberately deferred until a compositor/client configure-and-
acknowledge protocol exists; scaling client pixels would be fake functionality.

## Bounded x86_64 VFS/device/socket stress (2026-09-07)

Decision: exercise shared kernel VFS, descriptor/file pools, character devices,
and socket lifecycle from concurrent AP-owned kernel workers, with one private
descriptor table per AP and unique temporary paths. Enable file-pool lifetime
locking before the workload and protect socket allocation/bind/poll/close with
an IRQ-safe pool lock. Keep external network traffic and persistence commits
outside this early bounded test.

Reason: this validates shared object lifetime and tree-lock ordering under real
AP concurrency without introducing a blocking network or disk dependency into
boot self-tests. Broader socket I/O, persistent-disk transactions, and future
VFS/device migration still need dedicated stress coverage. The later
Persistent/network SMP extension below supersedes the last sentence for its
bounded commit and transmit paths.

## Persistent/network SMP extension (2026-09-07)

Decision: run the AP workload only after PCI, VirtIO block/network, and
writable akaFS initialization; retain one unique per-AP file for host-side
image verification; copy each persistence snapshot under the VFS tree lock,
then perform disk I/O under the existing persistence lock; serialize the
single VirtIO-Net TX descriptor/buffer; send bounded UDP bursts to the QEMU
gateway without requiring a reply.

Reason: this turns the earlier in-memory lifecycle test into deterministic
coverage of real persistent commits and concurrent external transmit while
avoiding a blocking DNS/remote-service dependency. Remote responses,
unrestricted transactions, and full socket I/O still need dedicated tests.

## AP placement is enabled only after boot self-tests (2026-09-07)

Decision: after the automated boot/self-test phase, normal x86_64 shell
descendants may be assigned to AP-owned run queues. Built-in test-mode
workloads stay on the BSP so their deterministic startup ordering remains
stable. Remote wakeups use a fixed Local APIC reschedule vector, and each x86
thread carries FXSAVE-compatible state. ARM/RISC-V keep scheduler execution
single-core until their secondary entry paths gain audited per-CPU state.

Reason: this exposes useful multicore behavior while keeping the known-safe
test path deterministic. At the time of this decision, process scalar
snapshots, descriptor/vnode lifetime, page-cache serialization, and persistence
serialization were in place while VMM/TLB ownership still needed completion.
The later x86_64 VMM/TLB hardening decision below records that bounded AP
private-root workload as verified; arbitrary future VFS/device/socket migration
still requires its own stress coverage.

## Bounded AP Ring 3 scheduling before general shell migration (2026-09-07)

Decision: enable x86_64 AP-owned user threads first in a deterministic,
test-mode workload. Process slots, descriptor tables, executable vnode/image
references, waiters, and remote TCB reaping must be validated by QEMU before
the interactive shell is allowed to migrate from the BSP. Keep ARM/RISC-V
process scheduling single-core until their newly-added secondary entry paths
gain audited per-CPU scheduler state.

Reason: a bounded workload proves the cross-CPU scheduler and lifetime edges
without silently claiming that every mutable process address-space, credential,
or FPU/SIMD field is already safe for arbitrary multicore userspace.

## Reference-counted vnode lifetime (2026-09-07)

Decision: opened vnode descriptors and VFS-backed executable images acquire a
node reference. `unlink`/rename replacement removes the directory name but
defers block reclamation until the last descriptor/image reference is released.
Path lookup for process images uses the reference-returning API; legacy raw
pointer lookup remains only for static internal callers.

Reason: lazy ELF page faults and concurrent close/unlink must not read recycled
file pages. The reference boundary is architecture-independent and preserves
the existing VFS ABI while making the x86_64 AP userspace test safe.

Do not delete historical decisions. If a decision changes, append a replacement with its date and reason.

## Bounded terminal ioctl ABI before a general device ioctl framework (2026-09-02)

Decision: Introduce syscall 49 with fixed, architecture-neutral termios,
window-size, and terminal-name structures. Apply the same settings to both ends
of each PTY and keep console settings in the shared TTY layer.

Reason: Canonical/raw input, echo, signal characters, terminal identity, and
window sizing are required for real terminal programs now. A small versionable
ABI provides those semantics consistently across six architectures without
prematurely pretending that XSCore has Linux ioctl numbers or a complete
driver-generic ioctl registration system.

## Kernel-owned terminal sessions, PTYs, and proc snapshots (2026-09-02)

Decision: Track SID, PGID, controlling-TTY presence, and the terminal foreground
PGID in the shared process/TTY layer. Expose bounded PTY pairs through
`/dev/ptmx` and `/dev/pts/N`, and generate `/proc/self/status` plus
`/proc/processes` as stable per-open snapshots.

Reason: Terminal signal delivery and shell foreground control must use process
groups rather than a single-process shortcut. PTYs and inspectable process
state provide the next Unix-like userspace foundation without pretending that
akaOS already has complete termios, ioctl, procfs, or POSIX conformance.

## Independent operating system

Decision: akaOS uses the original XSCore kernel and must not become a Linux distribution.

Reason: The project's defining goal is independent operating-system implementation. Unix/POSIX behavior is useful as an interface model, but Linux is not a kernel or userspace dependency of akaOS.

## Kernel name is XSCore (2026-09-01)

Decision: Rename every public, source, symbol, path, and artifact reference
to XSCore. `xscore/` is the canonical kernel source directory and
`XSCORE*.ELF` is the artifact family.

Reason: “Xe Core” is readily confused with Intel Iris Xe graphics branding.
XSCore preserves the project identity while giving the independent akaOS
kernel a distinct name.

## Shell expansion and command argument contract (2026-09-01)

Decision: Perform unquoted `*` and `?` expansion in `akash`, keep quoted and
unmatched patterns literal, and reject unsupported options/extra operands in
utilities. Preserve conventional stdin `-` operands where implemented and
literal `-` output for `echo`.

Reason: Wildcard expansion belongs to the shell, not `rm`; passing a literal
asterisk made standard commands appear broken. Consistent argument validation
prevents malformed commands from returning plausible but incorrect output.

## Target and boot environment

Decision: The first target is x86-64 UEFI, developed and tested primarily with QEMU q35 and OVMF.

Reason: This provides a modern, deterministic boot environment with graphics and serial diagnostics while keeping firmware interaction isolated in a small loader.

## Loader/kernel boundary

Decision: UEFI protocols stay in the loader; XSCore receives a versioned plain-data `akaos_boot_info` structure.

Reason: Firmware objects must not become kernel APIs. A narrow ABI allows future loaders to target the same XSCore entry contract.

## ELF kernel and userspace images

Decision: XSCore and the first user program are ELF64 images with validated program headers.

Reason: ELF gives explicit load segments and permissions and provides a path toward filesystem-backed user program loading without inventing an executable format prematurely.

## Native memory ownership

Decision: XSCore replaces firmware page tables with its own PMM/VMM and uses high-half regions for heap and stacks.

Reason: The kernel must control allocation, permissions, guard pages, and future process isolation. Transitional identity mappings are accepted only as an explicitly documented milestone.

## Kernel hardening sequence

Decision: Enable NXE/WP, split kernel-image large pages, enforce section permissions, and use guarded NX stacks before expanding drivers and userspace.

Reason: Correct privilege and memory foundations reduce the risk of building higher layers on unsafe mappings.

## Interrupt controller and timer

Decision: Use the Local APIC and a PIT-calibrated APIC timer for the current BSP milestone; mask the 8259 PIC.

Reason: This is a direct path to modern x86 interrupt and scheduling foundations. IOAPIC/MSI and ACPI discovery remain later work.

## Cooperative scheduling before preemption

Decision: Kernel threads may switch through explicit yield, but timer-driven context switching stays disabled until switching can be deferred safely to the interrupt-return path.

Reason: Switching stacks inside the current ISR would leave the CPU's `iretq` frame on the old stack and is not safe.

## Temporary syscall ABI

Decision: Use a DPL3 `int 0x80` gate for the first two syscalls.

Reason: It provides a small, inspectable privilege transition for the first Ring 3 milestone. It is not declared stable and may later be replaced or complemented by `syscall/sysret` after the process model is sound.

## Boot UI and logging separation

Decision: Serial logging always remains available; framebuffer early logs are optional; the graphical splash is cleared and isolated from background log calls.

Reason: Boot visuals must represent real progress without obscuring diagnostics or allowing text to bleed into the splash.

## Native graphics direction

Decision: akaOS will develop its own display/compositor architecture rather than making X11 or Wayland fundamental runtime dependencies.

Reason: The project requires an independent graphics stack aligned with akaOS kernel and IPC interfaces. Compatibility layers can be considered later.

## Framebuffer ownership belongs to a userspace compositor (2026-09-01)

Decision: Expose the active display as a root-owned `/dev/fb0` descriptor and
give writable descriptors explicit graphics ownership. Detach the framebuffer
TTY sink while graphics is owned, keep serial logging active, and restore the
TTY sink when the final owner closes. Build the first compositor as the
ordinary Ring 3 `/bin/lumen` process using named shared-memory surfaces and
pollable input.

Reason: Rendering policy, surface composition, and pointer handling belong in
userspace. Explicit ownership prevents kernel/TTY output from corrupting a
composed frame, while descriptor cleanup gives process exit a deterministic
fallback to the working text console.

## Fullscreen TTY font

Decision: The framebuffer shell uses a standard 8x16 IBM VGA/CP437 bitmap font at 1:1 scale, while the graphical splash may use a separate anti-aliased font atlas.

Reason: A strict 8-pixel character advance gives authentic, crisp terminal spacing and deterministic row/column behavior. Painting both foreground and black background per cell prevents boot-artifact bleed.

## Initial console identity

Historical decision (superseded 2026-08-29): The kernel shell started with the
built-in identity `root` and prompt `root@akaOS:~# `. The later process-
credentials decision replaces this simulated identity with authenticated users.

Reason: The OS has no account database, authentication service, or multi-user userspace yet. The identity is an explicit initial administrator convention, not a claim that a complete credential system exists.

## Explicit firmware and disk build targets (2026-08-29)

Decision: Keep one `build.sh` interface with explicit `--arch`, firmware
(`--uefi`/`--bios` or `--firmware`), and disk type (`fat32` or `iso`). Infer a
compatible disk type only when the caller omits it, reject mismatches, and keep
UEFI `build/akaos.img` separate from BIOS `build/akaos-bios.iso`.

Reason: Build intent should be visible and reproducible, while preventing a
BIOS request from silently producing or overwriting a UEFI disk image. The
non-x86 smoke matrix has no native firmware loaders yet, so it rejects image
options rather than making an unsupported boot claim.

## Unified firmware value replaces firmware aliases (2026-08-29)

Decision: Replace the separate `--uefi` and `--bios` flags with the single
`--firmware uefi|bios|both` option. Pair it with `--disk-type fat32|iso|both`
in `build.sh`. Firmware `both` produces both independent images from one common
kernel build. `run.sh` accepts the same architecture/firmware model, but one
QEMU instance boots one selected disk type.

Reason: One value-bearing firmware option avoids conflicting aliases and makes
combined output an explicit target. Keeping the runner single-image preserves
predictable interactive QEMU behavior while still allowing it to ensure both
artifacts are built.

## Disk type reduced to FAT or ISO (2026-08-29)

Decision: Accept only `--disk-type fat|iso`. Remove the `fat32` spelling and
the `both` disk-type value. UEFI requires `fat`, BIOS requires `iso`, and
firmware `both` accepts either value while still creating both artifacts. In
the runner, that value selects which artifact is booted; omission defaults to
FAT.

Reason: Firmware already expresses whether one or both boot paths are wanted.
Keeping disk type to the two concrete image formats removes a redundant
`both` value and makes command lines shorter and clearer.

## FAT32 is the public disk-type spelling (2026-08-29)

Decision: Correct the FAT selector from `fat` to `fat32`. The only accepted
disk-type values are now `fat32` and `iso`; `fat` and `both` are rejected.

Reason: The generated UEFI disk uses the FAT32 filesystem, so the explicit
`fat32` spelling is more accurate and avoids ambiguity with other FAT variants.

## Combined firmware is build-only (2026-08-29)

Decision: Keep `--firmware both` in `build.sh`, but remove it from `run.sh`.
The runner accepts only `uefi` or `bios` and boots one matching image.

Reason: A single QEMU instance boots one firmware path. Keeping combined mode
in the builder preserves efficient dual-artifact generation without giving the
runner an ambiguous target.

## One ISO must boot through UEFI and BIOS (2026-08-29)

Decision: `build/akaos.iso` is a dual-firmware optical image. It contains a
no-emulation El Torito FAT16 image with `EFI/BOOT/BOOTX64.EFI` for UEFI and a
GRUB Multiboot2 El Torito entry for legacy BIOS. `--disk-type iso` is valid with
firmware `uefi`, `bios`, or `both`. In `run.sh`, firmware selects OVMF or
SeaBIOS while both modes use the same ISO file. FAT32 remains UEFI-only.

Reason: Real installation/live media normally carries both firmware entry
paths. Disk format identifies the medium; it should not artificially prevent
UEFI from booting optical media.

## Init system direction

Decision: The planned native init is `aka-init`, not systemd.

Reason: Core userspace should remain independent and match akaOS process, service, and IPC semantics.

## Persistent AI memory

Decision: `AI/` is the provider-neutral persistent development memory, and source code remains the final source of truth.

Reason: Development must continue correctly across model quota limits, model changes, and human handoffs without relying on conversation memory.

## Per-process address spaces and task binding

Decision: Each process owns a private PML4 root whose user branches are private while supervisor-only XSCore branches are shared. Every scheduler thread records the CR3 root that must be activated when it runs.

Reason: Identical user virtual addresses must resolve to process-owned physical memory without duplicating the kernel. Binding address-space activation to the scheduling boundary prevents process code from relying on ad hoc CR3 changes.

## Cooperative process milestone

Decision: The first isolated-process milestone permits multiple READY user tasks but runs each Ring 3 interval cooperatively to exit; it does not claim preemptible concurrent user execution.

Reason: The present interrupt-return path and global low-level Ring 3 return storage are not safe for preemption. Address-space ownership, scheduling integration, exit, wait, and reap can be tested honestly before making that assembly path reentrant.

## Task-owned user transition state

Decision: The Ring 3 kernel continuation and exit result live in the current thread control block. Syscall exit resolves the active TCB through the scheduler instead of reading assembly-global return slots.

Reason: User tasks must not overwrite one another's suspended kernel continuation or exit status. Keeping this state with the schedulable task establishes the correct ownership boundary even while preemption remains disabled.

## Blocking process wait and explicit reap

Decision: Process wait transitions the calling thread to BLOCKED, process exit wakes its recorded waiter, and a non-current thread explicitly reaps EXITED TCB resources.

Reason: Repeated yielding is not a real wait primitive, and a thread cannot safely free its own active kernel stack. Separating exit from reap permits deterministic wakeup, stack unmapping, physical-page return, and slot reuse.

## Per-task privilege-entry stacks

Decision: Every created task owns a guarded 8 KiB privilege-entry stack in addition to its normal kernel execution stack, and context switching updates TSS.RSP0 to the incoming task's entry-stack top.

Reason: Ring 3 interrupts and syscalls must not overwrite another task's privilege-transition frames or the suspended execution frames on its normal kernel stack. Separate guarded stacks establish ownership needed for future user-task preemption.

## Timer requests before interrupt-return preemption

Decision: Timer ticks may latch a preemption request, but the request is consumed only at an explicit safe thread checkpoint until complete interrupt-frame switching exists.

Reason: This proves timer-driven scheduling policy and accounting without switching away inside the current ISR or fabricating safety. Automatic interrupt-return preemption remains a separate milestone.

## User-mode interrupt-return preemption

Decision: Preempt user tasks directly at the timer IRQ return boundary when the timer interrupts Ring 3 execution (CPL=3). Kernel-mode preemption remains disabled.

Reason: When interrupting Ring 3, the CPU automatically switches to the task's dedicated privilege-entry stack (via TSS.RSP0) and saves the user `iretq` frame. The IRQ stub then saves all user GPRs on that same stack. Performing a context switch directly on this privilege-entry stack preserves the complete user execution context in the task's stack frame. When the task is later resumed, the context switch returns to the IRQ handler, which restores all GPRs and `iretq`s back to the exact interrupted Ring 3 instruction. Kernel execution continues to switch cooperatively at safe checkpoints, avoiding complex kernel reentrancy issues.

## Process-owned file descriptor boundary

Decision: Each user process owns a descriptor table, while scheduler threads carry only a pointer to the owning table. Syscalls resolve descriptors through the current TCB, and descriptors reference shared kernel file descriptions.

Reason: File descriptors are process state, not UI commands or global kernel state. This boundary supports correct `dup`/`dup2` reference semantics now and can accept VFS files, pipes, and TTY objects later without placing command behavior in the terminal or shell renderer.

## Internal process lifecycle API

Decision: Process records remain opaque and are owned by `process.c`. Kernel clients use a narrow interface for ELF-image creation, wait, and explicit reap; boot self-tests do not own or duplicate lifecycle implementation.

Reason: `aka-init`, future spawn/exec syscalls, and VFS-backed loading need the same tested lifecycle without reaching into process-table internals. Separating wait from reap preserves the exited state for status observation and makes resource destruction an explicit owner action.

## Initial read-only VFS and initramfs

Decision: Begin filesystem support with a bounded read-only vnode tree and memory-backed regular-file descriptions. Package `/bin/hello` and `/etc/motd` into an initial root and require process creation and user `open` to resolve paths through VFS.

Reason: Path lookup, open-file offsets, descriptor ownership, and executable loading can be proven before block drivers or writable-storage semantics exist. The read-only boundary makes unsupported mutation return a real error instead of simulating persistence and gives future initramfs archive, directory, libc, and shell work a clean native XSCore foundation.

## Shared provisional ABI and native C runtime

Decision: Define syscall numbers and bounds once under `sdk/include/akaos`, compile userspace C with position-independent references, and provide an original freestanding startup/libc subset that translates XSCore errors into `errno`.

Reason: Kernel and userspace must not silently drift on ABI numbers, and real utilities should be ordinary C programs rather than hand-written syscall assembly. Position-independent references allow the existing loader to place low-linked ELF segments at the process user base while preserving separate permissions and avoiding a dynamic loader or unresolved relocations.

## VFS metadata and directory descriptors

Decision: Expose fixed shared-ABI metadata and directory-entry structures, open directories as reference-counted file descriptions, and make `getdents` advance the directory description's scan offset.

Reason: Real utilities need kernel-provided metadata and enumeration rather than hardcoded kernel command output. Treating directory scans like ordinary open descriptions preserves the existing descriptor ownership model and gives `dup`/`dup2` the expected shared-position behavior without adding a parallel handle system.

## Generated initramfs archive and command arguments

Decision: Package VFS directories and files into a binary archive (`initramfs.img`) generated at build time by a host tool and parsed at boot. Pass command-line arguments to user processes by constructing a standard System V x86-64 stack frame (`argc`, `argv` pointer array, null-terminated strings) during process creation.

Reason: Individually linking ELF binaries into the kernel image causes symbol pollution and requires kernel code changes for every new userspace program. A generated archive decouples userspace packaging from kernel linking. Standardizing the user stack layout for `argc`/`argv` allows native C programs (`main(int argc, char **argv)`) to receive dynamic arguments without ad-hoc syscalls or non-standard protocols.

## Dynamic in-memory VFS, CWD tracking, and standard utility error semantics

Decision: Support dynamic VFS node creation (`mkdir`, `open(O_CREAT)`), deletion (`unlink`), and writable buffers up to 4 KiB per node in XSCore VFS while preserving read-only protection (`-EROFS`) on initramfs nodes. Maintain per-process Current Working Directory (CWD) state in `struct xscore_process` with child process inheritance, `getcwd`/`chdir` syscalls, and relative/absolute path resolution. Implement standard Unix utilities (`ls`, `cat`, `touch`, `mkdir`, `rm`, `pwd`, `echo`) as pure C userspace programs that report genuine filesystem errors to `stderr` and exit with non-zero exit codes on failure.

Reason: Operating systems cannot rely on fake UI simulations or hardcoded filenames. All path lookups, mutations, and directory enumerations must flow through real VFS nodes, process descriptor tables, and kernel syscalls. Providing true CWD tracking, relative path navigation (`.`, `..`), standard errno mappings (`ENOENT`, `EISDIR`, `ENOTDIR`, `EROFS`, `ENOTEMPTY`), and `$?` exit code inspection establishes an authentic Unix-like foundation for akaOS.

## Pure userspace shell (`akash`), kernel TTY line discipline, and process syscalls

Decision: Run the interactive command interpreter as an unprivileged Ring 3 binary (`/bin/akash`) rather than inside the kernel. Implement a kernel TTY line discipline (`xscore/kernel/tty.c`) that processes PS/2 scan codes into a canonical line buffer with real-time character echo and backspace erasure, and route descriptor 0 (`stdin`) reads to `xscore_tty_read()`. Implement `sys_spawn` (13) and `sys_waitpid` (14) syscalls to allow user processes to launch and wait for child executables.

Reason: A modern Unix-like operating system must keep command interpretation in userspace. The kernel's responsibility is managing hardware, memory, scheduling, file descriptors, and privilege transitions. Connecting `stdin` to a canonical TTY line discipline and exposing `spawn`/`waitpid` enables multi-process userspace execution where any program can read input and launch child utilities via standard syscalls.

## Userspace heap allocator (`sys_brk`, `malloc`, `free`) and formatted I/O (`printf`, `snprintf`)

Decision: Implement `AKAOS_SYSCALL_BRK` (15) in XSCore to allow processes to dynamically expand and shrink their virtual address space starting at `USER_HEAP_START = 0x0000008000400000`. Manage user heap pages dynamically in the process's address space root and track all physical frames in `struct xscore_process` for 100% reclamation on process reap. Provide a 16-byte aligned freelist allocator with block splitting and bidirectional coalescing in `userspace/libc` (`malloc`, `free`, `calloc`, `realloc`), and implement a standard formatted I/O subsystem (`printf`, `snprintf`, `vsnprintf`, `fprintf`).

Reason: Fixed stack buffers prevent applications from handling arbitrary-sized files, long directory listings, or complex dynamic data structures. Implementing `sys_brk` alongside a robust userspace heap allocator and standard `printf` library provides essential runtime primitives required for advanced userspace programs while preserving total memory isolation and clean resource reclamation.

## Unix pipes (`sys_pipe`), child descriptor inheritance, and shell pipelines

Decision: Implement unidirectional IPC pipes (`AKAOS_SYSCALL_PIPE`, 16) in XSCore backed by ring buffers with reader/writer tracking, reference counting, and standard blocking/EOF semantics. Clone caller descriptor tables on process spawn (`xscore_fd_table_clone`), and support command pipelines (`cmd1 | cmd2`) in `/bin/akash` using standard `pipe()` and `dup2()` redirection.

Reason: Operating systems achieve composability by allowing independent programs to pass byte streams through pipes. Providing kernel-backed pipes and descriptor inheritance allows any native utility (`cat`, `grep`, `ls`, etc.) to be chained seamlessly in the shell without temporary files or special-case hacks.

## Initial foreground signals and Ctrl+C

Decision: Implement a narrow signal foundation first: `kill(pid, signal)` accepts a small shared signal set, `/bin/akash` children are marked foreground, TTY Ctrl+C delivers `SIGINT` to those children, blocked TTY/pipe operations return `EINTR`, and a Ring 3 timer-return trampoline terminates CPU-bound tasks with status `128 + signal`.

Reason: Foreground interruption is a visible shell primitive that must be owned by the kernel TTY, process lifecycle, scheduler, and syscall boundary together. The implementation proves safe delivery and cleanup without pretending to provide POSIX handlers, process groups, background jobs, stop/continue states, or terminal ownership before those abstractions exist.

## PCI-first storage foundation

Decision: Add storage in layers: scan PCI configuration space through mechanism #1, enable bus mastering explicitly, and implement only a polled legacy transitional `virtio-blk` sector-read path first. Keep it optional during normal boots and require a deterministic secondary disk only in the automated storage test.

Reason: The current initramfs and dynamic VFS are memory-backed, so persistent storage needs a real device boundary before filesystem policy. A small read-only DMA path proves BAR discovery, queue ownership, device completion, and sector data without coupling an untested filesystem to boot. Writes, caching, mounting, and persistence-facing APIs will be separate milestones.

## Separate akaFS persistent data disk (2026-08-29)

Decision: Add virtio sector writes and persist the bounded dynamic VFS as a
checksummed akaFS snapshot beginning at sector 8 of a separate data image.
`run.sh` creates `build/akaos-data.img` only when it is absent and reuses it for
either firmware path. Never write persistent user files into the UEFI FAT32
boot image or the read-only BIOS ISO.

Reason: Boot media and mutable data have different ownership and failure modes.
The sidecar disk makes reboot persistence real without risking UEFI boot files,
pretending an ISO is writable, or requiring an incomplete FAT32 implementation.
Sector 0 remains available for the deterministic storage-test marker.

## Architecture-porting boundary (2026-08-28)

Decision: Keep the complete x86-64 UEFI/XSCore build as the regression target,
add compiler-derived shared architecture metadata, and require a freestanding
C/assembly/link smoke image before beginning each native port. The initial
matrix is x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32.

Reason: XSCore's current boot, privilege, paging, interrupt, timer, context
switch, syscall, ELF, and loader code are architecture-specific. A smoke build
gives early portability feedback without conflating a linked ELF with a
bootable kernel or destabilizing the tested x86-64 image. Real ports will add
separate architecture directories and firmware/board tests.

## Independent UEFI and legacy-BIOS images (2026-08-29)

Decision: Keep UEFI and legacy BIOS as separate x86-64 boot artifacts. UEFI
continues to use the PE/COFF loader and FAT32 image; BIOS uses a GRUB
Multiboot2 ISO and a 32-bit protected-mode-to-long-mode bridge. Both adapters
produce the same plain `akaos_boot_info` contract before entering XSCore.

Reason: Firmware protocols and entry CPU states are fundamentally different.
Separate images avoid firmware conditionals inside the tested UEFI loader and
make each path independently reproducible. A shared handoff lets the kernel,
framebuffer splash, drivers, and userspace self-tests stay common while BIOS
receives a real E820 map and framebuffer tag.

## Core Unix Userspace Utilities Suite (2026-08-29)

Decision: Implement `/bin/head`, `/bin/tail`, `/bin/wc`, `/bin/uname`, and `/bin/whoami` as native freestanding Ring 3 executables in `userspace/bin/` backed by real VFS syscalls, libc stdio, dynamic heap allocation, and pipe stream I/O. Package all utilities directly into `initramfs.img`.

Reason: Text processing and system observation utilities form the core interface of any authentic Unix-like operating system. Providing standalone binaries for these tools (rather than restricting them to shell built-ins) enables full composability across pipelines (`|`), file redirection (`>`), and child process spawning across user programs.

## Advanced Shell Capabilities Architecture (2026-08-29)

Decision: Support general input (`<`), output (`>`), and append (`>>`) redirection in the shell by redirecting `STDIN_FILENO` and `STDOUT_FILENO` via standard `dup2()` syscalls before process execution, paired with kernel `O_APPEND` flag support in the VFS file descriptor layer. Implement multi-stage pipeline execution using $N-1$ kernel pipes spawned concurrently with parent pipe descriptor cleanup to guarantee standard EOF propagation. Implement command line history in the kernel TTY line discipline with visual in-place backspace line erasure.

Reason: Redirection and pipelines are foundational primitives in Unix philosophy. Handling redirection at the standard file descriptor table level ensures identical behavior across both built-in commands and external ELF executables, while kernel TTY history provides seamless interactive terminal usability without external readline libraries.

## Dynamic Page-Backed VFS Multi-Block Storage & LRU Buffer Cache (2026-08-29)

Decision: Replace fixed single-buffer vnodes with on-demand physical page-allocated multi-block mapping (`XSCORE_VFS_DIRECT_BLOCKS 32`, `XSCORE_VFS_BLOCK_SIZE 4096`), expanding maximum file size from 4 KiB to 128 KiB while consuming 0 KiB for unallocated blocks. Free all allocated physical pages upon `unlink` and `O_TRUNC`. Interpose an in-kernel 64-sector (32 KiB) LRU buffer cache (`xscore/kernel/bufcache.c`) between the VFS persistence layer and the VirtIO block device with dirty buffer tracking, writeback, and explicit synchronization via `sys_sync` (syscall 18) and the `/bin/sync` utility.

Reason: Dynamic page-backed multi-block storage allows files of meaningful size without bloating kernel BSS or static tables, while guaranteeing 100% physical memory reclamation when files are truncated or deleted. The LRU sector buffer cache minimizes slow, synchronous disk transfers and establishes standard Unix caching semantics.

## System Lifecycle Subsystem & PID 1 Supervisor Architecture (2026-08-29)

Decision: Hand off user-mode execution at kernel boot to a dedicated root supervisor init process (PID 1, `/bin/init`), replacing the hardcoded kernel-loop shell launch. Implement `AKAOS_SYSCALL_REBOOT` (19) in the kernel to flush dirty sector buffers (`xscore_bufcache_sync()`), commit VFS metadata, and reset hardware (8042 keyboard pulse, PCI reset, ACPI shutdown). Provide `<sys/reboot.h>` with standard commands (`RB_AUTOBOOT`, `RB_POWER_OFF`, `RB_HALT_SYSTEM`), and build standalone `/bin/reboot` and `/bin/poweroff` utilities. Expand kernel process table capacity `USER_MAX_PROCESSES` from 4 to 16.

Reason: Operating systems require a top-level user-mode process to manage session lifecycles, supervise interactive shells, and clean up orphaned zombie child processes. Pairing PID 1 with safe `reboot`/`poweroff` syscalls ensures clean hardware teardown with zero data corruption.

## Interactive QEMU reset policy (2026-08-29)

Decision: Normal `run.sh` sessions must allow guest hardware reset so `reboot`
returns to akaOS in the same QEMU window. Keep `-no-reboot` only in automated
tests, capture tools, and debugger workflows that intentionally stop on reset.

Reason: Reboot and poweroff are distinct lifecycle operations. Treating reset
as process termination made the visible `reboot` command behave like shutdown,
even though the XSCore syscall correctly requested a hardware reset.

## Native ARM and RISC-V lifecycle interfaces (2026-08-30)

Decision: Use PSCI 0.2 HVC calls for AArch64/ARM32 system reset and poweroff,
and use the OpenSBI SRST extension for RISC-V 64/32 cold reset and shutdown.
Keep the RISC-V QEMU syscon write only as a defensive fallback after SBI.

Reason: Sleeping forever after logging a lifecycle request leaves the display
and emulator running. PSCI and SBI are the firmware-defined lifecycle
interfaces exposed by the QEMU `virt` machines and preserve the required
semantic difference: reboot resets the guest in the same QEMU session, while
poweroff and shutdown terminate it.

## Linux-Compatible `/bin/rm` Utility & Recursive VFS Removal (2026-08-29)

Decision: Upgrade `/bin/rm` from a simple single-file unlinker into a full Linux-compatible removal utility supporting recursive directory tree traversal (`-r`, `-R`, `--recursive`), forced silent removal (`-f`, `--force`), empty directory removal (`-d`, `--dir`), verbose feedback (`-v`, `--verbose`), combined flag parsing (e.g. `-rf`, `-rfd`), `--` option termination, and non-recursive directory safety checks (`rm: cannot remove '<path>': Is a directory`). Keep stack footprint small with a 4-dirent stream buffer to prevent user stack page faults during nested recursion.

Reason: Standard Unix command-line workflows and shell scripts require recursive directory cleanup (`rm -rf`) and option flexibility without hardcoded or incomplete implementations. Handling directory checks at the utility level while leveraging kernel VFS primitives ensures full POSIX fidelity and stability.

## Process credentials and authenticated sessions (2026-08-29)

Decision: Treat user identity as kernel-owned process state, not shell text.
Processes carry real/effective/saved UID and GID values; VFS access uses the
effective identity; setuid/setgid bits are applied during executable spawn; PID
1 supervises `/bin/login`; account data uses conventional public passwd/group
files and a root-only shadow file. Password input disables TTY echo and history.

Reason: A root-looking prompt without kernel authorization is a simulator.
Credentials must cross the syscall boundary and be checked at every protected
resource. A small native account stack provides a usable Unix-style model
without importing Linux or a Linux userspace.

Decision: The development image ships generic recovery credentials only:
`root`/`root` and `user`/`akaos`. Passwords are stored as 4096-round SHA-256
hashes salted by account name, never as plaintext. Users should change them
with `passwd`; stronger random salts and a memory-hard KDF remain hardening work.

Reason: A bootable development OS needs deterministic first-login access while
remaining free of private developer information and avoiding plaintext secrets.

## Native x86_32 Architecture Port (2026-08-29)

Decision: Implement native 32-bit Protected Mode boot and kernel architecture (`arch/x86_32`) via Multiboot 1 and Multiboot 2 specification headers, 32-bit GDT/IDT/TSS, 2-level 32-bit paging (Page Directory + Page Tables), cooperative thread switching, and `int 0x80` syscall dispatch into Ring 3 user mode.

Reason: Broadens akaOS from a 64-bit-only platform into a true multi-architecture operating system capable of running natively on classic x86 32-bit hardware and virtual machines with independent memory management and execution.

## Job Control, Process Groups & Signals Architecture (2026-08-29)

Decision: Model process groups via `pgid` tracking on each process struct, with syscalls `sys_setpgid` (31) and `sys_getpgid` (32), process group signal broadcasting, and process stop/continue states (`XSCORE_PROCESS_STOPPED`). Support `Ctrl+Z` (`SIGTSTP`), background execution (`&`), and shell built-ins `jobs`, `fg`, and `bg` in `/bin/akash`.

Reason: Job control is an essential Unix interactive shell primitive that enables users to pause, resume, and run commands in the background without suspending or terminating the shell session.

## Virtual Memory Fork & Physical Page Reference Counting (2026-08-29)

Decision: Implement physical memory frame reference counting (`page_ref_counts[]`) in the PMM, and implement `sys_fork` (syscall 35) in XSCore to duplicate user address spaces, page mappings, open file descriptors, credentials, and spawn child threads.

Reason: `fork()` is the foundational Unix process creation primitive that allows processes to duplicate their exact runtime state and execute concurrently.

## Bounded automatic user-stack growth (2026-08-30)

Decision: Start each user process with one mapped stack page and permit a
user-mode page fault to grow it only by the immediately adjacent lower page,
up to 64 pages (256 KiB). Map grown pages writable and non-executable, track
them as process-owned frames, and keep the next lower page unmapped.

Reason: Real programs need more than a fixed 4 KiB stack, but accepting sparse
faults across a broad range would turn invalid pointers into mappings. A
contiguous bounded policy supplies useful automatic growth, preserves a moving
guard, works uniformly across the six MMU ports, and provides the recoverable
fault foundation required by later executable demand paging and COW.

## Anonymous heap demand paging and Copy-on-Write fork (2026-08-31)

Decision: Reserve process heap virtual space with `sys_brk`, commit a
zero-filled page only on its first recoverable user translation fault, and
share tracked user image/stack/committed-heap frames read-only across
`sys_fork` until a write fault requires a private copy. Track references by
absolute physical page number so the native ARM/RISC-V RAM base is supported,
and pass normalized write/present fault bits from every architecture handler
to one shared process policy.

Reason: Lazy anonymous allocation keeps untouched heap capacity cheap while
preserving strict process isolation. COW avoids duplicating every frame during
fork and provides a real write-fault path that can be tested uniformly across
x86, ARM, and RISC-V. Executable/file-backed pages use the same fault boundary
so process creation does not allocate untouched code, data, or BSS frames.

## Architecture-owned fork syscall frames (2026-08-31)

Decision: Capture the complete user syscall frame at each architecture's trap
entry, clone it into kernel-heap-owned child state, patch only the ABI return
register to zero, and restore it through a port-specific `xscore_user_resume`
path on the child's own privilege-entry stack.

Reason: A real `fork()` must preserve every user register, stack pointer,
program counter, and privilege status while returning different values to the
parent and child. Copying the frame onto the child's kernel stack also keeps
future traps and process exit on valid task-owned kernel state.

## Native AArch64 direct-kernel path (2026-08-29)

Decision: Implement the first AArch64 native path as a self-contained direct
QEMU `virt` kernel image (`build/XSCORE_AARCH64.ELF`) with a matching
freestanding userspace/initramfs build and `tools/test-aarch64.sh`; keep the
x86 UEFI/BIOS disk-image selectors separate because the AArch64 path does not
yet have a firmware-specific loader.

Reason: This provides a real architecture port with CPU entry, exception
vectors, MMU, process/syscall execution, and login verification without
pretending that an x86 firmware image or a smoke ELF boots on AArch64.

Decision: Extend the AArch64 saved thread context with `ELR_EL1`, `SPSR_EL1`,
and `SP_EL0`, and save/restore them in the assembly context switch.

Reason: Those registers are CPU-global. A shell blocked in `waitpid` must
recover its own exception return PC and user stack after a child enters and
exits EL0; leaving the child values installed causes a data abort or return to
the child's `_exit` loop.

## Native ARM32 direct-kernel path (2026-08-29)

Decision: Implement ARM32 as a self-contained ARMv7-A direct QEMU `virt`
kernel (`build/XSCORE_ARM32.ELF`) with an ARM EABI userspace/initramfs build
and `tools/test-arm32.sh`. Link ARM32 userspace at `0x10000000`, the runtime
mapping base, because the freestanding static images do not include a dynamic
relocator.

Reason: This gives ARM32 a genuine entry/vector, MMU, process, syscall, and
login path while keeping it independent from the x86 UEFI/BIOS image format.

Decision: Treat the entire `0x10000000..0x10ffffff` ARM32 user window as
private during kernel-map synchronization, and save/restore the banked user SP
in the SVC path around blocking syscalls.

Reason: ARM32 has one CPU-global user SP bank. Without protecting all user L1
entries, the stack or heap can be replaced by kernel sections; without saving
the banked SP, a child task changes the shell's stack while the shell waits.

## Build and Run Interface Option Validation Harmonization (2026-08-29)

Decision: Strictly validate CLI flags in `build.sh` and `run.sh` across all architecture targets. Non-applicable firmware and disk options supplied to direct QEMU kernel targets (`x86_32`, `aarch64`, `arm32`) are immediately rejected with clear diagnostic messages.

Reason: Prevents silent misconfigurations where users specify flags like `--disk-type iso` or `--firmware bios` for targets that build direct Multiboot or direct QEMU `virt` kernels.

## Uniform selectors supersede native-port rejection (2026-08-30)

Decision: Supersede the 2026-08-29 native-port validation decision. AArch64,
ARM32, RISC-V64, and RISC-V32 must accept `--firmware`, `--disk-type`,
`--test`, and `--verbose` through the common build interface, and their run
paths must accept firmware/disk selections without telling the user to omit
them. `--test` invokes the matching port parity suite.

Reason: The project requires one predictable command shape across all six
architectures. Architecture backends may emit different artifact formats, but
that implementation detail must not turn shared selectors into a CLI error.

## No valid build/run combination rejection (2026-08-30)

Decision: Extend uniform selector handling to x86 as well. Every combination
of a documented `--arch`, `--firmware`, and `--disk-type` value must enter a
working architecture backend instead of exiting with a compatibility error.
Where firmware cannot directly boot the selected media layout, transparently
select the architecture's bootable hybrid-ISO backend. Continue rejecting only
missing values, unknown option names, and values outside the documented sets.

Reason: The public build and run interface is a stable cross-architecture API.
Backend image-format details must not make an advertised value combination
unusable.

## Six-architecture default scope (2026-08-30)

Decision: Treat architecture-relevant requests as applying to every supported
port by default: x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32. Only
narrow that scope when the user explicitly requests a particular architecture.

Reason: akaOS promises behavioral parity across x86, ARM, and RISC-V; fixing a
feature on one port alone silently breaks that promise.

## x86_32 FAT32 media is a real UEFI artifact (2026-08-30)

Decision: Preserve `--disk-type fat32` for x86_32 as an actual FAT32 image,
`build/akaos32.img`, containing the native kernel and standalone GRUB EFI
loaders. A BIOS selection additionally emits `build/akaos32.iso` and the
runner selects that ISO because legacy BIOS cannot execute an EFI partition.

Reason: A selector must describe the artifact it requests. Silent conversion
of every x86_32 FAT32 build into an ISO was incorrect even though it avoided a
compatibility error.

## Shared persistent data disk across architectures (2026-08-30)

Decision: Attach the persistent `build/akaos-data.img` sidecar to every normal
QEMU run. Keep x86 on legacy `virtio-blk-pci`; implement the same sector API
for AArch64, ARM32, RISC-V64, and RISC-V32 with VirtIO-MMIO. Mount the shared
buffer cache and AKAFS002 overlay before starting userspace.

Reason: Boot-media selection must not change shell filesystem behavior. A real
`echo value > file` should write successfully and survive a power cycle on
every architecture just as it does on x86, while keeping boot media immutable.

## x86_32 BIOS storage fallback (2026-08-31)

Decision: Keep VirtIO PCI for x86 UEFI, but attach the writable sidecar as a
primary IDE disk for x86 BIOS and service it through the shared block API's ATA
PIO fallback. Keep VirtIO-MMIO for ARM/RISC-V.

Reason: SeaBIOS/i440FX legacy VirtIO can complete requests in QEMU without
reliably exposing the used-ring update to a BIOS guest. ATA PIO is the
firmware-compatible storage path and preserves identical akaFS persistence
semantics without changing the user-facing selectors.

## Interactive configuration over the stable CLI (2026-08-31)

Decision: Launch a raspi-config-style terminal UI when `build.sh` or `run.sh`
is invoked without arguments from a TTY. Implement the UI as a thin producer
of the existing command-line arguments, retain all flags for automation, offer
an explicit `--menu`, and keep no-argument non-interactive behavior unchanged.
Prefer `whiptail` but provide a numbered terminal fallback with no new package
requirement.

Reason: Interactive users should be able to discover and select every
architecture, firmware, disk, and display/test option without memorizing
flags. Routing the menu back through the stable CLI parser prevents the UI and
automation paths from developing different compatibility rules.

## Read-only cross-distribution dependency assistant (2026-08-31)

Decision: Make build and run validate the effective target's host prerequisites
through one shared checker. Support apt, pacman, dnf, zypper, and apk package
guidance, portable font/GRUB/OVMF discovery, `--check-deps`, and an equivalent
interactive-menu action. Never install packages, invoke `sudo`, or silently
change the host.

Reason: A new contributor should receive an actionable setup command instead of
a late compiler or QEMU failure, while retaining control of administrative host
changes. Target-aware checks avoid forcing ARM/RISC-V or ISO-only dependencies
onto contributors who do not need them.

## Preflighted user copies with kernel bounce buffers (2026-08-31)

Decision: Route every syscall pointer through shared page-range preparation and
copy helpers. Resolve valid anonymous and COW faults before copying, reject all
other invalid ranges with `EFAULT`, and keep raw user pointers out of VFS, TTY,
pipe, and directory implementations by using bounded kernel-heap buffers.

Reason: Mapping checks followed by direct subsystem dereferences are not a safe
kernel boundary. A valid lazy or COW page needs controlled fault resolution,
while a bad pointer must fail without crashing the kernel or partially changing
file/descriptor state.

## Fixed-address native userspace ELF layouts (2026-08-31)

Decision: Link AArch64, ARM32, RISC-V64, RISC-V32, and x86_32 userspace at the
virtual address where XSCore maps it; retain the existing relocated-zero layout
for x86-64. Teach the shared ELF loader to distinguish these layouts by machine.

Reason: akaOS has no dynamic ELF relocator. Fixed native layouts guarantee that
absolute GOT/data references—including libc `errno`—address the mapped image
instead of low unmapped memory.

## Separate XSCore and akaOS Naming Architecture (2026-08-31)

Decision: Keep XSCore and akaOS conceptually and syntactically distinct throughout the codebase and documentation.
- “akaOS is the operating system. XSCore is the kernel. Do not use akaOS as a generic prefix throughout XSCore.”
- Inside `xscore/`, use natural domain terminology (`kernel`, `core`, `arch`, `memory`, `scheduler`, `process`, `thread`, `vfs`, `syscall`, `interrupt`, `timer`, `driver`) and avoid prefixing internal subsystems with `akaOS*` (no `akaOSMemory`, `akaOSScheduler`, `akaOSProcess`, `akaOSVFS`).
- Userspace tools and applications must use distinct, independent names (e.g. `ted`, `akash`, `login`) rather than automatically forcing the `aka` prefix (avoid `akaTerminal`, `akaFileManager`, `akaEditor`).
- Existing working names (such as `akash`) and established external ABI structures (like `struct akaos_boot_info`) are preserved with compatibility aliases where appropriate.

Reason: Maintaining clear separation between the kernel (XSCore) and the OS (akaOS) ensures modular software architecture, prevents repetitive naming antipatterns, and gives both the kernel and userspace applications independent project identities.

## Shared Clean File-Page Caching & Executable Image Lifetime Tracking (2026-08-31)

Decision: Implement a shared clean file-page cache in XSCore (`xscore/page_cache.h` & `xscore/page_cache.c`).
- Read-only executable text and constant data pages (`!(flags & XSCORE_PAGE_WRITABLE)`) faulted in by user processes are cached in a global page cache table and shared across all processes executing the same binary image.
- Physical memory manager reference counting (`xscore_pmm_refcount_inc`/`dec`) coordinates page retention between active process page tables and the clean page cache.
- Writable pages (data, BSS, heap, stack) remain strictly private per-process or copy-on-write (COW).

Reason: Sharing clean text/rodata pages eliminates redundant physical memory allocations when multiple processes (e.g. `akash`, `cat`, `ls`, `hello`) execute concurrently, significantly reducing physical memory consumption while preserving total process isolation.
## Bounded named shared memory and physical MAP_SHARED backing (2026-09-01)

Decision: Implement `MAP_SHARED` with refcounted backing objects that own
demand-allocated physical pages. Add a bounded POSIX-style named namespace
exposed through `shm_open`, `shm_unlink`, and `ftruncate`, and add `mprotect`
for mapping-range permission changes. Shared mappings bypass fork COW;
private mappings continue to use it. Preserve open/mapped objects after unlink
until the final descriptor and mapping reference is released.

Reason: A userspace compositor and its clients need a real common pixel-buffer
mechanism whose writes are visible across process address spaces. A bounded
namespace and explicit page/reference ownership fit XSCore's current
single-core, fixed-capacity architecture while establishing standard Unix-like
lifetime semantics without importing Linux. Write-through shared mappings of
ordinary VFS files remain separate future work.

## Pollable descriptors and one stable kernel input stream (2026-09-01)

Decision: Add one architecture-independent `poll()` readiness contract to the
existing file-description layer and expose keyboard, text, pointer, and
synchronization events through a root-readable `/dev/input/event0` descriptor.
Use a bounded circular stream with a sequence cursor per open rather than one
destructive global reader. Keep x86 PS/2 and ARM/RISC-V VirtIO parsing in their
drivers while forwarding the same type/code/value ABI.

Reason: A compositor must wait for input and IPC without busy-waiting or
owning kernel TTY internals. Per-open cursors permit a session service and
diagnostic reader to observe the same input without stealing events, while
root-only access prevents ordinary processes from passively recording login
keystrokes. The bounded design matches XSCore's current fixed-capacity model
and stays independent of Linux while retaining familiar Unix readiness
semantics.

## Resolve visible device nodes before descriptor dispatch (2026-09-01)

Decision: Represent kernel devices as typed character nodes in a boot-created
`/dev` VFS namespace, then resolve paths through the caller's CWD before
dispatching to the device-specific descriptor implementation. Keep these
nodes outside the persistent akaFS overlay.

Reason: A path recognized only inside the open syscall is invisible to
`stat`, directory enumeration, and users. VFS-visible nodes make `/dev`
coherent while resolved identity avoids fragile absolute-string matching and
supports normal relative Unix paths. Device state and I/O remain kernel-owned;
the nodes provide namespace, type, ownership, and permission metadata.

## SMP scope: AP work slots now, user-task multicore later (2026-09-06)

Decision: x86_64 SMP boots real APs through ACPI MADT INIT/SIPI with per-CPU
GDT/TSS/IST/local-APIC timers. Each AP registers an idle scheduler TCB and can
run an explicitly CPU-owned kernel worker through the normal guarded-stack
context switch; the round-robin process scheduler still keeps ordinary user
tasks on the BSP. ARM and RISC-V ports intentionally keep one online
schedulable CPU in the `xscore/include/xscore/smp.h` stubs even when QEMU offers
more; their separate firmware secondary-entry heartbeat is documented below.

Reason: The first shared-state foundation now protects PMM bitmap/refcounts,
heap growth/freelists, VFS metadata mutators, process-table slot reservation,
and descriptor-table/file-description lifetime. Descriptor I/O takes a
temporary reference before releasing the table lock, while close/dup/dup2 and
clone serialize entry replacement. The AP self-test concurrently exercises
the PMM and heap. Vnode lifetime and full per-process cross-CPU
synchronization are still not complete. The bounded kernel-worker handoff proves CPU ownership and context
cleanup without entering unsafe userspace paths. Moving user tasks to APs
before per-process references and per-CPU run queues would create untestable
races. Keeping the scheduler single-core on ARM/RISC-V keeps the status matrix
honest instead of counting heartbeat-only harts as schedulable. The test suite
(`tools/test-smp.sh`) therefore proves
AP liveness, per-CPU TSS/timer bring-up, CPU ownership, and shared allocator
correctness — not user-task multicore. Per-process references and per-CPU run
queues are prerequisites for any future user-task multicore work.

## Honesty boundaries for remote-access and storage features (2026-09-06)

Decision: Document `/bin/ssh` as a plain-TCP login/banner conversation (no
SSH transport cryptography). The storage portion of this decision was
superseded later the same day by IRQ-assisted completion below.

Reason: Feature names borrowed from Unix tools set expectations. akaOS is
independent and must not overstate protocol compatibility; logs proved the
kernel disk paths poll the virtqueue/ATA status rather than take completion
interrupts. The audit matrix used PARTIAL and NOT TESTED accordingly at that
time.

## Interrupt notification with deferred ring consumption (2026-09-06)

Decision: hardware IRQ handlers acknowledge controllers and mark device work
pending. VirtIO used rings are validated and consumed in process context.
x86_64 uses fixed MSI-X vectors 42/43 for its current block/network queues;
ARM/RISC-V use GICv2/PLIC routing and bounded timer wakeups. Timeouts disable a
device before its single DMA buffer can be reused. x86_32 VirtIO and ATA PIO
remain synchronous.

Reason: keeping parsing, allocation, VFS, and network work outside interrupt
context avoids entering subsystems that are not yet SMP-safe. Fixed vectors are
appropriate for the current bounded device set; a generic MSI/vector allocator
belongs with the future driver model.

## x86_64 VMM lock and TLB shootdown ordering (2026-09-07)

Decision: serialize x86_64 page-table access with a global VMM lock, release it
before issuing synchronous TLB shootdowns, and make both the VMM lock and TLB
mailbox lock interrupt-aware while contended. Track the active CR3 root per CPU;
target private-root invalidations only at CPUs using that root and broadcast
kernel-root changes. Keep vector 253 for TLB work and vector 254 for remote
reschedule wakeups, including CPU 0.

Reason: a page-fault or interrupt path can enter with IF cleared. Waiting for a
lock while IF is cleared can prevent the IPI acknowledgement that the lock
holder needs, creating a circular deadlock. Releasing the VMM lock before the
mailbox wait and temporarily servicing interrupts only during contended waits
breaks that cycle while preserving atomic lock acquisition and the original
interrupt state. The 2/4-CPU SMP suite now exercises this ordering with AP
private-root page faults and remote wait/reap.

## ARM/RISC-V secondary bring-up boundary (2026-09-08)

Decision: Bring up non-x86 secondary execution units through their native
firmware interfaces, register an idle TCB, and stop each unit after bounded
heartbeat/tick accounting until per-CPU process migration state is audited.
PSCI HVC `CPU_ON`
is used on AArch64/ARM32 and OpenSBI HSM `hart_start` on RISC-V.

Reason: QEMU exposes multiple ARM/RISC-V CPUs, but entering the existing
single-BSP process scheduler from a firmware-created stack would race shared
address-space, timer, and TTY state. Private early stacks plus an observable
heartbeat prove the firmware/assembly/C contract without claiming a scheduler
that is not yet implemented. QEMU `-smp 2` and `-smp 4` verification passes on
all four native ports.

## ARM/RISC-V timer ownership boundary (2026-09-08)

Decision: Give every firmware-started ARM/RISC-V secondary its own interrupt
controller context and timer source before publishing it online. A GICv2 PPI
30 or per-hart SBI/PLIC timer is rearmed and acknowledged in the shared IRQ
dispatcher, and `xscore_sched_timer_tick()` records the owning logical CPU's
hardware ticks. ARM32 uses a dedicated banked IRQ stack; RISC-V derives PLIC
windows from the raw hart ID. Keep actual thread scheduling gated to x86 until
non-x86 trap-frame and address-space migration is audited.

Reason: a heartbeat-only secondary can prove firmware entry but cannot prove
interrupt delivery. Per-CPU timer ownership is the smallest real scheduler
boundary and is independently testable without exposing the existing
BSP-owned process scheduler to unsafe context switches. The four-core native
matrix now requires one timer IRQ on every online secondary.

## ARM/RISC-V per-CPU READY admission boundary (2026-09-08)

Decision: expose the firmware-published online-secondary count to the shared
thread layer and maintain one READY bit mask per logical CPU. Explicit
`xscore_thread_create_on_cpu()` placement may target an online CPU, while
`multicore_userspace_enabled` remains false on native ARM/RISC-V and no
user-task migration is attempted. x86_32 uses dedicated single-CPU SMP stubs.

Reason: owner-CPU admission and queue bookkeeping can be tested independently
of trap-return and address-space migration. This provides a real per-CPU
run-queue boundary without claiming that native non-x86 threads can yet switch
or execute user address spaces safely on secondary CPUs.

## Non-x86 context and address-space ABI boundary (2026-09-08)

Decision: lock each architecture's C thread-context offsets to its assembly
switch routine with compile-time assertions, and run a BSP-only native probe
that activates two private roots and restores the kernel root before init.
Treat the existing native parity boots as the user trap-entry/return proof;
do not enable cross-CPU user-task migration yet.

Reason: the current ARM/RISC-V exception paths are exercised on their BSP, but
secondary CPUs still park after timer accounting. The assertions prevent silent
ABI drift, while the root round-trip makes page-table activation observable
without pretending that a saved user trap frame can be restored on another CPU.

## Native static TLS layout and TP ownership (2026-09-21)

Decision: keep static TLS in a bounded loader-owned 64 KiB area per akaOS
thread, with positive offsets from TP on AArch64/RISC-V and negative TPOFF
offsets from an end-of-area TP on x86/ARM32. Store the authoritative TP in the
XSCore user-transition record and expose explicit SET_TP/GET_TP syscalls; each
architecture's privileged return path restores its hardware TP where the ABI
requires it. ARM32 userspace uses soft TLS and a register-preserving
`__aeabi_read_tp` syscall helper rather than borrowing r9.

Reason: this provides a real static-TLS contract for compiler-generated DSOs
without pretending that the kernel already has a scalable ELF TLS allocator.
Routing GD/TLSDESC and dlsym through the same static instance prevents duplicate
per-thread copies while leaving module/thread slot growth, fork/exec inheritance,
and full POSIX TLS variants as explicit follow-up work. The six-guest loader
matrix is the compatibility boundary for this tranche.
# 2026-09-20: register-independent first TLS tranche

Keep the first TLS implementation independent of architecture thread-pointer
registers: each loaded DSO owns a bounded template and each akaOS pthread gets
an initialized block keyed by its native thread ID. Support the portable
general-dynamic DTPMOD/DTPOFF descriptor model and route it through
`__tls_get_addr`; reject static TP relocations explicitly until a real TLS
layout/TP ABI exists. i386 is the one ABI exception: its resolver receives the
descriptor in EAX, so `regparm(1)` and the triple-underscore alias are required.
This keeps desktop progress testable without pretending that complete upstream
TLS semantics are already present.

# 2026-09-21: bounded GLib default context

Use one stable process-local default main-context object for the current
freestanding GLib-shaped runtime. Loops retain the resolved context handle,
while source dispatch remains bounded and process-wide across the separately
packaged compatibility DSOs. Unknown cross-DSO context pointers fall back to
the local default rather than being dereferenced. This gives clients the
required non-null/default-context contract without claiming full GLib source
polling or arbitrary context allocation.
 
# 2026-09-21: event-loop-driven native D-Bus transaction

Use a caller-owned fixed-size async PING state machine as the first D-Bus
transport boundary. Write the tiny request during initialization, then use
the existing POLL-backed GLib-shaped descriptor source to consume a bounded
response with short reads. Do not add a hidden D-Bus thread or make libc own a
global main context.

Reason: this proves that real native D-Bus I/O can complete through the same
event-loop path needed by desktop services while keeping ownership and failure
states explicit. It avoids falsely implying that a one-shot test is a complete
libdbus implementation; message queues, cancellation, reconnect, and
per-context source ownership remain deliberate follow-up decisions.

# 2026-09-21: bounded standard D-Bus frame ownership

Keep the first binary async client transaction caller-owned with a fixed
512-byte request/response workspace. Require the caller to provide the already
authenticated connection and to attach the descriptor to its own event loop;
the pump validates only one aligned METHOD_RETURN frame and never allocates or
spawns a worker.

Reason: this extends the line-PING proof to the wire format desktop services
actually use while preserving explicit lifetime and bounded-memory guarantees.
Reusable serial queues, cancellation, reconnect, and context ownership remain
separate work rather than being hidden in a global transport singleton.

# 2026-09-21: bounded reusable asynchronous D-Bus queue

Use a caller-owned four-slot `akaos_dbus_async_client` for standard binary
method calls. Allocate a serial at queue time, write a bounded frame, and let
the existing event-loop descriptor source drive incremental reads. Match
METHOD_RETURN/ERROR replies by serial, retain only bounded reply bodies, and
make cancellation remove a slot without introducing a hidden worker or global
event loop.

Reason: concurrent Ping/GetId traffic is the smallest useful proof that the
wire transport can serve real desktop clients beyond one request. Fixed slots
and bounded bodies preserve deterministic freestanding behavior; reconnect,
per-context source ownership, and the complete D-Bus signature/type API remain
explicit follow-up work rather than being implied by the probe.

# 2026-09-21: retain and filter desktop sources by context

Attach each runtime timer, descriptor, and GIOChannel watch to a retained
`runtime_context`; select the registration target with bounded
`g_main_context_push_thread_default()`/`pop_thread_default()`, and filter
loop/poll/iteration/pending dispatch by that context. Explicit contexts are
created from the fixed context table and references are released on source and
loop teardown.

Reason: GLib clients rely on multiple independent contexts and must not have a
watch registered for one loop consumed by another. A fixed table and explicit
retention provide the ownership contract without hidden threads or an
unbounded allocator; cross-thread locking and the full upstream source API are
left as follow-up work.

# 2026-09-21: explicit asynchronous D-Bus reconnect ownership

Make reconnect a caller-driven operation on the async client: close the old
authenticated descriptor, invalidate pending serials and partial-frame state,
re-run the bounded binary handshake, and require the caller to register the
replacement descriptor with its own context. Restart serial allocation for the
new connection and do not preserve stale replies.

Reason: desktop sessions must recover from a bus restart without a hidden
thread or global retry policy. Explicit watch reattachment keeps descriptor and
context ownership observable; cancelled-reply states, retry budgets, and
close/reset convenience APIs remain deliberate follow-up boundaries.

# 2026-09-21: retain cancellation until caller acknowledgement

Represent cancellation as a bounded reply-slot state. `cancel()` marks the
pending serial cancelled; `take_reply()` consumes it once and returns
`ECANCELED`, while a missing serial remains `ENOENT`. Add a client-owned close
operation that closes the descriptor and clears all slot/frame state.

Reason: silently dropping a cancelled serial makes ownership and error
handling ambiguous for GLib/D-Bus clients. A one-shot acknowledgement preserves
bounded memory and lets callers distinguish cancellation from transport failure
without adding a worker or retry loop. Automatic retry/backoff remains a
separate policy decision.

# 2026-09-21: bounded caller-driven asynchronous D-Bus retry

Add `akaos_dbus_async_client_retry()` as an explicit, capped recovery
operation. Permit one through three attempts, clear all old descriptor,
serial-slot, cancellation, and partial-frame state between attempts, and
re-authenticate a fresh binary connection. Restart serial allocation at one
and require the caller to attach the replacement descriptor watch.

Reason: a desktop session needs bounded recovery from a closed/error bus, but
implicit background retries or hidden watch mutation would obscure ownership
and make event-loop behavior nondeterministic. Richer backoff/address parsing
and complete upstream D-Bus message semantics remain separate follow-up work.

# 2026-09-21: bounded pthread cleanup-stack ownership

Implement `pthread_cleanup_push/pop()` as caller-lexical macros backed by a
fixed per-thread LIFO stack, and route both `pthread_exit()` and cancellation
through the same drain-before-destructors path. Record the initial thread TID
before creating workers and keep its cleanup stack separate from worker
records.

Reason: GLib and Qt worker wrappers depend on cleanup handlers to release
locks, references, and cancellation state. Explicit bounded storage keeps
ownership visible and cross-architecture while avoiding hidden host-libc state;
clone flags, asynchronous interruption, and full process teardown remain
separate follow-up work.

### 2026-09-22 — verify typed desktop boundary on every port

Decision: treat the six-port compositor/client matrix as the acceptance gate
for shared desktop ABI changes. The matrix passed on x86_64, x86_32, AArch64,
ARM32, RISC-V64, and RISC-V32 after typed Properties support. Keep the ABI
bounded and relocation-safe; do not claim complete KDE/GNOME/LXDE/LXQt support
until property-change signals and full D-Bus marshalling are implemented.

### 2026-09-22 — bounded typed desktop properties

Use the standard `org.freedesktop.DBus.Properties` method names and signatures
for desktop discovery, but keep the implementation fixed-size and explicit.
`Get`/`GetAll` expose only stable akaOS session properties; `Set` is rejected as
read-only. Encode variants and dictionary entries in the actual message-body
coordinate space so D-Bus alignment is preserved. Avoid static pointer tables
in resident images because akaOS relocates images as a whole without applying
ordinary absolute data relocations; switch/PC-relative strings are the safe
form.

### 2026-09-22 — bounded PropertiesChanged signal

Decision: expose readiness changes through one standard `PropertiesChanged`
signal carrying `sa{sv}as`, with all six published properties represented as
string/boolean variants and an empty invalidated array. Emit it from the
claimed service connection and let the native bus apply normal `AddMatch`
delivery. Verify the complete mixed dictionary on all six ports before
expanding the marshaller to arbitrary signatures.

### 2026-09-22 — segment stream writes at the kernel packet boundary

Decision: keep the user-visible socket API stream-oriented, but split every
IPv4/TCP write into ordered payloads below the fixed Ethernet packet buffer.

Reason: the previous sender returned success while dropping any payload whose
headers plus body exceeded one 1536-byte packet. That made a valid larger
D-Bus introspection reply hang its reader and would affect X11/toolkit frames
as well. Segmentation belongs in the transport, while complete D-Bus
signature marshalling remains a separate bounded-runtime milestone.

### 2026-09-22 — bounded standard broker method set

Decision: add the standard connection credential/process, match-rule,
queued-owner, activation-environment, and reload methods with explicit typed
replies before attempting a general-purpose marshaller.

Reason: these calls are common session-bus discovery traffic and can be
validated with fixed-size parsers. Keeping the method set bounded preserves
resident-image relocation safety and makes six-port verification meaningful;
it does not justify claiming unmodified KDE/GNOME/LXDE/LXQt compatibility.

### 2026-09-22 — shared libc through PT_INTERP

Decision: make `libc.so.6` a real shared ET_DYN and link the dynamic loader
probe against it, while retaining the static CRT/libc path for existing
programs. Give i386 its own GOT-aware dynamic CRT and teach RV32 the data
relocation emitted by that shared image.

Reason: desktop runtimes need a genuine shared-library contract, not a test
that only names a DSO while embedding libc. Keeping both startup paths lets
the six architectures validate the new ABI without regressing static tools.
This remains a prerequisite milestone; full upstream desktop compatibility
still requires complete D-Bus, toolkit, pthread, and display-protocol ABIs.

### 2026-09-22: keep D-Bus codec separate from broker policy

The generic signature parser, wire validator, and value-tree marshaller live
in `userspace/libc/src/dbus_codec.c`, while the broker retains its explicit
method/credential policy. This separation permits the same codec to serve
GLib/GIO, Qt, and native services without silently broadening authorization.
The current codec is a bounded foundation; generic decode, strict object-path
validation, and arbitrary dispatch must be added before claiming full upstream
desktop compatibility.

Follow-up decision: use the generic codec as a transport gate, not a policy
engine. Outbound callers and resident services must reject malformed typed
bodies before writing or dispatching them; the broker still decides which
methods and credentials are permitted. Array lengths follow standard D-Bus
alignment semantics, and object paths are validated centrally.

Futexes key shared waiters by the physical address of the mapped 32-bit word,
so `pshared` objects mapped at different virtual addresses can rendezvous.
Callers that request the private flag use the pair of address-space root and
virtual address. A scheduler pending-wake bit is required to close the interval
between queue registration and transition to BLOCKED; returning only a wake
count from the futex table would permit a lost wake in that interval. At the
time of this decision (before the 2026-09-23 scheduler expansion), this choice
was bounded by XSCore's 16-thread table. The table has since been expanded;
see the follow-up decision below.

### 2026-09-23 follow-up: resolve shared futexes through mmap metadata

The initial process-shared futex test found that `xscore_process_page_info()`
searched executable/stack page records but not the separate heap and mmap
arrays. Include those mappings and return their recorded physical page and
flags. The parent-WAIT/child-WAKE test now passes with separately mapped shared
memory on all six guest architectures. A follow-up preserves each
`PTHREAD_PROCESS_SHARED` attribute in mutex, condition, rwlock, and barrier
objects and uses the physical futex key; semaphores use their `pshared` field.
All five object types pass cross-process tests on all six guest ports. Robust
owner recovery and priority inheritance remain separate work.

### 2026-09-23 follow-up: bounded scheduler growth

Decision: use a 64-entry XSCore TCB table, a 64-bit per-CPU ready bitmap, and
a 64-record libc pthread pool. Keep the table fixed-size for now rather than
introducing heap-backed scheduler metadata during this compatibility step.
Require a 24-simultaneous-worker TLS/join regression on all six architectures.

Keep x86_32 `USER_MAX_MMAP_PAGES` at 128 (512 KiB): larger caps reproduced an
early supervisor page fault. Use 16 KiB stacks in the current worker scaling
probe and defer a higher i386 mmap limit until its fixed page-record storage
can be replaced safely. This decision improves tested concurrency but does not
assert full POSIX pthread semantics or upstream desktop compatibility.

The x86_32 single-ISO UEFI test runs with 256 MiB of QEMU memory. The earlier
128 MiB test configuration timed out before GRUB/kernel serial handoff; 256 MiB
passed UEFI and BIOS login checks (BIOS retained its 128 MiB smoke-test
configuration) without changing the kernel's separate 128-page per-process
mmap limit.

### 2026-09-23 follow-up: block instead of retrying on an empty run queue

Decision: `xscore_thread_block()` must preserve BLOCKED state while there is
no READY thread. Recheck the queue and current state with local interrupts
masked; switch to a peer if present, otherwise enable interrupts atomically
with an architecture idle instruction and rescan after the IRQ. Timed futex
durations round up to a representable timer-counter tick.

Reason: undoing BLOCKED and returning immediately made condition/semaphore
wait loops consume a CPU when they had no runnable peer. The existing timeout
conversion rounds up to a representable tick; four 1 ms relative waits now
regress that behavior on all six live guests. The compositor/client matrix, x86_64
desktop-readiness gate, and x86_32 UEFI/BIOS ISO boot gate pass. This remains
cooperative scheduling on ARM/RISC-V, not full POSIX or upstream desktop
compatibility.

## Package Manager Architecture & Mirror Protocol (2026-11-02)

Context:
akaOS previously bundled software exclusively at build time into a monolithic `initramfs.img` using `tools/mkinitramfs.py`. A native runtime package management solution was needed to allow users to install, upgrade, and remove modular software packages over the network without rebuilding the entire OS image.

Decision:
1. Implement `/bin/pkg` as a native freestanding C utility in `userspace/bin/pkg/main.c` using akaOS libc socket and filesystem interfaces.
2. Mirror Protocol: Use standard HTTP/1.0 over TCP port 80 to communicate with an external web server mirror (configured by default to `http://mirror.zyqu.pl/akaos`). HTTPS is avoided at this stage due to the absence of a resident TLS/crypto stack in the micro-libc.
3. Package Archive Format (`.akp`): Use POSIX `ustar` tar archives. Each package includes a root `.PKGINFO` metadata manifest, optional `CONTROL/postinst` and `CONTROL/prerm` lifecycle scripts, and normalized payload files under `usr/`, `etc/`, or `bin/`.
4. Security & Safety:
   - Path Traversal: Path sanitization strictly strips leading slashes and resolves relative segments, preventing directory escape attacks via `..`.
   - Integrity: Integrated an in-binary SHA-256 digest engine. The repository index (`packages.db.txt`) publishes file sizes and SHA-256 hashes, verified before extraction.
   - Atomicity: File downloads stream to temporary `.part` files, and database state updates write to `.tmp` files before an atomic `rename()`.
5. Dependency Resolution: Recursive multi-package resolution with topological sort and cycle detection (up to 32 recursion levels) before commencing downloads.
6. Host Tooling: Provided `tools/aka-makepkg.py` to package compiled binaries into `.akp` archives and generate deduplicated `packages.db.txt` repository indices for Apache2 servers.
