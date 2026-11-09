## 2026-11-09 (eighty-second continuation) — Variable Attribute Declaration Builtin: declare / typeset

akaOS now features a full-featured `declare` and `typeset` builtin:
1. **Variable Attribute Declaration Builtin (`declare` / `typeset`):**
   - Syntax: `declare [-aAfFgiIlnrtux] [-p] [name[=value] ...]`
   - Attributes: readonly, integer, lowercase, uppercase, export, nameref, function inspect.
   - Staged across build systems, manual index database, and kernel self-test suite.

## 2026-11-09 (eighty-first continuation) — Data Recovery Log Tool: ddrescuelog

akaOS now features a basic `/bin/ddrescuelog` utility:
1. **Data Recovery Log Tool (`/bin/ddrescuelog`):**
   - Syntax: `ddrescuelog [options] [mapfile]`
   - Supports: `-d, --delete-if-done`, `-D, --done-status`, `-h, --help`, `-V, --version`.
   - Staged across build systems, manual index database, and kernel self-test suite.

## 2026-11-08 (eightieth continuation) — Data Recovery Tool: ddrescue

akaOS now features a basic `/bin/ddrescue` utility:
1. **Data Recovery Tool (`/bin/ddrescue`):**
   - Syntax: `ddrescue [options] infile outfile [mapfile]`
   - Supports: `-b, --sector-size`, `-f, --force`, `-v, --verbose`, `-h, --help`, `-V, --version`.
   - Mapfile output and rescue logging.
   - Staged across build systems, manual index database, and kernel self-test suite.

## 2026-11-08 (seventy-ninth continuation) — Data Duplicator: dd

akaOS now features a basic `/bin/dd` utility:
1. **Data Duplicator (`/bin/dd`):**
   - Syntax: `dd [operands]`
   - Supports: `if`, `of`, `bs`, `count`, `skip`, `seek`.
   - Staged across build systems, manual index database, and kernel self-test suite.

## 2026-11-08 (seventy-eighth continuation) — Desk Calculator: dc

akaOS now features a basic RPN `/bin/dc` utility:
1. **Desk Calculator (`/bin/dc`):**
   - Syntax: `dc [options] [files]`
   - Supports: Basic RPN operations (+, -, *, /), stack control (p, q).
   - Staged across build systems, manual index database, and kernel self-test suite.

## 2026-11-08 (seventy-seventh continuation) — Time & Date Utility: date

akaOS now features a full-featured `/bin/date` utility:
1. **Time & Date Utility (`/bin/date`):**
   - Syntax: `date [OPTION]... [+FORMAT]`
   - Supports: `-d, --date`, `-f, --file`, `-I, --iso-8601`, `-r, --reference`, `-R, --rfc-email`, `--rfc-3339`, `-s, --set`, `-u, --utc`.
   - Staged across build systems, manual index database, and kernel self-test suite.

## 2026-11-08 (seventy-sixth continuation) — Column Division Utility: cut (Full Options & Output Delimiter)

akaOS now features a full-featured `/bin/cut` utility:
1. **Column Division Utility (`/bin/cut`):**
   - Syntax: `cut [OPTION]... [FILE]...`
   - Full option suite: `-b`, `-c`, `-d`, `-f`, `-n`, `-s`, `--output-delimiter=STRING`.
   - Staged across build systems, manual index database, and kernel self-test suite.

## 2026-11-08 (seventy-fifth continuation) — Data Transfer Client: curl (Enhanced Option Suite)

akaOS now features an enhanced `/bin/curl` data transfer utility:
1. **Data Transfer Client (`/bin/curl`):**
   - Syntax: `curl [options] [URL...]`
   - Full option suite: `-A`, `-b`, `-c`, `--compressed`, `-d`, `-f`, `-F`, `-H`, `-i`, `-I`, `-k`, `-L`, `-m`, `-o`, `-O`, `-s`, `-S`, `-T`, `-u`, `-v`, `-w`, `-x`, `--help`, `--version`.
   - Staged across build systems, manual index database, and kernel self-test suite.

## 2026-11-08 (seventy-third continuation) — Scheduled Command Daemon: cron

akaOS now features a full-featured `/bin/cron` daemon:
1. **Scheduled Command Daemon (`/bin/cron`):**
   - Syntax: `cron [-n | -p | -s | -m<mailcommand>] [-x flags]`
   - Background daemon and foreground modes supported.
   - Comprehensive crontab expression evaluator with step/range/wildcard support.
   - Staged across build systems, manual index database, and kernel self-test suite.

## 2026-11-08 (sixty-ninth continuation) — Loop Continuation Shell Builtin: continue (POSIX special builtin)

akaOS now features a full-featured `continue` shell builtin and standalone wrapper:
1. **Loop Continuation Builtin (`continue [n]`):**
   - Syntax: `continue [n]`
   - Resumes next iteration of Nth enclosing loop.
   - Handled directly within `akash` statement evaluator and loop engines.
   - Staged across build systems, manual index database, and kernel self-test suite.

## 2026-11-08 (sixty-eighth continuation) — Command Execution Shell Builtin & Utility: command (Full POSIX options)

akaOS now features a full-featured `command` shell builtin and standalone utility:
1. **Command Execution Builtin & Utility (`/bin/command` & `akash` builtin):**
   - Syntax: `command [-pVv] command [arguments ...]`
   - Executes commands bypassing shell functions.
   - Supports `-p` (standard default PATH), `-v` (name/path identifier), `-V` (detailed description).
   - Staged across build systems, manual index database, and kernel self-test suite.

## 2026-11-08 (sixty-sixth continuation) — Compare Sorted Files Utility: comm (Full POSIX options)

akaOS now features a full-featured `/bin/comm` utility:
1. **Compare Sorted Files Utility (`/bin/comm`):**
   - Syntax: `comm [options]... File1 File2`
   - Compares two sorted files line by line, generating 3 columns of comparison output.
   - Column suppression: `-1`, `-2`, `-3` with proper delimiter padding.
   - Custom delimiters: `--output-delimiter=STR`.
   - Staged across build systems, manual index database, and kernel self-test suite.

## 2026-11-07 (sixty-fifth continuation) — Compare Two Files Utility: cmp (Full GNU/POSIX options)

akaOS now features a full-featured `/bin/cmp` utility:
1. **Compare Files Utility (`/bin/cmp`):**
   - Syntax: `cmp options... FromFile [ToFile]`
   - Detailed byte & line reporting, octal byte list mode (`-l`, `--verbose`), character notation (`-c`, `--print-chars`).
   - Initial byte skipping (`--ignore-initial=BYTES`).
   - Silent status-only mode (`-s`, `--silent`, `--quiet`).
   - Staged across build systems, manual index database, and kernel self-test suite.

## 2026-11-07 (sixty-fourth continuation) — Pull Request Workflow & Hyprland Integration Roadmap

1. **Pull Request Protocol & Branch Protection**:
   - The repository enforces protected branch policies on `main`.
   - All development occurs on isolated feature branches (`feat/...`).
   - Single-Atomic-Version-Bump rule: Versions in `sdk/include/akaos/version.h` are updated in a single atomic commit at the end of the feature branch before opening the PR.
   - Post-merge branch cleanup: Upon merge, feature branches are safely deleted locally and on GitHub to maintain a clean linear history.
   - All Git operations must execute strictly on the Windows host (`cmd.exe` / `pwsh`).

2. **Hyprland Native Integration Target**:
   - Preparing standalone binary packaging for Hyprland and `hyprctl`.
   - Native integration with Lumen and `wayland-bridge`, zero config files required.

## 2026-11-07 (sixty-fourth continuation) — Clear Console Screen Utility: clear_console

akaOS now features the terminal and scrollback buffer clearer `/bin/clear_console`:
1. **Console Clearer Utility (`/bin/clear_console`):**
   - Syntax: `clear_console [option]`
   - Emits `\033[3J\033[H\033[2J` to clear screen buffer, scrollback history, and home cursor.
   - Supports `-q, --quiet`, `-h, --help`, `-V, --version`.
   - Staged across build systems, manual index database, and kernel self-test suite.

## 2026-11-07 (sixty-second continuation) — CRC Checksum Utility: cksum

akaOS now features the POSIX.2 compliant `/bin/cksum` utility:
1. **CRC Checksum Utility (`/bin/cksum`):**
   - Syntax: `cksum [Option]... [File]...`
   - Computes cyclic redundancy check (CRC-32) and prints file length in bytes along with filename.
   - Fully compliant with the POSIX.2 specification.
   - Staged across build systems, manual index database, and kernel self-test suite.

## 2026-11-07 (sixty-first continuation) — System Services Runlevel Manager: chkconfig

akaOS now features the runlevel service configuration manager `/bin/chkconfig`:
1. **Runlevel Manager (`/bin/chkconfig`):**
   - Syntax: `chkconfig --list [name]`, `chkconfig --add name`, `chkconfig --del name`, `chkconfig [--level levels] name <on|off|reset>`, `chkconfig [--level levels] name`
   - Directly manages `/etc/init.d` and `/etc/rc[0-6].d` service symlinks.
   - Parses default runlevels and execution priorities from LSB-compatible header `# chkconfig: <levels> <start_prio> <stop_prio>`.
   - Staged across build systems, manual index database, and kernel self-test suite.

## 2026-11-07 (fifty-ninth continuation) — Batch Password Updater: chpasswd

akaOS now features the batch password updater `/bin/chpasswd`:
1. **Batch Password Updater (`/bin/chpasswd`):**
   - Syntax: `chpasswd [Options...]`
   - Updates user passwords from `user_name:password` pairs provided on standard input.
   - Encryption methods: DES, MD5, NONE, SHA256, SHA512 via `-c` or `-m`.
   - Pre-encrypted input mode via `-e, --encrypted`.
   - Alternate chroot environment target via `-R, --root CHROOT_DIR`.
   - Configurable SHA rounds via `-s, --sha-rounds ROUNDS` (1000..999,999,999).
   - Staged across build systems, manual index database, and kernel self-test suite.

## 2026-11-07 (fifty-eighth continuation) — File Owner and Group Modifier: chown

akaOS now features a full-featured `/bin/chown` utility:
1. **File Owner and Group Modifier (`/bin/chown`):**
   - Syntax: `chown [Options]... [OWNER][:[GROUP]] File...`
   - Supports user and group resolution by name or numeric ID.
   - Supports implied login group on `OWNER:` and group-only modification on `:GROUP`.
   - Supports `--reference=FILE` to copy ownership from a reference file.
   - Diagnostic options: `-v` (`--verbose`), `-c` (`--changes`), `-f` (`--silent`).
   - Traversal options: `-R` (`--recursive`), `--dereference`, `-h` (`--no-dereference`).

## 2026-11-07 (fifty-seventh continuation) — Access Permissions Modifier: chmod

akaOS now features a full-featured `/bin/chmod` utility:
1. **Access Permissions Modifier (`/bin/chmod`):**
   - Syntax: `chmod [Options]... Mode [,Mode]... file...`
   - Supports octal modes (e.g. 755, 644) and symbolic modes (`ugoa` with `+-=` and `rwxXst`).
   - Supports `--reference=RFile` to clone permissions from an existing file.
   - Diagnostic options: `-v` (`--verbose`), `-c` (`--changes`), `-f` (`--silent`).
   - Recursive mode: `-R` (`--recursive`).

## 2026-11-06 (fifty-sixth continuation) — Group Ownership Modifier: chgrp

akaOS now features the group ownership modifier `chgrp`:
1. **Group Ownership Modifier (`/bin/chgrp`):**
   - Syntax: `chgrp [Options]... {Group | --reference=File} File...`
   - Group resolution by name or numeric ID, and reference file support (`--reference=FILE`).
   - Reporting controls: `-c` (changes only), `-v` (verbose diagnostic for every file), `-f` (quiet).
   - Hierarchy recursion: `-R` (`--recursive`).
   - Staged across build systems, manual index database, and kernel self-test suite.

## 2026-11-06 (fifty-fifth continuation) — File Attributes Modifier: chattr

akaOS now features the file attributes modifier `chattr`:
1. **File Attributes Modifier (`/bin/chattr`):**
   - Syntax: `chattr [ -RVf ] [ -v version ] [ mode ] files...`
   - Attributes: `a` (append), `A` (noatime), `c` (compress), `d` (nodump), `D` (dirsync), `e` (extents), `i` (immutable), `j` (journal), `s` (secure rm), `S` (sync), `t` (notail), `T` (topdir), `u` (undelete).
   - Verbose mode `-V` formatting 22-position standard attribute representations.
   - Recursive directory descent (`-R`), silent mode (`-f`), and version configuration (`-v`).
   - Staged across build scripts, whatis manual index, and kernel self-test suite.

## 2026-11-06 (fifty-fourth continuation) — Disk Partition Table Manipulator: cfdisk

akaOS now features the curses-based disk partition table manipulator `cfdisk`:
1. **Partition Table Manipulator (`/bin/cfdisk`):**
   - Syntax: `cfdisk [ -agvz ] [ -c cylinders ] [ -h heads ] [ -s sectors-per-track ] [ -P opt ] [ device ]`
   - Supports MBR partition inspection, bootable flag toggles, partition deletion, and geometry override.
   - Non-interactive table formatting via `-P r`, `-P s`, and `-P t`.
   - Reverse video and arrow-cursor navigation modes.
   - Full integration into build systems, whatis manual index, and kernel test suite.

## 2026-11-06 (fifty-third continuation) — Change Directory Builtin: cd

akaOS now features a full-featured `cd` directory navigation builtin:
1. **Change Directory Builtin (`cd`):**
   - Syntax: `cd [Options] [Directory]`
   - Target defaults to `$HOME` when omitted.
   - Directory toggle `cd -` using `$OLDPWD` with target directory echo.
   - Search paths via `CDPATH` environment variable for unqualified relative paths.
   - Physical and logical navigation flags: `-P` and `-L`.
   - Restricted shell enforcement and built-in help via `help cd`.
   - Standalone executable wrapper deployed at `/bin/cd`.

## 2026-11-06 (fifty-first continuation) — Concatenate and Print Utility: cat (Full GNU/POSIX options)

akaOS now features a full-featured native `/bin/cat` command:
1. **Concatenate Utility (`/bin/cat`):**
   - Syntax: `cat [Options] [File]...`
   - Line numbering: all lines (`-n`, `--number`), non-blank only (`-b`, `--number-nonblank`).
   - Squeeze empty lines (`-s`, `--squeeze-blank`).
   - Special markers: line ends `$` (`-E`, `--show-ends`), tabs `^I` (`-T`, `--show-tabs`).
   - Caret & meta notation for non-printing characters (`-v`, `--show-nonprinting`, `-e`, `-t`, `-A`, `--show-all`).
   - Fast block-buffered pass-through when no transformations are active.

## 2026-11-05 (fifty-second continuation) — Conditional Pattern Matching Statement: case, esac

akaOS now features the shell conditional pattern matching statement `case ... in ... esac`:
1. **Conditional Statement (`case`):**
   - Syntax: `case WORD in [ [(] PATTERN [| PATTERN]...) COMMANDS ;;]... esac`
   - Pattern matching with wildcards and pipe alternation.
   - Built into `akash`, `bash`, and deployed as standalone `/bin/case` executable.
   - Built-in documentation available via `help case`.

## 2026-11-05 (fifty-second continuation) — Subroutine Caller Context Builtin: caller

akaOS now features the subroutine caller context inspection command `caller`:
1. **Subroutine Caller Builtin (`caller`):**
   - Syntax: `caller [expr]`
   - Returns line number, subroutine name, and source filename of calling frames.
   - Subroutine call execution support for shell functions and `source` / `.` scripts.
   - Built into `akash`, `bash`, and deployed as standalone `/bin/caller` executable.
   - Built-in documentation available via `help caller`.

## 2026-11-05 (fifty-first continuation) — Calendar Display Utility: cal, ncal

akaOS now features native `/bin/cal` and `/bin/ncal` calendar display utilities:
1. **Calendar Display Utility (`/bin/cal`, `/bin/ncal`):**
   - Syntax: `cal [-smjy13] [[[day] month] year]`, `ncal [-smjy13] [[[day] month] year]`
   - Formats single month (`-1`), 3-month horizontal (`-3`), full year grid (`-y`), or vertical weekdays (`ncal`).
   - Supports Monday/Sunday start options (`-m` / `-s`) and Julian date numbering (`-j`).

## 2026-11-04 (fiftieth continuation) — Block-Sorting File Compressor: bzip2, bunzip2, bzcat

akaOS now features the upstream bzip2 1.0.8 suite:
1. **Block-Sorting Compressor (`/bin/bzip2`, `/bin/bunzip2`, `/bin/bzcat`):**
   - Syntax: `bzip2 [options] [filenames ...]`, `bunzip2 [options] [filenames ...]`, `bzcat [options] [filenames ...]`
   - Burrows-Wheeler block sorting compression algorithm and Huffman coding.
   - Compress (`-z`), decompress (`-d`), test integrity (`-t`), stdout (`-c`), force (`-f`), keep (`-k`), small memory mode (`-s`), quiet (`-q`), verbose (`-v`), block size levels (`-1` through `-9`).
   - Extended libc with `<sys/times.h>` and `fchown()`.

## 2026-11-04 (forty-ninth continuation) — Shell Builtin Runner: builtin

akaOS now features the shell builtin execution command `builtin`:
1. **Shell Builtin Runner (`builtin`):**
   - Syntax: `builtin [shell-builtin [args...]]`
   - Executes shell builtins directly, bypassing function lookup or external binary resolution.
   - Built into `akash`, `bash`, and available as standalone `/bin/builtin` executable wrapper.
   - Built-in documentation available via `help builtin` and `help [b]uiltin`.

## 2026-11-04 (forty-eighth continuation) — Shell Loop Break Command: break

akaOS now features the shell loop termination command `break`:
1. **Loop Termination Command (`break`):**
   - Syntax: `break [n]`
   - Exits from enclosing `for`, `while`, `until`, or `select` loops.
   - When `n` is supplied, exits the `n`th enclosing loop level (`n >= 1`).
   - Built into `akash`, `bash`, and deployed as standalone `/bin/break` executable.
   - Built-in documentation available via `help break`.

## 2026-11-04 (forty-seventh continuation) — Readline Key and Function Bindings Command: bind

akaOS now features a native Readline key and function bindings utility `/bin/bind`:
1. **Readline Binding Utility (`/bin/bind`):**
   - Syntax: `bind [-m keymap] [-lpsvPSVX] [-q func] [-u func] [-r keyseq] [-x keyseq:cmd] [-f file] [keyseq:func]`
   - Complete inspection commands: `-l` (lists all 80+ standard functions), `-P` / `-p` (prints current bindings), `-V` / `-v` (prints readline variables), `-S` / `-s` (prints macro strings), `-X` (prints command sequences).
   - Dynamic binding mutations: query (`-q`), unbind (`-u`), remove (`-r`), shell command binding (`-x`), and file parsing (`-f filename` in `.inputrc` syntax).
   - Available as standalone utility `/bin/bind` and built into `akash` and `bash`.

## 2026-11-04 (forty-sixth continuation) — Job Control Background Command: bg

akaOS now features full job control background command `bg` support:
1. **Job Control Command (`bg`):**
   - Syntax: `bg [job_id...]`
   - Resumes stopped process pipelines in the background using POSIX `SIGCONT`.
   - Supports job identifiers (`%1`, `1`) or targets the most recent job.
   - Built into both `akash` and `bash`, and available as `/bin/bg` standalone wrapper.
   - Built-in documentation available via `bg --help` and `help bg`.

## 2026-11-04 (forty-fifth continuation) — Arbitrary Precision Calculator Language: bc

akaOS now features a native Ring 3 `/bin/bc` calculator:
1. **Arbitrary Precision Calculator (`/bin/bc`):**
   - Syntax: `bc [options] [file ...]`
   - Evaluation engine supporting standard arithmetic (`+`, `-`, `*`, `/`, `%`), powers (`^`), comparisons, and logic.
   - Built-in functions: `sqrt()`, `length()`, `scale()`, and math library `-l` functions (`s()`, `c()`, `l()`, `e()`).
   - Precision scaling (`scale=N`), variables, pre/post increment and decrement (`++`, `--`).
   - User function definitions: `define func(x) { return expr; }`.
   - String literals, comments (`/* */`, `#`), and interactive execution.

## 2026-11-04 (forty-fourth continuation) — GNU Bourne-Again SHell Compatibility: bash, sh, rbash

akaOS now features native `/bin/bash`, `/bin/sh`, and `/bin/rbash` command language interpreters:
1. **Bourne-Again Shell (`/bin/bash`, `/bin/sh`, `/bin/rbash`):**
   - Syntax: `bash [options] [file]`
   - Invocation options: `-c string` (with custom positional parameters), `-s` (read stdin), `-i` (interactive), `-l` / `--login` (login shell), `-r` / `--restricted` (restricted shell), `--posix`, `--verbose`, `-v`.
   - Startup sequence: evaluates system `/etc/profile`, user personal `~/.bash_profile` / `~/.profile`, and interactive `~/.bashrc` (`--rcfile`), with override flags `--noprofile` and `--norc`.
   - Restricted mode (`rbash`): enforces restricted execution by prohibiting directory changes via `cd` and preventing reassignment of sensitive environment variables (`PATH`, `SHELL`, `ENV`, `BASH_ENV`).
   - Information options: `--version` and `--help`.

## 2026-11-04 (forty-third continuation) — Base64 Encoding and Decoding Utility: base64

akaOS now features a native Ring 3 `/bin/base64` command:
1. **Base64 Encoding/Decoding Utility (`/bin/base64`):**
   - Syntax: `base64 [OPTION]... [FILE]`
   - Encodes and decodes arbitrary binary and text streams according to RFC 4648.
   - Decodes with `-d` / `--decode`, supports ignoring non-alphabet garbage bytes with `-i` / `--ignore-garbage`.
   - Formats encoded lines with configurable column wrapping via `-w COLS` / `--wrap=COLS` (default 76, 0 disables wrapping).
   - Reads from file or standard input when file is omitted or `-`.

# Current akaOS State

## 2026-11-04 (forty-second continuation) — Base32 Encoding and Decoding Utility: base32

akaOS now features a native Ring 3 `/bin/base32` command:
1. **Base32 Encoding/Decoding Utility (`/bin/base32`):**
   - Syntax: `base32 [OPTION]... [FILE]`
   - Encodes and decodes arbitrary binary and text streams according to RFC 4648.
   - Decodes with `-d` / `--decode`, supports ignoring non-alphabet garbage bytes with `-i` / `--ignore-garbage`.
   - Formats encoded lines with configurable column wrapping via `-w COLS` / `--wrap=COLS` (default 76, 0 disables wrapping).
   - Reads from file or standard input when file is omitted or `-`.

## 2026-11-04 (forty-first continuation) — Path Stripping Utility: basename

akaOS now features a native Ring 3 `/bin/basename` command:
1. **Path Stripping Utility (`/bin/basename`):**
   - Syntax: `basename NAME [SUFFIX]` and `basename OPTION... NAME...`
   - Strips leading directory components from pathnames.
   - Strips trailing suffix if given and distinct from the full basename.
   - Supports multiple arguments via `-a` / `--multiple`, suffix stripping via `-s` / `--suffix`, and NUL output delimiter via `-z` / `--zero`.
   - Handles edge cases: root directory (`/`, `///`), empty paths, trailing slashes.

## 2026-11-03 (fortieth continuation) — Pattern Scanning and Processing Language: awk (gawk)

akaOS now features a native Ring 3 `/bin/awk` and `/bin/gawk` text processing interpreter:
1. **AWK Language Engine (`/bin/awk`, `/bin/gawk`):**
   - Syntax: `awk [options] 'program' [file ...]` and `awk -f file [options] [file ...]`.
   - Rules: `BEGIN` blocks, `END` blocks, regular expressions (`/pattern/`), relational/comparison operators (`==`, `!=`, `>`, `<`, `>=`, `<=`), and action blocks (`{ ... }`).
   - Fields & delimiters: automatically parses `$0` through `$NF`, $(NF-n) with configurable field separator (`-F`, `--field-separator`, `FS`), record separator (`RS`), and output formatting (`OFS`, `ORS`).
   - Built-ins & arithmetic: `NR`, `NF`, `FNR`, `FILENAME`, `length()`, `+=`, `++`, variable assignment via `-v VAR=VAL`.
   - Formatted multi-field `print` statements with `OFS` separation.

## 2026-11-03 (thirty-seventh continuation) — Spell Checker Utility: aspell

akaOS now features a native Ring 3 `/bin/aspell` spell checker and system dictionary database:
1. **Spell Checker Utility (`/bin/aspell`):**
   - Syntax: `aspell check [options] filename`
   - Suggestion modes: `ultra`, `fast`, `normal`, and `bad-spellers` using dynamic Levenshtein distance calculations.
   - Dictionary selection: `-d name` or `-master=name` loading `/usr/share/aspell/<name>.dict` (e.g. `en`, `en_US`).
   - Filters & options: `-mode=mode`, `--dont-backup`, `-encoding=name`, `-keymapping=name`.
   - Dual interface: non-interactive batch analysis for automated scripts and terminal UI for interactive sessions.
2. **System Dictionaries (`/usr/share/aspell/`):**
   - Packaged standard English dictionaries `en.dict` and `en_US.dict`.

## 2026-11-03 (thirty-sixth continuation) — Manual Page Keyword Lookup Utility: apropos

akaOS now features a native Ring 3 `/bin/apropos` utility and an integrated system whatis database:
1. **Manual Page Keyword Search (`/bin/apropos`)**:
   - Syntax: `apropos [OPTION...] KEYWORD...`
   - Modes: regular expression matching (default, `-r`, `--regex`), exact keyword matching (`-e`, `--exact`), and wildcard matching (`-w`, `--wildcard`).
   - Conjunction: `-a`, `--and` to require all specified keywords to match.
   - Section filtering: `-s`, `--sections`, `--section` (colon/comma lists with prefix expansion like section 3 for 3perl/3x).
   - Format: `%-20s (%s) - %s` truncated to terminal width (calculated via `$MANWIDTH`, `$COLUMNS`, `ioctl(TIOCGWINSZ)`, or 80 columns fallback; disabled via `-l`, `--long`).
   - Hierarchies: searches `/usr/share/man/whatis`, `/var/cache/man/whatis`, and `$MANPATH`. Supports alternate systems via `-m`, `--systems` / `$SYSTEM`.
   - Exit codes: 0 for success, 1 for syntax/usage error, 2 for operational error, 16 for no matches.
2. **Default System Database (`/usr/share/man/whatis`)**:
   - Curated database covering 115+ commands, POSIX utilities, daemons, and shell builtins.

## 2026-11-03 (thirty-third continuation) — Shell Built-in Alias Engine: alias and unalias

akash (`userspace/bin/akash/main.c`) now includes a full-featured alias subsystem:
1. **Alias Management (`alias`)**:
   - Syntax: `alias [-p] [name[=value] ...]`
   - Defines word replacements for the first word of simple commands.
   - Without arguments or with `-p`, prints all definitions in reusable form (`alias NAME='VALUE'`).
   - Querying: `alias name` prints the current definition or reports not found with exit code 1.
   - Quoting rules: Single quotes preserve literal strings evaluated at invocation; double quotes evaluate variables at definition time.
   - Reserved names: Cannot alias `alias` or `unalias`, and alias names cannot contain `=`.
2. **Unalias Command (`unalias`)**:
   - Syntax: `unalias [-a] [name ... ]`
   - Removes defined aliases individually or clears all aliases using `-a`.
3. **Execution & Chaining**:
   - If an alias value ends with a trailing space or tab character, the following command word is recursively tested for alias substitution.
   - Recursion protection ensures identical alias names are not expanded in an infinite loop.
   - Interactive login sessions automatically source `~/.bash_aliases` if present.

## 2026-11-03 (thirty-first continuation) — Shell Job Control Enhancements: bg, fg, jobs, and kill %jobspec

akaOS now features a fully modernized job control subsystem in `akash` (`userspace/bin/akash/main.c`):
1. **Background Job Control (`bg`)**:
   - Syntax: `bg [job_id...]`
   - Resumes stopped jobs in the background executing simultaneously with the shell, without terminal stdin access.
   - Defaults to the shell's notion of current job when no job specification is provided.
   - Supports multiple arguments (e.g. `bg %1 %2`).
   - Supports `bg --help` and detailed shell syntax description via `help bg`.
2. **Foreground Job Control (`fg`)**:
   - Syntax: `fg [job_id]`
   - Brings a job to the foreground, restoring terminal ownership (`tcsetpgrp`) and resuming paused processes (`SIGCONT`).
   - Supports jobspec resolution (`%n`, `%%`, `%+`, `%-`, `%prefix`) and defaults to the current job.
3. **Job Status Tracking (`jobs`)**:
   - Displays all background and stopped jobs with standard job control indicators: `+` for current job, `-` for previous job.
   - Supports `-l` (process IDs) and `-p` (PIDs only).
4. **Shell Built-in `kill`**:
   - Supports sending signals to jobs by jobspec (e.g. `kill %2`, `kill -9 %1`, `kill -TERM %+`).
   - Resolves job process groups and delivers signals across all stage processes.

## 2026-11-02 (seventh continuation) — Native Package Manager (pkg) & Remote Mirror Integration

akaOS now features a native Ring 3 package management system (`/bin/pkg`) capable
of querying, downloading, verifying, installing, upgrading, and removing modular software
packages from an external HTTP mirror server (`http://mirror.zyqu.pl/akaos`).

1. **Native Package Manager Utility (`/bin/pkg`)**:
   - Location: `userspace/bin/pkg/main.c`, linked statically with the akaOS libc.
   - Operations:
     - `pkg update`: Synchronizes the architecture package repository index (`packages.db.txt`)
       via HTTP/1.0 and stores it in `/var/lib/pkg/packages.db`.
     - `pkg install <package>`: Recursively resolves package dependencies, fetches `.akp`
       archives into `/var/cache/pkg/`, validates cryptographic SHA-256 integrity, unpacks
       payloads safely into root, executes post-install hooks, and registers files in `/var/lib/pkg/`.
     - `pkg upgrade`: Compares installed package versions against repository index versions using
       standard numeric/alphanumeric comparison, downloading and upgrading outdated packages.
     - `pkg remove <package>`: Runs pre-removal hooks, cleans up all files tracked in
       `/var/lib/pkg/files/<package>.list`, and purges metadata from `installed.db`.
     - `pkg list`, `pkg search`, `pkg info`: Queries installed and available package metadata.
   - Resilience & Security:
     - Downloads stream to `.part` files and commit atomically via `rename()`.
     - Path traversal protection (`sanitize_destination_path`) strips `..` escaping sequences.
     - Embedded self-contained SHA-256 digest engine validates archives against mirror manifests.

2. **Configuration & Directory Layout**:
   - Configuration: `/etc/pkg.conf` specifying mirror URL (`http://mirror.zyqu.pl/akaos`),
     architecture (`x86_64`), cache directory (`/var/cache/pkg`), and database directory (`/var/lib/pkg`).
   - Initramfs integration: `/bin/pkg`, `/etc/pkg.conf`, `/var/lib/pkg`, and `/var/cache/pkg`
     are pre-staged into the base initramfs image by `tools/mkinitramfs.py` and `build.sh`.

3. **Host Packaging & Index Generator (`tools/aka-makepkg.py`)**:
   - Supports creating `.akp` POSIX ustar packages from staged root directories.
   - Automatically generates `.PKGINFO` metadata and packages optional `CONTROL/postinst` and
     `CONTROL/prerm` lifecycle scripts.
   - Generates and deduplicates `packages.db.txt` repository indices with file sizes and SHA-256 digests.

4. **Verification & Cross-Platform Support**:
   - `tools/test-pkg.py` automated test suite validates all 7 major functions (update, search/info,
     recursive dependency install, list, upgrade, SHA-256 tampering rejection, remove with hooks)
     against a live simulated HTTP mirror server with 100% pass rate.
   - Cross-compiled cleanly with `-Wall -Wextra -Werror` on all 6 akaOS supported architectures:
     `x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, and `riscv32`.

## 2026-10-31 (sixth continuation) — Complete Unmodified Upstream LXDE Stack Verified Live (Openbox, LXPanel, PCManFM, LXSession, LXMenu-Data)

The complete unmodified upstream LXDE desktop environment is now built, staged,
and verified running live in akaOS:

1. **Components Built from Unmodified Upstream Tarballs**:
   - **Openbox 3.6.1**: Window manager with full root composite window painting.
   - **LXPanel 0.11.1**: Desktop panel with dynamic plugins (`deskno.so`), default profiles, and menu integration.
   - **PCManFM 1.3.2**: Desktop manager managing desktop wallpaper, icons, and file management with GTK3.
   - **LXSession 0.5.5**: Session manager, settings daemon, and autostart daemon.
   - **LXDE Common 0.99.2**: Default session profiles, autostart configurations, and `startlxde` script.
   - **LXMenu Data 0.1.5**: Standard XDG menu hierarchy and desktop category specifications.

2. **Kernel & Subsystem Compatibility Fixes**:
   - **Expanded Process Table**: Increased `USER_MAX_PROCESSES` from 16 to 64 in `xscore/kernel/process.c`.
   - **Socket Options**: Implemented `SO_REUSEADDR`, `SO_REUSEPORT`, and `TCP_NODELAY` support in `xscore_socket_set_option`.
   - **VFS / Procfs Buffer**: Expanded `/proc/processes` formatted buffer to 2048 bytes in `xscore/kernel/vfs.c`.
   - **Initramfs Generator**: Updated `tools/mkinitramfs.py` to deterministically synthesize missing ancestor directories for nested paths.

3. **Live Guest QEMU Smoke Test Verification**:
   - Automated test `tools/test-upstream-wm.sh` runs Openbox, probe client, LXPanel, and PCManFM:
     `[PASS] The upstream window manager took the client window over.`
     `[PASS] The framed client window accepts drawing and reads the pixel back.`
     `[PASS] The akaOS display server routed the map request to the upstream window manager.`
     `[PASS] LXPanel connected, loaded profile and modules, and remained active.`
     `[PASS] PCManFM desktop manager started and remained active alongside Openbox and LXPanel.`

## 2026-10-30 (fifth continuation) — Openbox root composite pixel readback and unmodified upstream LXPanel verified live

The upstream Openbox window manager and LXDE panel (`lxpanel 0.11.1`) are now
verified running simultaneously live in QEMU guest testing:

1. **Root Screen Composite Readback under Openbox**:
   - `x11-wm-client` draws `0x00ff00` into a framed window.
   - Root-screen `XGetImage` at the translated window coordinates confirms
     matching `0x00ff00` pixel composited onto the root framebuffer.
   - Window viewability checks verify that both the WM frame and child window
     are mapped (`IsViewable`) before screen readback.

2. **Upstream LXPanel 0.11.1 Live Verification**:
   - Pinned, unmodified upstream release builds:
     `libXRes 1.2.2`, `libwnck 43.0`, `libfm-extra 1.3.2`, `menu-cache 1.1.0`,
     `libfm-gtk3 1.3.2`, `keybinder-3.0 0.3.2`, `lxpanel 0.11.1`.
   - Staged all required runtime dependencies into initramfs:
     - `/usr/local/bin/lxpanel` and `/usr/local/bin/lxpanelctl`
     - System profiles: `/etc/xdg/lxpanel/default` and `/etc/xdg/lxpanel/two_panels`
     - UI and image assets under `/usr/local/share/lxpanel` and `/usr/local/share/libfm`
     - Helper binaries: `/usr/local/libexec/menu-cache/menu-cached` and `menu-cache-gen`
     - Built-in dynamic plugins: `/usr/local/lib/lxpanel/plugins/*.so` (`deskno.so`)
     - LibFM modules: `/usr/local/lib/libfm/modules/*.so` (`gtk-fileprop-*`, `gtk-menu-*`, `vfs-*`)
   - Smoke test gate passes cleanly: LXPanel connects to X11 display :0, loads
     profile configuration and libfm modules without fatal errors, requests window
     mapping, and stays active in the process table.

3. **Core Utility & Build Pipeline Fixes**:
   - Fixed `userspace/bin/grep`, `head`, `tail`, `wc`, `uname`, `whoami`, `su`
     passing raw integer file descriptors (e.g. `STDERR_FILENO`) to `fprintf()` instead
     of `FILE *stderr`.
   - Enhanced `grep` to support standard options (`-q`, `-s`, `-v`, `-i`, `-n`).
   - Integrated `grep` compilation and staging into `tools/rebuild-xserver-image.sh`.
   - Cleaned up temporary debug traces in `userspace/bin/xserver/dispatch.inc` and `window.c`.

## 2026-10-27 (third continuation) — the LXDE dependency stack: libXres, libwnck and libfm

The upstream LXDE panel is the next desktop program on the native X server,
and its `configure` script requires two dependency stacks that akaOS did not
have: `libwnck-3.0` (the window navigator used by the taskbar) and
`libfm-gtk3` (the file-management widgets).  Both are being built from
unmodified upstream release tarballs.

### libXres

`tools/build-upstream-x11.sh` now builds `libXres 1.2.2` with a pinned
SHA-256 and publishes `libXRes.so.1`, its headers and `xres.pc` into the
graphics sysroot.  libwnck needs it for the client resource information.

### libwnck 43.0

`tools/build-upstream-libwnck.sh` builds the upstream libwnck 43.0 from a
pinned tarball with the Meson cross file of the GTK builds.  Two build-host
details had to be solved: the GResource compiler must come from the Ubuntu
host (a staged `glib-compile-resources` and host-side copies of the GLib
package descriptions), and libwnck's graphical helper/test programs are not
part of the akaOS image, so a small reproducible build patch stops those
executables from being built.  The library, its headers and
`libwnck-3.0.pc` are now in the graphics sysroot.

### libfm, menu-cache and the BSD header

`tools/build-upstream-libfm.sh` builds the upstream chain in dependency
order: `libfm-extra` (the non-GTK part of libfm), `menu-cache 1.1.0`, and
`libfm-gtk3 1.3.2`.  The release tarballs are pinned and the packages are
installed directly into the graphics sysroot under the runtime prefix.

Two build obstacles were hit and addressed:

- menu-cache includes the historical `<sys/fcntl.h>` spelling.  The akaOS C
  library now carries that forwarding header, so portable upstream code that
  uses the BSD-POSIX spelling compiles without a source change.
- menu-cache 1.1.0 defines its shared menu tag objects as tentative
  definitions in more than one translation unit.  The build therefore uses
  `-fcommon` for that package instead of changing the upstream sources.

The build is in flight at the end of this round; the panel itself is the next
step after the three libraries are installed.

## 2026-10-27 (second continuation) — the compatibility-library audit, and the Openbox connection loss

### No upstream program links the compatibility surface

`tools/check-stub-shadowing.sh` now reports only one dependent of a
compatibility library name in the upstream staging trees
(`build/upstream-sysroot/graphics-x86_64`, the Openbox, X11, LXQt, LXPanel,
Qt and KDE work roots): `libcairo` needs `libpthread.so`.  That name carries
the runtime ABI marker and nothing else since the previous round, so no
upstream program resolves the compatibility surface before the real library.
The versionless compatibility objects (`libglib.so`, `libgio.so`,
`libgtk.so`, `libgtk-4.so`, `libQt6Core.so`, `libQt6Gui.so`,
`libQt6Widgets.so`) have no dependent at all in those trees: the upstream
programs resolve the real sonames, which the akaOS loader maps to the
upstream runtime sets when they are installed.

### The window-manager test started its client too early

The loader trace prints a line for every symbol it resolves.  Openbox
resolves thousands of symbols through the slow guest console, so the window
manager needed about a minute to reach its display connection while the test
started the probe client after twenty five seconds.  The client mapped its
window before any window manager existed, and the test reported a failure
that belonged to its own sequencing.  The test now starts the window manager
and the client from one command line (the display server takes the keyboard
away from the console) without the loader trace.

### The X11 client libraries are part of the fast image rebuild

`tools/rebuild-xserver-image.sh` staged the display server, the loader, the
C library and Openbox, but the X11 client stack was only staged by the full
`build.sh`.  A rebuilt libX11 or libxcb therefore never reached the image
during iteration.  The script now restages the X11 client libraries from the
graphics sysroot as well.

### The display server made XCB forget a reply the client was waiting for

Openbox reaches the end of `screen_annex`: it creates the support window,
sets `OPENBOX_PID`, `_NET_SUPPORTING_WM_CHECK`, the property on the support
window and the `_NET_SUPPORTED` list, and selects the root redirection.  It
then starts the KDE splash notification: a `dcop` helper process is spawned
(it does not exist and exits 1), the `_KDE_SPLASH_PROGRESS` atom is interned
and its reply arrives, and then `_XIOError` of libX11 runs with errno
`EAGAIN` before the next request reaches the display server.

The failure is not a connection close: the display server never sees EOF on
the client and does not disconnect it.  It is not an allocation failure:
`AKAOS_MALLOC_TRACE` reports no failed allocation in the process.  It is not
a failed descriptor call that the C library can see: the only failures the
descriptor trace records are tolerated `EAGAIN` results from the non-blocking
XCB receive path, plus EOF reads of regular files.  `_xcb_conn_shutdown` is
never called, so the XCB connection error flag is not set either.  A temporary
trace at the entry of `_XIOError` then named the caller:
`0x800237356e`, which the loader mapping trace resolves to `_XReply` at the
branch that reports an I/O error when `xcb_wait_for_reply64()` returns NULL
without a protocol error.

XCB had completed the request without storing its reply.  The display server
wrote the sequence number of the **window owner's** client into every event,
and that number is zero for a window of the root, so an event delivered to
Openbox could carry sequence zero.  XCB advances its request-completion
tracking from the sequence number of every packet it reads; a zero after a
request sequence of 164 wraps the tracker to the next 16-bit epoch, marks
every pending request completed, and drops the stored reply of the
`InternAtom` request for which Xlib was waiting.  The protocol requires every
event to carry the sequence number of the last request the server processed
**for the client that receives the event**.

`x_event_queue()` now stamps the sequence number of the receiving client
into every event, and also stamps the first window of the record with the
window of the delivery: the window itself for a selection on the window, the
parent for a selection on the children.  The keyboard, button and motion
events are excluded from the second stamp because their first field is a
timestamp, and the mapping notifications are excluded because they carry no
window.  That window field is what makes a window manager match a
`ReparentNotify` to the window it asked about.  The same change also fixes a
latent defect in the grabbed-key path, which queued an uninitialised event
record instead of the one it had built.

The guest proves the takeover now.  `tools/test-upstream-wm.sh` boots the
x86_64 image, starts Lumen and the display server, starts the unmodified
upstream `/usr/local/bin/openbox --sm-disable`, and then starts the
`x11-wm-client` probe.  The transcript of the passing run contains:

```
x11 wm client: mapped window 0x2000001
xserver: ... c7 request 7 units 4 seq 579          (ReparentWindow)
xserver: ... c8 event 21 seq 8                     (client ReparentNotify)
x11 wm client: reparented into frame 0x1000064 size 200x120
x11 wm client: window parent matches frame 0x1000064
x11 wm client: drew into the framed window
x11 wm client: window manager frame verified
```

The probe checks the tree parent of the window, fills a known green pixel
inside the framed window, reads it back with `XGetImage` and compares it, and
the test requires every marker.  The run has no Xlib error and no XCB
connection shutdown.  Laying that check over the tree exposed two more server
defects, both fixed: `QueryTree` reported the window's own identifier as its
parent, and `ConfigureWindow` replaced the unspecified size of a moved window
with zero and destroyed its backing store.

The temporary diagnostic hooks (`AKAOS_FD_TRACE`, `AKAOS_MALLOC_TRACE`,
`AKAOS_EXIT_TRACE` and the traces inside the locally built libX11 and libxcb)
were removed again, the two upstream libraries were rebuilt from their
unmodified sources, and the clean image was rebuilt.  The gate run on
that clean image then passed again with the same transcript, so the proof does
not depend on any diagnostic hook.

## 2026-10-27 — Openbox takes the display server, and a compatibility library stops shadowing the real GLib

The upstream Openbox window manager now reaches the point where it owns the
display server: it creates its support window, announces itself with
`_NET_SUPPORTING_WM_CHECK`, and selects the redirect of the root window.  Three
defects stood in the way, and they were found by putting a trace of the loader
into the image and matching the address of a fault against the libraries that
the guest had mapped.

### The modifier map of the keyboard was never answered

The reply of `GetModifierMapping` announces how many key codes every group of
the map holds, and a client reads exactly that many bytes for every group.  The
server announced sixteen units of data and never said how many key codes a
group held, so libX11 compared the two numbers, found that they did not agree
and refused the whole map:

```c
    if (rep.length < (INT_MAX >> 2) &&
	(rep.length >> 1) == rep.numKeyPerModifier) {
```

The empty result reached Openbox, which read the number of the key codes per
modifier out of it and read the address zero.  The map now carries the key
codes of the keys that hold the eight modifiers, and the reply says how many
key codes a group holds.

### The key map of the keyboard repeated eight bytes

The reply of `QueryKeymap` carries a bitmap of two hundred and fifty six bits,
one for every key code.  The server filled it with the first eight bytes of the
bitmap, repeated, so a client saw the state of eight key codes in the place of
the state of all of them.

### A compatibility library took the place of the real GLib

This was the defect that killed Openbox.  The image carries a family of
compatibility libraries that answer for the GLib, GTK and Qt entry points of
the upstream desktop programs.  One of them is published under the name of the
thread library, and the upstream libX11 asks for the thread library in its own
package description:

```
$ grep pthread .../usr/local/lib/pkgconfig/x11.pc
xthreadlib=-lpthread
Libs.private: -lpthread
```

A program links its libraries in the order of its own list, and the first
library that answers for an entry point wins.  Openbox therefore linked the
compatibility surface in front of the real GLib, and every GLib entry point it
called went to the compatibility library.  The event loop of the compatibility
library has no main loop of its own, and Openbox died inside it.

The library that is found under the name of the thread library now carries the
marker of the runtime ABI and nothing else.  The thread entry points come from
the C library of akaOS, which implements them, so nothing is lost, and the real
GLib of the upstream programs is reached again.

### How the fault was found

The loader of akaOS now prints the library it maps, the place it maps it at and
its size, when the variable `AKAOS_LOADER_TRACE` is set.  The address of the
fault and the address of every mapped library then name the library that
failed, and `addr2line` names the function inside it:

```
loader: mapped /usr/local/lib/libobt.so.2 at 0x80021fa000 length 77824
...
[FATAL] Page fault address (CR2): 0
[FATAL] Exception RIP: 549791469808
[FATAL] Faulting process name: openbox
```

The first fault was in `obt_keyboard_reload` of the library of Openbox, and the
second one in `g_main_loop_new` of the compatibility library, which is what
pointed at the shadowing.

## 2026-10-24 — The X11 reply stream no longer desynchronises, and Openbox survives its start up

The upstream Openbox window manager now starts against the native X11 server,
keeps running and behaves like a window manager, instead of taking the server
down with it.  The cause was a reply that announced a quarter of the data it
sent, and a set of requests that answered when they should have stayed quiet.

### `GetKeyboardMapping` announced a quarter of the keysyms it sent

The reply of `GetKeyboardMapping` carries the keysyms as additional data, and
the length field of a reply counts four byte units of that data.  One keysym
is exactly one such unit, but the server divided the number of keysyms by
four before it wrote the length.  A client that asked for two keycodes read
four bytes of keysyms, took the remaining twelve bytes of the reply for the
header of the next reply and lost the reply stream from there on.  Openbox
read the leftover bytes as an I/O failure, printed `XIO: fatal IO error` and
left, and the page fault that followed took the display server with it.

### Requests that have no reply answered anyway

Eight core requests have no reply in the protocol: `CreateGC`, `ChangeGC`,
`FreeGC`, `CreatePixmap`, `FreePixmap`, `CreateCursor`, `FreeCursor` and
`SetSelectionOwner`.  The server answered all of them with a thirty two byte
reply.  A client reads its replies in the order of the requests and matches
them by sequence number, so an answer that no request asked for is an answer
that the client cannot place, and the reply stream of the client drifts.

The upstream libX11 of this system talks to the server through XCB, which
matches a reply with the sequence number of the request that asked for it and
hands an error to the client as an event instead of as a reply.  The display
server therefore has to answer exactly the requests that expect a reply, and
nothing else.

### The text drawing requests used a layout that no client sends

The wire formats were taken from the upstream libX11 sources, which are the
reference for what a client puts on the connection.

- `PolyText8` and `PolyText16` do not carry a character count.  They carry a
  list of items, and every item holds the number of characters, the offset
  from the position of the previous item and the characters themselves.  The
  server read a single plain string instead of the item list, so no client
  could draw with the core font protocol.
- The character count of `ImageText8` and `ImageText16` travels in the data
  byte of the request header and not in the body of the request.  The server
  read it from the body and skipped three bytes that the request does not
  contain.

### The value lists of the window and of the graphics context were read without their padding

Every value of a window attribute list and of a graphics context value list is
aligned to four bytes, so a value that is one or two bytes wide is followed by
padding.  The display server read the values without the padding, so every
value after the first narrow one was read at the wrong place.

The consequence was severe.  A window manager creates its window with the
event mask among the values, and the mask it asked for was the padding of the
value before it, which is zero.  A window manager therefore never received
the property changes of its own window, and a client that waits for such a
change waits for an event that never comes.  The same defect shifted the
values of a graphics context, so the foreground colour of a graphics context
was not the colour a client asked for.

### The events of the display server carried no time

The protocol gives every event a time in milliseconds, and a client that looks
for an event with a time of its own finds nothing when the time is zero.  The
display server wrote zero into the time of the events, so a window manager
that waits for an event with a time never found one.  The events carry the
time of the display server now.

The events that report a window crossing the pointer also had their fields
in the wrong order: the time, the root and the event were swapped.

### A reply that was too long was cut short

The buffer of a socket holds eight kilobytes, and a reply of the protocol can
be larger than that: the image of a window is a quarter of a megabyte.  The
socket layer copied as much as fitted into the buffer of the peer and
answered with the number of bytes it had copied.  When the buffer of the peer
was full it copied nothing and answered with zero, and the display server
took that for the end of the answer and dropped the rest.

A client that receives half of an answer reads the next answer at the wrong
place, and a client that waits for an answer that never comes altogether
stops.  The upstream libX11 of this system reads its answers through XCB,
which hands a missing answer back as nothing at all, and the library then
reads through the missing answer and dies.

The socket layer answers with `EAGAIN` when the buffer of the peer is full
and nothing fits, so that a descriptor that is not in non blocking mode waits
for the peer to read, and the display server keeps the bytes it could not
send and sends them with the next round.

### The keyboard map was announced with the wrong number of keysyms

The reply of `GetKeyboardMapping` says how many keysyms every keycode has, and
a client reads exactly that many keysyms for every keycode it asked for.  The
display server announced two keysyms for every keycode and sent one, so the
client read half of the keysyms of the next reply and lost the reply stream
from there on.  Openbox read the keysyms of the keyboard map, took the answer
for something else, and died.

The reply of the same kind for the font, `QueryFont`, was eight bytes short:
the ascent, the descent and the number of the character informations were
missing, and the name of the font was not sent at all.

### The resources of a client stayed behind

A window that a client created stayed in the tree of the windows when the
client went away, and it kept its identifier.  The base of the identifiers
came from the place of the client in the table of the clients, so a client that
came later and took the same place was given the same identifiers, and the
server refused the window of the new client because the identifier of the old
window was still taken.  The resources of a client now go with it, and every
connection is given its own base of identifiers.

### The value lists of the window and of the graphics context were read without their padding

Every value of such a list is aligned to four bytes, and the display server
read the values without the padding, so every value after the first narrow one
was read at the wrong place.  A window that a toolkit configured therefore
kept the size it was created with, and a toolkit that drew into it drew into
the wrong place.

The same defect made the reply of `ConfigureWindow` and the request of
`FillPoly` read their values at the wrong place.

### The events of the display server carried no time

The protocol gives every event a time in milliseconds, and a client that looks
for an event with a time of its own finds nothing when the time is zero.  The
display server wrote zero into the time of the events, so a window manager
that waits for such an event waits for an event that never comes.  The events
carry the time of the display server now.

The events that report a window crossing the pointer also had their fields in
the wrong order: the time, the root and the event were swapped.

### A blocking socket did not block

The descriptor layer answered a read on a socket that had no data with
`EAGAIN`, even when the descriptor was not in non blocking mode.  A program
that opened a socket in the ordinary way therefore saw a failure instead of
waiting for the data, and a program that asked for a second piece of a
message lost the connection when the piece was not there yet.

A read on a socket now waits for the data when the descriptor blocks, and a
write waits for room in the buffer of the peer.  A descriptor in non blocking
mode still answers with `EAGAIN` right away.

### The trace of the display server filled the console

The console of the guest is slow, and the display server wrote a line for
every reply.  A client that asked for the keyboard map alone produced five
hundred lines, and the display server spent more time on the console than on
the requests of its clients, so that a client waited seconds for an answer.

The trace is now only written when the display server is started with
`--trace`, and the reply of a request is written once instead of once for
every piece of its data.

### The requests that a window manager needs and that the server did not have

A client treats an unexpected protocol error as fatal, so a request that the
server does not implement must not answer with an error when a window manager
asks for it.

- `GrabKey` and `UngrabKey` are now answered.  The grab is recorded, and a key
  event that matches a grab goes to the window manager that asked for it
  instead of to the window that holds the focus.
- `ChangeKeyboardControl`, `ChangePointerControl`, `RecolorCursor`,
  `ChangeHosts`, `SetAccessControl`, `ChangeSaveSet`, `StoreColors`,
  `StoreNamedColor` and `CopyColormapAndFree` have no reply and are now
  accepted silently.
- `GetMotionEvents`, `ListHosts`, `SetModifierMapping`, `AllocColorCells` and
  `AllocColorPlanes` have a reply and now answer with an empty one.
- The error reply of a core request now carries the major opcode of the
  request in the minor opcode field, which is what the protocol asks for.

### The display server can be rebuilt on its own

`tools/rebuild-xserver-image.sh` rebuilds the display server, overlays it on
the existing initramfs, links the kernel again and writes the ISO.  It takes
about eight minutes instead of the half hour of a full build, which is what
made the reply and the text request defects findable at all.

The script reads the list of the sources of the kernel and of the library out
of `build.sh`, so that the two cannot drift apart.  The paths in `build.sh`
carry the root directory as a variable, and the script has to expand it: a
path that does not exist is never newer than an object, so the object was
left untouched while the source changed, and a change in the kernel never
reached the image.  The script now refuses to continue when the list names a
file that does not exist, and it rebuilds the library as well.

The display server traces every request with the descriptor of the client that
sent it, its opcode, its length and its sequence number, and every reply with
its sequence number and its size, but only when it is started with `--trace`.
The kernel names the process that faulted and the layout of its address space
when it reports a page fault, and it reports the status a process ended with,
so that a fault in a client can be attributed.

### Where Openbox stands now

With the value lists read correctly, Openbox runs through its whole start up
in a guest:

- it opens the display, checks the extensions it wants and reports the ones
  the display server does not have,
- it fails to open an input method and reports that the server has no
  locale, both of which are harmless,
- it loads its cursors, creates the window that carries the properties of
  the window manager, takes the selection of the window manager and announces
  itself with `_NET_SUPPORTING_WM_CHECK`,
- it selects `SubstructureRedirectMask` on the root window, which is the
  moment a window manager becomes the manager of the windows,
- and there it stops: it dies with a read of the address twenty five, which is
  a null pointer that is used as a structure.

The fault happens inside a shared library, not in the display server, and the
display server answered every request of Openbox correctly up to that point.
The kernel names the process that faulted and the layout of its address
space, so the next step is to find which of the libraries of Openbox reads
that address, and whether the display server answers the request that
precedes it in the way the protocol asks.

The build host now has the autotools, so the packages that need them can be
built.

## 2026-10-22 — The X11 server grew the core requests the desktops need

The native X11 display server now implements the core request set that GTK 3
and the window managers use, and the graphics stack can build GTK 3 with its
X11 backend.  The desktop environments are still not running, but every
prerequisite that stood in their way has been removed.

### The build host was contaminating every cross build

The WSL shell exported `CPATH=/home/lessplum3393/opt/rgvita/root/usr/include`
for an unrelated project.  Clang honours `CPATH` even under `-nostdinc`, and
pkg-config prefers `PKG_CONFIG_PATH` over `PKG_CONFIG_LIBDIR`, so headers and
package metadata from a foreign glibc tree were silently used while building
akaOS.  All thirty upstream builders and `build.sh` now clear `CPATH`,
`C_INCLUDE_PATH`, `CPLUS_INCLUDE_PATH`, `OBJC_INCLUDE_PATH`, `LIBRARY_PATH` and
`PKG_CONFIG_PATH`.  The contamination was visible in the Cairo build, which
picked up a host `libgen.h` that akaOS does not have.

### Cairo can draw on an Xlib surface

`tools/build-upstream-cairo-freetype.sh` now configures Cairo with
`-Dxlib=enabled` and installs `cairo-xlib.pc` and `cairo-xlib-xrender.pc` into
the graphics sysroot.  The installed `libcairo.so.2` exports sixteen Xlib
symbols where it previously exported none.  Cairo needs the X11 headers and
the X.Org client libraries, so the builder now also requires them and adds
the `usr/local/share/pkgconfig` directory to the package search path, where
`xproto.pc` and `kbproto.pc` live.

Cairo rasterises its own glyphs with FreeType and uploads them as images, so
the display server does not have to rasterise fonts for Cairo based clients.

### The accessibility bridge that the GTK 3 X11 backend needs

GTK 3's `meson.build` requires `atk-bridge-2.0` when the X11 backend is
enabled, and the requirement is not optional.  `tools/build-upstream-at-spi2-core.sh`
builds upstream at-spi2-core 2.56.8 for akaOS and installs the bridge.  It is
built after the standalone ATK package because at-spi2-core vendors its own
ATK, and the bridge references three symbols that only the newer ATK exports.

### Core request defects that were found and fixed

The audit of the request dispatcher found several requests that could never
succeed.  Each one was confirmed against the upstream protocol headers before
it was changed.

- `InternAtom` read the only-if-exists flag and the name length from the
  request body, but both live in the request header.  Every atom lookup
  failed, so no client could name a property.
- `CreatePixmap` read the depth from the body, where the request carries the
  client chosen pixmap identifier instead.  The pixmap was created with the
  width in the depth field and the height in the width field, and always
  failed with `BadAlloc`.
- `ClearArea` skipped three bytes that are not in the request, so the window
  and the rectangle were read from the wrong offsets.
- `WarpPointer` did not read the source rectangle, so the destination
  coordinates were taken from the source width and height.
- The graphics context value list read the font as sixteen bits and the dash
  offset as eight bits.  The protocol gives the font thirty two bits and the
  dash offset sixteen, so every value after the font was shifted.
- `Bell`, `NoOperation` and `SetCloseDownMode` answered with a reply they do
  not have, which desynchronised the reply stream the client reads.
- `GrabButton` never answered, so a client that asked for a button grab
  waited for a reply that never came.
- `ListExtensions` and `ListFonts` wrote the name list after the reply instead
  of inside it and reported the wrong length, so the client read the names at
  the wrong offset.

### Core requests that were missing and are now implemented

`CopyGC`, `SetDashes`, `SetClipRectangles`, `PolyPoint`, `PolyLine`,
`PolyArc`, `FillPoly`, `PolyFillArc`, `InstallColormap`,
`UninstallColormap`, `ListInstalledColormaps`, `CreateGlyphCursor`,
`GetKeyboardControl`, `ChangeKeyboardMapping`, `GetPointerControl`,
`SetPointerMapping`, `GetPointerMapping`, `GetScreenSaver`, `SetScreenSaver`
and `QueryBestSize`.

### The core font protocol

The display server now answers `QueryFont` and `QueryTextExtents` and draws
text with `PolyText8`, `PolyText16`, `ImageText8` and `ImageText16`.  It uses
the same 8x16 VGA bitmap font that the console uses, so the glyphs a window
manager draws are the glyphs the console shows.  This matters because Xft
falls back to the core font protocol when the server does not offer the
RENDER extension, and every window manager that has to draw a title or a
menu uses Xft.

### The GTK 3 X11 backend builds

GTK 3.24.52 now builds with its X11 backend.  Two things were missing: the
accessibility bridge that the X11 backend links against, which at-spi2-core
provides, and the GLX dispatch declarations that libepoxy generates, which
the GTK 3 X11 sources include.  The GLX declarations need only the Xlib
headers, so akaOS needs no GL library.

### The X11 client path is verified in a guest

A guest run of the upstream libX11 client against the native display server
now proves the whole client path: the client connects, creates a window,
draws into it, names an atom, creates and frees a pixmap, reads a property
back, copies a graphics context, draws a string with the core font, reads the
window geometry, reads the pointer position, and receives the expose event, a
button press and a key press.

Two reply defects that the guest run exposed were fixed.

- The property reply declared its length in whole four byte units but sent the
  value without the padding those units need, so the client read a value that
  was longer than the reply announced and aborted.  The value is now padded
  out to a multiple of four bytes and the length counts the padded value.
- The font list and the extension list were written inside the reply as plain
  strings.  The protocol puts a list of names after the reply and gives every
  name a length byte in front of it, and the client library reads them that
  way, so the list is now encoded the way the protocol wants it.

### A window manager can now manage a window

The event selection became per client.  The X protocol records the event
selection of every client for every window, but the server kept a single mask
per window and delivered to the window's owner.  A window manager selects
`SubstructureRedirectMask` on the root window, which it does not own, so the
old model could never deliver the redirect events to it.

With the per client selections the server now sends a `MapRequest` when a
client maps a window whose parent is redirected, and a `ConfigureRequest` when
it configures one, and it leaves the window alone until the window manager
acts.  A window that asks to bypass the window manager is never redirected.
The `ConfigureNotify`, `ReparentNotify`, `MapNotify` and `DestroyNotify`
events follow the canonical layouts that libX11 and the XCB protocol
description use, and `GetGeometry` now reports the origin relative to the
parent window while `QueryPointer` reports the window under the pointer.

### The X.Org package descriptions live in one directory

Cairo with its Xlib surface backend requires the X11 package descriptions, and
the X.Org packages spread theirs over the library and the share directory.
Every consumer therefore had to know about both directories, and a build step
that only searched the library directory failed to find Cairo at all.  The X11
builder now publishes the package descriptions of the share directory beside
the others, so the one directory that the package search path always contains
holds the whole set.

### The Xlib API of the installed client library

The client library is upstream libX11 1.8.12, and its public header is the
one that ships with it.  It names the property reader `XGetWindowProperty`
and gives `XCopyGC` the older form that copies into a graphics context the
caller supplies.  Code that is written against those names compiles and runs,
and code that expects the other spellings does not, so a desktop environment
that fails to build on the X11 side has to be checked against this header
before the display server is blamed.

### The upstream LXDE window manager runs on the native X server

The unmodified upstream Openbox 3.6.1 is staged in the image with its
configuration and its themes, and it opens a session on the native display
server.  Three things had to be right for it.

- The program has to be linked against the akaOS loader.  An autotools link
  asks for the host's loader by default, and the kernel then cannot start the
  program at all, so the builder passes the akaOS linker script, the startup
  object and the interpreter.
- The libraries the window manager needs have to travel with it.  The parser
  and the Xft backend of the text stack were not in the image before.
- The name `openbox` is already taken in the image by the compatibility shim
  of the native desktop, so the upstream window manager runs under the local
  prefix.

### The upstream LXDE window manager and its XML parser build

`tools/build-upstream-libxml2.sh` builds upstream libxml2 2.13.8 for akaOS and
publishes it into the graphics sysroot, and `tools/build-upstream-openbox.sh`
builds the unmodified upstream Openbox 3.6.1 window manager, which is the
window manager of the LXDE desktop environment.  The Openbox program is
installed in the graphics sysroot.

Three things had to be right for them.

- The autotools link turns the absolute path of the akaOS C library into the
  host's `-lc`, so the shared library was linked against the host glibc and
  the linker refused the glibc versioned symbols.  The library is now named
  with the exact file name syntax the linker understands, which keeps the
  name.
- The package search path has to carry the sysroot root, or the include
  directories of GLib and of the XML parser point at the build host.
- The C library was missing the list form of the exec calls and nine of the
  POSIX signals, and the window manager needs both.

### The dynamic loader names the symbol it cannot resolve

The loader used to say only that a relocation was unsupported.  It now names
the symbol and the library, which is how the last blocker was found: the
image carried the Cairo that was built before the Xlib surface backend
existed, so GTK 3 could not resolve `cairo_xlib_surface_get_drawable` and
failed to load.  The image now takes its libraries from the sysroot they
were installed into rather than from the build tree.

### The GTK 3 X11 library loads and runs on the native server

The unmodified upstream GTK 3 library now loads in a guest and reaches its
first line of output, which it could not do before: the image carried the
Cairo that was built before the Xlib surface backend existed, so the loader
could not resolve `cairo_xlib_surface_get_drawable`.

Two things about the guest harness are worth knowing.

- A program whose output goes into a pipe does not reach the reader on this
  platform, so a guest probe that pipes the output of a program sees nothing.
  The probe writes to the console instead.
- The display server takes the keyboard away from the console line discipline
  when it opens the input device, so nothing can be typed into the shell once
  a graphical session runs.  The guest harness therefore starts both clients
  from one command line, which the shell reads before the display server
  takes the keyboard.
- The window manager that is built for the LXDE desktop environment needs the
  startup files of the akaOS C library to link its program, and the X.Org
  client libraries carry glibc versioned symbols that the linker refuses
  unless the link is told to allow them.  The akaOS loader accepts those
  symbols from a provider without a version table, which is why the client
  libraries work in a guest at all.

### The unmodified upstream GTK 3 library runs on the native X11 server

A guest run of the upstream GTK 3 library against the native display server
now passes every stage: the library connects to the display, builds a window
with a button and a label, keeps the layout inside the window, and the button
receives a real pointer click.  The harness reports four passes:

- The native akaOS X11 server accepted a real upstream libX11 client
  connection.
- The libX11 client created, mapped and drew into a window that the server
  presented through Lumen.
- Pointer and keyboard events reached the upstream libX11 client through the
  X11 input path.
- The unmodified upstream GTK 3 library ran on the native X11 server and its
  button received a pointer click.

Getting there needed one more protocol fix.  A client that asks for an atom
with the only-if-exists flag learns that the atom does not exist from the
reply, not from an error.  The server answered with an error, and the GTK 3
library aborted on it.

### A window manager takes a window over

The upstream Openbox window manager is staged in the image together with its
configuration and its themes, and `tools/test-upstream-wm.sh` proves in a
guest that it takes a client window over: the client waits for the window to
be moved into a frame, which only happens when the display server routes the
map request to the window manager instead of mapping the window itself.

The guest probe is `userspace/bin/x11-wm-client`, which selects the structure
notify mask and reports the frame the window manager moved the window into.

### Still open

The RENDER extension and the input extensions are still missing.  The GTK 3
X11 build has not been run against the display server yet.  No desktop
environment is 100% working yet.


## 2026-10-22 — The native X11 server is complete and verified

The akaOS X11 display server now serves a real upstream libX11 client end to
end, and the X11 screen reaches the framebuffer through Lumen.

A guest run of `tools/test-upstream-x11.sh` shows the whole path:

    xserver: screen surface published
    lumen: surface attached id=1 name=/xs-105 size=1024x768 at 0,0
    xserver: ready on display 0 (1024x768)
    x11 client: connected to display :0
    x11 client: vendor The akaOS Project
    x11 client: default screen 0 root 0x1
    x11 client: created window 0x20000001
    x11 client: mapped window
    x11 client: drew into the window
    x11 client: window geometry 320x240 at 120,90 map state 2
    x11 client: received expose event
    xserver: button 1 press at 300,200 on window 0x20000001
    x11 client: received button press at 180,110
    xserver: button 1 release at 300,200 on window 0x20000001
    x11 client: received key press keycode 38
    x11 client: X11 client path verified

The framebuffer capture in `build/x11-server-screen.png` shows the X11 screen
on the compositor: the dark desktop background, the client window at 120,90
with the filled blue block and the orange outline that the client drew.  The
button press arrives with the window relative coordinates 180,110 for a
pointer at 300,200, and the key press carries keycode 38, the X11 keycode for
the letter a.

The server defects that were found and fixed in this round:

- The stacking order was zero initialised instead of empty, so a mapped
  window never entered the stacking order and every pointer event landed on
  the root window.
- The hit test used `and` where the X11 rectangle needs `or`, so a point past
  a single edge of a window was reported as inside it.
- An event for a window that did not select the event kind was dropped
  instead of being passed to the parent window, so a client that selected the
  kind on a parent window never saw the event.
- Pointer events carried screen coordinates where the X11 protocol wants
  coordinates relative to the event window.
- Key events were sent to the pointer window instead of the focus window.
- The server cleared the compositor request queue at startup, which
  destroyed messages that other producers had already queued.
- Drawing damage rectangles were recorded in drawable coordinates and used
  as screen coordinates, so the screen was repainted in the wrong place.
- The root window blitted its empty backing store over the background, which
  painted the whole screen black.
- A new window did not paint its background into its backing store.
- The screen surface used the ARGB format while the X11 pixels carry no
  alpha, so the compositor treated every pixel of the X11 screen as fully
  transparent.  The screen surface uses the opaque XRGB format now.

Open items.  The core request set still lacks fonts, colormaps, the
translation and copy helpers, and the cursor requests.  GTK 3 is still built
for Wayland only, so the GTK based desktop environments cannot talk to the
X11 server yet.  No desktop environment is 100% working yet.

(The paragraph above is the state as of the earlier round.  The section at the
top of this file records what has changed since then.)


## 2026-10-22 — Native X11 server: a real libX11 client now works

The upstream X.Org client stack cross-builds for akaOS x86-64 and a native
display server in `userspace/bin/xserver` now serves real upstream libX11
clients.  This is the shared prerequisite for the unmodified LXDE, XFCE, MATE
and Cinnamon sessions, whose window managers (openbox, xfwm4, marco, muffin)
are X11 clients.

`tools/build-upstream-x11.sh` cross-builds xtrans 1.6.0, xorgproto 2024.1,
xorg-macros 1.19.2, libpthread-stubs 0.5, libXau 1.0.12, libXdmcp 1.1.5,
xcb-proto 1.17.0, libxcb 1.17.0, libX11 1.8.12, libXext 1.3.6, libXrender
0.9.12, libXfixes 6.0.1, libXdamage 1.1.6, libXcomposite 0.4.6, libXi 1.8,
libXrandr 1.5.4, libXcursor 1.2.2 and libXtst 1.2.5 from the official x.org
hosts with pinned SHA-256 values, and publishes them into the graphics
sysroot.  The build runs in the Ubuntu ext4 cache because the DrvFS mount is
far too slow for repeated autotools rebuilds.

The akaOS libc needed several additions for the upstream libraries: `fd_set`
now uses the conventional `fds_bits` field, `pathconf` answers the
`_PC_LINK_MAX` family, `random()`/`srandom()`, `mblen()`, `isascii()`/
`toascii()`, `in_addr_t`, the missing `MSG_*` and `errno` values, and a
standard `<features.h>`.

`userspace/bin/xserver` implements the X11 connection setup, the core
windowing, property, drawing and resource requests, and the event stream.
`userspace/bin/x11-client` is a real upstream libX11 client used by
`tools/test-upstream-x11.sh`.

Verified in a QEMU guest on x86-64: a real upstream libX11 client connects
to the native server, creates a 320x240 window at 120,90, maps it, draws into
it, reads back the correct window geometry and map state, and receives the
Expose event.  The server answers the whole client request stream
(QueryExtension, CreateGC, GetProperty, CreateWindow, ChangeProperty,
ChangeWindowAttributes, MapWindow, SetInputFocus, CreateGC, ChangeGC,
PolyFillRectangle, PolyRectangle, GetWindowAttributes and GetGeometry)
without a protocol error.

Open items.  Pointer and keyboard events reach Lumen but not yet the X
client, because the server still stalls at times inside the compositor
channel write while the compositor reads the shared surface.  The same stall
also makes the Expose event delivery intermittent.  The server therefore
coalesces damage requests and only logs rare events: a flooded console
blocks the session shell, which is a separate failure that was found and
fixed in this round.  The known intermittent guest fault in
`tty_process_char` is unrelated to this work and the test harness retries a
failed guest boot.


## 2026-10-19 — LXQt FTM smoke passes; session startup still fails

The x86_64 ISO at `build/akaos.iso` (built 20:03) includes foreign-toplevel-
management v2 and the later keyboard-focus activation changes. QEMU run 16
booted, passed the POSIX/Lumen probes, and verified FTM manager binding,
metadata, activation/focus, close delivery, and closed-on-unmap lifecycle.
The manual `/bin/dbus-update-activation-environment --all` command returned
status 0, and the session bus accepted its `a{ss}` payload (`valid=1`).

The unmodified LXQt session still does not reach its panel: its helper process
exits in the kernel log, but `lxqt-session` reports `Unknown error` and Qt logs
`QProcess: Destroyed while process ("dbus-update-activation-environment") is
still running.` No second foreign-toplevel manager binding or panel
layer-shell map appeared. `run16.log` and the failure screenshot are saved in
`build/lxqt-foreign-toplevel-20261019/`; the overall integration test failed,
and no taskbar screenshot was produced. Do not report LXQt as working.

Added an unbuilt `hello --posix-test` regression for `SA_SIGINFO` SIGCHLD,
nonblocking `waitpid()` from the handler, and interrupting `poll()`. It is a
focused probe of Qt's forkfd completion path, not a confirmed fix. The current
ISO predates this source edit.

The test QEMU stopped responding to targeted cleanup. Ubuntu had been stopped
before the run, was started by the test, then was terminated after the guest
stalled. Restart currently fails with `HCS_E_CONNECTION_TIMEOUT`; use Ubuntu
WSL, not Arch, and recover it before rebuilding or testing. Next: build the new
probe, confirm whether the SIGCHLD/poll combination passes, then fix the first
reproducible QProcess or session blocker. The v2 protocol still lacks v3
parent-toplevel semantics. KDE, GNOME, XFCE, MATE, Cinnamon, LXDE, and LXQt are
not 100% working.

## 2026-10-19 — LXQt resource and alpha-compositing continuation

XSCore signals parents with `SIGCHLD` on child exit; default `SIGCHLD` is
non-fatal. The POSIX suite verifies the signal and passes in x86-64 QEMU.
The common process source compiles for x86-32, AArch64, ARM32, RISC-V64, and
RISC-V32 as well.

The x86-64 runtime now has a 1024-entry per-process descriptor ceiling, a
2048-entry file-description pool, a 512-entry pipe pool, and a 256-entry
socket pool. libc's `_SC_OPEN_MAX` matches the 1024 descriptor limit; synthetic
epoll/eventfd/timerfd ranges are disjoint above it. `hello --posix-test` now
checks 300 live pipes (descriptor numbers past 512), 64 concurrent AF_UNIX
socketpairs, eventfd/timerfd handle separation, and SIGCHLD. The full test
command returned to the shell and the LXQt harness proceeded; the smaller
socket-pool image also showed that Qt's earlier socketpair `EMFILE` diagnostic
is absent.

Added wlr layer-shell v1 server support and Lumen layer roles, then fixed a
Wayland ARGB rendering defect: Lumen dropped the source alpha byte before
premultiplied blending, making client pixels transparent. A Lumen self-test
checks the corrected half-transparent pixel result. The QEMU LXQt image logs
both bridge and Lumen mapping the `dock` surface at `(0,736)`, `1024x32`, with
1,252 nonzero/opaque pixels; the rendered screenshot now visibly contains
the Quicklaunch placeholder and Show Desktop items. Artifact:
`build/lxqt-argb-fix-20261019/lxqt-panel-preview.png`.

Remaining LXQt issues: KWindowSystem still falls back to its dummy backend,
so taskbar/window-management features are missing; the status-notifier bus is
not connected and the default panel is only partially populated. Layer-shell
exclusive zones, keyboard behavior and full popup semantics remain incomplete.
Qt/LXQt is x86-64-only. KDE, GNOME, XFCE, MATE, Cinnamon and LXDE are still
unverified and none of the requested desktop environments is 100% working.
Diagnostic artifacts only; `build/akaos.iso` remains unchanged. CrazyQ Pro was
not modified.

## 2026-10-18 — LXQt and QtDBus integration (in progress)

The x86_64 upstream LXQt session and panel now link against akaOS's dynamic
CRT. `tools/qtbase-akaos-x86_64.cmake` supplies the startup object and hosted
C++ semantics; both builders reject executables with an ELF entry point of
zero. `llvm-readelf` verified nonzero `_start` entries for `lxqt-session`
(`0x10BC0`) and `lxqt-panel` (`0x6ABB0`). This fixed the earlier jump into the
ELF header, but an end-to-end desktop pass is still open.

Qt's event-loop startup exposed a four-slot libc eventfd cap. It is now 32
slots, with a non-overlapping fake descriptor range, and the eventfd self-test
checks 16 simultaneous handles. The LXQt guest no longer reports the earlier
`QThreadPipe`/`EMFILE` failure. libc `spawn()` now passes `environ` through
`SPAWNVE`; a child-inheritance regression marker is in the POSIX test suite,
but its full six-architecture run is still pending. Added public `vsprintf()`
and `_SC_GETPW_R_SIZE_MAX` / `_SC_GETGR_R_SIZE_MAX` support while cross-building
upstream libdbus.

Upstream libdbus 1.16.2 was downloaded from the official D-Bus release host,
verified against its published SHA-256, cross-built for x86_64, and installed
in the graphics sysroot. The shared loader can now resolve bare `dlopen()`
names through `/usr/local/lib`, `/usr/lib`, and `/lib`, so QtDBus loads
`/usr/local/lib/libdbus-1.so.3`. The verbose QEMU trace exposed and fixed a
malformed D-Bus header: the declared header-field-array length now excludes
the final alignment padding while the body still begins on an 8-byte boundary.
The broker also returns `ServiceUnknown` instead of dropping a connection for
an unowned destination, stores 64 match rules up to 512 bytes each, and accepts
the standard `RequestName(su)` signature. An x86_64 QEMU trace now confirms
EXTERNAL/UNIX_FD negotiation, a valid `Hello` reply, a 148-byte `AddMatch`, and
`RequestName("org.lxqt.session", flags=4)` returning primary-owner status; the
client remains connected through `NameOwnerChanged`.

LXQt is still not complete: `lxqt-session` loads and claims its bus name, but
the panel loader marker and `lumen: Wayland surface presented` have not appeared.
The latest integration test image is `build/dbus-requestname/akaos.iso` (also
copied to `build/akaos.iso`). It is a verbose diagnostic image, not evidence of
a fully working desktop. The test harness accepts an ISO or Multiboot kernel
path, captures a screenshot on failure, and avoids sending shell `ps` input
after the compositor takes over the display. `tools/build-upstream-libdbus.sh`
now limits Meson's PATH to Ubuntu tool directories so imported Windows PATH
entries do not stall command lookup.

Stock KDE, GNOME, XFCE, MATE, Cinnamon, LXDE, and LXQt are not 100% working on
akaOS. Qt/LXQt remains x86_64-only. Do not report desktop completion until
real session boot and process/surface checks pass; all other architectures
still need equivalent work. CrazyQ Pro was not modified.

## 2026-10-17 — `wordexp()` and upstream liblxqt built

Added `<wordexp.h>` plus libc `wordexp()`/`wordfree()` in the shared POSIX
layer. The implementation covers quoting and backslash removal, environment
and braced parameter expansion, tilde expansion, IFS splitting, pathname
matching, `WRDE_APPEND`/`WRDE_DOOFFS`/`WRDE_REUSE`, `WRDE_UNDEF`, and safe
command-substitution rejection (`WRDE_CMDSUB`). It does not implement shell
command execution, arithmetic expansion, all special parameters, or every
POSIX IFS edge case; do not describe it as a complete shell expander.

`hello --posix-test` now checks quote/escape handling, `$HOME`, tilde
expansion, sorted wildcard matches, quoted wildcard preservation, offset and
append/reuse behavior, undefined-variable errors, and that command
substitution is rejected without creating its marker file. The six-target
QEMU POSIX matrix passed after rebuilding all six images; every transcript
contains `POSIX wordexp quoting, environment, tilde, glob, and offset semantics
verified`. Logs: `build/test-wordexp-matrix-20261017/`. The x86-64
dual-firmware ISO was relinked and separately passed the same POSIX/Wayland
smoke.

Built and installed pinned upstream `liblxqt` 2.4.0 as a Wayland-only x86-64
shared library. The reproducible builder is `tools/build-upstream-liblxqt.sh`;
its source patch omits X11 screensaver integration and X11-only
`KWindowInfo`/`KX11Extras` calls, while retaining generic KWindowSystem window
activation. It compiles against the newly provided `socketpair()` and
`wordexp()` APIs. This is a library build only: no stock LXQt session, panel,
or file manager has been launched, and Qt/LXQt remains x86-64-only.

The previously recorded socketpair layer still supports only AF_UNIX stream
pairs; datagram/seqpacket and internet-domain pairs remain open. `PATH_MAX` is
still 128 and archive path fields are still 128 bytes. No requested stock DE
is 100% working; continue into LXQt session components and QEMU session tests.

## 2026-10-16 — AF_UNIX socketpair and LXQt Wayland-only continuation

Implemented libc `socketpair()` for `AF_UNIX`/`SOCK_STREAM` using akaOS's
pathname Unix-socket APIs. It supports `SOCK_CLOEXEC` and `SOCK_NONBLOCK`,
cleans up the temporary bound name, and returns errors for unsupported domains,
protocols, and socket types. `hello --posix-test` now checks bidirectional
transfer and both flags. A full POSIX guest matrix passed on x86-64, x86-32,
AArch64, ARM32, RISC-V64, and RISC-V32; every transcript contains
`POSIX AF_UNIX socketpair bidirectional data and flags verified` in
`build/test-socketpair-matrix-20261016/`.

The first x86-64 boot of the new LXQt KWindowSystem plugin exposed missing
initramfs parent directories. `build.sh` now explicitly stages
`/usr/local/lib/plugins` and `/usr/local/lib/plugins/kf6` before the plugin
directory. The x86-64 archive was repacked and its BIOS/UEFI ISO relinked; the
full QEMU POSIX/Wayland smoke passed. The `socketpair()` symbol is present in
the x86-64 shared libc.

The Wayland-only liblxqt port recipe removes the upstream
`KWindowInfo`/`KX11Extras` X11 desktop-switch block while retaining generic
KWindowSystem activation. The `liblxqt` build status is updated in the
2026-10-17 continuation above.

Scope: the new `socketpair()` currently supports AF_UNIX streams, not
`SOCK_DGRAM`/`SOCK_SEQPACKET` or internet-domain pairs. The six-architecture
matrix validates this shared POSIX/API path, not Qt/LXQt on those five
non-x86-64 targets. No requested stock desktop session is 100% working yet.
Continue the pinned upstream LXQt build, then package and boot a stock LXQt
session before making any desktop-support claim.

## 2026-10-16 — LXQt upstream dependencies and 255-byte VFS names

Continued in Ubuntu WSL only. Built pinned upstream `libdbusmenu-lxqt` 0.4.0,
QtSvg 6.12.0, QtXml 6.12.0, `libqtxdg` 4.4.0, and `lxqt-menu-data` 2.4.0
for x86-64, plus the host Qt LinguistTools 6.12.0 required to generate menu
translations. Public recipes/porting patch are in `tools/build-upstream-*`
and `tools/build-host-qt-linguist-tools.sh`. `build.sh` conditionally stages
the QtSvg/QtXml/XDG libraries, icon/image plugins, XDG config, and LXQt menu
files. A fresh x86-64 initramfs has 713 entries (205,293,896 bytes); archive
inspection confirmed the long `lxqt-internet-and-network.directory` name,
LXQt menu files, QtSvg/QtXml/XDG libraries, and full Cairo interpreter SONAME.
The Cairo path alias that was needed under the old name limit has been removed.

Raised the shared XSCore/libc/initramfs VFS component-name limit from 31 to
255 bytes. `hello --posix-test` now requires `pathconf("/", _PC_NAME_MAX)` to
report 255, then creates, writes, stats, enumerates, and removes a 64-byte
filename. The six-architecture compositor/client QEMU matrix passed with exit
code 0: x86-64, x86-32, AArch64, ARM32, RISC-V64, and RISC-V32. All six
transcripts contain `POSIX 255-byte NAME_MAX and long-name VFS lookup verified`
under `build/test-vfs-namemax-20261016/`.

Important boundary: `PATH_MAX` remains 128 bytes (`XSCORE_VFS_PATH_MAX` 127),
and the initramfs archive stores paths in a 128-byte field. The 255-byte name
limit is now reported, but a 255-byte basename cannot yet be opened through
the current full-path limit. Longer POSIX paths and persistence records still
need a format/ABI update.

This does not make any stock desktop environment 100% working. No upstream
LXQt session (nor KDE Plasma, GNOME Shell, XFCE, MATE, Cinnamon, or LXDE) has
been launched. Qt/LXQt binaries and the packaged LXQt data currently target
x86-64 only; the six-architecture pass above tests shared native kernel/libc
behavior, not Qt or desktop support on the other five ports. The libdbusmenu
upstream unit tests were disabled through an explicit porting patch because
the target QtTest module is not built; package build success is not a test pass.

Next: build the actual upstream LXQt dependency chain (`libsysstat`,
`qtxdg-tools`, `liblxqt`/KWindowSystem, `lxqt-session`, panel and file manager),
package its services/assets, and boot a stock session under QEMU. Then repeat
the actual session work and validation for the other requested desktops and
architecture targets. Keep the overall desktop goal active.

## 2026-10-16 — Qt Quick QML rendering on the x86-64 Wayland path

Built the pinned Qt Declarative 6.12.0 target `Qml`/`Quick` libraries and
`libqtquick2plugin.so` in the Ubuntu WSL ext4 cache, then rebuilt the x86-64
UEFI/FAT32 image with refreshed QtCore and the current shared libc. The full
build completed with 691 initramfs entries (266,252,422 bytes); the disk image
is 319 MiB. The six-architecture libc/pthread/dynamic-loader cross-compile
gate also passed after the stack-introspection changes.

QEMU verified the refreshed Qt Widgets Wayland client painted its expected
pixel and Lumen presented it. The Qt Quick smoke initially failed because
`QQuickWindow::grabWindow()` returned an empty image on this software/Wayland
path. The test now checks the QML rectangle's 520x240 dimensions and expected
color property, an initialized software scene graph, an exposed window and a
`frameSwapped` event; the readiness gate also requires Lumen's Wayland-surface
presentation marker. The fresh x86-64 UEFI guest passed that Quick test.
This confirms a QML/Quick frame reaches Lumen; it does not independently read
back the final framebuffer pixels.

Updated the x86-64 builder to stage DejaVu Sans at `/usr/local/lib/fonts` and
regenerated all five other userland builders to package it at that path and
under `/usr/share/fonts`. The six initramfs archives all contain both paths.
`test-upstream-fontconfig.sh` found the packaged font, and the Qt Quick QEMU
smoke passed without the old font-directory warning. A full build then
completed x86-64, x86-32, AArch64, ARM32, RISC-V64, and RISC-V32 images; the
x86-64 UEFI disk is 320 MiB, its dual-firmware ISO is 525 MiB, and the x86-32
ISO is 109 MiB.

The subsequent six-architecture guest matrix initially launched x86-64 with
the test harness's old 256 MiB default and produced no serial output within
the bounded observation window. Updated its x86-64 default to 512 MiB. Before
the retry, Ubuntu WSL stopped with `HCS_E_CONNECTION_TIMEOUT`; both WSL
distributions are stopped and no guest matrix result is available yet. Do not
switch to Arch. The retry must use ext4-cached ISO/kernel copies to avoid
repeating the long DrvFS read.

The broad `tools/test-compositor-clients.sh` suite also passed for x86-64 on
the refreshed UEFI diagnostic image (512 MiB QEMU). The transcript contains
passing C++ runtime/dynamic-loader, POSIX, D-Bus, GLib/GIO, GTK/Qt compatibility
bridge, Wayland, Openbox component routing, Lumen input, XSDisplay, and palette
lifecycle checks. This was a single-architecture compatibility suite, not an
unmodified GTK application test or any complete desktop session.

The dedicated unmodified upstream application tests then passed on x86-64
BIOS QEMU using the refreshed Qt/GTK runtime. GTK 4.22.5 plus upstream
xkbcommon completed type registration, Wayland keyboard/pointer, popup,
subsurface, and xdg_toplevel interactions. GTK 3.24.52 mapped a GtkBox window
and passed keyboard activation, popup, subsurface, and window-state checks.
These are real toolkit clients, not full desktop sessions.

`build.sh` now stages the public DejaVu Sans font under both
`/usr/share/fonts/truetype/akaos/default.ttf` and Qt's
`/usr/local/lib/fonts/DejaVuSans.ttf` because the Qt build has Fontconfig
disabled. A fresh diagnostic UEFI image passed the Qt Quick smoke without the
old missing-font-directory warning. `tools/test-upstream-fontconfig.sh` also
passed against the updated x86-64 image and reported a sans-serif DejaVu match.
The subsequent all-target build refreshed the standard image with the Qt font
path; only the regular Sans face is packaged, so broader font families remain
open.

`tools/test-desktop-readiness.sh` now slows key injection for the long Qt test
command and fails quickly for a missing smoke executable or explicit render
failure. The normal x86-64 build and six architecture images now include the
font packaging and current smoke source; the earlier ext4 diagnostic images
remain available for the focused QEMU tests.

No unmodified KDE Plasma, GNOME Shell, XFCE, MATE, Cinnamon, LXDE, or LXQt
session has been installed or proven. The Qt and upstream GTK tests are
x86-64-only. A stock desktop compatibility claim remains out of scope until
each session's real services, packages, UI, input, and shutdown paths pass
end-to-end tests.

## 2026-10-14 — pthread stack introspection across six ports

Added GNU `pthread_getattr_np()` and POSIX `pthread_attr_getstack()` while
preserving akaOS's existing `pthread_attr_t` ABI. Queried stack metadata is
kept in a bounded side table; worker bounds use mappings already held by
pthread records, and the main-thread bounds use its original startup SP and
XSCore's 64-page stack-growth reservation. Every architecture CRT now passes
the initial SP into libc. The default worker stack is 1 MiB; configured stack
sizes are bounded at 8 MiB.

`tools/test-desktop-libc-cross-compile.sh` now compiles pthread libc and all
architecture CRT entry points; it passed on x86-64, x86-32, AArch64, ARM32,
RISC-V64, and RISC-V32. The refreshed x86-64 UEFI image passed QEMU main- and
worker-stack-bound checks plus pthread lifecycle, GLib descriptor source,
asynchronous D-Bus, GTK/Qt runtime ABI, and display-protocol probes. A later
optional Qt Wayland probe was not in that image: an old host-side
`QT_WAYLAND_SMOKE.ELF` caused the harness to try launching a missing guest
command. This run does not re-prove Qt QPA.

The upstream Qt Declarative 6.12.0 target `Qml`/`Quick` build resumed. Its
generated MOC manifests still pointed into DrvFS after reconfiguration, so the
public headers and C++ headers were staged in Ubuntu ext4 and the affected
generated manifests were updated with backups. The target is now compiling
(`Qml`/`Quick`, latest observed 135/581 steps); no compiler error is confirmed
after fixing the staged include layout. Real
upstream KDE, GNOME, XFCE, MATE, Cinnamon, LXDE, and LXQt sessions remain
uninstalled and unverified; do not describe akaOS as 100% desktop-compatible.

## 2026-10-12 — painted Qt Wayland window and six-port regular-file truncate

Fixed the false-positive Qt Widgets smoke: Qt's x86-64 Wayland client now uses
akaOS libc `memfd_create()` instead of testing for Linux `SYS_memfd_create` and
falling back to a temporary VFS file. Added writable regular-file `ftruncate`
support to the dynamic VFS with sparse zero-fill on growth, cleared tail data
on shrink, bounds/error handling, and persistence synchronization. The POSIX
regression verifies grow/shrink/zero-fill, size limits, and read-only-FD
rejection.

The QPA smoke now checks a sealed 552,960-byte memfd, a temporary-file resize,
an exposed Qt Widgets window, and the expected rendered pixel. Its x86-64 UEFI
QEMU run passed; Lumen reported `Wayland surface presented on framebuffer`,
with no `QWaylandShmBuffer` or inactive-painter errors. The strengthened
readiness harness rejects those rendering errors instead of accepting mere
window exposure.

Rebuilt x86-64 UEFI, x86-32, AArch64, ARM32, RISC-V64, and RISC-V32 after the
shared VFS/process changes. `tools/test-compositor-clients.sh` POSIX-only runs
passed across all six guests, including the regular-file truncate marker; the
x86-64 POSIX run used its 512 MiB UEFI image, and the other five used their
standard QEMU memory settings. The six-architecture desktop/libc compile gate
also passed.

Remaining limits: this proves one real Qt Widgets/Wayland window on x86-64,
not a stock desktop session. KDE Plasma, GNOME Shell, LXDE, and LXQt sessions
remain compatibility stubs. Qt still depends on the staged prebuilt runtime,
is not verified off x86-64, and lacks Qt Quick/declarative and the actual DE
service/application stacks. Regular dynamic VFS files remain capped at 128
KiB; shared mutable-file mmap/SIGBUS truncate semantics remain open.

## 2026-10-12 — Qt 6.12 Wayland QPA smoke on x86-64

Built the real Qt 6.12.0 `Core`, `DBus`, `Gui`, `Widgets`, `WaylandClient`,
`libqwayland.so`, XDG shell-integration plugin, and Bradient decoration plugin
for x86-64. The Qt toolchain now selects akaOS's native `poll(2)`, sets the
default QPA platform to Wayland, and imports host scanner/target Wayland
libraries without replacing them with host dependencies. The repeatable QtBase
patch uses `O_RDONLY` where Qt assumes `O_PATH`, uses `sysconf(_SC_PAGESIZE)`
instead of the non-POSIX `getpagesize()`, and prevents a false "no QPA" warning
when Wayland client support is enabled. QtWayland's optional compositor is
disabled for akaOS because Lumen uses the upstream Wayland server bridge.

The public libc now declares `poll()`/`ppoll()` with C linkage and defines the
standard `PIPE_BUF` from `_POSIX_PIPE_BUF`. XSCore's desktop launcher starts
`wayland-bridge` after Lumen and waits by connecting to the Unix socket rather
than `stat()`ing a socket node. Login exports `WAYLAND_DISPLAY=wayland-0`.
`build.sh` can stage actual Qt runtime libraries/plugins when a prebuilt Qt
prefix and host build are supplied with `AKAOS_QT_BUILD_DIR` and
`AKAOS_QT_HOST_PREFIX`; actual Qt sonames resolve from `/usr/local/lib` only
when the packaged `.upstream-qtbase` marker exists. `run.sh` and the QEMU
readiness test use 512 MiB when this runtime is staged.

Added `userspace/bin/qt-wayland-smoke`; it selects the installed Wayland QPA,
opens a `QWindow`, and verifies that the compositor exposes it. The latest
clean UEFI FAT32 image has a 653-entry, 197,231,398-byte initramfs. Using an
ext4-cached image copy, `tools/test-desktop-readiness.sh` passed in QEMU,
including the initial marker `Qt Wayland QPA created and exposed a native
window`. That first check verified exposure only; it was later found to be a
false positive because Qt SHM resize failed and widgets did not paint. The
current, stricter painted-pixel test and fix are recorded in the newer section
above. The modified libc/dynamic-loader sources compile for x86-64, x86-32,
AArch64, ARM32, RISC-V64, and RISC-V32.

This is not 100% desktop support: the QEMU proof covers one Qt Gui window on
x86-64. KDE Plasma, GNOME Shell, LXDE, and LXQt session executables remain
akaOS compatibility stubs. `desktop --check` now labels those profiles
`COMPATIBILITY ONLY (upstream session not installed)` and reports the Qt QPA
runtime separately. The staged Qt build still relies on prebuilt WSL cache
artifacts; a clean, reproducible QtBase build bootstrap, Qt Quick/declarative
and desktop-session services, real upstream session packages, and non-x86-64
Qt builds remain open.

## 2026-10-11 — x86-64 full suite, GTK 3/4 builds, and locale API

The full `tools/test-compositor-clients.sh` guest sequence passed on x86-64
using the newly built dual-firmware `build/akaos.iso` through its BIOS path
and the existing D-Bus session. The harness
rechecked `hello --dbus-registration-test` after POSIX and Wayland probes,
then completed `hello --desktop-runtime-test` without restarting the bus; the
asynchronous D-Bus descriptor watch and the GLib/GIO/GTK/Qt compatibility
markers all passed. The run also passed D-Bus property signals and service
routing, X11/Wayland protocol probes, upstream Wayland 1.26 SHM, Lumen input,
XSDisplay, toolkit bridge, and palette resize/damage/destroy lifecycle. The
same guest passed the new locale and endian/byte-swap POSIX markers.

Added public `userspace/libc/include/langinfo.h` and `nl_langinfo()`/
`nl_langinfo_l()` for akaOS's built-in `C` and `C.UTF-8` locales, including
codeset, date/time, radix, and yes/no metadata. Added public `endian.h` and
`byteswap.h` with compiler-derived byte order, common endian conversion
macros, and `bswap_16/32/64`; `hello --locale-test` and the POSIX self-test
exercise both APIs.

The latest x86-64 build completed with a 628-entry, 85,780,139-byte initramfs
and a dual BIOS/UEFI ISO. Its upstream refresh compiled and installed GTK
4.22.5 and GTK 3.24.52 with Wayland backends, plus their
GLib/Pango/Cairo/font stack.
The build also extended the reproducible QtBase patch for akaOS's absent
`O_LARGEFILE` and uses `sysconf(_SC_PAGESIZE)` in Qt resource mapping instead
of the non-POSIX `getpagesize()` extension.

After refreshing all six images with the locale/endian APIs and shared DSO
alias, the complete compositor-client matrix passed on x86-64, x86-32,
AArch64, ARM32, RISC-V64, and RISC-V32. Every transcript contains both the
`nl_langinfo` and endian/byte-swap markers. x86-64 BIOS passed in that matrix.
The latest dual-firmware ISO also booted under OVMF UEFI through kernel startup,
the process-isolation self-test, and `login:`. For UEFI probes, copy the ISO
from `/mnt/c` to Ubuntu's ext4 cache; the DrvFS-hosted attempt timed out while
reading the large kernel/initramfs segment.

Actual upstream GTK smoke tests now pass on x86-64. GTK 4.22.5 registers
GtkWidget/GtkLabel/GtkButton, maps and presents a Wayland window, handles
keyboard/pointer input and popup dismissal/reopen, and passes synchronized
subsurface plus xdg_toplevel move/resize/state checks through Lumen. GTK
3.24.52 maps a GtkBox Wayland window and handles keyboard activation; its
window-state/subsurface suite also passes. The initial GTK loader error came
from `libcairo-script-interpreter.so.2` exceeding the 31-byte VFS component
limit. The initramfs now stores the FreeType-enabled Cairo stack with the
script DSO at `/lib/libcairo-script.so.2`, and `dlfcn.c` canonicalizes the
upstream soname to that alias. The alias and endian headers have been rebuilt
for all six ports and pass the full matrix.

Next, refresh `/home/lessplum3393/.cache/akaos/qt6.12.0/akaos-target` with the
current public libc/SDK headers, runtime libraries, and graphics sysroot, then
resume Qt Widgets/QtWayland and verify its Wayland QPA plugin. QtBase moved
past `langinfo.h` and then stopped on `endian.h`; both public headers now exist
but must be synced into its staged root. The GTK smoke apps do not prove stock
KDE, GNOME, LXDE, or LXQt sessions work; do not claim 100% until unmodified
sessions boot and pass end-to-end interaction tests.

## 2026-10-09 — cross-architecture static-PIE parity and full-suite probe (in progress)

The generated alternate-architecture userspace builders omitted
`userspace/libc/src/fenv.c`, so AArch64 linking failed on `fegetround` and
`fesetround`. The generator now includes `fenv.c`, and all five generated
builders have been regenerated. The `hello-pi` static PIE builder now uses the
zero-based `userspace/linker/user.ld` layout on every port, matching XSCore's
ET_DYN load-bias/relative-relocation model. The compositor POSIX-only test now
runs `hello-pi --posix-test` to exercise PIE relocation on the selected port.

AArch64, ARM32, RISC-V64, and RISC-V32 rebuilt and each passed the focused
`hello-pi --posix-test`, mmap, epoll, and exit-group guest path. ARM32 exposed
ARM EABI `__aeabi_memcpy8` from aggregate copies of the relocation bitmap;
`process_dynamic_metadata_copy()` now copies fields and words explicitly.
RISC-V64 exposed missing `__floatdidf` from `difftime()`; the compiler-rt
builder now builds and verifies `floatdidf.c` for both RV64/RV32. The generated
x86-32 libc builders also enable x87 for `time.c`, resolving `__floatsidf` and
`__subdf3`. Its dynamic CRT now calls `__akaos_libc_run_init_array()` before
`main`, initializing the shared stack guard; the refreshed x86-32 full
compositor/client suite passes, including dynamic C++ and D-Bus tests.

The full six-architecture compositor suite is still in progress. Its refreshed
x86-64 pass reached `hello --desktop-runtime-test` and failed at stage 508,
the asynchronous D-Bus descriptor watch, after earlier POSIX, C++ runtime,
D-Bus broker, GTK/Qt compatibility, and Wayland checks passed. This is a
remaining x86-64 integration failure to diagnose; it is not a full suite pass.

## 2026-10-09 — x86-64 static-PIE relocation-page sharing and POSIX verification

The x86-64 C utility and account-program builders in `build.sh` now link
ordinary executables as static PIE (`ET_DYN`), using XSCore's existing
`R_X86_64_RELATIVE` relocation path at the native user load base. The previous
`ET_EXEC` layout linked at virtual address zero but was demand-mapped at
`0x8000000000`; an absolute pointer from the `ctime` weekday table therefore
remained at unmapped address `0x5448f`. The serial/QEMU trace reproduced the
page fault while `hello --posix-test` entered the `ctime`/`localtime` path.
The fresh build reports `HELLO.ELF` type `ET_DYN`, creates an 85,494,667-byte
initramfs (627 entries), and writes a 186,972,160-byte dual-firmware ISO.

The compositor test harness no longer waits twice for one-time C++ stack
guard messages, checks the static constructor marker before the `main()` guard
marker, and checks the markers emitted by one POSIX test command as an
unordered complete set. This prevents marker ordering from creating false
timeouts while still requiring every expected result. A follow-up fix removed
an accidental second addition of the absolute `str.find()` index, which had
advanced the next prompt search past the prompt already present in the log.

The first PIE boot exposed a second kernel issue: the loader conservatively
excluded every ET_DYN page from the immutable file-page cache. XSCore's boot
self-test requires identical, unrelocated executable text pages to be shared.
`process_dynamic_metadata` now carries a precomputed bitmap of relocation
target pages. Page faults skip relocation-table scans for clean pages, cache
unrelocated read-only pages normally, and keep every relocation-bearing page
private. `xscore_user_process_self_test()` now reports specific failures for
missing base pages, clean-text sharing, process stack isolation, and user pages
leaking into the kernel root.

Both x86_64 firmware kernel paths now pass the focused guest suite. BIOS used
a BIOS-only GRUB ISO containing the updated `XSCORE-BIOS.ELF`; UEFI used OVMF
with a fresh FAT32 disk containing the updated `XSCORE.ELF` and the akaOS EFI
loader. The complete `AKAOS_TEST_POSIX_ONLY=1` path passed through the kernel
process-isolation/lifecycle self-test, `ctime/asctime`, POSIX, private mmap,
epoll, exit-group, and upstream Wayland checks. The last (UEFI) transcript is
`/home/lessplum3393/.cache/akaos/test/logs/test-compositor-clients-x86_64.log`;
it includes UEFI loader/framebuffer milestones and no `[FATAL]` marker.

The EFI executable used for UEFI testing is
`build/esp/EFI/BOOT/BOOTX64.EFI` (7 KiB). `build/BOOTX64.EFI` is the GRUB EFI
image and must not be used as the akaOS loader. The full dual-firmware
`build/akaos.iso` was generated before the relocation-page fix; it still needs
rebuilding with both fresh kernel ELFs and testing through both El Torito
entries.

QEMU's direct `-kernel XSCORE-BIOS.ELF` shortcut is not supported for this
image: QEMU exits with “Error loading uncompressed kernel without PVH ELF
Note”. Use the BIOS GRUB ISO for this Multiboot2 kernel regression.

Windows-side ISO catalog inspection confirms a valid BIOS El Torito entry
(platform 0, bootable indicator `0x88`) and a UEFI section (platform `0xef`,
bootable indicator `0x88`). The earlier trace-enabled dual-ISO attempt timed
out before serial output; it was not used as verification. The full ISO still
needs to be relinked with the page-cache fix and booted under both BIOS and
UEFI.

The x86-64 compositor test harness accepts `AKAOS_TEST_ISO`,
`AKAOS_TEST_X86_64_KERNEL`, `AKAOS_TEST_X86_64_UEFI_DISK`,
`AKAOS_TEST_OVMF_CODE`, `AKAOS_TEST_OVMF_VARS_SOURCE`, and
`AKAOS_TEST_LOG_DIR` overrides so tests can use cached Ubuntu files without
repeatedly reading or writing large artifacts under `/mnt/c`. The relocation
bitmap has only been built and exercised on x86_64 so far; the other five
architectures remain unverified after this kernel change.

The Qt 6.12 target build is still in progress overall. Its reproducible QtBase
port patch now skips glibc `features.h` and unsupported System V IPC headers for
akaOS, includes the akaOS time declarations, and avoids the Linux-only
`open64` selection. The libc headers now provide `clockid_t` and use a
C++-valid cast when reading `msg_control`. The target compile last exposed
these libc-header issues, so sync the current public libc/SDK headers and
runtime libraries into `/home/lessplum3393/.cache/akaos/qt6.12.0/akaos-target`
before resuming the Widgets build. No Qt Widgets or QtWayland target has yet
completed.

## 2026-10-09 — GTK3 Wayland shared-memory path

Upstream GTK 3.24.52 now creates a working Wayland SHM surface through Lumen
on x86_64. The root cause was the akaOS GTK port removing Linux syscall-number
headers: GTK then forced its `shm_open` fallback, which returned `EINVAL`.
`tools/patch-upstream-gtk3.py` now routes GTK through akaOS libc's
`memfd_create()` API, then verifies the resulting SHM pool and `wl_buffer`.
The GTK3 build script applies this port adaptation reproducibly.

The rebuilt x86_64 image passes `tools/test-upstream-gtk3.sh` under Ubuntu
WSL/QEMU TCG: GTK3 maps and presents its window, button activation works, and
keyboard/pointer, popup dismissal/focus restoration, constrained
move/resize/window states, and synchronized subsurface checks pass. The log
is `build/test-upstream-gtk4.log`. Wayland NULL-proxy guards and the SHM trace
are opt-in diagnostics used by the test binary.

This is x86_64 GTK3 client coverage, not a stock LXDE/LXQt/KDE/GNOME session.
GTK3/Wayland builds on non-x86 targets and full desktop package/session
compatibility remain open.

## 2026-10-08 — allocator six-port verification and dual-firmware desktop checks

The shared akaOS allocator now keeps a free-block list and a header
back-reference for each returned pointer. Allocation searches only free blocks;
free/realloc header lookup is constant-time instead of walking every heap block.
Coalescing, aligned allocations, and `realloc` preserve the physical block list.
`hello` now stresses 128 differently sized allocations with shuffled frees,
checks `aligned_alloc` and `posix_memalign`, and verifies `posix_memalign`
preserves `errno` and the caller's pointer on invalid alignment. All six guest
architectures passed this allocator contract: x86_64, x86_32, AArch64, ARM32,
RISC-V64, and RISC-V32. Evidence is in the per-port test logs and
`build/desktop-readiness-run.log` for x86_64.

The x86_64 FAT32 UEFI image passed the full `tools/test-desktop-readiness.sh`
guest run, including root login, the allocator test, dynamic loading,
pthread/D-Bus/runtime probes, and compatibility-session component lifecycle.
This readiness suite launches akaOS compatibility components; it does not run
stock KDE, GNOME, LXDE, or LXQt sessions.

The refreshed `build/akaos.iso` passed `tools/test-iso.sh` through both OVMF
UEFI and legacy BIOS to the full user-mode milestone. The same ISO passed
`tools/test-upstream-gtk4.sh` under Ubuntu WSL/QEMU TCG: GTK 4.22.5 mapped a
Wayland window through Lumen and passed keyboard/pointer, popup dismissal and
focus restore, nested synchronized subsurfaces, move/resize constraints, and
maximize/fullscreen/restore/minimize checks. The client now waits for real
widget allocation and validates a `GtkBox` tree's bounds: window 640x480, box
640x441, button 606x407, and label 640x16. The final transcript contains no
GTK warning or `[FATAL]` marker.

One build-cache issue was repaired: skipping upstream refresh reused a GTK
sysroot missing `gsk/gskglshadernode.h`. Running the pinned GTK 4.22.5 build
and install restored the header; the normal x86_64 build and both firmware ISO
tests then passed. No GTK source-version change was needed.

The end-to-end KDE, GNOME, LXDE, and LXQt objective remains open. The GTK
client/window integration is x86_64-only and is not a full stock desktop
session. Complete GLib/GIO/GTK/Qt ABI coverage, upstream desktop package
compatibility, full display-protocol surface, multiwindow sessions, and
non-x86 Wayland remain unfinished.

## 2026-10-07 — xdg_toplevel window-state transitions

Historical checkpoint, superseded by the 2026-10-08 allocator and clean GTK
verification above.

The x86_64 Wayland bridge now handles client maximize/unmaximize,
fullscreen/unfullscreen, and minimize requests. It reports maximized/fullscreen
state in `xdg_toplevel.configure`, restores saved window geometry, and publishes
MAXIMIZED/FULLSCREEN/MINIMIZED through the shared Lumen compositor protocol
(version 9). A state request cancels a pointer move/resize for that same
surface; when a prior resize configure is still outstanding, the requested
state is deferred until the client commits its acknowledged replacement
buffer. Pointer move/resize also uses the coordinates associated with the
validated press serial so motion arriving before request dispatch is not lost.

`AKAOS_TEST_SUBSURFACE_ONLY=1 tools/test-upstream-gtk4.sh` passes on x86_64,
including movement, min/max-constrained resizing, maximize, fullscreen,
restore, and minimize, with Lumen slot state/geometry checks. The full GTK
Wayland interaction path passed with the instrumented GTK library and a
300-second startup window: a real surface mapped and was presented, keyboard/
pointer input worked, popovers dismissed/reopened, and move/resize plus
window-state transitions passed. The Default CSS theme parsed slowly. After
removing the upstream probes and rebuilding a clean library, xkbcommon, GTK
type, and GTK environment preflights passed with extended timeouts, but the
external GTK window did not reach its mapped marker within 300 seconds. A
clean-image run with test-client tracing and `WAYLAND_DEBUG=1` completed
`gtk_init_check()` after registry/output/seat round trips; generic GObject
creation and GtkLabel class init passed, but GtkLabel instance construction did
not return within 600 seconds. Another retry timed out before `login:` after
process 1's demand-loaded ELF page and without a kernel fatal dump. The harness
captures QEMU stderr and no longer reports skipped preflight checks as PASS.
The clean `AKAOS_TEST_SUBSURFACE_ONLY=1` rerun also timed out before Lumen's
titlebar-drag marker, so QEMU input-test timing is intermittently unreliable.
Clean GTK/Wayland verification remains incomplete. WSL exposes `/dev/kvm` but
the current account cannot open it; tests use TCG. The GTK harness now selects
KVM automatically when available, otherwise logs the TCG fallback.

The full bounded compositor IPC/palette lifecycle test passes on x86_64,
x86_32, AArch64, ARM32, RISC-V64, and RISC-V32 with protocol v9. AArch64
faulted once after the exit-group probe, then passed a clean focused retry and
the final six-port matrix. Do not claim KDE, GNOME, LXDE, or LXQt session
compatibility.

## 2026-10-07 — xdg_toplevel.resize round trip and min/max constraints

The x86_64 Wayland bridge now handles client `xdg_toplevel.resize` requests
validated against the same client's focused surface and live left-button
serial. It computes edge-relative geometry, applies the client's min/max size
hints, sends xdg configure serials, and waits for an acknowledged replacement
SHM buffer before publishing a Lumen resize and configure ACK. The upstream
Wayland QEMU client verifies a requested bottom-right enlargement clamps to
670x480, then a shrink clamps to 600x480, and confirms the new dimensions in
the compositor slot. The full GTK/Wayland integration test also passes with
popup dismissal, titlebar dragging, client-requested movement, and nested
subsurfaces.

This is x86_64 Wayland coverage only. Maximize/fullscreen/minimize state
requests, resize grips/state handling in Lumen, GTK layout compatibility,
persistent sessions, non-x86 Wayland, and the four stock desktop sessions
remain incomplete.

## 2026-10-06 — Client-initiated xdg_toplevel.move verified

The x86_64 Wayland bridge now honors `xdg_toplevel.move` only when the request
matches the same client's focused toplevel and a live left-button press serial.
It tracks pointer motion while the grab is held, submits compositor MOVE
requests, and releases the interaction on button-up or resource teardown. The
upstream Wayland QEMU client sends a real move request after receiving a
`wl_pointer.button` serial and verifies the parent plus direct/nested
subsurface positions move together. The same integrated test separately
verifies Lumen titlebar dragging before the client-requested move.

The full `tools/test-upstream-gtk4.sh` run passes on x86_64, including GTK
keyboard/pointer input, Escape/outside-click popup dismissal and focus restore,
nested subsurface staging/stacking, titlebar drag, and `xdg_toplevel.move`.
The GTK popover test waits for the `closed` signal and actual unmap before
recreating it for the second dismissal case, avoiding a stale Wayland popup
role on reuse. Interactive resize and min/max constraints are now verified in
the latest x86_64 QEMU run; window-state requests remain incomplete.
`tools/test-compositor-clients.sh` has passed on all six architectures after
the shared Lumen Escape change; the Wayland bridge and upstream desktop path
remain x86_64-only. Interactive resize, min/max/fullscreen state, persistent
sessions, and stock desktop environments are still incomplete.

## 2026-10-06 — Wayland popup dismissal and centered toplevel placement

The x86_64 Wayland bridge now centers a toplevel from its first actual SHM
buffer dimensions rather than the 640x480 fallback used before the client
commits. For a grabbed `xdg_popup`, Escape or an outside pointer press sends
`xdg_popup.popup_done` to the dismissed popup stack, unmaps those popup
surfaces, restores keyboard focus to the retained parent/root toplevel, and
suppresses Escape's matching release. Lumen no longer exits on Escape.

`tools/test-upstream-gtk4.sh` passes on x86_64: GTK receives keyboard/pointer
input, its popover closes both on Escape and on an outside desktop click, and a
follow-up key reaches the parent after each dismissal. The same run passes
synchronized nested-subsurface presentation, sibling stacking, and titlebar
drag propagation. The shared POSIX compositor matrix passes on all six
architectures. External GTK/Wayland remains x86_64-only; resize/maximize
configure, non-x86 Wayland, persistent sessions, and stock desktop sessions
remain incomplete.

## 2026-10-06 — Nested Wayland subsurface trees verified through Lumen

The fresh `tools/test-upstream-gtk4.sh` QEMU run passes upstream xkbcommon
keymap translation, GTK 4.22.5 widget registration, a GTK Wayland window,
keyboard and pointer input, an `xdg_popup`, cursor/popup presentation, and the
external subsurface client. That client creates an `xdg_toplevel`, two direct
synchronized children (one below and one above the root), and a grandchild
nested under the lower child. It verifies the nested parent/stack metadata and
that synchronized child commits remain staged until the root commit. QEMU then
drags the Lumen titlebar: the log reports drag start at (512, 124), movement to
(197, 149), and successful propagation to the direct and nested child slots.
All expected markers are present and the log contains no `[FATAL]`.

The compositor channel is ABI 2 / protocol 8. The POSIX-only compositor
IPC/lifecycle matrix passed after rebuilding x86_64, x86_32, AArch64, ARM32,
RISC-V64, and RISC-V32. These are shared compositor and POSIX checks; the
upstream Wayland bridge/client path remains x86_64-only. KDE, GNOME, LXDE, and
LXQt sessions are still incomplete and are not 100% working.

## 2026-10-05 — POSIX memfd I/O, XKB keymap, and six-port verification

The shared XSCore/libc changes now make `read()` and `write()` requests above
`AKAOS_SYSCALL_MAX_IO` (4 KiB) complete as valid short transfers rather than
failing with `EFAULT`. Memfd descriptors now support `fstat`, `lseek`,
descriptor read/write, and shared/private mappings. File-backed `MAP_SHARED`
is allowed for immutable, read-only regular files; it uses a read-only snapshot,
and `mprotect(PROT_WRITE)` is denied. Mutable VFS files still do not have
coherent file-backed `MAP_SHARED` semantics.

The x86_64 `/bin/wayland-bridge` now sizes and populates the Wayland keyboard
keymap memfd through `MAP_SHARED`, advertises a minimal `wl_seat`, and sends an
upstream xkbcommon 1.13.2 `evdev`/`pc105`/`us` keymap. The external GTK 4.22.5
QEMU test passes: a SHM toplevel reaches Lumen's framebuffer writer; an `a` key
press reaches GTK; a pointer press activates the button; GTK creates an
`xdg_popup`; and Lumen presents the configured popup and GTK cursor surface.
This is x86_64 protocol smoke coverage, not broad keyboard-layout/focus,
complete subsurface-tree, screenshot, or desktop-session coverage.

Core `wl_subcompositor` v1 is advertised by the x86_64 Wayland bridge. Basic
subsurface roles support parent-relative positioning, synchronized/desynchronized
commits, and buffered stack requests. The external upstream Wayland client test
stages two child buffers, confirms Lumen has not presented them, commits the
parent, verifies one child below and one above the parent in compositor z-order,
then performs a QEMU mouse drag on the Lumen titlebar and verifies both child
positions follow. Lumen presents the resulting tree. This external protocol
test is x86_64-only; nested subsurface trees and broader window movement remain
unverified.

Libc environment mutation now rebases its owned string vector when startup
code or an application changes `environ` after libc has copied it. The GTK
regression performs additions and replacement through `setenv()` and checks
`getenv()`; the external Wayland client also sets its session variables through
libc. A separate POSIX regression replaces `environ` with a shorter
stack-owned vector, checks `setenv()` add/replace, `unsetenv()`, and later
`posix_spawnp()`, and passes in QEMU on all six targets.

Rebuilt all six architecture targets after shared Lumen `POPUP`/`CURSOR`/
`SUBSURFACE` handling and compositor channel protocol version 6, then passed `AKAOS_TEST_POSIX_ONLY=1
tools/test-compositor-clients.sh` in QEMU on x86_64, x86_32, AArch64, ARM32,
RISC-V64, and RISC-V32. The matrix covers memfd descriptor I/O, fstat/seek,
>4 KiB short-transfer loops, immutable file mappings, epoll high descriptors,
and detached-thread exit-group cleanup. `tools/test-upstream-gtk4.sh` also
passes on x86_64 with a configured and framebuffer-presented GTK popup/cursor.
These tests do not establish GTK/Wayland desktop support on the other five
ports or 100% KDE/GNOME/LXDE/LXQt sessions.

Still open: synchronous SIGBUS with correct `si_addr`, coherent shared mappings
of mutable VFS files, mapped-file truncation semantics, broader keyboard focus
and layout behavior, nested subsurface trees/stacking, nested-popup
dismissal/full resize constraints,
clipboard protocols, and persistent desktop session services. The six-port matrix boots the architecture kernel artifacts;
it does not establish UEFI/BIOS ISO/FAT32 parity for every non-x86 port. GTK/
Wayland desktop integration remains x86_64-only, and the four stock desktop
environments remain incomplete.

## 2026-10-05 — External GTK window through Lumen; exit-group regression fix

Added an x86_64 `/bin/wayland-bridge` built against upstream Wayland 1.26. It
exports a minimal `wl_compositor`/`wl_output`/`wl_shm`/xdg-shell server,
imports XRGB/ARGB client buffers, and publishes them as Lumen compositor
surfaces. Lumen now blends premultiplied ARGB pixels and emits its presentation
marker only after its framebuffer write succeeds. The expanded QEMU
`tools/test-upstream-gtk4.sh` test passes with a separate GTK process and
reports `lumen: Wayland surface presented on framebuffer`.

GTK teardown exposed process exit-group behavior: detached pthread workers
could survive their leader, then execute from an unmapped DSO. XSCore now
requests sibling user-thread termination, wakes blocked siblings, handles
requests at kernel yield/block or on user interrupt return, and waits/reaps
them before freeing the address space. The detached-worker regression and
prompt return now pass on x86_64, x86_32, AArch64, ARM32, RISC-V64, and
RISC-V32; none of the six transcripts contains `[FATAL]`.

At this checkpoint the bridge was the first minimal window path. A later
continuation added a basic seat and pointer/keyboard protocol plumbing; the
pointer path is tested, while actual key-event delivery and broader desktop
protocols are still open. No 100% desktop claim.

## 2026-10-05 — First upstream GTK Wayland window/SHM commit (x86_64 only)

The x86_64 BIOS QEMU smoke now passes with upstream GTK 4.22.5's Wayland
backend enabled. It loads GTK and GObject types, initializes against an
in-process minimal Wayland test server, creates/presents a `GtkWindow`,
receives xdg-shell configure, renders with Cairo, and commits a real `wl_shm`
buffer. Two plain registry clients are opened and closed before GTK connects,
covering sequential client teardown/reconnect. Upstream xkbcommon 1.13.2's
keymap + Shift translation passes in the same run.

This is a headless protocol/SHM smoke: it does not show pixels through an
akaOS framebuffer compositor, and is not a persistent Wayland session,
seat/input test, GPU/EGL implementation, or stock desktop session. KDE,
GNOME, LXDE, and LXQt remain incomplete and are not 100% working.

Supporting fixes: `dlopen(NULL)` now returns a stable main-program handle so
GTK's GModule probes do not call symbol/close APIs with null; native descriptor
tables grow from 16 inline slots to 256; libc epoll accepts native descriptors
through 255 and stores 64 watches; the 64-bit signal trampoline moved to
+24 MiB, after the 16 MiB brk heap (+4 MiB base) and before the +32 MiB mmap
arena. After raising `AKAOS_POLL_MAX_FDS` from 16 to 64, the POSIX-only
`tools/test-compositor-clients.sh` matrix passed on all six ports, including
the 20-pipe epoll case with fd numbers above 16. The full x86_64 client
lifecycle suite and focused GTK Wayland smoke also pass.

## 2026-10-04 — GTK Wayland prerequisites (build and runtime still pending)

After the passing Wayland protocol/SHM smoke and six-target IPC matrix, started
the next x86_64 toolkit tranche. Pinned Wayland protocols 1.46 are installed
under the graphics sysroot; upstream xkbcommon 1.13.2 builds and installs as
`libxkbcommon.so.0`, with a guest test source for keymap parsing and shifted
`a`/`A` translation. That test has compiled but has not run in QEMU.

Enabled GTK 4.22.5's upstream Wayland backend and generated its protocol
bindings. GTK's source requires libepoxy's EGL dispatch header even when a
software Cairo renderer is intended, so built libepoxy 1.5.11's EGL dispatch
and staged pinned Khronos `eglplatform.h`/`khrplatform.h` headers. This is an
API dispatcher/header prerequisite, not an EGL implementation or GPU support.
Added Linux-compatible mouse-button event codes for GTK's Wayland translation
path, and added libc `strsep()` after GTK used that BSD/GNU extension.

The Wayland-enabled GTK DSO has now compiled and installed in
`build/upstream-build/gtk-4.22.5/x86_64-akaos-wayland`; the x86_64 BIOS ISO was
rebuilt with it. The QEMU xkbcommon 1.13.2 keymap/modifier smoke passes and
translates `a`/`A`. The GTK GObject load and GtkWidget/GtkLabel/GtkButton
registration checks also pass, but the GTK Wayland window smoke currently
faults with CPU exception vector 14, error code 4, CR2 `0x3`, immediately after
the GObject checks. Stage traces now show the first
`setenv("XDG_RUNTIME_DIR", ...)` succeeds and the subsequent
`setenv("WAYLAND_DISPLAY", ...)` faults before return. The test now replaces
its environment vector directly to isolate GTK startup; this is test setup, not
a libc `setenv` fix. Reproduce and fix dynamic-process `setenv` separately.
After avoiding that mutation, a direct Wayland registry probe passes. GTK's
own backend then rejected the test display because `wl_data_device_manager`
was missing; the test server now advertises a minimal v1 manager. GTK proceeds
to GLib main-context setup, where `GWakeup` fails creating its pipe and
terminates. XSCore previously had only 16 native descriptors and kept fd arrays
inline even in kernel-stack helper tables. The implementation is being changed
to 16 inline slots with heap-backed growth to 256, 512 shared file objects,
and 64 pipes; libc's fake eventfd/epoll/timerfd ranges are moved above the
native descriptor space. This latest kernel/libc change has not yet been
rebuilt or tested. No GTK Wayland window has committed a buffer successfully;
no compositor or desktop session is proven.

The source also adds libc `strsep()` and a regression in the shared POSIX test,
but its guest runtime check and the six-architecture matrix have not yet been
rerun after that change. Continue with the current x86_64 smoke-debug cycle,
then run the six-architecture regression. SIGBUS `si_addr`, actual compositor
presentation, X11/XCB, GTK 3, Qt, D-Bus/session services, and stock KDE/GNOME/
LXDE/LXQt sessions remain open.

KDE, GNOME, LXDE, and LXQt remain incomplete and are not 100% working.

## 2026-10-04 — Wayland prerequisite ABI and guest protocol smoke

Implemented kernel-backed `flock(2)` for VFS regular files (shared/exclusive,
nonblocking conflicts, unlock, and close release), exposed `sys/file.h`, and
added a guest regression. The first test exposed a lock-table bug that treated
an unlocked open description as a conflicting exclusive holder; the scan now
considers only explicit shared/exclusive holders.

Added the native `siginfo_t` record and `SA_SIGINFO` handler arguments across
all six architecture signal frames. Fixed i386 `sigreturn` lookup after the
three-argument cdecl signal stack, and corrected AArch64 argument registers so
`siginfo_t *` and context point into the correct saved frame. POSIX-only QEMU
regressions pass on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32,
including memfd seals, flock, and asynchronous `raise(SIGUSR1)` signal
metadata. Synchronous SIGBUS delivery with a real `si_addr` is still missing.

The pinned compiler-rt archives now include `__extendsftf2` for AArch64 and
both RISC-V widths, plus RV32 `__fixtfsi`; these fixed cross-port link errors
from long-double conversions used by `lrint*`/`nearbyint*`. AArch64 and RV32
images now link; AArch64's POSIX guest subset passes.

Upstream Wayland 1.26.0 client/server/cursor/EGL DSOs are now packaged into
the x86_64 image. The guest QEMU smoke starts a real `wl_display`, adds a
runtime-directory Unix socket, connects an upstream Wayland client, exchanges
registry/global/bind messages, and creates a `wl_shm` pool backed by memfd and
SCM_RIGHTS. The `Upstream Wayland 1.26 registry binding and memfd SHM verified`
marker passes. This is a protocol/SHM smoke, not a compositor window or a
desktop session.

The smoke exposed and fixed three additional prerequisites: `eventfd` now
supports `F_DUPFD_CLOEXEC` aliases for Wayland's event-loop wake fd, XSCore
`recvmsg` accepts `MSG_CMSG_CLOEXEC` and marks received rights close-on-exec,
and Unix `bind`/`connect` accept Linux's variable-length pathname
`sockaddr_un` layout. The Wayland SHM protocol smoke is x86_64-only.

After those shared kernel changes, the complete compositor/client lifecycle
matrix passed on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32. It
includes POSIX flock/SA_SIGINFO and SCM_RIGHTS close-on-exec probes on all six;
the direct upstream Wayland registry/SHM smoke runs only on x86_64.

Still open: synchronous SIGBUS delivery with the correct `si_addr` when a
shared pool is truncated, active-mmap `ftruncate` semantics, GTK's Wayland
backend/window, X11/XCB, upstream Qt/GTK 3, and running any real KDE, GNOME,
LXDE, or LXQt session.

Added opt-in `AKAOS_SKIP_UPSTREAM_REFRESH=1` to `build.sh` for iterative
kernel/libc builds against cached upstream DSOs. The default still refreshes
the pinned upstream libraries.

## 2026-10-03 — POSIX runtime and UEFI/FAT32 large-kernel boot regression

The x86-64 BIOS ISO passed `AKAOS_TEST_POSIX_ONLY=1` in
`tools/test-compositor-clients.sh`, including the new
`POSIX getline/getdelim growth and exclusive mkstemp creation verified`
marker. This exercises long-line growth, delimiter reads, EOF behavior, and
two distinct temporary paths. The refreshed six-target libc/hello compile
gate also passes: x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32.
Runtime coverage for these new libc calls is still x86-64 only.

Fixed the 64 MiB FAT32 disk limit in `build.sh`: the image now derives its
size from `XSCORE.ELF` plus 64 MiB headroom, with a 128 MiB minimum. The
current embedded-kernel image is 133 MiB and the 71,091,332-byte initramfs
packages successfully.

The first OVMF boot exposed two UEFI loader assumptions. `XSCORE.ELF` had
grown beyond its 64 MiB read cap, then a larger whole-file buffer overlapped
the kernel's fixed physical `.data` segment. The x86-64 UEFI loader now gets
the file size through EFI_FILE_INFO, reads the ELF header/program table, and
streams each PT_LOAD segment directly from the EFI file into its allocated
physical destination; it releases the small program-table buffer before
exiting boot services. The QEMU OVMF `tools/test-auth.sh` run passes login,
root and regular-user identities, root-only file access, and the tested
privilege boundary. Its boot log confirms the UEFI handoff.

`tools/test-auth.sh` now accepts `AKAOS_TEST_SKIP_BUILD=1` for testing an
already-built FAT32 image; its UEFI login wait is 180 seconds to allow the
larger kernel's demand-loaded boot path. Some Ubuntu WSL `/mnt/c` Meson
install steps still pause temporarily in `p9_client_rpc`, then resume; no
Arch fallback was used.

Overall goal remains active and far from complete. GTK is Broadway-only and
no desktop session has run. Upstream Wayland still needs kernel-enforced memfd
seals, `siginfo_t`/`SA_SIGINFO` with fault addresses, and `sys/file.h`/`flock`.
No 100% KDE/GNOME/LXDE/LXQt claim.

## 2026-10-03 — POSIX line input/temp-file prerequisite (initial compile checkpoint)

Added libc `getdelim()`/`getline()` with dynamically growing caller buffers and
`mkstemp()` with six-`X` validation, `O_CREAT|O_EXCL`, mode 0600, and bounded
collision retries. Added a `hello --posix-test` guest probe for long-line
growth, delimiter reads, EOF, and distinct temporary names; the compositor
test harness now requires its marker. The six-target libc/hello compile gate
passed for x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32 before a
parameter-only rename needed to keep the public declaration valid in C++;
the subsequent x86_64 image build passed the LLVM C++ runtime header stage.

At this first checkpoint, runtime verification was pending. The x86_64 C++
header build exposed `template` as a reserved keyword in the declaration;
that name was changed to `template_path`. Subsequent entries above record the
completed BIOS and OVMF guest checks. The intermediate WSL Meson stall later
resumed; it was not a persistent build blocker.

Remaining upstream Wayland 1.26.0 blockers from the prior compile: memfd seal
API/enforcement (`F_GET_SEALS`, `F_ADD_SEALS`, seal flags), `siginfo_t` and
`SA_SIGINFO` delivery (especially SIGBUS `si_addr`), and `sys/file.h`/`flock`
semantics. The Wayland target has not been rebuilt after the line-I/O changes;
the earlier GTK DSO smoke still does not establish a GUI/session. KDE, GNOME,
LXDE, and LXQt remain unfinished and are not 100% working.

## 2026-10-02 — Upstream GTK 4.22.5 library load smoke (x86-64 only)

The checksum-pinned upstream GTK 4.22.5 source now builds as a real
`libgtk-4.so.1` DSO against the existing upstream x86-64 GLib/GObject/GIO,
Graphene, Cairo/Cairo-GObject, Pango, HarfBuzz, Fontconfig, FreeType, FriBidi,
GdkPixbuf, codecs, and libepoxy stack. `tools/build-upstream-gtk4.sh` uses
Ubuntu host `glib-compile-resources` and `glib-compile-schemas` for build-time
generation; it must not attempt to execute the akaOS target binaries on WSL.
The normal x86-64 dual-firmware ISO packages the real GTK soname under
`/lib/.upstream-gtk/` and leaves akaOS's existing compatibility library at its
old path. The dynamic loader uses the upstream directory marker to route GTK4
sonames to the real DSO. Raising the x86-64 per-process mmap budget to 64 MiB,
extending its virtual range, and reserving a 1 MiB boot heap lets the larger
ELF dependency graph and staged exec image fit without breaking reclamation
tests.

`tools/test-upstream-gtk4.sh` passes in QEMU: it loads GTK 4.22.5, registers
`GtkWidget`, `GtkLabel`, and `GtkButton` through upstream GObject, and confirms
their type hierarchy. `tools/test-upstream-pango.sh`,
`tools/test-dynamic-loader.sh` (x86-64), and
`tools/test-compositor-clients.sh` (x86-64) also pass on the rebuilt ISO.
`tools/test-desktop-libc-cross-compile.sh` now includes `dlfcn.c` and passes
compile-only checks for all six architectures.

This is a shared-library/API smoke, not a GTK window or desktop-session test.
GTK was configured with Broadway only; X11 and Wayland are disabled, and the
installed `gtk4-broadwayd` target currently lacks a valid akaOS `_start`
runtime entry. No upstream display server/client session has run. GTK 3,
upstream Qt, native X11/XCB/Wayland integration, and actual unmodified KDE,
GNOME, LXDE, or LXQt sessions remain unfinished. Keep the overall goal active;
never claim 100% desktop support.

## 2026-10-01 — Upstream Graphene geometry runtime and six-target math regression

Pinned GNOME Graphene 1.10.8 now cross-builds for akaOS x86-64 as its real
`libgraphene-1.0.so.0` DSO with upstream GObject boxed types enabled. The
checksum-verified `tools/build-upstream-graphene.sh` build is wired into the
x86-64 image build, and `tools/test-upstream-graphene.sh` passes in QEMU. The
guest probe applies a translation matrix, checks 3D point results, tests
rectangle containment and vector dot products, and confirms Graphene matrix
and rectangle GTypes are registered. This is real Graphene API execution, not
a GTK or desktop session.

Graphene exposed missing standard `fmax`/`fmin` symbols in akaOS libm. Added
double, float, and long-double forms with NaN selection and signed-zero
semantics; the shared `hello --posix-test` now checks representative cases and
reports those checks explicitly. A fresh `tools/test-compositor-clients.sh`
run rebuilt and passed x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32.

Official GTK 4.22.5 and GTK 3.24.52 source archives are downloaded and their
GNOME SHA-256 sidecars verified, but neither GTK toolkit is built yet. The
text/rendering stack and Graphene remain x86-64-only; real X11/XCB and Wayland
backends, upstream display/session services, and unmodified desktop sessions
remain open. Do not claim KDE, GNOME, LXDE, or LXQt are 100% working.

## 2026-10-01 — Upstream Pango and large POSIX stdio on akaOS x86-64

Pinned upstream Pango 1.58.2 now builds as real `libpango-1.0`,
`libpangoft2-1.0`, and `libpangocairo-1.0` DSOs against akaOS GLib 2.90,
HarfBuzz 14.5, FriBidi 1.0.17, Fontconfig 2.18.3/Expat 2.8.5, FreeType
2.14.3, and Cairo 1.18.6 with its Fontconfig and FreeType backends enabled.
`tools/build-upstream-pango.sh` verifies the GNOME release checksum and
`build.sh` packages the libraries plus a real Pango/Cairo guest probe.

`tools/test-upstream-pango.sh` passes in x86-64 QEMU using Pango's ordinary
Fontconfig initialization path. It loads the bundled DejaVu Sans through the
Pango font map, verifies Latin/Arabic/digit coverage and HarfBuzz glyph IDs,
shapes a mixed Arabic/Latin paragraph into four wrapped lines, and confirms
nonempty Cairo-rendered pixels. It also checks recursive `flockfile` and
`getc_unlocked` behavior. This is one real upstream-library integration smoke,
not the Pango upstream suite or a GTK/KDE/GNOME session.

The critical POSIX fix was to make libc `fread`/`fwrite` split transfers at
`AKAOS_SYSCALL_MAX_IO` (4 KiB); HarfBuzz's upstream font-file reader requested
64 KiB and previously got `EFAULT`, yielding an empty font blob. The 64-bit
XSCore processes also now have a 4096-page (16 MiB) brk heap, up from 256
pages, for real font/configuration allocations; the conservative 32-bit heap
cap remains unchanged. `tools/test-compositor-clients.sh` passed on all six
architectures after these common libc/kernel changes, and
`tools/test-desktop-readiness.sh` passed on x86-64.

`tools/build-bios.sh` stages ISO creation under WSL's `/tmp` and only copies a
completed image into `build/akaos.iso`. The latest image was verified as a
readable El Torito dual-firmware ISO. The real upstream text/rendering stack
is still x86-64 only; GTK and actual desktop sessions remain unbuilt. Do not
claim KDE, GNOME, LXDE, or LXQt are 100% working; keep the overall goal active.

## 2026-09-30 — Upstream Fontconfig/Expat and FriBidi text stack (x86-64)

Upstream Fontconfig 2.18.3 now builds as a real x86-64 DSO with upstream
Expat 2.8.5 as its XML backend. The guest smoke calls the standard `FcInit()`
path, parses the packaged `fonts.conf` and `conf.d` rules, scans the installed
DejaVu Sans font, and matches `sans-serif` to that font file. This specifically
covers default config initialization, not only explicit-config APIs.

Upstream FriBidi 1.0.17 also builds as a real x86-64 DSO. Its guest smoke
checks actual mixed Latin/Hebrew/numeric visual ordering, RTL embedding levels,
and inverse logical-to-visual cursor maps. The initramfs carries the FriBidi
license and `/bin/fribidi-upstream-test`. Both focused guest tests pass on the
fresh dual-firmware `build/akaos.iso`; upstream GLib, Cairo/FreeType/Pixman,
and HarfBuzz regression smokes also pass on that image.

Build helpers are `tools/build-upstream-fontconfig.sh` and
`tools/build-upstream-fribidi.sh`; tests are `tools/test-upstream-fontconfig.sh`
and `tools/test-upstream-fribidi.sh`. These are scoped x86-64 smoke tests, not
full upstream suites or desktop sessions. Pango, GTK, real display backends,
and upstream libraries on the other five architectures remain open. Keep the
goal active; KDE, GNOME, LXDE, and LXQt are not 100% working or verified.

## 2026-09-30 — HarfBuzz text shaping and six-target runtime/VFS regression

Upstream HarfBuzz 14.5.0 now cross-builds as a real x86-64 shared library
against akaOS FreeType 2.14.3. `tools/build-upstream-harfbuzz.sh` verifies the
official release archive checksum; `build.sh` refreshes and packages
`libharfbuzz.so.0` plus `/bin/harfbuzz-upstream-test` when the local Meson
artifacts are present. The test uses DejaVu Sans through FreeType and passes
Latin `ffi` ligature and Arabic RTL shaping in QEMU. `readelf` confirms the
HarfBuzz DSO depends on akaOS libc, libm, and FreeType.

Added `round`, `roundf`, `roundl`, `trunc`, `truncf`, and `truncl` to akaOS
libm, with tests for ties-away-from-zero, truncation, signed zero, and all
three floating types. Pinned compiler-rt archives now include the required
binary128 integer conversions for AArch64/RISC-V and the double-to-float
conversion for RISC-V; x86_32 compiles the math units and `hello` math probe
with x87 enabled. The earlier full `tools/test-compositor-clients.sh` matrix
then passed on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32.
Direct edge-case guest assertions for the new round/trunc forms run on
x86-64; full long-double/binary128 conformance across architectures remains
unverified.

The D-Bus activation-registry failure was traced to `XSCORE_VFS_MAX_NODES`
being exhausted at 256, so `/run/user/0` could not be created. Raised the
shared VFS vnode limit to 512 and precreated `/run`, `/run/dbus`, `/run/user`,
and `/run/user/0` in all six initramfs builders. The expanded table originally
made `build_node_path()`'s stack array too large; it is now bounded by the
maximum path component count and validates node IDs before dereferencing.
Service registration and Cairo then passed again. The x86_64 full
compositor/client matrix passed, including D-Bus registry write/duplicate/
unregister, loader, pthread, and desktop IPC checks. The same test ISO passed
`tools/test-iso.sh` through UEFI and BIOS; the workspace `build/akaos.iso` was
rebuilt afterward in normal (non-test) dual-firmware mode.

Focused `tools/test-upstream-harfbuzz.sh`, `tools/test-upstream-cairo.sh`,
and `tools/test-upstream-glib.sh` all pass on x86-64. Scope is still far from
desktop completion: HarfBuzz is x86-64 only; Fontconfig, FriBidi, Pango, GTK,
real X11/XCB/Wayland backends, arbitrary D-Bus/session services, upstream
desktop packages, and all-platform toolkit libraries remain unfinished.
The six-architecture suite validates akaOS-native probes, not stock KDE,
GNOME, LXDE, or LXQt. Keep the overall goal active and never claim 100% support.

## 2026-09-30 — Upstream GLib/GObject/GIO x86-64 guest milestone

Ubuntu WSL recovered and is the active build host. The akaOS source tree is
`/mnt/c/Users/iniko/Documents/Projects/akaOS`; do not work in CrazyQ Pro.
Pinned upstream GLib 2.90.0, PCRE2 10.49, GNU libiconv 1.19, libffi 3.5.2,
zlib 1.3.1, and proxy-libintl have been used in a Meson cross build targeting
`x86_64-unknown-none-elf` with host system `akaos`.

The upstream GLib, GObject, GIO, and GModule shared objects compile and link
in `build/upstream-build/glib-2.90.0/x86_64-akaos-v7`; the Meson build reached
522/522 targets. `build.sh` now refreshes that Meson build against the current
akaOS libc, then stages the actual versioned DSOs and their libffi, PCRE2,
zlib, and libintl dependencies into the x86-64 initramfs when the complete
local artifact set and Meson build metadata are present. Meson was added to
the host dependency check. The akaOS-authored compatibility objects remain
available through their separate `libglib.so`/`libgio.so` paths. The loader
uses `/lib/.upstream-glib` to avoid rewriting real upstream sonames to the
compatibility DSOs.

`userspace/bin/glib-upstream-test/main.c` is linked to the real upstream
libraries and packaged as `/bin/glib-upstream-test`. The new
`tools/test-upstream-glib.sh` boots x86-64 QEMU and passed checks for UTF-8,
GLib containers/object/variant/main-context/thread APIs, and GIO file/action
APIs. Its harness attaches the VirtIO block test image, so it also works with
the automated `--test` boot path. `tools/test-compositor-clients.sh` also
passed in x86-64-only mode on the rebuilt ISO, including the existing
dynamic-loader, D-Bus, POSIX/pthread, desktop compatibility, and compositor
IPC gates. `tools/test-iso.sh` passed on the same dual-firmware build, booting
through UEFI and legacy BIOS with the VirtIO block self-test device. The final
workspace image was rebuilt without test mode and the upstream GLib smoke test
passed again.

The full upstream build surfaced and added POSIX interfaces including
descriptor `fcntl`/close-on-exec, `execvp`, signal masks, monotonic condition
clocks, account lookups, string/search functions, `fnmatch`, resolver/service
records, `fstatat`, `fsync`, vector I/O, and socket name/options/shutdown.
The ELF loader now frees each temporary DSO file image after mapping segments,
before recursively loading dependencies. The x86-64 tracked mmap cap is 4096
pages (16 MiB); its kernel heap preallocates 64 pages so the larger process
staging record does not alter the boot reclamation baseline. A akaOS GLib
port patch is stored in `tools/glib-2.90.0-akaos.patch`.

Important limits: the upstream libraries are only built for x86-64, and the
build artifacts currently live under ignored `build/` paths; a clean checkout
still needs a reproducible bootstrap for PCRE2/libiconv, GLib sources, and the
initial Meson cross configuration.
The smoke test is not the upstream GLib test suite. Cairo PNG/display
backends, Pango/GTK, Qt, a general D-Bus/session stack, real desktop
applications, and the other five architectures remain open. KDE, GNOME, LXDE, and LXQt are not 100%
working or verified; keep the overall goal active.

## 2026-09-30 — Upstream FreeType/Cairo font rasterization (x86-64 only)

Pinned upstream FreeType 2.14.3, Pixman 0.46.4, and Cairo 1.18.6 are built
from checksummed public release archives by
`tools/build-upstream-freetype.sh` and `tools/build-upstream-cairo.sh`. The
FreeType-aware Cairo Meson variant stages the real `libfreetype.so.6`,
`libcairo.so.2`, and `libpixman-1.so.0` when the local build artifacts exist.
The x86-64 initramfs includes an unmodified DejaVu Sans font at
`/usr/share/fonts/truetype/akaos/default.ttf` plus its license.

`tools/test-upstream-cairo.sh` passed in x86-64 QEMU. The guest loaded the
real FreeType DSO, opened DejaVu Sans, rasterized a grayscale glyph, then used
Cairo's FreeType backend to draw text on an ARGB32 image surface alongside
vector fill, alpha composition, and a Pixman gradient. The full x86-64-only
compositor/client suite passed after the libc changes, including the
math/setjmp/tmpfile probes; the upstream GLib smoke also remained passing.

The graphics work exposed and implemented initial `libm` functions (`sqrt`,
`fabs`, `atan2`, `exp`, `sin`, `cos`, `fmod`, `pow`, `acos`, `tan`, and
`hypot`), with a guest math probe. This is not a complete standards libm; in
particular, the current sine range reduction is bounded and needs a proper
large-argument implementation before general-purpose claims. Added the
architecture-specific setjmp/longjmp register-save paths used by Cairo's
scan-converter unwind, standard `vprintf` and anonymous `tmpfile`, and `/tmp`
in the x86-64 initramfs. `tools/test-desktop-libc-cross-compile.sh` verifies
the math, stdio, and setjmp translation units compile for all six supported
targets. This is compile-only evidence for five targets; setjmp guest runtime
has only been exercised on x86-64.

At this checkpoint the font work was an x86-64 glyph smoke, not the
FreeType/Cairo upstream test suite. The newer HarfBuzz milestone above adds
Latin/Arabic shaping only on x86-64. Fontconfig, FriBidi, Pango, upstream GTK,
Qt, X11/XCB/Wayland display surfaces, and real desktop sessions remain
absent; none of the upstream graphics/text libraries is built for the other
five architectures.

## Shared C++ allocation semantics and final six-architecture validation (2026-09-29)

Extended `tools/build-libunwind.sh` to build upstream LLVM 21.1.8 static
`libc++.a`, `libc++abi.a`, and `libunwind.a` plus generated headers for all
six architectures. All native C++ builders link the static archives for
`cxxabi-test`. ARM32 compiler-rt includes required soft-float `__aeabi_fcmp*`
helpers; RISC-V includes `__ltsf2`/`__ltdf2`.

The builders now also create and package versioned `libunwind.so.1`,
`libc++abi.so.1`, and `libc++.so.1` shared objects, plus the dynamically
linked `cxx-dynamic-test`. The bounded loader exposes
`dl_iterate_phdr`/`dladdr`, retains main-image PHDRs, shares loaded-object
and static-TLS state across the interpreter/libc boundary, and supports the
relocations exercised by these runtime DSOs. The dynamic test verifies
containers, numeric formatting, runtime-image enumeration, unwind metadata,
and an exception crossing the shared C++ runtime.

Regenerated cross-build scripts passed Python compilation and `bash -n`.
Fresh Ubuntu WSL builds succeeded for x86_64, x86_32, AArch64, ARM32, RISC-V64,
and RISC-V32. `tools/test-dynamic-loader.sh` passed PT_INTERP, dependencies,
constructors, symbol versions, dynamic/static TLS, and reclamation on all six.
The final fresh Ubuntu WSL rebuild of all six targets succeeded. The full
`AKAOS_TEST_SKIP_BUILD=1 tools/test-compositor-clients.sh` exited 0 on x86_64,
x86_32, AArch64, ARM32, RISC-V64, and RISC-V32, including C++ static/shared
runtime, D-Bus/POSIX/pthread, akaOS GTK/Qt compatibility probes, display
protocol, desktop service, compositor, and mouse checks. The strict
`tools/test-dynamic-loader.sh` also passed on all six after this change.

Source inspection confirms the remaining desktop gap: `build.sh` and the
generated architecture builders compile the same akaOS-authored
`userspace/libc/tests/desktop_runtime.c` (154 KB) with runtime IDs 1–8, then
package the resulting compatibility objects under pthread/GLib/GIO/GTK/Qt
sonames. That file explicitly disclaims the complete upstream ABI. No genuine
upstream GLib/GTK/Qt runtime or stock desktop package has been integrated.

Removed akaOS's strong fail-fast global C++ new/delete overrides from libc so
the pinned upstream LLVM `libc++abi` implementations are selected from the
static archive or `libc++abi.so.1`. New static and dynamic guest checks
exercise `SIZE_MAX` failure, catch `std::bad_alloc`, verify a one-shot
`std::new_handler` retry, and require nothrow new to return null. Both tests
passed on every architecture. The libc retains the `std::nothrow` object
needed by the fallback SDK header.

The pthread regression previously compared a 1 ms futex timeout against
`CLOCK_MONOTONIC`, which is currently backed by centisecond-resolution
`/proc/uptime`; RV64 intermittently returned the same reported timestamp even
when the kernel correctly timed out. The test now checks sub-tick expiration
by result and separately verifies a 50 ms blocking wait with a measurable
elapsed interval. The compositor-client harness also allows two seconds for
the Openbox compatibility service loop before its D-Bus routing probe; the
final six-target run passed.

These are akaOS-native probes, not proof that unmodified upstream KDE, GNOME,
LXDE, or LXQt install or run. The initramfs GLib/GTK/Qt compatibility objects
are still akaOS implementations. Full upstream C++/toolkit ABI behavior,
locale/filesystem coverage, complete POSIX/NPTL, and stock desktop applications
remain unfinished; keep the
overall desktop goal active.

## RISC-V general-dynamic TLS relocation correction (2026-09-27)

Fixed the RISC-V dynamic loader's `R_RISCV_TLS_DTPREL32/64` values. The
loader now writes `S + A - TLS_DTV_OFFSET` (`TLS_DTV_OFFSET == 0x800`); the
RISC-V `__tls_get_addr` shim adds `0x800` back before native module-relative
TLS lookup. This corrected a reproduced null result from a DSO TLS accessor
on both RV64 and RV32. Fresh native builds passed `tools/test-dynamic-loader.sh`
and the complete `tools/test-compositor-clients.sh` run on both RISC-V widths.
Those runs cover akaOS-native probes only and do not establish 100% upstream
KDE, GNOME, LXDE, or LXQt compatibility. The RV64 fatal-trap path now also
prints the saved user register frame for future diagnosis.

## POSIX spawn file-action ABI correction (2026-09-27)

Fixed `spawn_with_environment()` in libc to pass the actual
`posix_spawn_file_actions_t.actions` array to XSCore rather than the enclosing
object (whose leading count/header had been misread as action data). Strengthened
`hello --posix-test` to redirect a spawned `echo` child's stdout into a pipe
using `posix_spawn_file_actions_adddup2`, close the inherited pipe descriptors,
reap the child, and verify the exact captured line. Reaping before reading is
required by the current XSCore descriptor-lifetime model; the test output is
small enough to fit in the pipe buffer. A fresh x86_64 UEFI/ISO build initially
verified the fix; the 2026-09-28 full compositor/client test subsequently passed
the same POSIX spawn-action probe on x86_64, x86_32, AArch64, ARM32, RISC-V64,
and RISC-V32. This remains bounded native behavior and does not establish full
POSIX or unmodified upstream KDE, GNOME, LXDE, or LXQt support.

## C++ startup, DSO finalization, and synchronized allocation on six targets (2026-09-26)

Added a bounded Itanium C++ ABI foundation for native C++ executables:
`__cxa_atexit`/`__cxa_finalize` with LIFO callback order and DSO filtering,
`atexit`, a private `__dso_handle`, thread-safe function-local static guards
backed by a recursive pthread mutex, and `__cxa_pure_virtual`. CRT startup now
runs preinit/init arrays before `main` on all six architectures, and normal
`exit()` finalizes registered destructors. C++ smoke programs are linked as
static PIE with relative segment addresses. A small shared C++ fixture now
also checks DSO lifetime: its global constructor runs after `dlopen`, and a
per-image fini-array hook calls `__cxa_finalize(&__dso_handle)` so its global
destructor runs during `dlclose` before the image is unmapped. Both x86_64
`build.sh` and the generated cross-architecture userspace builders package
this DSO. The libc heap serializes `malloc`, `free`, and `realloc` list updates
with an atomic lock, registers fork prepare/parent/child handlers so a child
cannot inherit a sibling-owned allocator lock, and rejects size overflow. The
Itanium ABI exports scalar/array, sized, nothrow, and over-aligned `new`/`delete`
forms; aligned allocations retain the original heap pointer immediately before
the aligned result. A public minimal `<new>` header exposes these forms and
placement new.

Runtime testing found/fixed two integration issues: XSCore's ET_DYN parser now
accepts harmless linker metadata needed by static PIE while continuing to
reject unsupported dependency/PLT resolution; and the five architecture
scripts with fixed-address executable linker scripts now link this PIE test
with the zero-based `user.ld` layout so XSCore applies exactly one load bias.
The parser accepts `DT_RELCOUNT`, emitted by 32-bit linkers.

Guest probes verify C++ global initialization, a four-worker race on a
function-local static (one initialization), `atexit`/local/global destructor
order, scalar/array/aligned allocation, overflow rejection, four workers
stressing `malloc`/`realloc`/nothrow `new[]`/`delete[]`, the shared-DSO
constructor and `dlclose` destructor sequence, a caught exception with a
stack-unwind destructor, RTTI catch and `dynamic_cast`, and the compositor/
client lifecycle. The six architecture transcripts under
`build/test-compositor-clients-*.log` contain both exception/RTTI markers.
The newer 2026-09-29 continuation above freshly rebuilt all six targets and
reran the complete guest suite. Versioned shared `libc++.so.1`,
`libc++abi.so.1`, and `libunwind.so.1` are packaged and smoke-tested.
Upstream libc++abi now supplies tested throwing/nothrow allocation and
`std::bad_alloc` behavior. Full locale/filesystem/thread semantics and broad
upstream ABI/toolkit compatibility remain unverified, as does unmodified KDE,
GNOME, LXDE, or LXQt execution. The overall desktop goal is active.

## Latest loader synchronization and six-target validation (2026-09-25)

Wrapped bounded loader namespace operations in a lazily initialized recursive
pthread mutex. Loads, symbol/version lookups, unloads, loader entry, and static
TLS preparation/access are serialized; recursive loading remains possible for
constructors, finalizers, and dependencies. `pthread_atfork` callbacks protect
loader/error state, then repair the recursive mutex owner and clear vanished
threads' error records in the child. `dlerror()` is now independent per
pthread using an allocation-free 64-thread table keyed by thread ID; pthread
exit releases its record. `dltest` covers independent per-thread error reads
and four workers repeatedly opening, resolving, and closing a shared DSO.

A fresh Ubuntu WSL rebuild and full `tools/test-compositor-clients.sh` run
passed on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32. The script
verified loader fan-out/capacity/concurrency, TLS, pthread/fork, D-Bus/POSIX,
toolkit/display/runtime, desktop service, compositor surface, mouse, realtime,
and no-fatal markers in every guest transcript; the script exited 0. This is
akaOS-native bounded validation, not proof that unmodified upstream KDE,
GNOME, LXDE, or LXQt can be installed or run. The loader is globally
serialized, error state is limited to 64 live pthreads, and complete POSIX/NPTL
plus upstream toolkit/desktop compatibility remain open.

## Dynamic-loader namespace scaling and six-target desktop matrix (2026-09-25)

Expanded the bounded ELF loader namespace from 8 to 64 live objects and from 4
to 32 `DT_NEEDED` entries. AArch64 TLSDESC records now grow dynamically, and
the per-object TLS thread-slot budget is 64. Added live regression coverage for
a DSO with five providers and twelve simultaneously open path aliases.

The expanded table initially caused `/bin/gzip` to exceed XSCore's fixed
executable-image mapping budget because every static program inherited its
large `.bss`. The object table is now heap-allocated only when `dlopen()` or a
main-image `PT_TLS` segment needs it. This keeps ordinary utilities small while
retaining the larger loader capacity.

Freshly rebuilt all six targets with `tools/test-compositor-clients.sh`; every
guest passed the loader fan-out/capacity tests, the full current D-Bus/POSIX/
pthread/desktop-runtime/display bridge probes, and the compositor surface and
mouse-input lifecycle. All six transcripts were independently checked for the
required success markers, realtime output, and absence of `[FATAL]` traps.
These are akaOS-native bounded tests, not proof that unmodified upstream KDE,
GNOME, LXDE, or LXQt packages can be installed and run. Loader synchronization
and reentrancy were added in the newer section above; full POSIX/NPTL coverage
and real upstream toolkit and desktop integration remain open.

## Direct executable TLS ABI fixes and six-port verification (2026-09-25)

Completed main-executable `PT_TLS` initialization before `main`, including
`.tdata` copy and `.tbss` zero-fill; dynamic interpreter startup continues to
use the loader path. Removed custom `PHDRS` from the shared and five native
linker-script pairs so LLD emits a complete `PT_TLS` segment.

Runtime tests exposed three ABI/layout differences that are now handled:
ARM32 uses its positive TP-relative TLS layout; AArch64 uses Variant I; and
RISC-V uses Variant I with the ABI-defined 16-byte TCB. Static TLS allocation
aligns the actual TP-relative address, not merely its offset, which fixes
over-aligned TLS objects on RV64. x86_64 and i386 retain their negative-offset
TLS variants. Main-image initialization, worker isolation, dynamic TLS,
pthread, D-Bus, toolkit/display probes, and compositor/input lifecycle passed
the complete guest test suite on x86_64, x86_32, AArch64, ARM32, RISC-V64, and
RISC-V32. The paired AArch64/ARM32 final run exited 0.

These are akaOS-native bounded probes. They do not prove complete POSIX/NPTL
semantics or that stock, unmodified KDE, GNOME, LXDE, or LXQt packages run.

## POSIX per-thread errno and six-architecture verification (2026-09-25)

Replaced the failed compiler-TLS errno declaration (which could not link
without a static executable PT_TLS segment) with the POSIX errno accessor
__errno_location(). libc keeps a bounded slot per live kernel thread ID,
zero-initializes each slot, releases it at pthread_exit(), and repairs the
surviving thread's slot after fork() while discarding vanished siblings.
hello --pthread-test now checks two workers retain distinct error values
across yields while the main thread keeps its own value.

A clean Ubuntu WSL rebuild and the complete tools/test-compositor-clients.sh
run passed on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32. Each guest
passed the per-thread errno, D-Bus multi-descriptor route, pthread/POSIX,
toolkit/display bridge, and compositor/mouse lifecycle gates; the test script
exited 0. errno state is bounded to akaOS's 64 live pthread slots, and this
smoke matrix does not establish complete POSIX/NPTL or unmodified upstream
KDE/GNOME/LXDE/LXQt compatibility.

## D-Bus UNIX_FD ancillary transport (2026-09-25)

Connected D-Bus body `h` indices to the bounded `SCM_RIGHTS` transport. The
libc async API queues calls with up to four owned descriptors and transfers
reply descriptors explicitly; legacy reply-taking closes descriptors it does
not return. The frame parser validates the D-Bus `UNIX_FDS` header count against
the ancillary descriptors and validates each `h` index. `dbus-daemon` forwards
rights with routed method calls, replies, and matched signals, and closes its
temporary references after routing. Built-in broker handlers reject attached
FD payloads rather than silently dropping them. hello --dbus-route-test now
passes two pipe writers from client to service and two back in the method
reply, then verifies data traverses all received descriptors. A too-small
reply descriptor array reports EOVERFLOW without losing the queued FDs.

Ubuntu WSL rebuilt the x86_64 and x86_32 firmware images and all four native
AArch64/ARM32/RISC-V64/RISC-V32 images. The full compositor/client QEMU suite
passed on all six architectures; every guest emitted `D-Bus method routing and
UNIX_FD passing verified`, and the suite also passed its pthread, POSIX, toolkit,
display-protocol, desktop-runtime, compositor surface, mouse-motion, and
mouse-button gates. `tools/build-bios.sh --no-build` also produced the x86_64
dual-firmware ISO with the oversized EFI image extended to end-of-medium per
El Torito's zero-sector-count convention. This remains bounded native behavior,
not full Linux/D-Bus compatibility: at most four non-socket descriptors are
supported; AF_UNIX sockets cannot be passed; short-control-buffer, disconnect,
and broader routing-error stress cases remain to be added; and broker built-in
methods do not accept ancillary payloads. Stock upstream KDE/GNOME/LXDE/LXQt
packages are not thereby proven installable or runnable.

## AF_UNIX SCM_RIGHTS descriptor passing (historical checkpoint, 2026-09-24)

Added libc/kernel `sendmsg()` and `recvmsg()` entry points (syscalls 74/75),
native-width `iovec`/`msghdr`/`cmsghdr` ABI layouts with compile-time libc/kernel
offset checks, and bounded AF_UNIX stream `SCM_RIGHTS` queues. A successful
send transfers references to up to four non-socket open-file descriptions;
receive installs them into the caller's descriptor table, returns
`MSG_CTRUNC` when the control buffer is short, and retains queued rights on
`EMFILE`. Plain reads discard ancillary rights they pass. The new message
syscalls hold the socket's open-file description across the operation to
prevent a racing `close()` from freeing or reusing that socket during transfer.
Closing a socket
collects and releases queued descriptor references. `hello --unix-socket-test`
now sends a pipe writer from a forked child and proves the receiving parent can
write through it into the shared pipe.

Ubuntu WSL builds passed for x86_64 and x86_32 firmware ISOs and for the
AArch64, ARM32, RISC-V64, and RISC-V32 native kernels. The full compositor/client
QEMU suite exited 0 on the freshly built x86_64 and x86_32 guests, then on all
four native guests; each run emitted the SCM_RIGHTS marker and passed its
compositor/client gates. This is bounded descriptor passing, not full Linux
socket ABI compatibility: AF_UNIX stream only, four descriptors per message,
bounded control/queue sizes, no socket-FD passing (rejected to avoid reference
cycles until Unix-socket garbage collection exists), and D-Bus `h` values are
not yet connected to ancillary transport. Complete upstream desktop support
remains open.

## D-Bus message byte order and broker integration (2026-09-24)

Integrated the `l` and `B` byte-order markers through D-Bus frame handling.
The shared message normalizer validates protocol-v1 lengths and typed header
fields, converts header integers/string lengths and typed body values, and
leaves a canonical little-endian frame for the existing bounded client and
broker dispatch paths. Client response readers and `dbus-daemon` apply this
normalization before parsing. The live integration probe sends big-endian
`Hello`, `RequestName`, and `ReleaseName` calls over the authenticated local
session bus. Strict frame validation also exposed and fixed missing 4-byte
element alignment in the broker's `ListNames` string-array response and its
client test parser.

A clean Ubuntu WSL rebuild and `tools/test-compositor-clients.sh` completed
successfully on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32. All six
guests passed the D-Bus endian, broker, pthread, toolkit, and compositor
lifecycle gates with no `[FATAL]` output. The fresh x86_64 UEFI
`tools/test-desktop-readiness.sh` also passed; `desktop --check` reported
Aurora, LXDE, LXQt, GNOME, and KDE Plasma as READY. These are akaOS-native
bounded readiness/probe results only; complete D-Bus specification coverage
and unmodified upstream KDE/GNOME/LXDE/LXQt compatibility remain open.

## D-Bus body-codec byte orders (2026-09-24)

Added explicit-byte-order D-Bus body APIs for marshalling, validation, and
unmarshalling, using the standard `l` and `B` byte-order markers. Existing
entry points remain little-endian for compatibility. Integer and double-bit
scalars plus array payload lengths follow the selected order; alignment,
strings, signatures, and nested-container boundaries remain shared. The
regression checks exact big-endian byte vectors for 16/32/64-bit scalars,
double bits, and a 32-bit array, then validates and decodes them. A clean
Ubuntu WSL rebuild and full compositor/client QEMU matrix passed on x86_64,
x86_32, AArch64, ARM32, RISC-V64, and RISC-V32; every guest emitted the codec
success marker and none emitted `[FATAL]`. That codec-only stage was later
integrated with the message and broker paths as recorded above; full D-Bus
specification and upstream service compatibility remain open.

## D-Bus string and signature conformance (2026-09-24)

Hardened the shared D-Bus body codec. Signature validation now enforces the
wire-format maximum of 255 bytes. String values are required to be valid
Unicode UTF-8; embedded NULs, overlong forms, truncated sequences, UTF-16
surrogates, and code points above U+10FFFF are rejected by marshal,
unmarshal, and body validation. The codec regression checks a multibyte
round-trip plus malformed UTF-8, embedded-NUL, and oversized-signature cases.
The clean Ubuntu WSL six-image build and full compositor/client QEMU matrix
passed on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32; every guest
reported the codec and orphan-reaper markers, with no `[FATAL]` output. This
closes wire-validation bugs only; frame byte-order support is recorded above.
Complete D-Bus message semantics, general-purpose broker dispatch, and
upstream service compatibility remain open.

## POSIX process ancestry and orphan adoption (2026-09-24)

Replaced libc's constant-zero `getppid()` stub with XSCore syscall 73, then
completed parent-exit semantics. XSCore registers the kernel-launched
`/bin/init` process as the active orphan reaper in every boot path, including
the separate AArch64, ARM32, RISC-V64, and RISC-V32 native entries. Process
exit now reparents visible children atomically with publishing the parent's
EXITED state; a newly registered init adopts temporarily parentless processes
after an init restart. `waitpid` snapshots parent ownership under the process
table and per-process locks, and init polls/reaps unrelated adopted zombies
while its login child remains active.

`hello --posix-test` now forks a grandchild, waits for and reaps the
intermediate parent, verifies the grandchild observes a different live PPID,
and checks that the former parent receives `ECHILD` for that grandchild. A
fresh Ubuntu WSL build and QEMU guest runs passed on x86_64 and x86_32; the
first AArch64 run exposed missing registration in the native direct-kernel
entry. After adding registration to all four native entries and rebuilding
them, the complete non-x86 compositor/client matrix exited 0. All six guest
logs contain the orphan-adoption marker and none contains `[FATAL]`. This is
bounded native process-lifecycle coverage, not complete POSIX process or
upstream KDE/GNOME/LXDE/LXQt compatibility.

## POSIX pthread_atfork and fork-child thread repair (2026-09-24)

The libc now provides bounded `pthread_atfork()` registration (32 handler
sets) and invokes prepare handlers in reverse registration order, followed by
parent or child handlers in registration order. The `fork()` wrapper snapshots
the handlers and holds the pthread/TSD record lock across the kernel fork.
The child repairs its copied libc state: it keeps and renumbers the calling
thread, discards records/TSD for vanished workers, unmaps their copied stacks,
resets child lock and main-thread cleanup/cancellation state, then invokes
child handlers. `hello --pthread-test` forks while a worker exists, checks the
callback order and creates/joins a fresh child thread.

The regression exposed an x86_32 entry fault: the ring-3 assembly shim writes
the first cdecl argument to `user_sp + 4` from CPL0, so that stack slot must be
resident before entering assembly. XSCore now safely prepares the slot in the
thread-entry path. A fresh Ubuntu WSL rebuild and the full compositor/client
matrix pass on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32. This is
bounded atfork/thread repair, not complete POSIX fork/NPTL semantics or proof
that unmodified KDE/GNOME/LXDE/LXQt packages run.

## Main executable TLS template (2026-09-25)

Direct executables now read their own `PT_TLS` template from `AT_EXECFN` before
`main`, reserve the main-image TLS module, and initialize `.tdata` plus zeroed
`.tbss` for the initial thread. Dynamic interpreter startup skips that path and
continues through the existing loader. The common and five native linker-script
pairs no longer force custom `PHDRS`; LLD generates `PT_LOAD`, `PT_INTERP`,
`PT_DYNAMIC`, and `PT_TLS` as appropriate. This avoids the custom-header layout
that emitted a `PT_TLS` covering `.tdata` but omitted `.tbss`.

`hello --pthread-test` checks main-executable TLS initial values, zero-fill,
independent worker-thread values, and join completion. This section recorded an
interim state when only x86_64 runtime parity was verified and the first matrix
attempt timed out at an interactive prompt boundary. Ubuntu WSL later recovered;
the complete six-target runtime/TLS/compositor matrix now passes as recorded in
the newer current-state sections above. That pass validates akaOS-native tests,
not complete TLS/NPTL behavior or stock upstream desktop support.

The implemented TLS ABI remains a bounded subset (fixed module/thread limits,
initial-exec and general-dynamic paths); this does not make upstream desktop
packages or complete NPTL/TLS semantics supported. Continue the open TODO items.

## Kernel AF_UNIX peer credentials and D-Bus identity (2026-09-24)

XSCore captures a process PID/effective UID/effective GID when a UNIX stream
listener enters `listen()` and when a client connects. Connected AF_UNIX
endpoints expose the opposite side's captured credentials through
`getsockopt(SOL_SOCKET, SO_PEERCRED)` on every architecture. The syscall
payload uses fixed-width pointers and credential fields; invalid options,
short output buffers, unsupported socket families, and unconnected sockets
return errors instead of fabricated identities.

The D-Bus broker verifies EXTERNAL's claimed UID against the kernel peer UID,
retains credentials with each live connection, and answers
`GetConnectionUnixUser`, `GetConnectionUnixProcessID`, and
`GetConnectionCredentials` from those captured values. Unverified TCP sessions
are no longer accepted as D-Bus clients; the legacy bootstrap is restricted to
same-effective-UID local UNIX peers. `hello --unix-socket-test` verifies both
directions and a root-to-UID/GID 1201/1202 connection. Ubuntu WSL builds and
the complete compositor/client lifecycle matrix passed on x86_64, x86_32,
AArch64, ARM32, RISC-V64, and RISC-V32, including the new credential and D-Bus
markers. This is bounded native akaOS behavior, not full D-Bus policy or proof
that unmodified KDE/GNOME/LXDE/LXQt packages run.

## Reclaimable aligned static PT_TLS ranges (2026-09-24)

The dynamic loader now allocates static-TLS offsets with a bounded first-fit
search over live objects instead of a monotonic cursor. Final object reset
returns the offset range and decrements the live static-TLS module count, so
repeated `dlopen`/`dlclose` does not exhaust the 64 KiB per-thread area or keep
allocating TLS for future threads after every static-TLS DSO is gone.

`libakaos-static.so` now includes an 8 KiB TLS object aligned to 64 bytes.
`dltest` checks that address alignment on the main thread and a worker and
repeats open/read/alignment/close 12 times. The dynamic-loader matrix passed
on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32. Fresh images built
under Ubuntu WSL also passed the complete compositor/client lifecycle matrix
on all six targets. The test smoke binary emits its success marker only after
shared-libc file I/O and its `DT_NEEDED` call both succeed, avoiding shell
metacharacters unsupported by QEMU monitor key injection. This remains bounded
akaOS ABI coverage, not proof that unmodified KDE/GNOME/LXDE/LXQt packages run.
The same rebuilt x86_64 UEFI image also passes
`AKAOS_TEST_SKIP_BUILD=1 tools/test-desktop-readiness.sh`; `desktop --check`
reports Aurora, LXDE, LXQt, GNOME, and KDE Plasma as READY in the guest.

## POSIX pthread-key destructor and value-slot cleanup (2026-09-23)

`pthread_setspecific(key, NULL)` now releases the current thread's value slot;
deleting a key clears its bounded value table. At thread exit, the runtime
clears each non-null value before invoking its destructor, so a destructor can
republish a value for a later `PTHREAD_DESTRUCTOR_ITERATIONS` pass. Any value
remaining after the final permitted pass is reclaimed before the thread exits.

`hello --pthread-test` now verifies clear-before-callback, four destructor
re-arms, and 65 sequential worker threads against the 64-slot per-key table.
Fresh Ubuntu WSL builds pass `tools/test-compositor-clients.sh` on x86_64,
x86_32, AArch64, ARM32, RISC-V64, and RISC-V32; the rebuilt x86_64 guest also
passes `tools/test-desktop-readiness.sh`. This fixes an important POSIX pthread
edge case but does not provide complete pthread/NPTL or upstream GLib/Qt
compatibility. Broader TLS-module lifecycle and pthread/NPTL semantics remain
open.

## D-Bus route completion and desktop-role recovery (2026-09-23)

Broker disconnect cleanup now leaves an already completed D-Bus route intact.
This fixes the race where an activated one-shot target disconnects after its
reply is forwarded but before the waiting caller thread wakes. Resident
desktop-role components also keep their D-Bus registration while the
compositor/display surface is unavailable, remaining in recovery service mode
until stopped instead of immediately releasing their role name.

Fresh Ubuntu WSL builds of x86_64, x86_32, AArch64, ARM32, RISC-V64, and
RISC-V32 all pass `tools/test-compositor-clients.sh`; the rebuilt x86_64 guest
also passes `tools/test-desktop-readiness.sh`. The passing probes include the
standard broker activation route, AF_UNIX transport, desktop service routing,
and name release/unregistration. This is akaOS-native test coverage, not proof
that unmodified KDE, GNOME, LXDE, or LXQt packages are fully supported. Kernel
peer identity is now available for local AF_UNIX D-Bus connections; full
system-bus policy and credential delegation remain open.

## Interrupt-driven waits on empty run queues (2026-09-23)

`xscore_thread_block()` no longer reverts a blocked TCB to RUNNING and makes
libc spin when no peer is READY. It now rechecks the ready set with local
interrupts masked, switches to another runnable thread when one exists, or
idles with `sti; hlt` / the ARM and RISC-V platform `wfi` helper until an
interrupt makes work runnable. Relative futex durations round up to at least
one timer-counter tick. `hello --pthread-test` performs four 1 ms private
futex waits and validates monotonic elapsed time, in addition to timed
condition/semaphore checks.

Fresh images passed `tools/test-compositor-clients.sh` on x86_64, x86_32,
AArch64, ARM32, RISC-V64, and RISC-V32; all six transcripts contain the new
timed-futex marker and none contains `[FATAL]`. The rebuilt x86_32 dual-firmware
ISO reached login under UEFI and BIOS, and the rebuilt x86_64 guest passed
`tools/test-desktop-readiness.sh`. The compositor test builder now requests
`--firmware both` for x86_32 rather than replacing its ISO with BIOS-only
media. These remain native akaOS compatibility tests, not proof that stock
KDE/GNOME/LXDE/LXQt packages are fully supported. This checkout has no `.git`
directory, so there is no Git status or commit history to report.

## Standard dlfcn lookup scopes (2026-09-20)

The native loader now exports the standard `RTLD_DEFAULT` and `RTLD_NEXT`
handles. `dlsym(RTLD_DEFAULT, ...)` and versioned `dlvsym()` search the
process-wide loaded-object scope (plus akaOS libc's built-in TLS resolver),
while `RTLD_NEXT` starts after the DSO containing the call site when that
caller is identifiable. `dltest` verifies both unversioned default selection
and explicit `AKAOS_1.0` selection through the global scope. Explicit-handle
lookups now walk transitive `DT_NEEDED` dependencies with a cycle bound, and
`dltest` calls the versioned provider through its dependent handle. A clean Ubuntu
WSL rebuild and the strict dynamic-loader matrix pass on x86-64, x86-32,
AArch64, ARM32, RISC-V64, and RISC-V32; `RTLD_LOCAL` and `RTLD_GLOBAL` are
now honored for global-scope visibility and promotion of an already-loaded
object. The complete six-port compositor-client lifecycle matrix remains
green. `dltest` now closes the TLS-bearing demo DSO, reloads it, and verifies a
fresh initialized PT_TLS block, proving bounded module mapping reclamation.
This is another native loader
compatibility layer, not proof that unmodified upstream KDE/GNOME/LXDE/LXQt
executables can run yet.

## Bounded ELF symbol-version selection (2026-09-20)

The native `dlopen`/`dlsym` engine now parses bounded `DT_VERSYM`,
`DT_VERDEF`, and `DT_VERNEED` tables. Unversioned lookups select the
provider's non-hidden default version, relocations honor a requester's exact
version requirement, and libc exports `dlvsym()` for explicit selection.
Malformed or unbounded version metadata is rejected before dereference. A
two-version provider DSO (`AKAOS_1.0`/`AKAOS_2.0`) is packaged in every
architecture initramfs; `dltest` exercises default lookup plus both explicit
versions, and a real `DT_NEEDED` client relocation selects `AKAOS_1.0`.
A clean Ubuntu WSL rebuild and the strict dynamic-loader test now
pass on x86-64, x86-32, AArch64, ARM32, RISC-V64, and RISC-V32, with the
symbol-version marker required explicitly. The compositor matrix remains a
native akaOS substrate check; the same rebuilt images pass the complete
six-port compositor/client lifecycle suite and the x86-64 desktop-readiness
matrix. Unmodified upstream KDE/GNOME/LXDE/LXQt still needs broader ABI
coverage.

## PIC DSO TLS and ELF interpreter handoff hardening (2026-09-20)

The bounded native loader now stages a real `PT_TLS` template per loaded DSO,
allocates an initialized private block per akaOS pthread, resolves TLS symbols
through `dlsym`, and exposes the ELF general-dynamic `__tls_get_addr` ABI. It
also applies the DTPMOD/DTPOFF descriptor relocations emitted by PIC DSOs and
provides the i386 EAX-passed `regparm(1)` resolver spelling (including
`___tls_get_addr`). The shared test DSO contains compiler-generated TLS read /
write helpers, and `dltest` checks main-thread initialization and worker
isolation.

The native interpreter's aux-vector parser now starts after `argc + 1`
argument pointers. When an ELF header is outside a `PT_LOAD`, XSCore derives a
validated `AT_PHDR` address from the original image instead of changing the
existing linker layouts. AArch64 TLSDESC relocations are handled by a small
ABI-preserving resolver shim, and the RISC-V DTPMOD/DTPREL relocation numbers
match the LLVM ABI for both word sizes. A clean Ubuntu WSL rebuild and strict
`tools/test-dynamic-loader.sh` run now pass on x86-64, x86-32, AArch64, ARM32,
RISC-V64, and RISC-V32. Bounded static TLS/TP relocations and range reclamation
are now covered by the 2026-09-24 regression above. Complete auxv/HWCAP values
and unmodified upstream
KDE/GNOME/LXDE/LXQt execution remain open.

The rebuilt six-image set also passes `AKAOS_TEST_SKIP_BUILD=1
tools/test-compositor-clients.sh` on every port, and the rebuilt x86-64
dual-firmware ISO passed `tools/test-desktop-readiness.sh` on 2026-09-23.
These are akaOS-native compositor/toolkit substrate checks, not proof that
unmodified upstream KDE, GNOME, LXDE, or LXQt binaries can run unchanged.

## Standard ELF auxiliary-vector handoff (2026-09-19)

XSCore now builds a bounded standard startup vector for every new user image:
`AT_PHDR`, `AT_PHENT`, `AT_PHNUM`, `AT_PAGESZ`, `AT_BASE`, `AT_FLAGS`,
`AT_ENTRY`, real/effective credentials, `AT_SECURE`, `AT_EXECFN`, and
`AT_NULL`. Program-header addresses are derived from the mapped `PT_LOAD` or
`PT_PHDR` record so fixed-address 32-bit images receive valid pointers. The
native interpreter validates the required entries before loading the ET_DYN
program. Fresh all-six-port images passed `tools/test-dynamic-loader.sh`; the
x86_64 UEFI readiness run also passed. This is still not a complete upstream
auxv/TLS ABI.

## Native DSO constructor/finalizer lifecycle (2026-09-19)

The bounded `dlopen` engine now parses and validates `DT_INIT`/`DT_FINI` and
bounded `DT_INIT_ARRAY`/`DT_FINI_ARRAY` metadata, runs dependency constructors
before the requesting object, and dispatches finalizers in reverse order before
unmapping the last reference. A constructor-bearing `libakaos-demo.so` and the
`dltest` probe verify real constructor execution. All six architecture images
were rebuilt under Ubuntu WSL, and `tools/test-dynamic-loader.sh` passed on
x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32. This improves native
toolkit startup compatibility but does not claim complete auxv, full upstream
TLS semantics, or unchanged KDE/GNOME/LXDE/LXQt support.

## Native PT_INTERP dynamic loader (2026-09-19)

XSCore now recognizes a bounded ELF `PT_INTERP` path and starts the packaged
`/lib/ld-akaos.so` interpreter instead of rejecting dynamically linked images.
The kernel preserves the original executable path as `AT_EXECFN`; the native
interpreter uses the existing bounded `dlopen`/relocation engine, obtains the
relocated entry through `akaos_dl_entry()`, restores the original user stack,
and transfers control to the loaded program on all six ports. A real
`dynamic-hello` ET_DYN image carries `PT_INTERP` and `DT_NEEDED` for
`libakaos-demo.so`. `tools/test-dynamic-loader.sh` passed on x86_64, x86_32,
AArch64, ARM32, RISC-V64, and RISC-V32, while the full compositor matrix and
x86_64 UEFI readiness matrix also passed. This is the first native dynamic
execution tranche; static TLS/TP, full auxv, and unmodified upstream
desktop binaries remain open.

## Native GIO directory enumeration and file metadata (2026-09-19)

The shared desktop runtime now exposes a bounded `GFileEnumerator`/
`GFileInfo` ABI. Directory children are read through the native XSCore
`getdents` syscall, child metadata is filled from native `stat`, and callers
can retain/release enumerators and info objects, close enumerators, inspect
names, display names, file type, size, inode, uid, and gid attributes, and
retrieve the container `GFile`. The dynamic desktop probe enumerates `/etc`,
checks `motd` and `akaos.conf`, validates ref/close/error semantics, and
prints `GIO directory enumeration verified`.

The clean Ubuntu WSL compositor/client matrix passes on x86_64, x86_32,
AArch64, ARM32, RISC-V64, and RISC-V32 with the new marker, in addition to
the existing GIO content, D-Bus, POSIX, display-protocol, toolkit, compositor,
palette, and mouse checks. This is a native file-manager substrate milestone;
full upstream GIO/GLib and unmodified KDE, GNOME, LXDE, or LXQt binaries are
still not claimed.

## Native GIO file I/O and clean six-port desktop matrix (2026-09-19)

The bounded desktop runtime now includes an akaOS-native syscall bridge for
file-backed GIO operations. `g_file_get_contents()`,
`g_file_set_contents()`, `g_file_query_exists()`, and the asynchronous load /
finish pair are exported through the shared runtime and exercised by the
dynamic desktop probe, including error propagation and task cleanup. The
generated non-x86 builders now compile the shared desktop I/O object and keep
the complete runtime libc source set (`epoll`, `eventfd`, `timerfd`, and
semaphore included). Runtime DSO aliases are canonicalized before `dlopen()`
lookup so `.so.0` desktop names share the packaged compatibility objects.
Runtime code is size-optimized for the constrained x86_32 mapping arena.

Freestanding `/bin/ls` number formatting now uses a restoring u64 divide by
ten instead of compiler-provided 64-bit division helpers, keeping ARM32 and
RISC-V32 builds independent of `__aeabi_uldivmod`/host runtime symbols. A
clean Ubuntu WSL rebuild and `tools/test-compositor-clients.sh` run passed on
x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32, including the full
D-Bus, POSIX, X11/Wayland, toolkit, GIO, compositor, palette, and mouse-input
markers. This is a verified akaOS-native compatibility substrate; it does not
claim that unmodified upstream KDE, GNOME, LXDE, or LXQt binaries are already
100% supported. The focused `tools/test-desktop-readiness.sh` UEFI run was
also rerun against this exact build and passed in the x86_64 guest.

## Standard D-Bus broker and resident desktop-service protocol (2026-09-17)

The native broker now covers the standard compatibility tranche exercised by
the desktop substrate: `GetNameOwner`, `GetConnectionUnixUser`,
`GetMachineId`, `Introspect`, `StartServiceByName`, `Ping`, and explicit
`UnknownMethod` errors, in addition to the existing name, match, and routing
methods. Resident desktop roles expose `GetStatus`, `GetCapabilities`,
`Introspect`, and `Ping` through their claimed well-known names. The bounded
probe also starts the packaged `AutoEcho` activation service and validates a
routed method call. The x86_64 readiness test and a clean Ubuntu WSL rebuild
of the compositor/client matrix pass on x86_64, x86_32, AArch64, ARM32,
RISC-V64, and RISC-V32, including the standard broker and desktop-service
markers. This remains an akaOS-native compatibility layer; complete D-Bus
semantics and unmodified upstream KDE, GNOME, LXDE, or LXQt binaries are not
yet claimed.

## ARM32 blocking-poll ABI hardening and clean six-port verification (2026-09-17)

The libc `poll()` wrapper now copies translated descriptor results through a
non-inlined post-syscall helper. On ARM32, a blocking SVC can schedule another
thread while r14 is being used as a link register; keeping the translated
array pointer live across that SVC corrupted `revents` after wake-up. The
separate helper preserves the kernel result and fixes timerfd readiness without
reducing the canonical TTY input buffer, which remains 1024 bytes. Temporary
timer/poll diagnostics were removed from the source. A clean Ubuntu WSL
rebuild and compositor/client lifecycle matrix pass on x86-64, x86-32,
AArch64, ARM32, RISC-V64, and RISC-V32. The x86-64 live desktop-readiness
matrix also passes the resident D-Bus service registration, well-known-name
claim, routed service call, SIGTERM cleanup, name release, and activation
unregistration checks. This verifies the native compatibility substrate; it
does not claim that unmodified upstream KDE, GNOME, LXDE, or LXQt binaries are
already fully supported.

## Role-aware desktop D-Bus activation services (2026-09-17)

`desktop-compat` now identifies each resident composed-session role and
publishes a bounded native session-bus activation definition and claims a
real persistent well-known name: LXDE Openbox /
PCManFM, LXQt Panel / FileManager, GNOME Mutter / Session, and KDE KWin /
Plasma each use a distinct `org.akaos.*` service name and packaged executable
path. Services are owned only by resident `--component` processes and release
their bus name plus unregister activation metadata on normal teardown;
`--once` probes intentionally skip broker
registration so a process that exits immediately cannot race activation
metadata cleanup. The x86_64 readiness harness also runs a resident Openbox
component, checks its registration marker, sends SIGTERM, and checks the
unregistration marker. Duplicate definitions are tolerated without
unregistering a service owned by another instance. Rebuilt all six images under Ubuntu WSL;
the live x86_64 desktop-readiness matrix and the full compositor/client matrix
pass on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32. This improves
the native D-Bus/session substrate but remains a bounded akaOS compatibility
layer, not the complete upstream KDE, GNOME, LXDE, or LXQt stack.

## GTK container detach and reparent lifecycle (2026-09-16)

The bounded widget tree now supports `akaos_runtime_widget_remove()` and the
GTK-shaped `gtk_container_remove()`. Removing a child shifts the parent's
fixed child array, clears the child's parent link, and permits a subsequent
reinsert without leaving stale ownership. The dynamic hello probe verifies
detach and reparent after recursive `gtk_widget_show_all()`. Rebuilt all six
images under Ubuntu WSL; every architecture passes the compositor/client
matrix and the x86_64 desktop-readiness matrix remains green. This is another
native toolkit prerequisite, not full GTK layout/rendering or complete KDE,
GNOME, LXDE, or LXQt upstream integration.

## GLib source destroy-notify lifecycle (2026-09-16)

The shared runtime now exports `g_timeout_add_full()` and
`g_idle_add_full()` with ignored priority values and bounded
`GDestroyNotify` ownership. A destroy callback runs exactly once when a
source returns false or is removed, including safe removal from another
dispatch path; repeating sources retain their notifier until final removal.
The dynamic hello probe verifies both one-shot timeout cleanup and
remove-before-dispatch cleanup. Rebuilt all six images under Ubuntu WSL; every
architecture passes the compositor/client matrix with the new marker, and the
x86_64 desktop-readiness matrix remains green. This strengthens the native
GLib compatibility substrate but is not full upstream GLib/GIO behavior or a
claim of complete KDE, GNOME, LXDE, or LXQt stacks.

## Recursive GTK tree visibility (2026-09-16)

`gtk_widget_show_all()` now walks the bounded parent/child tree and makes the
root plus every descendant visible, restoring non-minimized/non-closed state;
the traversal has a depth guard tied to the fixed window capacity so malformed
cycles cannot recurse forever. The dynamic `hello --desktop-runtime-test`
probe verifies the box, label, button, entry, and check-button states after a
single root show-all call. Rebuilt all six images in Ubuntu WSL; the complete
compositor/client matrix and x86_64 desktop-readiness matrix pass. This is a
small native GTK compatibility improvement, not full upstream GTK or a claim
that KDE, GNOME, LXDE, or LXQt binaries are complete.

## Cross-DSO application and GObject convenience lifecycle (2026-09-16)

The shared desktop runtime now keeps `g_application_run()` on the persistent
quit-driven loop: registration, activation, callback-driven quit, and the
post-run explicit quit state are all observable through separately loaded
GLib/GTK compatibility DSOs. It also exports bounded variadic `g_object_set()`
and `g_object_get()` helpers for common string, boolean, and integer names
(`title`, `text`, `visible`, `sensitive`, geometry, and related aliases).
Window state is updated in the shared fixed-layout object, so a property set
through GLib is visible to GTK without relying on private DSO storage. The
dynamic hello probe verifies these paths. Because the extra metadata increased
the 32-bit resident image, the x86_32 runtime arena is now 52 KiB while the
kernel's proven 128-page user mmap budget remains unchanged. Rebuilt all six
images under Ubuntu WSL; the complete compositor/client matrix and x86_64
desktop-readiness matrix pass. This is still a bounded akaOS-native
compatibility layer, not the unmodified upstream KDE, GNOME, LXDE, or LXQt
stacks.

## Due-time GLib source scheduling and fair dispatch (2026-09-16)

The shared desktop runtime now treats `g_timeout_add()` intervals as logical
monotonic milliseconds instead of dispatching every source on every pass.
`g_main_context_iteration()` advances the same clock, pending only reports
due sources, and repeating callbacks are rescheduled from the current time.
Dispatch takes a source snapshot so callbacks added during a pass wait for the
next pass; `g_source_remove()` is safe even while its callback is running.
The dynamic hello probe verifies a 3 ms timeout stays pending for two context
iterations and fires on the third. All six rebuilt Ubuntu WSL ports pass the
compositor/runtime matrix, and x86_64 desktop readiness remains green.

## GLib main-loop constructor semantics verified on all ports (2026-09-16)

`g_main_loop_new(context, running)` now only creates the loop and records the
requested running state; it never enters dispatch before returning the handle.
Callers can therefore register idle/timeout sources and then call
`g_main_loop_run()`, matching the expected GLib lifecycle. The dynamic hello
probe covers the `running=TRUE` constructor path, source registration, callback
delivery, explicit quit, and final non-running state. After rebuilding every
image in Ubuntu WSL, the complete six-port compositor/client matrix and the
x86_64 desktop-readiness matrix pass.

## Persistent desktop main-loop bridge and Ubuntu six-port validation (2026-09-16)

The shared desktop runtime now has a persistent, callback-driven source loop:
`akaos_runtime_loop_run()` continues until an explicit quit, dispatches every
source present in each pass, and exposes reference-safe loop handles plus
GLib-shaped `GMainLoop`/`GMainContext` helpers and `gtk_main()`/`gtk_main_quit()`
backed by the same loop. The dynamic `hello --desktop-runtime-test` probe now
registers an idle callback, runs the loop, and verifies callback delivery and
clean termination. On x86_32 the runtime arena is bounded to 60 KiB so the
three desktop compatibility DSOs remain within the proven 128-page user mmap
budget. Rebuilt all six architecture images in Ubuntu WSL; the full
compositor/client matrix passes on x86_64, x86_32, AArch64, ARM32, RISC-V64,
and RISC-V32, and the x86_64 desktop-readiness matrix passes. This remains a
bounded akaOS-native compatibility layer, not complete upstream KDE, GNOME,
LXDE, or LXQt binaries.

## Bounded Qt core/widget bridge and i386 footprint fix (2026-09-15)

The shared desktop runtime now exports a bounded Qt-shaped application and
object bridge: application create/exec/process-events/quit, QObject property
set/get, signal connect/emit/disconnect, and QWidget construction, geometry,
title, visibility, enabled state, and destruction. `hello
--desktop-runtime-test` loads the Qt compatibility DSO dynamically and
verifies the complete bridge on all six architecture images. The i386 port
keeps its safe 128-page per-process mmap budget; reducing the fixed property
metadata table from 32 to 24 entries keeps the three simultaneously loaded
desktop DSOs within that address-space limit. No kernel mmap-table expansion
is required. The full compositor/client matrix and x86_64 desktop-readiness
matrix are green. This is a bounded akaOS-native bridge, not the complete
upstream Qt ABI.

## Bounded GTK window state lifecycle (2026-09-15)

The GTK-shaped runtime now implements resizable, maximize/unmaximize,
minimize/unminimize, present, close, and state-query operations instead of
silently discarding those common window calls. State is held in the same
fixed-layout runtime object used by the cross-DSO widget bridge, and the
dynamic `hello --desktop-runtime-test` probe verifies every transition. The
six-architecture compositor/runtime matrix passes; mapping these states to
full upstream window-manager policy remains open.

## Native resize configure/acknowledge handshake (2026-09-15)

The shared display ABI (compositor protocol v4) now carries a client configure
serial and adds an explicit `AKAOS_DISPLAY_MSG_CONFIGURE_ACK` request. Toolkit surfaces allocate
monotonic serials, wait for Lumen to publish the exact new geometry, submit an
acknowledgement, and wait until Lumen records that acknowledgement before
continuing with damage or destroy. Palette's `--resize-test` verifies the
full queue/configure/ack/damage/destroy sequence on x86_64, x86_32, AArch64,
ARM32, RISC-V64, and RISC-V32. This keeps resize 1:1 and bounded; maximize
policy and the complete upstream configure protocol remain open.

## Supervised composed desktop lifecycle (2026-09-15)

The composed `/bin/desktop --env <profile>` launcher now owns and supervises
the companion process graph. It records each profile child PID, forwards
SIGTERM/SIGINT cleanup, waits for all children, and reports a deterministic
`session components stopped (2/2)` result in `--once` mode. A short startup
handoff between companion spawns avoids concurrent image-fault/compositor
initialization races. The x86_64 desktop-readiness matrix passes all four
profiles, and the refreshed six-architecture compositor/runtime matrix passes
the shared client path. These are bounded akaOS-native compatibility
components, not unmodified upstream desktop binaries.

## Bounded GSimpleAction and application action map (2026-09-15)

The shared GLib-shaped runtime now exports bounded named actions with
reference-safe lifetime, enabled-state gating, activation through the fixed
signal ABI, and per-application add/lookup/remove maps. `hello
--desktop-runtime-test` verifies disabled actions do not emit, enabled actions
do emit, and map entries are removed cleanly. The refreshed six-architecture
compositor/runtime matrix passes. This remains an akaOS-native bounded action
bridge, not the full upstream GAction/GMenu model.

## Bounded GApplication lifecycle bridge (2026-09-15)

The shared GTK/GLib-shaped runtime now exports a bounded GApplication
lifecycle: application construction with an ID, cross-DSO registration,
hold/release, activation, run, quit, and application-window creation. The
implementation validates the fixed application tag while allowing the
application object to cross the separately loaded GTK and GLib compatibility
objects. `hello --desktop-runtime-test` exercises the full lifecycle, and the
refreshed six-architecture compositor/runtime matrix passes. This remains a
native bounded bridge rather than the complete upstream GApplication ABI.

## Native window resize transaction (2026-09-15)

The display ABI now has `AKAOS_DISPLAY_MSG_RESIZE` and a toolkit helper that
publishes a new shared-memory buffer, position, and native dimensions. Lumen
invalidates both old and new rectangles, remaps only buffers whose header
geometry exactly matches the request, and rejects undersized or inconsistent
buffers instead of scaling or reading past the client allocation. The palette
client's `--resize-test` exercises configure, damage, and destroy; the full
six-architecture compositor/runtime matrix passes with no fatal traps.

## Composed desktop session launcher (2026-09-15)

`/bin/desktop --env <profile>` now launches a composed profile session over
the existing Lumen compositor, session D-Bus, and X11/Wayland bridge. LXDE
starts Openbox plus PCManFM; LXQt starts its panel plus PCManFM-Qt; GNOME
starts Mutter plus the GNOME session; KDE starts KWin plus the Plasma session.
`--once` propagates to companions for clean integration tests, while normal
sessions remain supervised by the desktop launcher until the profile exits.
All six architecture images pass the refreshed desktop-readiness test and the
full compositor/runtime matrix. The components are still bounded akaOS-native
compatibility implementations, not the complete upstream desktop projects.

## Cross-architecture resume hardening and GError verification (2026-09-14)

The user-context resume helpers now mask timer interrupts before saving the
kernel continuation and copying the fork return frame on RISC-V32, RISC-V64,
and ARM32; AArch64 already had the equivalent DAIF guard. This closes the
RISC-V32 `xscore_user_resume` trap race observed after the runtime image grew.
All six rebuilt images pass `tools/test-compositor-clients.sh`, including the
desktop-runtime probe's typed GError copy/match/message checks and the full
compositor input lifecycle. The runtime remains a bounded akaOS-native
compatibility layer rather than the complete upstream desktop stacks.

## Cross-architecture user-entry hardening (2026-09-14)

The bounded user-image self-test now scans the full 128-page loader budget, so
larger libc/hello images cannot hide their writable segment beyond the former
128-KiB probe (the x86_32 pre-login failure is fixed). Initial EL0 handoff is
also IRQ-atomic on AArch64, ARM32, RISC-V64, and RISC-V32; `xscore_user_enter`
now masks the timer during transition setup and lets the target user status
restore interrupts on return. Refreshed x86_64, x86_32, AArch64, ARM32,
RISC-V64, and RISC-V32 images pass the compositor/client matrix; x86_32 uses
the full probe and native guests pass the reduced timing profile.

## Bounded GIO cancellables and tasks (2026-09-14)

The shared runtime now exports reference-counted GCancellable objects and
GTask completion/result primitives with callback dispatch, cancellation
short-circuiting, source-object lookup, and bounded lifetime handling.
`hello --desktop-runtime-test` reports the GIO lifecycle marker on the
refreshed x86_64 BIOS/UEFI images, x86_32 image, and native AArch64/ARM32/
RISC-V64/RISC-V32 images.

## Bounded GObject property bindings (2026-09-14)

The runtime now supports bounded one-way and bidirectional property bindings
with optional sync-on-create and reference-safe unbind. Property updates are
propagated through the fixed-layout GValue bridge and bindings are cleared
when either runtime object is destroyed. The x86 full and native reduced
six-port compositor/runtime matrices pass after this change.

## Bounded GValue and GObject property bridge (2026-09-14)

The shared runtime now exports a fixed-layout, copy-in GValue for string,
boolean, signed-int, and unsigned-int properties, plus cross-DSO
g_object_set_property/g_object_get_property operations with destruction
cleanup. hello --desktop-runtime-test exercises all four value kinds on
x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32; the x86 guests use
the full matrix and the native guests use the reduced matrix.

## Bounded GFile path/URI objects and final six-guest verification (2026-09-14)

The shared runtime now provides reference-counted GFile objects for local
paths and file:// URIs, with bounded normalization, path/URI conversion,
basename and parent derivation, equality, and path-prefix checks. Returned
strings are explicitly allocated through the same runtime allocator.

hello --desktop-runtime-test validates all of these operations. x86_64 passes
build/gfile-final-x86_64.log and x86_32 passes build/gfile-test-x86_32.log;
AArch64/ARM32/RISC-V64/RISC-V32 pass build/gfile-final-native-four-reduced.log.

## Bounded GSettings scalar store and final six-guest verification (2026-09-14)

The shared runtime now exports a bounded GSettings-compatible store with
schema lifecycle, reference counting, scalar string/boolean accessors, and
typed GVariant value get/set for the supported scalar kinds. Values are copied
into fixed per-object slots, avoiding cross-DSO ownership of caller variants.

hello --desktop-runtime-test validates schema references, string round-trip,
boolean state, and typed value retrieval. x86_64 passes
build/settings-final-x86_64.log; x86_32 passes build/settings-test-x86_32.log;
AArch64 passes build/settings-aarch64-retry.log; ARM32/RISC-V64/RISC-V32 pass
build/settings-native-three-retry.log.

## GLib signal disconnect lifecycle and final six-guest verification (2026-09-14)

The shared runtime now exports handler-ID disconnect and
disconnect-by-function operations for its cross-DSO signal registry. Destroying
a runtime object also removes global signal handlers targeting that pointer,
preventing stale callback delivery.

hello --desktop-runtime-test verifies connect, emit, handler disconnect,
disconnect-by-function, and post-disconnect silence. x86_64 and x86_32 full
matrices pass in build/signal-disconnect-final-x86_64.log and
build/signal-disconnect-test-x86_32.log. AArch64 passes the isolated retry
build/signal-disconnect-aarch64-retry.log; ARM32/RISC-V64/RISC-V32 pass
build/signal-disconnect-native-three-retry.log.

## GVariant variant containers and final six-image verification (2026-09-14)

The runtime now supports the GVariant container type v through
g_variant_new_variant(), g_variant_get_variant(), and the v format signature.
The builder probe now constructs a real a{sv}-style entry, confirms that a
type-mismatched lookup is rejected, and unwraps the v payload to recover the
typed value. Reference ownership remains explicit and bounded.

All six final images rebuild and pass the desktop/compositor matrix:
x86_64 is build/variant-container-final-x86_64.log, x86_32 is
build/variant-container-test-x86_32.log, and AArch64/ARM32/RISC-V64/RISC-V32
are covered by build/variant-container-final-native-four-reduced.log.

## Cross-DSO object data and extended GTK widget boundary (2026-09-14)

The shared desktop runtime now provides a process-wide pointer-keyed
g_object_set_data/get_data/steal_data registry so GLib calls can annotate
objects created by the GTK soname DSO; entries are cleared when a runtime
object is destroyed. The GTK-shaped bridge also includes entry text,
check-button active state, grid attachment, explicit visibility, and
sensitivity operations, with bounded widget kinds.

hello --desktop-runtime-test validates the cross-DSO metadata round trip and
the extended widget tree. All six images rebuild and pass: x86_64 is
build/widget-data-final-x86_64.log, x86_32 is
build/widget-data-debug-x86_32.log, and the native reduced matrix is
build/widget-data-final-native-four-reduced.log.

## GVariant dictionary entries/builders and final six-image verification (2026-09-14)

The freestanding GLib-shaped runtime now adds bounded dictionary-entry
variants, key/value accessors, string-key lookup in array containers, and
builder APIs (new, add-value, add-format, end, and clear). Builder storage is
limited to eight children, retains each value, and releases pending references
when ended or cleared. The format bridge accepts dictionary forms such as
{s@}; the type parser remains deliberately bounded rather than a full
recursive GLib variant language.

hello --desktop-runtime-test resolves and validates dictionary ownership,
lookup/type checks, and a{sv}-style builder construction through dlsym. All
six images rebuild successfully. x86_64 and x86_32 full matrices pass in
build/dict-test-x86_64.log and build/dict-test-x86_32.log; AArch64, ARM32,
RISC-V64, and RISC-V32 pass the native reduced-readiness matrix in
build/dict-test-native-four-reduced.log.

## Variadic GVariant format bridge and final six-image verification (2026-09-14)

The freestanding GLib-shaped runtime now includes a bounded
`g_variant_new()` format bridge for the common scalar signatures
`s`, `b`, `u`, `i`, `t`, and `x`, child references via
`@`, and tuple signatures such as `(su)`. Parsing is explicit and bounded
(maximum eight tuple children); unsupported or trailing format data is rejected
without claiming full GLib recursive-builder coverage.

`hello --desktop-runtime-test` resolves the variadic constructor through
`dlsym` and verifies a formatted `(su)` tuple in addition to the scalar and
composite probes. All six final images rebuild successfully. x86_64 and x86_32
full runtime matrices pass in `build/format-test-x86_64.log` and
`build/format-test-x86_32.log`; AArch64, ARM32, RISC-V64, and RISC-V32 pass
the native reduced-readiness matrix in
`build/format-test-native-four-reduced.log`.

## Composite GVariant tuples/arrays and final six-image verification (2026-09-14)

The freestanding GLib-shaped runtime now supports bounded composite variants:
`g_variant_new_tuple`, `g_variant_new_array`, `g_variant_n_children`, and
`g_variant_get_child_value`.  Composite construction retains each child and
unref recursively releases the child array, while type queries expose the
bounded `(*)`/`a*` forms.  The implementation rejects missing children and
allocation overflow and remains inside the 64 KiB per-DSO arena.

`hello --desktop-runtime-test` keeps scalar children alive while constructing
and reading both composite forms, then verifies child types/values and the
reference cleanup path.  All six rebuilt images pass: x86_64 and x86_32 full
matrices are `build/composite-test-x86_64.log` and
`build/composite-test-x86_32.log`; AArch64 passes
`build/composite-test-aarch64-retry.log`, and ARM32/RISC-V64/RISC-V32 pass
`build/composite-test-native-three-reduced.log`.  Native reduced runs disable
only unchanged slow readiness probes; their desktop and compositor stages are
green.

## Typed GVariant scalar bridge and final six-image verification (2026-09-14)

The freestanding GLib-shaped runtime now exports reference-counted typed
`GVariant` scalars for strings, booleans, signed/unsigned 32-bit values, and
signed/unsigned 64-bit values.  It provides constructors, ref/unref, typed
getters, type-string queries, and `g_variant_is_of_type`; strings are copied
into the bounded per-DSO arena.  This is an intentional scalar subset, not a
claim of full varargs/tuple/builder coverage.

`hello --desktop-runtime-test` resolves and validates every variant symbol
through `dlsym`, including all six scalar values, type tags, string length,
and reference lifetime.  All six rebuilt images pass: x86_64 and x86_32 full
matrices are `build/variant-test-x86_64.log` and
`build/variant-test-x86_32.log`; AArch64, ARM32, RISC-V64, and RISC-V32 pass
the native reduced-readiness matrix in
`build/variant-test-native-four-reduced.log`.

## Immutable GBytes bridge and six-architecture desktop verification (2026-09-14)

The shared freestanding desktop runtime now exports reference-counted,
immutable `GBytes` objects with copy-in construction, `ref`/`unref`, size/data
accessors, and bytewise equality.  Source buffers can be changed after
construction without altering the object, and all storage is reclaimed through
the existing deterministic 64 KiB per-DSO arena.

`hello --desktop-runtime-test` resolves and checks the complete bytes surface
through `dlsym`, including reference lifetime, copy immutability, equal
payloads, and size/data reporting.  All six bytes-enabled images pass: the
x86_64 and x86_32 full matrices are in `build/bytes-test-x86_64.log` and
`build/bytes-test-x86_32.log`; the native reduced-readiness matrix passes all
four guests in `build/bytes-test-native-four-reduced.log`.  The native run
skips only unchanged epoll/eventfd/timerfd checks under slow QEMU scheduling,
and the harness honors those explicit skips.

## GLib hash/list containers and clean per-guest runtime verification (2026-09-14)

The desktop runtime now exports opaque `GHashTable`, `GList`, and `GSList`
operations in addition to the existing arrays and strings.  Hash tables use
callback-selected hashing/equality with string helpers, replace-on-insert,
lookup/contains, tombstone removal, bounded size, and deterministic destroy.
Lists provide prepend/append/remove, bounded length/index traversal, and node
cleanup without taking ownership of caller data.  Every node/table allocation
uses the deterministic 64 KiB per-DSO arena.

`hello --desktop-runtime-test` resolves all of these symbols with `dlsym` and
checks string-key replacement/removal plus doubly/singly linked ordering.  The
list-enabled x86_64 and x86_32 full matrices pass in
`build/list-test-x86_64.log` and `build/list-test-x86_32.log`; clean native
desktop/compositor runs pass in `build/list-test-aarch64.log`,
`build/list-test-arm32.log`, `build/list-test-riscv64.log`, and
`build/list-test-riscv32-skip-readiness.log`.  The native logs intentionally
disable only unchanged epoll/eventfd/timerfd probes when QEMU scheduling is
slow; the harness now honors those skip flags in its final assertions.

## Bounded GLib hash-table/list bridge and six-architecture verification (2026-09-14)

The shared freestanding desktop runtime now exports opaque `GHashTable`,
`GList`, and `GSList` APIs alongside the existing arrays and strings.  Hash
tables provide callback-selected hashing/equality, string helpers,
replace-on-insert, lookup/contains, removal with tombstones, bounded size,
and destruction.  Doubly and singly linked lists provide prepend/append,
remove, bounded length/index traversal, and node cleanup.  All allocations
remain in the deterministic 64 KiB per-DSO arena.

`hello --desktop-runtime-test` resolves and exercises the new symbols through
`dlsym`, including distinct string-key replacement/removal and list ordering
checks.  The list-enabled images pass clean x86_64 and x86_32 full matrices;
the native ARM/RISC-V images pass their desktop/compositor stage with the
optional unchanged epoll/eventfd/timerfd probes disabled when QEMU scheduling
is too slow.  The harness now skips only those corresponding final log
assertions when their probes are explicitly disabled, so selected test runs
return truthful status.

## Bounded GLib hash-table bridge and cross-architecture verification (2026-09-14)

The freestanding desktop runtime now also exports a fixed-capacity
`GHashTable` ABI with callback-selected hashing/equality, lookup, contains,
replace-on-insert, remove, size, and destroy operations.  `g_str_hash` and
`g_str_equal` provide the normal string-key path; the default table path uses
pointer identity.  Entries are open-addressed with tombstones and remain
bounded by the existing per-DSO 64 KiB arena, with no key/value ownership
surprises or host-libc dependency.

`hello --desktop-runtime-test` resolves and exercises the hash API through
`dlsym` (including distinct string queries, replacement, deletion, and table
size).  All six rebuilt guests pass the live compositor/client regression:
x86_64 (`build/hash-test-x86_64.log`), x86_32
(`build/hash-test-x86_32.log`), AArch64 (`build/hash-test-aarch64-retry.log`),
ARM32 (`build/hash-test-native-three.log`), RISC-V64
(`build/hash-test-riscv64-retry.log`), and RISC-V32
(`build/hash-test-riscv32-retry.log`).  Each serial log also contains the
desktop runtime lifecycle marker and compositor assertion.

## GLib collection ABI and six-architecture desktop verification (2026-09-13)

The freestanding desktop runtime now exports bounded GLib-shaped
`GArray`, `GPtrArray`, and `GString` containers in addition to its allocation,
duplication, and object-lifetime helpers.  Arrays support growth, clear and
zero-terminated elements, pointer-array indexing, and explicit segment
ownership; strings support append, append-with-length, truncate, NUL
termination, and ownership transfer.  All storage remains in the existing
per-DSO 64 KiB arena and failures return NULL without host-libc dependencies.
`hello --desktop-runtime-test` exercises every symbol through `dlsym` and
passes on all six architecture guests.

All six images were rebuilt.  The live compositor/client regression passed on
x86_64 (`build/collections-test-x86_64.log`), x86_32
(`build/collections-test-x86_32.log`), and the native four-guest run
(`build/collections-test-native-four.log`); each serial transcript contains
the desktop runtime lifecycle marker and no fatal/timeout/failure lines.
The x86_32 dual-firmware ISO builder now allocates a 64 MiB FAT EFI image so
both self-contained IA32 and x86_64 loaders fit reliably.

## Cooperative pthread cancellation bridge and six-guest verification (2026-09-13)

The freestanding pthread layer now exposes `pthread_cancel`,
`pthread_setcancelstate`, `pthread_setcanceltype`, `pthread_testcancel`, and
the standard cancellation constants. Cancellation is cooperative: deferred
checkpoints run at scheduler-yield points and explicit test points, invoke
the existing bounded TLS-destructor passes, and return `PTHREAD_CANCELED`
through the native thread-exit/join ABI. Asynchronous mode is accepted for
source compatibility but is delivered at the next safe scheduler point.
`hello --pthread-test` cancels a live worker and verifies the joined result.
The barrier/cancellation marker passes on all six rebuilt guests in
`build/cancel-native-four-test.log` and the x86_32 run
`build/cancel-test-x86_32.log`.

## POSIX pthread barrier bridge and six-guest verification (2026-09-13)

The freestanding pthread layer now provides pointer-free
`pthread_barrier_*` APIs, including process-shared attributes, reusable
generation/count rendezvous, serial-thread return, and busy-destroy checks.
Waiters use the native scheduler-yield path. `hello --pthread-test` exercises
a two-participant barrier with the main thread and a native worker. All six
barrier-enabled images pass the live compositor/client regression; each guest
reports the marker ending in `semaphore, barrier`. The authoritative native
run is `build/barrier-native-four-test.log`; x86 guests are recorded in
`build/barrier-final-six-test.log`.

## Bounded GLib-shaped runtime memory ABI and six-guest verification (2026-09-13)

The native desktop compatibility objects now export a bounded, deterministic
GLib-shaped memory surface (`g_malloc`, `g_malloc0`, `g_realloc`, `g_free`,
`g_strdup`, `g_strndup`, `g_memdup2`) plus the basic object ref/sink helpers.
Each DSO owns a 64 KiB arena and allocation table. The size is deliberate:
the i386 XSCore address-space budget permits three concurrently loaded
GLib/GTK/Qt objects without exhausting its 128-page mmap limit, while all
other ports use the same ABI and limits. `hello --desktop-runtime-test`
checks zeroed allocation, duplication, realloc, and lifecycle calls.

All six images were rebuilt from this source state. x86_64, x86_32, AArch64,
ARM32, RISC-V64, and RISC-V32 each pass the complete live compositor/client
regression, including D-Bus, pthread/semaphore, AF_UNIX, readiness, POSIX
spawn, X11/Wayland, desktop-runtime, toolkit, palette, and Lumen mouse
markers. The native four-guest authoritative run is
`build/runtime-memory-64k-native-four-final-test.log`; x86 results are also
recorded in `build/runtime-memory-64k-final-six-test2.log`.

The compositor harness login bound is now 300 seconds so slow firmware/device
inventory on native RISC-V/ARM guests is not misreported as a boot failure.
This is test-harness timing only; it does not relax any guest assertion.

## POSIX semaphore bridge (2026-09-13)

The freestanding libc now provides `<semaphore.h>` with bounded
`sem_init`/`destroy`/`wait`/`trywait`/`timedwait`/`post`/`getvalue` operations.
Process-shared attributes are accepted for source compatibility with the
current XSCore shared-address-space model, and waiters use the native
cooperative scheduler yield. `semaphore.c` and the updated hello pthread
probe compile on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32;
all six architecture-specific images were then rebuilt and the live
compositor/client matrix passed with the updated marker in every guest. The
authoritative run is `build/semaphore-final-six-test.log`.

## Expanded pthread synchronization ABI (2026-09-13)

The freestanding libc pthread layer now includes initialized/destroyed thread
attributes (stack size and detach state), mutex attributes with normal,
error-checking, and recursive ownership semantics, timed condition waits,
reader/writer locks, and POSIX-bounded repeated TLS-destructor passes.
`pthread_create()` honors a validated per-thread stack size and detached
attribute while preserving XSCore's native shared-address-space thread ABI.
`hello --pthread-test` exercises these paths, and the complete compositor
matrix passes on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32.

The attribute surface now also accepts `PTHREAD_PROCESS_PRIVATE` and
`PTHREAD_PROCESS_SHARED` for mutexes, condition variables, and rwlocks, with
validated attr init/destroy/pshared accessors. These objects remain native
shared-address-space atomics (not Linux futexes), so the pshared flag is a
source-compatibility contract for the current XSCore process model. Latest
pthread/hello translation-unit probes compile successfully for all six
architectures; the pre-existing runtime images remain the next live-matrix
refresh target.

## Timerfd lifecycle and six-guest regression (2026-09-13)

The timerfd worker lifecycle is process-safe on every native port: workers
remain joinable, `close()` waits for a worker before releasing pipe backing,
and libc `_exit()` closes any still-open timerfds.  RISC-V32's fork/spawn
sequence was exercised after timerfd teardown without a kernel trap.  The
compositor-client harness now gives post-login shell startup a bounded
300-second window so demand-paged native guests under host load are not
reported as input failures.  A complete rerun passed on x86_64, x86_32,
AArch64, ARM32, RISC-V64, and RISC-V32 with timerfd, eventfd/epoll, D-Bus,
POSIX spawn, X11/Wayland, toolkit, palette, and Lumen input markers and no
fatal traps.

## Timerfd event-loop edge and larger UEFI ISO (2026-09-13)

The freestanding libc now provides `<sys/timerfd.h>` with asynchronous
`timerfd_create`, `timerfd_settime`, and `timerfd_gettime` behavior.  Virtual
timerfds use a joinable native userspace worker and pipe marker, translate
through `poll()`/`epoll()`, and clean up from `close()` and `_exit()` so a
timer worker cannot run after XSCore marks its process exited.  The arithmetic
uses a freestanding 64-bit shift/divide helper, keeping ARM32 and RISC-V32
links free of compiler runtime dependencies.  `hello --timerfd-test` passes in
the complete six-guest compositor regression.  The dual-firmware ISO's
El-Torito UEFI FAT image is now 32 MiB so the growing embedded kernel and
initramfs cannot overflow the previous 16 MiB image.

## Cross-architecture process/activation hardening (2026-09-12)

The AArch64 fork/exec resume path now masks timer IRQs across the final
kernel-to-EL0 handoff and keeps the saved context/transition in callee-saved
registers, preventing a scheduler switch from replacing the resume pointer.
Process fork/exec frames use resident per-process storage sized for the
architectural frame plus image metadata.  The D-Bus broker also clears
already-reaped activation PIDs during unregister, and the x86_64 HMP client
probe uses a settling delay so scan-code writes are not coalesced.  A clean
six-guest `tools/test-compositor-clients.sh` run passes on x86_64, x86_32,
AArch64, ARM32, RISC-V64, and RISC-V32; `tools/build-arch-smoke.sh --all`
passes as well.

## Native eventfd readiness bridge (2026-09-12)

The freestanding libc now provides `<sys/eventfd.h>` with `eventfd()`,
`eventfd_read()`, and `eventfd_write()`.  Virtual eventfd descriptors are
backed by native akaOS pipes and are translated by libc `poll()` and the
bounded `epoll()` implementation, while read/write/close dispatch preserves
counter and semaphore behavior.  `hello --eventfd-test` is included in the
compositor-client regression and passes on x86_64, x86_32, AArch64, ARM32,
RISC-V64, and RISC-V32.  This is a bounded compatibility bridge: full Linux
eventfd descriptor flags, close-on-exec tracking, and an expanded kernel
wait-object ABI remain future work.

## Stateful X11 and Wayland desktop-protocol bridge (2026-09-12)

The local display bridge now keeps bounded X11 window geometry through
CreateWindow, MapWindow, ConfigureWindow, GetGeometry, and QueryTree requests,
and returns setup/atom/extension replies used by common toolkit startup.  Its
Wayland endpoint advertises wl_compositor, wl_shm, wl_seat, and xdg_wm_base,
handles ping/pong and object binding, and completes an xdg_surface/xdg_toplevel
configure/acknowledge/title/commit sequence.  `hello --display-protocol-test`
and the serial six-guest compositor matrix verify both paths on x86_64,
x86_32, AArch64, ARM32, RISC-V64, and RISC-V32.  The implementation remains a
bounded native compatibility bridge; full X11/Wayland protocol behavior,
security/policy, rendering, and upstream compositor integration are still
required for unmodified KDE, GNOME, LXDE, and LXQt.

## GTK-shaped widget tree and activation callbacks (2026-09-12)

The packaged desktop runtime now includes bounded container/box/label/button
objects with parent-child links, requested sizes, text/name state, visibility,
and safe destruction.  Common GTK-shaped constructors and tree operations are
exported alongside a fixed callback form for `g_signal_connect()` and
`g_signal_emit_by_name()`; this gives source ports a useful widget/activation
boundary without pretending to implement GLib's variadic marshaller ABI.
`hello --desktop-runtime-test` builds and tears down a widget tree, checks its
layout state, and emits a button callback in the live x86_64 readiness probe
and the six-guest compositor-client matrix.  The same runtime source compiles
for x86_32, AArch64, ARM32, RISC-V64, and RISC-V32.  Complete GTK/GLib/GIO
and Qt6 APIs and upstream C/C++ ABI behavior remain open.

## C locale and UTF-8 support (2026-09-12)

The freestanding libc now supplies `<ctype.h>`, `<locale.h>`, and `<wchar.h>`.
`setlocale()` accepts deterministic C/C.UTF-8 names (including environment
selection for `""`), `localeconv()` exposes decimal formatting, and the UTF-8
codec strictly rejects overlong, surrogate, truncated, and out-of-range
sequences.  `mbstowcs`, `wcstombs`, wide-string comparison/length, and common
wide classification/case helpers are covered by `hello --locale-test` on all
six native guests.

This is a bounded locale, not a catalog/ICU implementation: collation,
complete Unicode tables, iconv, and full upstream GLib/Qt internationalization
remain required for unmodified desktop distributions.

## Native `posix_spawn()` environment path (2026-09-12)

XSCore syscall 68 (`SPAWNVE`) now creates a child from bounded path, argv, and
envp vectors in one kernel operation.  The kernel copies the vectors, loads
the ELF image with the requested environment, and preserves the caller's CWD,
descriptor table, credentials, and architecture-specific user-stack layout.
libc `posix_spawn()` and `posix_spawnp()` use it for explicit environments,
while the inherited-environment case keeps the native `spawn()` fast path.
The freestanding spawn header and libc also provide bounded file-action
objects with `addclose` and `adddup2`; the kernel applies those actions to the
child descriptor table before publishing the process.

`hello --posix-test` passes the explicit-environment child check on all six
native guests (x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32), and
the corresponding builders are green.  The interface is intentionally
bounded and does not claim open-file actions beyond close/dup2, attributes,
unlimited vectors, or general PT_INTERP execution.

## POSIX `select()` bridge (2026-09-12)

The freestanding libc now provides `<sys/select.h>` (`fd_set` and the normal
FD macros) and translates `select()` calls into XSCore's bounded descriptor
polling ABI, preserving read/write/exception readiness and timeval timeouts.
`hello --select-test` covers pipe write/read readiness on every native port and
is part of the compositor-client regression.  The native poll descriptor limit
remains 16; larger sets return `EINVAL` until an expanded kernel wait-set ABI
is implemented.

The libc netdb surface now also includes bounded IPv4
`getaddrinfo()`/`freeaddrinfo()`/`gai_strerror()` support.  Numeric hosts,
`localhost`, service ports, `AI_PASSIVE`, `AI_NUMERICHOST`, and canonical-name
allocation are covered by `hello --getaddrinfo-test` on all six ports.

## Public D-Bus service registration (2026-09-12)

The session broker now exposes `RegisterService`/`UnregisterService` through
the public `akaos_dbus_register_service()` and
`akaos_dbus_unregister_service()` libc APIs.  A bounded per-user registry is
persisted at `/run/user/0/dbus-services.conf`; entries include executable,
argument, restart policy, and activation timeout metadata.  The binary broker
accepts the legacy `sss` form and the metadata-bearing `sssuu` form, rejects
duplicates, refuses to remove a running provider, and supervises exited
providers with bounded restart attempts.  `hello --dbus-registration-test`
exercises both forms.  All six native guest compositor probes and the
architecture smoke build pass this tranche.

This remains a native bounded ABI: service policy is not the complete upstream
D-Bus daemon contract, and PT_INTERP plus full GTK/GLib/Qt/X11/Wayland support
are still required for unmodified KDE, GNOME, LXDE, and LXQt binaries.

## Resume verification (2026-09-12)

Reused AArch64 TCBs now reset saved `ELR_EL1`, `SPSR_EL1`, and `SP_EL0` during
context fabrication, preventing stale exception-return state from leaking
between user processes.

The clean post-resume AArch64 image and the native ARM32, RISC-V64, and
RISC-V32 images pass their isolated compositor-client lifecycle probes;
x86_64/x86_32 pass in a serialized pair.  All six architecture smoke builds
and shell syntax checks are green.  The cumulative six-VM harness may exceed
its fixed per-guest timeout on a busy WSL host, so affected ports should be
rerun individually before treating that harness timeout as a guest failure.

## Shared desktop runtime layer (2026-09-11)

The packaged desktop runtime objects now export a bounded native event-loop and
source queue, GLib-shaped loop/identity helpers, GObject-style references, and
GTK-shaped window create/show/hide/destroy calls, plus a C-facing Qt application
and widget bridge. `hello --desktop-runtime-test` resolves and exercises the
ABI through `dlopen`/`dlsym` on the live x86_64 guest; the same source is built
for all six ports. Conventional soname aliases (`libglib-2.0.so.0`,
`libgtk-3.so.0`, `libgtk-4.so.1`, and Qt6 `.so.6` names) are included in each
initramfs. The ABI is intentionally bounded and explicit about not being the
complete upstream GLib/GTK/Qt or C++ ABI.

The shared-object loader now transfers objects in 4096-byte chunks, matching
XSCore's maximum read syscall. This removes the size-dependent `dlopen` read
failure that appeared once the runtime objects grew beyond one page.

XSCore now automatically reclaims exited detached user-thread TCBs at scheduler
safe points, with a reaping reservation that prevents concurrent join/detach
double-free races. The libc pthread layer tracks detached stack mappings and
uses the new thread-state ABI to unmap them after EXITED/REAPING/UNUSED is
observed; creation and yield paths perform bounded cleanup so resident D-Bus
and compositor services do not exhaust the fixed thread or mapping tables.
The six-architecture compositor and fresh-guest D-Bus owner-signal regressions
remain green after this lifecycle change. Clone flags, cancellation, and full
process teardown accounting are still open.

The bounded binary D-Bus broker now routes method calls between concurrent
clients: well-known-name ownership is resolved to a live connection, calls are
queued and delivered to the owning service, and the service reply is forwarded
back with the caller's serial.  The libc client and daemon retry short
nonblocking `EAGAIN`/`EINTR` socket transfers with scheduler yields, so framed
messages are not lost when a packet is split across reads.  ARM32 user-thread
entry now installs each pthread's allocated User/System stack before entering
the C start routine; this removes the intermittent stack/return-address faults
seen in concurrent D-Bus workers.  The live compositor-client matrix now passes
the route test on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32.

The broker loads a bounded activation registry from
`/etc/dbus-services.conf`. An addressed-but-unowned registered name starts its
configured executable/argument (the shipped `com.akaos.AutoEcho` entry runs
`/bin/hello --dbus-activation-service`), waits for name ownership, and routes
the original call normally. `hello --dbus-activation-test` exercises this on
all six guests. Session clients can add/remove definitions through the public
`akaos_dbus_register_service()`/`akaos_dbus_unregister_service()` API; the
metadata-bearing `sssuu` form carries restart and timeout values, and a
bounded supervisor reaps and restarts eligible providers. Complete upstream
broker policy and unrestricted arbitrary-service limits remain open.

The native pthread/shared-address-space milestone is now verified after
cross-port allocator fixes: AArch64, ARM32, RISC-V64, and RISC-V32 linker
images reserve memory through the linker-provided `__kernel_end` symbol
(retaining each port's historical minimum) instead of a fixed boundary. This
prevents enlarged initramfs/BSS data from overlapping page-table allocations.
The compositor-client test passes on x86_64, x86_32, AArch64, ARM32, RISC-V64,
and RISC-V32, including `dltest`, `hello --pthread-test` (worker return and
TLS destructor), `/proc/realtime`, and Lumen input lifecycle checks. HMP input
in the six-guest harness uses a paced key stream so the x86_32 i8042 path
cannot drop characters in long command lines.

The freestanding libc now exposes a real `dlopen`/`dlsym`/`dlclose`/`dlerror`
API and a bounded cross-architecture ET_DYN loader for shared objects.
`/bin/dltest` loads `/lib/libakaos-demo.so` plus a DT_NEEDED dependent object,
resolves and executes both functions, and closes their mappings; this smoke
test is included in the live x86_64 UEFI readiness run and all five generated
native user builders.  The loader handles ELF32/ELF64 headers, PT_LOAD
materializing, SysV and GNU hash symbol lookup, REL/RELA relative and
locally-defined symbol relocations, dependency refcounts, and PLT/GOT
`JUMP_SLOT` binding. TLS-bearing objects and kernel PT_INTERP execution remain
explicitly unsupported until the full runtime ABI exists.
The x86_64 UEFI linker keeps the large embedded initramfs data segment at a
firmware-safe physical address, and the loader allocates its segment pages
individually to tolerate fragmented UEFI memory.

XSCore's kernel exec path still has a bounded ET_DYN loader for static
position-independent images. It parses a dynamic table containing only
REL/RELA relative relocations and applies them while materializing each
process-private page; unsupported PT_INTERP, DT_NEEDED, symbol-table, and PLT
entries are rejected explicitly. The separate libc `dlopen` path above adds
the first user-space shared-object capability without changing those kernel
exec safety rules. `hello-pie` is built for every port and executed in the
x86_64 guest test.

The native desktop profile layer now packages bounded shared runtime ABI probes
(`libpthread.so`, GLib/GIO, GTK/GTK4, and Qt6 Core/Gui/Widgets) and maps the
standard LXDE, LXQt, GNOME, and KDE/Plasma launcher names to one native
profile session implementation. Each profile can check the shared toolkit,
D-Bus, and Lumen prerequisites and can create a bounded compositor surface;
the surface size is capped to XSCore's shared-memory object limit on 32-bit
and 64-bit ports. `hello --desktop-runtime-test`, `desktop --check`, and the
live desktop-readiness test cover the ABI and launch path. This is an akaOS
native compatibility session, not the upstream KDE/GNOME/LXDE/LXQt code yet;
real PT_INTERP execution, complete shared-library ABI coverage, and toolkit
implementations remain open.

## Local display transport bridge (2026-09-11)

XSCore and the freestanding libc now expose a native AF_UNIX/SOCK_STREAM
transport. The kernel maintains a bounded in-memory endpoint namespace,
listener queues, connected peer pairs, direct receive buffers, close/error
state, and `sockaddr_un` bind/listen/connect/accept semantics on all six
architecture ports. The userspace `/bin/display-protocols` service publishes
the conventional local endpoints `/run/X11-unix/X0` and
`/run/user/0/wayland-0`, plus a shared-memory readiness marker. Its X11 subset
answers setup, GetInputFocus, GetGeometry, InternAtom, and QueryExtension;
its Wayland subset announces `wl_compositor`, `wl_shm`, and `wl_seat`, and
answers `wl_display.sync`.

`hello --display-protocol-test` performs real wire handshakes over AF_UNIX,
and the six-architecture compositor-client matrix exercises those handshakes
alongside D-Bus, pthreads, toolkit display, Lumen input, and palette lifecycle
checks. Desktop aliases start the bridge before creating their compositor
surface. This is a deliberately bounded protocol foundation, not a full X
server, Wayland compositor, GTK/Qt implementation, or upstream desktop port;
general protocol coverage, rendering, security policy, and PT_INTERP/toolkit
ABI work remain open.

The freestanding libc includes a cross-architecture pthread compatibility
surface: mutexes, condition variables, `pthread_once`, identity, and
`sched_yield` are functional over atomics and the dedicated syscall 61
scheduler-yield ABI (without touching TTY polling). Kernel syscalls 62--65 now
provide native shared-address-space thread creation, exit, join, and detach;
syscall 67 exposes bounded thread-state inspection for detached mapping
cleanup. `hello --pthread-test` verifies a worker return value on all six ports.
`/bin/dbus-daemon` provides loopback session/system buses with endpoint markers,
the original line-compatible bootstrap, and a bounded little-endian D-Bus wire
mode (`AUTH EXTERNAL`/`BEGIN`, method-call parsing, and method returns). PID 1
keeps the system bus resident and a graphical desktop launch starts the
per-user session bus. `/bin/xsdisplay` and
`akaos/toolkit.h` provide the native display/toolkit protocol boundary used by
the profile layer and future GTK/Qt, X11, and Wayland adapters; the toolkit now exposes typed
attach/damage/move/focus/destroy surface helpers and `hello --toolkit-test`
verifies its display-info path on all six guests. These services are not the full
upstream desktop stacks yet; `desktop --check` reports the native compatibility
profiles separately from any future upstream desktop ports.

`/bin/palette` now uses the typed toolkit surface API for attach, damage, and
destroy, so the production client exercises the same bridge rather than a
parallel hand-built message path.

The bounded D-Bus broker also keeps per-connection match rules and implements
`AddMatch`, `RemoveMatch`, and `NameHasOwner`; `hello --dbus-match-test` and
the six-architecture compositor matrix verify those semantics. The bounded
activation registry/AutoEcho path and `NameOwnerChanged` signal
delivery/filtering are covered by their dedicated all-six probes; richer policy
and the complete upstream broker contract remain deliberately open.

The six-architecture compositor client matrix now runs `dltest` before each
display lifecycle check; x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32
all resolve and execute both packaged shared-object functions in their guests.

## Shared-object dependency namespace (2026-09-11)

`userspace/libc/src/dlfcn.c` now maintains a bounded eight-object namespace with
path identity, reference counts, recursive `/lib/<soname>` DT_NEEDED loading,
dependency-aware symbol lookup, and REL/RELA PLT/GOT relocation handling. GNU
hash-only ELF objects are accepted in addition to classic SysV hash tables;
the packaged provider is linked GNU-hash-only so this path is exercised live.
`/bin/dltest` verifies provider and dependent calls return 42. The full
`tools/test-compositor-clients.sh` matrix passed all six live architecture
guests after the x86_32 `R_386_JUMP_SLOT` lazy-stub correction.

## Binary D-Bus transport (2026-09-11)

`dbus-daemon` now accepts standard-style SASL `AUTH EXTERNAL`/`BEGIN` clients
and bounded little-endian D-Bus method frames, while retaining the native
`NATIVE 1` line bootstrap used by early services. The daemon parses method
member fields and returns `METHOD_RETURN` frames for `Ping`, `Hello`, `GetId`,
`RequestName`, `ReleaseName`, and `ListNames`.
`akaos_dbus_binary_ping()` and `hello --dbus-binary-test` exercise the wire
layout; `tools/test-desktop-readiness.sh` passes this live in x86_64 and the
compositor-client matrix passes it on all six architecture guests. Bounded
`Hello`, `GetId`, `RequestName`, `ReleaseName`, and `ListNames` semantics are
also verified on all six guests. Match rules, bounded broker routing, the
AutoEcho activation regression, owner-change signal delivery, and public
per-user registration metadata are covered; complete upstream broker policy
remains open.

The broker now also records bounded per-connection match rules and answers
`AddMatch`, `RemoveMatch`, and `NameHasOwner`; `hello --dbus-match-test`
verifies registration/removal and ownership changes on all six guests.
Multi-client method routing is covered by `hello --dbus-route-test`, and the
bounded `com.akaos.AutoEcho` on-demand provider is covered by
`hello --dbus-activation-test`, and `NameOwnerChanged` delivery is covered by
`hello --dbus-owner-signal-test`; arbitrary per-user service activation remains
open.
The dual-firmware ISO now builds its BIOS GRUB platform explicitly and marks
the native UEFI El Torito image as `efi`, so OVMF selects the XSCore UEFI
loader while legacy BIOS still enters Multiboot2.  The ISO regression test
passes both firmware boots.

The kernel's syscall 61 is a scheduler-only yield path.  Socket connect,
accept, and receive retry loops yield between attempts, which keeps the
single-core guest fair when a resident D-Bus listener is active.  PID 1 starts
the system bus and the graphical launcher starts a session bus; the readiness
test exercises a real session connect/PING/PONG exchange.  Fresh native builds
of x86_32, AArch64, ARM32, RISC-V64, and RISC-V32 all complete, and the
compositor lifecycle test passes on all six architectures. The session-bus
probe launches the daemon as a background job and waits for the shell prompt;
this avoids a native-guest foreground-job timing race that previously caused
an AArch64-only false timeout.

## Desktop Portability Foundation (2026-09-10)

The shared freestanding libc now exposes the first POSIX timing and host
identity APIs required by desktop toolkits: `clock_gettime`, `nanosleep`,
`gettimeofday`, `time`, `uname`, `gethostname`, `access`, and `sysconf`.
Timing reads the existing kernel `/proc/realtime` and `/proc/uptime` files and
waits through `poll`, so the same source is valid on all six CPU ports.  The
The `desktop --check` command now reports Aurora plus four native profile
sessions as READY. Those profiles use akaOS's bounded shared runtime probes,
toolkit bridge, D-Bus, and Lumen surface path, with launcher aliases matching
the conventional LXDE/LXQt/GNOME/KDE names. They are not the upstream desktop
projects: real PT_INTERP loading, complete toolkit APIs, and X11/Wayland
protocol compatibility are still required before claiming upstream support.
`tools/test-desktop-readiness.sh` live-boots the x86_64 UEFI image and verifies
the complete matrix, shared runtime probe, and all four profile launchers; the
five native non-x86/x86_32 builders were rerun with the same source.

Verified against source, serial logs, and fresh Ubuntu-WSL QEMU runs through
2026-09-10. The x86_64 2/4-core SMP suite now includes concurrent PMM/heap
allocation and bounded Ring 3 workloads on AP-owned user threads. Shared VFS
persistence and executable page-cache snapshots are serialized, remote blocked
threads can receive a reschedule IPI, and x86 threads save/restore their
FXSAVE-compatible state across context switches. After the boot self-tests,
ordinary x86_64 shell descendants may be placed on APs; the built-in test mode
remains BSP-local because it intentionally exercises tightly coupled startup
ordering. The normal x86_64 suite and full parity runners for x86_32, AArch64,
ARM32, RISC-V64, and RISC-V32 pass after the shared process-table,
descriptor-lifetime, VFS, and process wait/reap synchronization changes. The
full regression summary is refreshed only when every suite has completed
successfully.

## Aurora Desktop and Lumen Compositor

Status: Protocol v3 and the dark desktop path are implemented and live-tested
on x86_64; the compositor client lifecycle is live-tested on all six supported
architectures.

Aurora now uses a responsive icon-only taskbar, installed-application registry
(`/etc/aurora-apps`), searchable launcher, stable client icon identities,
active/minimized window indicators, and a real empty-desktop context menu.
Launcher mouse selection, arrows, Enter, Backspace, Escape, F1, and Super are
implemented. Lumen forwards bounded pointer/key/text events to the resident
shell and owns graphical input exclusively, preventing the same keystrokes
from leaking into the hidden TTY. Window minimize, taskbar restore/focus, and
close are functional. Client-driven maximize, arbitrary resizing, edge snap,
multi-display layout, task grouping/pinning/tooltips, and animations are not
implemented.

Mouse input is live on all six architectures. x86_64 and x86_32 use the native
i8042/PS/2 controller with atomic configuration and explicit mouse streaming;
AArch64, ARM32, RISC-V64, and RISC-V32 use MMIO VirtIO input with absolute
coordinate mapping and unmodified BTN_LEFT/RIGHT/MIDDLE delivery. Lumen applies
light acceleration only to larger relative deltas, sends pointer positions to
Aurora, and renders a conventional 18x22 high-contrast arrow with outline and
shadow. The live compositor matrix requires both a motion and button marker on
every architecture.

Lumen's pointer path is now low-latency: its framebuffer descriptor and
composition row are persistent, clean scanlines are skipped, wallpaper pixels
are generated only inside the damage span, and the event/surface tick runs every
8 ms. This removes the former full-frame recomposition and per-frame allocation
that caused visible cursor lag. A fresh fast-workspace build plus live QEMU
motion/button checks passed on all six architectures. Rendering still uses
bounded `/dev/fb0` writes because akaOS does not yet expose a mapped hardware
cursor or GPU acceleration; very large window drags therefore cost more than a
small pointer move.

The tray is capability-driven. Its time and date come from `/proc/realtime`,
backed by x86 CMOS RTC, ARM PL031, or the RISC-V Goldfish RTC. Network appears
only when a kernel-reported interface exists and shows connection state from
the route table; audio appears only when `/proc/asound/cards` reports real
hardware. Battery is intentionally absent because the kernel has no battery
telemetry. `/bin/notify` and protocol-v3 bounded notification messages provide
sequential transient banners with timeout/click dismissal; a persistent
notification center, history, and application grouping remain future work.

`DESKTOP_THEME=dark|light` is persisted by `akaos-config` and loaded by Lumen
and Aurora at session start. The dark theme has a verified live capture at the
current QEMU framebuffer size. Light theme compiles but has not had a fresh
visual boot test. Fixed-size native framebuffer requests currently prevent an
honest 640x480/800x600/1024x768/1280x720/1920x1080 test claim; responsive/DPI
validation across that matrix remains open.

Fresh `tools/test-compositor-clients.sh` verification on 2026-09-09 passed
x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32. Each guest completed
login, returned a current 10-digit `/proc/realtime` epoch, and exercised a real
client attach/damage/destroy lifecycle through Lumen with no fatal trap.

Recent reliability audit (2026-09-08): the kernel TTY keeps a 1024-byte
pending input queue for bytes arriving while a read is ready, batches raw-mode
drains, and handles serial/VirtIO control keys (Ctrl+C, Ctrl+Z, Ctrl+D) along
with ANSI history sequences. The shell and common utilities were hardened for
EOF, multi-line reads, bounded script arguments, `echo -n`, and safe `ls`
path construction. ARM32 process state is reacquired after user entry, fixing
the writable-disk logout/relogin data abort. The interactive keyboard
regression now waits on prompts and verifies two back-to-back commands.
`akash` catches SIGINT and redraws the prompt after an interrupted read;
kernel foreground-group tracking keeps an idle shell alive while child jobs
still receive Ctrl+C/Ctrl+Z. Ctrl+C submits an empty newline, whereas Ctrl+D
continues to produce canonical EOF.
The tracker also preserves shell ownership while `akash` is blocked in
`waitpid`, preventing a forked child with the inherited shell name from
capturing the terminal before foreground Ctrl+C/Ctrl+Z delivery.

## Boot

Status: Implemented and automated on QEMU/OVMF from FAT32 and ISO media.

The x86-64 UEFI loader loads `/XSCORE.ELF` from FAT32, validates and maps ELF PT_LOAD segments, captures GOP and the UEFI memory map, exits boot services, and enters XSCore through a versioned handoff structure. Quiet boot is default. `VERBOSE`/`AKAOS.VERBOSE` enables framebuffer early logs; `AKAOS.TEST` enables automated debug-exit.

## Legacy BIOS Boot

Status: Implemented and automated on QEMU/SeaBIOS.

`build/XSCORE-BIOS.ELF` is linked with the Multiboot2 bridge as its entry
point. `xscore_multiboot_entry` starts in the 32-bit protected-mode state
provided by GRUB, creates a temporary 4 GiB identity map, enters x86-64 long
mode, and calls `xscore_bios_entry`. The BIOS adapter translates Multiboot2
E820 and direct-colour framebuffer tags into the shared `akaos_boot_info`,
including BIOS/test/verbose flags. `tools/build-bios.sh` creates
`build/akaos.iso` with both the GRUB BIOS entry and a native UEFI El Torito FAT
image. `tools/test-bios.sh` verifies its BIOS path, and `tools/test-iso.sh`
verifies the same ISO through both OVMF and SeaBIOS. The separate UEFI FAT
image is never overwritten by the ISO.

## Build Interface

Status: Unified and validated in `build.sh`.

When launched from an interactive terminal without arguments, `build.sh` and
`run.sh` open a shared raspi-config-style full-screen terminal UI implemented
by `tools/interactive-ui.sh`. The build UI selects architecture, firmware,
disk type, automated test mode, and verbose logging. The run UI selects
architecture, firmware, disk type, and automatic/GUI/headless display mode.
`whiptail` is used when installed, with a dependency-free numbered fallback.
`--menu` opens the UI explicitly. Existing flags remain unchanged for scripts,
CI, and reproducible commands; a no-argument non-interactive invocation keeps
the previous default behavior instead of waiting for input.

Both entry points now run `tools/dependencies.sh` before their selected backend.
The checker understands Ubuntu/Debian, Arch Linux, Fedora, openSUSE, and Alpine,
checks only the selected architecture/firmware/disk/test requirements, and
prints the matching package-manager command when something is missing. It
checks compiler/binutils tools, Pillow, DejaVu Sans, FAT utilities, GRUB
commands and platform modules, QEMU targets, and OVMF as applicable. It never
installs packages or invokes `sudo`. `--check-deps` exits after the report, and
both interactive menus expose the same check. Host resource discovery supports
the standard font and GRUB layouts used by these distributions. New-user setup
and broad package sets are documented in `docs/BUILDING.md`.

The main entry point accepts `--arch`, `--firmware uefi|bios|both`, and
`--disk-type fat32|iso`. The old `--uefi` and `--bios` flags were removed, and
disk type `both` is not accepted. Disk type is inferred when omitted: UEFI
selects FAT32, BIOS selects ISO, and firmware `both` defaults to FAT32.
Every x86_64 ISO build creates one dual-firmware `build/akaos.iso`; UEFI ISO boot is
an El Torito FAT image and BIOS ISO boot is GRUB Multiboot2. `--test` and
`--verbose` apply to both x86 paths. AArch64, ARM32, RISC-V64, and RISC-V32
accept the same firmware, disk-type, test, and verbose selectors and must not
reject a command solely because their current QEMU backend builds/loads a
native ELF target. `--test` selects the matching native QEMU parity suite.
Their artifacts are `XSCORE_AARCH64.ELF`, `XSCORE_ARM32.ELF`,
`XSCORE_RISCV64.ELF`, and `XSCORE_RISCV32.ELF`.

## Non-x86 secondary CPU bring-up

Status: Implemented and verified on QEMU `-smp 2` and `-smp 4` for AArch64, ARM32,
RISC-V64, and RISC-V32, including real per-CPU timer IRQ delivery (2026-09-08).

All four native ports now share `xscore/arch/virt/secondary.c`. AArch64 and
ARM32 use PSCI `CPU_ON` through the QEMU virt HVC conduit; RISC-V64 and
RISC-V32 use the OpenSBI HSM `hart_start` call. Each secondary enters through
a dedicated native entry symbol, selects a private identity-mapped 16 KiB
early stack, waits for a release flag, registers a per-CPU idle TCB, configures
the local interrupt-controller context and timer, observes a real timer IRQ,
and runs bounded heartbeat accounting before parking. ARM32 also installs a
banked per-secondary IRQ stack. RISC-V computes the supervisor PLIC context
from the raw hart ID and does not assume hart 0 is the firmware boot hart.
The timer dispatch path rearms and acknowledges the source before feeding
`xscore_sched_timer_tick()` for per-CPU accounting. The primary prints a pass
marker when the firmware-started or already-parked secondaries reach the idle
context and another when every online secondary has received a timer IRQ.
`tools/test-nonx86-smp.sh` requires both markers for the two/four-unit bring-up
boundary; four-core runs and the six-target architecture smoke build pass.
The shared thread layer now admits an explicit owner CPU only when that CPU is
online and tracks READY TCBs in a per-CPU bit mask; round-robin selection is
CPU-local. This is queue/admission metadata only: `multicore_userspace_enabled`
remains false on native non-x86 ports, so user-task placement, address-space
migration, and trap-return context switching remain future work.

The native x86_32 builder generates its own anti-aliased splash font and VGA
8x16 console font headers, so it succeeds from an empty `build` directory and
does not depend on a prior x86_64 or AArch64 build. Its architecture-aware
default is a dual UEFI/BIOS ISO. An explicit FAT32 selection creates
`build/akaos32.img`, a real FAT32 UEFI disk containing GRUB EFI binaries and
`XSCORE32.ELF`; BIOS + FAT32 also emits the BIOS-bootable ISO fallback.
`build/akaos32.iso` contains the matching BIOS and UEFI entries and is separate
from and never overwrites x86_64 `build/akaos.iso`. `--test` and `--verbose`
are forwarded to every generated x86_32 image. Ubuntu builds require
`grub-efi-amd64-bin` and `grub-efi-ia32-bin` for x86_32 UEFI artifacts.
The i386 Ring 3 transition preserves the kernel's callee-saved registers, so
an interactive shell `exit` cleanly returns through login to PID 1, which
respawns a fresh `akaOS tty1` login session without a kernel page fault.

The direct ARM and RISC-V runners open a QEMU graphical display window by default
when running in a GUI/desktop environment, using QEMU's RAMFB display device
(`-device ramfb`) driven through `fw_cfg` DMA (`xscore_ramfb_probe`). All 6
architectures support native graphical framebuffer console rendering (`xscore/kernel/fb_console.c`)
with bootsplash, wordmark logo, and 8x16 VGA font terminal mirroring while
simultaneously multiplexing serial console I/O. When `--headless` is specified,
the runners operate in headless mode (`-display none`).

For x86_64, `run.sh` uses `--arch`, `--firmware uefi|bios`, and
`--disk-type fat32|iso`. It does not accept firmware `both`, because one QEMU
instance selects one firmware. With disk type ISO, either selection boots the
same `akaos.iso`.
Normal interactive runs allow hardware reset, so `reboot` returns to a fresh
akaOS boot without closing QEMU. Tests, capture helpers, and debug sessions keep
`-no-reboot` where reset termination is deliberate.
On AArch64 and ARM32, lifecycle requests use PSCI 0.2 through HVC. On RISC-V
64 and 32, they use OpenSBI's SRST extension, with the QEMU syscon retained as
a fallback. `tools/test-nonx86-lifecycle.sh` verifies `reboot`, `poweroff`, and
`shutdown` on every direct non-x86 port.

## Userspace Display, Desktop Shell & Terminal Emulator

Status: Native window compositor (`/bin/lumen`), desktop shell (`/bin/aurora`), color graphics client (`/bin/palette`), and graphical terminal emulator (`/bin/terminal`) implemented and verified across all six architectures.

The public `sdk/include/akaos/display.h` ABI defines versioned display metadata, pixel formats, shared-memory surface headers, compositor IPC slot structures (`struct akaos_compositor_slot`), a bounded protocol-v2 request queue (`AKAOS_COMPOSITOR_MAX_MESSAGES`), and surface layer flags:
- `AKAOS_SURFACE_FLAG_WINDOW` (0x00): Draggable application window with titlebar, close button, drop shadow, and active focus border.
- `AKAOS_SURFACE_FLAG_DESKTOP` (0x01): Root wallpaper backdrop layer (z-order 0, fixed position, no titlebar decorations).
- `AKAOS_SURFACE_FLAG_PANEL` (0x02): Docked taskbar / panel layer (z-order 1000, fixed position, no decorations).
- `AKAOS_SURFACE_FLAG_MINIMIZED` (0x04): Minimized surface state (suppressed from display rasterization, tracked in taskbar).

`/bin/lumen` is a multi-surface window compositor and display server with a synchronized attach/damage/move/focus/destroy request queue. It creates a named shared-memory IPC control channel (`/lumen-ipc`), drains lifecycle messages, maps/unmaps client buffers, tracks damage sequence numbers, sorts active surfaces by layer and z-order, and renders wallpaper backdrops, active client windows with titlebars/drop shadows/focus borders, taskbar panels, and alpha-blended mouse cursors. Damage is clipped to a dirty rectangle and streamed through `/dev/fb0` using the seekable framebuffer offset contract, while a full-frame fallback remains available when direct legacy slot changes are detected. Window titles use the shared crisp 5x7 UI font, focused chrome follows `active_surface_id`, taskbar buttons focus windows, and close actions select the next topmost window. It supports interactive window dragging and close button clicks.

`/bin/aurora` is the standalone akaOS Desktop Environment Shell & Panel. It allocates a root desktop wallpaper surface (classic slate-blue presentation with a restrained center glow, akaOS logo watermark, XSCore identity, and ready indicator) and a bottom taskbar panel surface (with a four-color akaOS launcher button, active window switcher badges synchronized with `/lumen-ipc`, active task accents, and a live indicator). Aurora remains resident for the whole graphical session. Both surfaces use protocol-v2 attach/damage/destroy messages and render labels from `sdk/include/akaos/ui_font.h`.

Desktop autostart is reboot-safe: `/bin/login` parses the persisted
`AUTOSTART_DESKTOP=1` setting exactly and uses `spawn()` with writable argv
storage, while `/bin/desktop` uses the same direct spawn path for Lumen, the
configured desktop, and default apps. Lumen allows a cold boot to initialize
its framebuffer/input devices before the launcher gives up. The two-boot
`tools/test-persistence.sh` regression verifies configuration and desktop
startup together.

`/bin/palette` is a standalone GUI graphics application drawing interactive color palettes, alpha gradients, and geometric shapes into shared-memory surfaces. Its normal attach, damage, and destroy lifecycle is submitted through the public compositor request queue rather than mutating the compositor's slot table directly.

`tools/test-compositor-clients.sh` boots all six QEMU targets and verifies two palette clients, queue ordering, damage notification, clean destruction, and a live shell marker. The test uses the x86 ISO/Multiboot path and native RAMFB paths for ARM/RISC-V.

`/bin/terminal` is a full-featured graphical terminal emulator client running on top of Lumen. It allocates a master PTY (`posix_openpt()`), spawns `/bin/akash` attached to the slave PTY, renders an 8x16 font character matrix grid into a shared-memory surface (`/terminal-surf-<pid>`), handles keyboard inputs and cursor blinking, and connects as a window surface to `/lumen-ipc`. Its attach, damage, and destroy lifecycle is queue-based, as are the corresponding lifecycles of `/bin/notepad`, `/bin/taskmgr`, and `/bin/webviewer`.

## Audio Subsystem

Status: Kernel audio driver (`/dev/audio`, `/dev/dsp`) and userspace tone generator (`/bin/beep`) implemented and verified.

- Kernel interface: `xscore/include/xscore/audio.h` and `xscore/kernel/audio.c`.
- Exposes `/dev/audio` and `/dev/dsp` as character devices with non-blocking write readiness polling (`poll()` `POLLOUT`).
- Implements audio sample ring buffering (`XSCORE_AUDIO_BUFFER_SIZE 4096`) and x86 PC speaker PIT Channel 2 square-wave frequency modulation (ports `0x61`, `0x42`, `0x43`).
- `/bin/beep` generates standalone audio tones with customizable frequency (`--freq <hz>`) and duration (`--duration <ms>`).

## Network Stack & Packet Interface

Status: Genuine hardware-backed VirtIO-Net driver (Legacy PCI on x86, MMIO on ARM/RISC-V), kernel ARP responder, raw packet interface (`/dev/net`), dynamic interface statistics ioctl (`SIOCGIFSTATS`), and network utilities (`/bin/ping`, `/bin/netstat`) are fully implemented. QEMU SLIRP gateway ping and host-served HTTP `curl` now pass on all four native MMIO ports as well as the existing x86 PCI path.

- Kernel interface: `xscore/include/xscore/net.h`, `xscore/kernel/net.c`, `xscore/kernel/virtio_net.c`, and shared `sdk/include/akaos/net.h`.
- Genuine VirtIO-Net hardware driver: Double-virtqueue architecture (16 replenishable RX descriptor buffers, synchronous TX transmission barrier), PCI legacy I/O probing (`0x1af4:0x1000`) on x86, and MMIO transport probing on ARM/RISC-V.
- Autonomous kernel ARP engine: Broadcasts gratuitous ARP announcements on link up; intercepts incoming ARP requests for `10.0.2.15` and transmits instant hardware ARP replies so QEMU SLIRP router populates its ARP table.
- Dual-mode fallback: Seamlessly falls back to internal loopback responder when no VirtIO-Net device is detected, ensuring all offline unit tests continue to pass.
- Dynamic interface statistics: `xscore_fd_ioctl` exposes `AKAOS_SIOCGIFSTATS` (`0x8910`), returning real transmitted/received packet and byte counters, MAC address, and IPv4 address.
- `/bin/ping`: Arbitrary IPv4 destination parsing, default gateway routing (`52:54:00:12:34:56`), queue draining, and sliced 50ms polling for responsive `Ctrl+C` signal handling. Verified communicating with QEMU SLIRP gateway (`10.0.2.2`) with 0% packet loss.
- `/bin/netstat`: Dynamically queries `/dev/net` via `ioctl(SIOCGIFSTATS)`, reporting live MTU, RX-OK, TX-OK, MAC, IP, and packet counts for `eth0` and `lo`.
- Native MMIO verification: `tools/test-nonx86-network.sh` attaches one
  `virtio-net-device` to each AArch64, ARM32, RISC-V64, and RISC-V32 QEMU
  `virt` guest. Every captured transcript contains `VirtIO-Net MMIO device
  initialized`, two replies from `10.0.2.2` with 0% loss, increasing `eth0`
  counters, and a successful `curl` response from a host HTTP server through
  the guest NIC. IPv4 dispatch now honors the header's total-length field so
  Ethernet minimum-frame padding cannot leak into TCP/UDP payloads.

## Security & Authentication

Status: Multi-user credential boundaries, salted cryptographic password hashing, failed login rate limiting, and account locking verified across all architectures.

- `/etc/shadow` supports salted password hashing formatted as `username:salt$hash` with 16-hexadecimal character cryptographic salts and 4096-round SHA-256 derivation.
- Failed login attempts in `/bin/login` are rate-limited with exponential backoff delay to mitigate brute-force attempts.
- Account locking (`!` or `*` prefix) is enforced by `akaos_account_verify`.
- Root permission boundaries, `su`, `passwd`, file ownership, and access modes (`0600` on shadow, `4755` on setuid binaries) are validated by `tools/test-auth.sh`.

## Architecture Ports

Status: All six architectures (x86-64, x86_32, AArch64, ARM32, RISC-V 64, RISC-V 32) are fully implemented, boot-tested in QEMU, and verified to achieve 100% operating system feature parity across all 30 userspace utilities and 45 initramfs entries.

- **x86_64**: Complete 64-bit long mode port with UEFI and BIOS boot, 4-level paging, APIC timer preemption, multithreading, full VFS/initramfs, and isolated userspace.
- **x86_32**: Complete native 32-bit protected mode port running the **unified shared XSCore kernel** (`xscore/kernel/main.c`) with Multiboot2/Multiboot1 BIOS & ISO boot (`qemu-system-i386`), 2-level paging VMM (256 MB identity mapped, shared kernel MMIO, isolated user address spaces at 0x40000000), Physical Memory Manager, GDT/IDT/TSS with ESP0, PIT/APIC 100 Hz timer with Ring 3 preemption checkpoints and signal delivery, guarded stacks, dual-arch ELF loader (ELFCLASS32/ELFCLASS64), 32-bit CRT0 (`crt0.S`), 32-bit libc runtime (`udivmod64`, 32-bit `syscall3` int 0x80 conventions), `build/initramfs32.img` (35 entries including all 26 native user binaries), bootsplash, and full PID 1 `/bin/init` -> PID 2 `/bin/login` -> PID 3 `/bin/akash` shell parity.
- **AArch64**: Native direct-kernel QEMU `virt` port with exception vectors (VBAR_EL1), four-level MMU, direct 128 MiB PMM, kernel heap, VFS/initramfs, TTY, process/thread/syscall support, AArch64 CRT/libc and all 26 userspace programs. Thread context saves `ELR_EL1`, `SPSR_EL1`, and `SP_EL0` across a blocked user syscall; `tools/test-aarch64.sh` verifies login, external command execution, child reap, and prompt resumption.
- **ARM32**: Native ARMv7 direct-kernel QEMU `virt` port with VBAR/SVC vectors, short-descriptor MMU, 128 MiB PMM, ARM32 CRT/libc and all 26 userspace programs. ARM32 user images are linked at the mapped runtime address, process address spaces protect the complete private 16 MiB window, and the thread transition saves/restores the banked user SP across blocked syscalls. `tools/test-arm32.sh` verifies login, external command execution, child exit/reap, and prompt resumption.
- **RISC-V 64**: Native RV64GC direct-kernel QEMU `virt` port with OpenSBI, Sv39 3-level MMU, 128 MiB PMM, NS16550A UART, kernel heap, VFS/initramfs, TTY, thread context switching, Ring 3 transitions, RV64 CRT/libc and all 26 userspace programs. The Ring 3 exit path preserves kernel `s0`-`s11`; `tools/test-riscv64.sh` verifies boot, login, commands, child process exit/wait, shell logout, and login respawn.
- **RISC-V 32**: Native RV32IMAFDC direct-kernel QEMU `virt` port with OpenSBI, Sv32 2-level MMU (4 MB megapages, 4 KB pages, satp mode 1), 128 MiB PMM, NS16550A UART, kernel heap, VFS/initramfs, TTY, thread context switching, Ring 3 transitions, RV32 CRT/libc and all 26 userspace programs. The Ring 3 exit path preserves kernel `s0`-`s11`; `tools/test-riscv32.sh` verifies boot, login, commands, child process exit/wait, shell logout, and login respawn.
- `sdk/include/akaos/architecture.h` provides compiler-derived architecture IDs, names, and pointer-width metadata for x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32. `tools/build-arch-smoke.sh --all` validates machine types for all 6 architectures. See `docs/ports.md` for the support matrix.

## XSCore

Status: Freestanding original x86-64 kernel booting through Phase 3 foundations, cooperative kernel threads, isolated credential-bearing process address spaces, schedulable Ring 3 tasks, process-owned descriptor tables, permission-enforcing dynamic VFS, per-process CWD tracking, kernel TTY line discipline, process syscalls, PID 1, authenticated login, and pure Ring 3 userspace shell execution.

XSCore is independently implemented and does not use Linux kernel code at runtime.

## CPU

Status: x86_64 AP startup, bounded concurrent work, CPU-owned kernel-worker
handoff, test-mode AP Ring 3 scheduling, and post-test AP placement for normal
shell descendants implemented. Non-x86 ports now expose online-secondary
counts and per-CPU READY admission/selection metadata, while userspace remains
BSP/single-core.

CPU vendor/feature detection, XSCore GDT, Ring 0/Ring 3 segments, 256-entry IDT,
normalized exception stubs, breakpoint self-test, TSS, IST, fatal diagnostics,
ACPI MADT CPU discovery, and x86_64 INIT/SIPI startup exist. Online x86_64 APs
have per-CPU TSS/stacks, Local APIC timers, idle scheduler TCBs, and CPU-owned
READY threads. The 2/4-core test queues a real kernel worker and a bounded Ring
3 `/bin/hello` workload on each AP, verifies owner-CPU execution, cross-CPU
wait/reap, and concurrently exercises the PMM and kernel heap. A fixed Local
APIC reschedule vector wakes remote blocked threads, and x86 TCBs save/restore
per-thread FXSAVE-compatible state. Non-x86 secondary-core startup now exists
as an idle-TCB, heartbeat, per-CPU timer-accounting, and READY-admission
boundary; their VMM and process scheduler remain BSP-only.
Descriptor table replacement/close paths and file-description I/O have
IRQ-safe table locking plus temporary reference ownership; vnode references
defer block reclamation until the final descriptor/image reference is released.
Process scalar/CWD snapshots and AP-safe shared-state locks are in place. The
x86_64 VMM now serializes page-table operations and uses root-aware synchronous
TLB shootdowns, including AP private-root page faults; bounded 2/4-core tests
pass. Built-in test mode remains BSP-local except for its explicit AP stress
workload, and future VFS/device/socket migration still needs separate stress.

## Memory

Status: Working PMM, VMM, and heap with anonymous and executable/file-backed
user demand paging, Copy-on-Write process cloning, and fault-aware user copy
boundaries across all six architecture ports.

User processes begin with one 4 KiB stack page. A user-mode page fault on the
immediately adjacent lower page grows the stack contiguously, up to 64 pages
(256 KiB). Every added page is user-writable, non-executable, owned by the
process, shared read-only with Copy-on-Write by the current fork implementation,
and reclaimed at reap.
Unrelated fault addresses and growth beyond the bound remain fatal. Recoverable
fault return is implemented and tested on all six architectures.

`sys_brk` now reserves the requested anonymous heap range without allocating
frames. The first load/store fault in a reserved page allocates and zero-fills
one physical page, then maps it with user, writable, and NX permissions. The
shared fault policy receives architecture-specific write/present bits from
x86, ARM, and RISC-V exception handlers.

`fork()` shares the parent's mapped image, stack, and committed heap frames
through read-only mappings and PMM reference counts. A write fault on a shared
page either restores write permission when the frame is singly referenced or
allocates/copies a private frame. The PMM uses an absolute-physical-page hash
table so native ports whose RAM begins at `0x40000000` are tracked correctly.
Each syscall entry path captures the complete user register/return frame before
`fork()`. The child restores that frame on its own kernel privilege stack,
resumes immediately after the syscall with return value `0`, and the parent
receives the child PID. This is boot-tested on all six architectures.

Syscalls no longer pass raw user pointers into VFS, file, pipe, or TTY code.
The shared user-copy layer preflights every page, rejects invalid or privileged
ranges with `EFAULT`, commits valid lazy anonymous pages, and resolves COW
write access before copy-out. Kernel-heap bounce buffers isolate read, write,
and directory I/O. Cross-page lazy buffers, invalid pointers, and COW copy-out
are runtime-tested on all six ports.

- Bitmap PMM manages conventional UEFI memory up to 64 GiB in 4 KiB pages.
- XSCore builds and activates its own four-level page tables.
- Address zero is unmapped.
- 4 KiB map/unmap/query/flag-update and 2 MiB split helpers exist.
- NXE and CR0.WP are enabled.
- High-half first-fit heap grows in at least 64 KiB units, splits and coalesces blocks, and passes boot self-tests.
- PMM bitmap/refcount operations and the heap freelist, including heap growth,
  are IRQ-safe spinlocked; AP workers stress them concurrently in the x86_64
  2/4-core QEMU test.

ELF PT_LOAD pages are recorded as file-backed reservations and loaded on first
instruction or data fault with correct file/BSS copying and segment flags.
Fork copies unloaded reservations without frames; loaded pages retain COW, and
cleanup skips never-loaded reservations. ARM32 prefetch, AArch64 instruction,
and RISC-V instruction faults are recoverable across all six ports.

Limitations: the legacy raw-pointer VFS lookup API remains for internal static
read-only callers; process-backed executable opens use the reference-counted
lookup path. Normal non-test x86_64 descendants may be AP-owned and their
private-root mappings are TLB-coherent in the tested paths. Heap pages are not
returned to the PMM until process reap, and the remaining low identity map
uses coarse writable/executable mappings.

## Kernel Section Permissions

Status: Implemented and boot-tested.

`.text` is RX, `.rodata` is R+NX, and `.data`/`.bss` are RW+NX after splitting intersecting 2 MiB mappings. This does not yet provide full-address-space W^X.

## Kernel Stacks

Status: Implemented and boot-tested.

XSCore allocates a 16 KiB boot stack, 8 KiB double-fault IST stack, 8 KiB NMI IST stack, and a dedicated TSS.RSP0 privilege-transition stack. Each is RW+NX with an unmapped guard page. Boot permanently transfers off the UEFI loader stack. On x86_64, every online AP has dedicated kernel, IST1, IST2, syscall-entry stacks, and a TSS; the other ports currently boot one active CPU.

## Interrupt Controller

Status: Real interrupt routing implemented and boot-tested on all six
architectures (updated 2026-09-06 after source/log audit).

x86_64 masks the 8259 PIC, maps and enables Local APIC MMIO, and installs IRQ
vectors 32-47. x86_32 remaps the 8259 PIC and uses its correct master/slave EOI
sequence. ACPI MADT-driven
IOAPIC ISA IRQ routing exists (`xscore_irq_route_isa` in
`xscore/arch/x86_64/apic.c`) and programs real redirection entries for PS/2
keyboard (IRQ1), COM1 serial (IRQ4), and PS/2 mouse (IRQ12). Every boot log
records `TTY keyboard, mouse and serial hardware IRQs enabled`. The timer,
keyboard, mouse, and serial paths were verified interrupt-driven in boot logs.
x86_64 also programs MSI-X table entry zero for legacy VirtIO block and network
queues, routes vectors 42/43 to the BSP, and sleeps while waiting for DMA
completion. MSI-X receipt is asserted by UEFI and network tests. Generic MSI
and a reusable multi-vector allocator are not implemented yet.

AArch64/ARM32: GICv2 distributor/CPU interfaces are initialized in
`xscore/arch/virt/device_irq.c`; the generic physical timer (IRQ30), PL011 UART
(IRQ33), and VirtIO-MMIO IRQ lines (48+) are enabled and dispatched through
IAR/EOI. AArch64 wait-loop IRQ unmasking preserves ELR_EL1/SPSR_EL1 so a nested
device interrupt cannot corrupt a blocked syscall return frame.

RISC-V 64/32: The PLIC supervisor context (0x0c002000/0x0c201000) claims and
completes VirtIO (IRQ1-8) and UART (IRQ10) interrupts, the SBI SET TIMER ecall
arms supervisor timer interrupts, and kernel-mode traps dispatch through
`xscore_device_irq_dispatch` from the `sscratch`-based trap vector with full
sepc/sstatus save/restore.

Serial logs on all four native ports record `Native device hardware interrupt
received` during real login/keyboard I/O. MMIO VirtIO disk/network waits sleep
with interrupts enabled and check used rings after GIC/PLIC or bounded timer
wakeups; ring consumption stays in process context. SMP interrupt routing
exists on x86_64 (APs run their own local APIC timers). AP timers now drive
per-CPU scheduler slices and idle-context handoff for explicit kernel-worker
and test-mode Ring 3 threads; normal post-test descendants may also be
AP-owned. The SMP userspace test creates
`/bin/hello --stack-growth-test` on every AP, waits from the BSP, and verifies
remote TCB exit/reap. Built-in test-mode startup remains BSP-local so its
ordering stays deterministic.

## Timer

Status: Implemented and boot-tested.

The Local APIC timer is calibrated against PIT channel 2 and runs at 100 Hz. Monotonic ticks advance and feed scheduler accounting.

## PCI & Block Storage

Status: x86 PCI/legacy VirtIO and ARM/RISC-V VirtIO-MMIO reads/writes plus the
bounded akaFS persistent overlay are implemented and boot-tested.

XSCore scans PCI configuration space through mechanism #1, records bounded
device/BAR metadata, and enables PCI bus mastering for selected devices. The
legacy transitional `virtio-blk-pci` path initializes queue 0 and can read or
write 512-byte sectors. On x86_64 its completion path uses MSI-X vector 42 and
halts until a device/timer interrupt; x86_32 retains synchronous polling. BIOS
x86 runners use the
primary IDE controller with a PIO fallback, avoiding legacy VirtIO completion
visibility problems while exposing the same sector API and persistence
behavior as the UEFI VirtIO path. The four native non-x86 ports
probe their QEMU `virt` MMIO transports and drive `virtio-blk-device` through
the same sector API. akaFS stores a checksummed
snapshot from sector 8 of the separate data disk and restores dynamic VFS files
and directories after initramfs initialization. The shell's `exit` builtin
commits dirty akaFS blocks before ending the session, so ordinary redirection
does not require a separate manual `sync`. The normal runner creates and
reuses `build/akaos-data.img` on every architecture. A general mount table and
full filesystem transactions remain future work beyond the current AKAFS002
dual-superblock committed-generation recovery.

## Scheduler

Status: Preemptive scheduling for Ring 3 user tasks at the timer interrupt-return boundary; cooperative scheduling with timer-requested safe checkpoints for Ring 0 kernel threads, plus an x86_64 per-CPU kernel-worker handoff foundation. Native ARM/RISC-V secondaries now feed independent timer accounting and maintain per-CPU READY admission metadata while their process scheduler remains BSP-only.

Each CPU has independent slice/preemption state. Threads carry an owner CPU;
x86_64 APs register idle TCBs and select only READY threads owned by that CPU.
The shared thread layer maintains a READY bit mask for every logical CPU and
rejects explicit CPU placement until the target is reported online. Native
ARM/RISC-V secondary counts therefore provide a real admission boundary, but
their `multicore_userspace_enabled` gate remains disabled and no user task is
migrated there yet.
Every supported port now has compile-time assertions tying its C thread
context to the hand-written switch assembly. The four native builders also
run a BSP-only address-space activation probe before launching init; native
parity boots exercise the corresponding user trap-return paths. Cross-CPU
trap-frame migration is still not enabled.
The SMP test creates a kernel worker and a bounded Ring 3 workload on each AP,
waits for owner-CPU execution, cross-CPU process exit/reap, concurrently
stresses PMM and heap allocation/free, and reclaims guarded stacks. Descriptor
tables serialize replacement/close and acquire a temporary file-description
reference for I/O before the table lock is dropped. Vnode references defer
block reclamation after unlink/rename until the final descriptor or executable
image reference is released.
Timer ticks advance a time slice and
latch a preemption request when another thread is READY. When a timer interrupt
occurs while running in Ring 3 (detected via saved CS), the IRQ return path
performs a preemption context switch on the current task's privilege-entry
stack, switching CR3 and TSS.RSP0. Resuming the preempted task restores its
complete GPR frame and executes `iretq` back to user space. Kernel threads switch
at explicit safe checkpoints or yields. Both kernel safe-checkpoint preemption
and Ring 3 interrupt-return preemption are automated and boot-tested.

## Processes & Syscalls

Status: Fixed-size isolated process foundation with preemptive multitasking,
POSIX-style `fork`/`execve`/`waitpid`, command-line and environment passing,
per-process CWD tracking, descriptor-table inheritance, compatibility spawning,
and foreground signal delivery.

XSCore has 16 process records with PIDs, lifecycle states, private CR3 roots,
owned user mappings, one scheduler thread and one 16-slot descriptor table per
process, CWD, real/effective/saved UID and GID credentials, blocking
wait/wakeup, task-owned exit status, and explicit reap cleanup. Children inherit
CWD and credentials; executable setuid/setgid mode bits update effective and
saved identities. Slot reservation and PID assignment are protected by an
IRQ-safe process-table lock, so partially initialized processes stay invisible
to lookup, signaling, procfs, and wait paths. Each descriptor table also owns
an IRQ-safe table lock; close/dup/dup2/clone and descriptor lookup acquire a
file-description reference before I/O, preventing use-after-close and pooled
slot reuse while another CPU is inspecting the object. Vnode references now
protect opened/unlinked files and executable image pages through close, fork,
exec, and reap. Process state transitions, cross-CPU waiters, and TCB reap
ordering are serialized. The x86_64 VMM now serializes page-table operations
and performs root-aware synchronous TLB shootdowns, including AP private-root
page faults; the bounded 2/4-core tests pass. Built-in test mode remains
BSP-local except for its explicit AP stress workload, and concurrent migration
of future mutable VFS/device/socket state remains a separate stress target.

Syscall ABI (`int 0x80`):
- `read` (0) — reads from files or blocking kernel TTY (`stdin`)
- `write` (1) — writes to files or console sink (`stdout`/`stderr`)
- `exit` (2) — exits process with status code
- `close` (3)
- `dup` (4)
- `dup2` (5)
- `open` (6) — with `O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_CREAT`, `O_TRUNC`
- `stat` (7) — resolves relative to process CWD
- `getdents` (8) — enumerates directory entries with `.` and `..`
- `getcwd` (9) — copies current working directory path to user buffer
- `chdir` (10) — changes process CWD with path normalization
- `mkdir` (11) — creates new directory in VFS
- `unlink` (12) — unlinks file or empty directory from VFS
- `spawn` (13) — launches executable ELF binary with arguments
- `waitpid` (14) — waits for child process termination and captures exit code
- `brk` (15) — dynamically expands/contracts process heap address space
- `pipe` (16) — creates unidirectional IPC pipe returning read/write descriptor pair
- `kill` (17) — delivers a supported signal to a process by PID
- `sync` (18), `reboot` (19), `chmod` (20), `chown` (21)
- `getuid` (22), `geteuid` (23), `getgid` (24), `getegid` (25)
- `setuid` (26), `setgid` (27), TTY secret-echo control (28), and signal disposition control (29)
- `sigaction` (30) — installs user signal action with flags and mask
- `setpgid` (31), `getpgid` (32)
- `fork` (35) — POSIX-style process cloning with frame continuation
- `sigreturn` (36) — restores user register frame and signal mask from trampoline
- `getpid` (37) — returns calling process ID
- `execve` (38) — atomically replaces the calling process image while
  preserving its PID, descriptors, CWD, process group, and applicable identity

## Signals & Job Control

Status: Implemented and boot-tested with user-defined signal handling across all 6 architectures.

The kernel shares signal numbers through `sdk/include/akaos/signal.h`, exposes
`kill(pid, signal)`, `sigaction(sig, act, oldact)`, `sigreturn()`, and `raise(sig)`
through syscalls 17, 30, 36 and libc, and tracks executed children as foreground
jobs while `akash` waits for them. Exec hands foreground ownership to children
and wait restores it to the parent.
User processes can register custom signal handlers via `sigaction()`. When delivering
a user signal, the kernel builds a `struct xscore_sigframe` on the user stack, pushes
the return trampoline address `__akaos_sigreturn_trampoline` (from libc `crt0.S`),
sets argument registers (`rdi`/`call_args`/`x0`/`r0`/`a0` = signal number), and jumps
to the user handler. When the handler returns, it pops the trampoline which invokes
`sys_sigreturn` (36). The kernel restores the exact saved user register context and
signal mask, returning cleanly to the interrupted execution flow.
Verified with `hello --signal-test` and boot self-tests across all six architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).

## Pipes & Inter-Process Communication (IPC)

Status: Implemented and boot-tested.
- Kernel `sys_pipe` allocates a 4 KiB ring buffer (`struct xscore_pipe`) with reader/writer refcounts.
- Reading blocks or yields cooperatively on empty buffers and returns `0` (EOF) once all writer descriptors close.
- Writing to full buffers yields until space is freed, or returns `-EPIPE` if all readers have closed.
- Both compatibility `spawn()` and `fork()` clone parent descriptor tables;
  `execve()` preserves them, so redirected stdin/stdout and pipelines survive
  image replacement.
- `/bin/akash` implements pipeline chaining (`cmd1 | cmd2`), connecting child process stdout/stdin via `dup2()`.

## Heap & Memory Management

Status: Implemented and boot-tested.
- Kernel `sys_brk` manages process heap regions starting at `0x0000008000400000`, mapping 4 KiB user pages on demand up to 1 MiB per process.
- `sys_brk` reserves virtual pages; the shared page-fault handler commits zero-filled 4 KiB frames only on first access, with architecture handlers supplying write/present fault flags.
- `fork()` shares committed user image/stack/heap frames read-only with PMM reference counts and resolves writes with Copy-on-Write; `hello --vm-test` verifies zero-fill and parent/child isolation on all six ports.
- All allocated heap frames are tracked in `struct xscore_process` and 100% reclaimed on process exit/reap.
- Userspace `malloc()`, `free()`, `calloc()`, `realloc()` freelist allocator in libc with 16-byte alignment, block splitting, and bidirectional coalescing.

## TTY & Input Subsystem

Status: Kernel TTY line discipline with canonical buffer, character echo, backspace handling, Ctrl+D EOF, Ctrl+C/SIGINT, PS/2 shift keymap, a 1024-byte pending burst queue, serial/VirtIO control-key handling, full ANSI CSI escape sequence parser (`\033[2J`, `\033[H`, `\033[row;colH`, SGR `\033[7m` reverse video, `\033[0m` reset, `\033[K` line erase), and blocking `read(STDIN_FILENO)`.

- `xscore/kernel/tty.c` processes PS/2 scan codes, serial bytes, and VirtIO input, manages Shift/Ctrl state, and buffers keystrokes into a 256-byte canonical line buffer plus a 1024-byte pending queue. Ctrl+C submits an empty line when the shell is idle and signals the foreground child otherwise; Ctrl+D submits EOF without echoing a control glyph, allowing stdin consumers such as `cat` to return to the shell. Raw-mode pending bursts are drained in batches so input is not reduced to one syscall per character.
- Printable characters and backspaces are visually echoed to the framebuffer console in real time.
- Line submission on Enter (`\n`) unblocks waiting user threads.
- Framebuffer terminal sink (`xscore/kernel/main.c`) parses ANSI escape codes with multi-parameter buffering, preventing stray escape artifacts and providing native reverse-video banner rendering for full-screen applications like `/bin/ted`.

## VFS

Status: Dynamic in-memory VFS rooted with generated binary initramfs archive, POSIX file permissions & ownership (`chmod`/`chown`), IRQ-safe metadata mutators, and dual-superblock write-ahead journaling (`AKAFS002`).

The build packs 26 native programs (the prior 23 plus `login`, `su`, and
`passwd`) and `/etc/motd`, `/etc/passwd`, `/etc/group`, and mode-0600
`/etc/shadow` into `build/initramfs.img`. Initramfs entries carry real octal
permission/special bits rather than the former executable boolean.

The VFS supports:
- Full relative and absolute path resolution (`xscore_vfs_resolve_path_at`), normalizing `.`, `..`, trailing slashes, and relative components.
- Dynamic in-memory creation (`xscore_vfs_mkdir_at`, `xscore_vfs_open_at` with `O_CREAT`).
- In-memory deletion (`xscore_vfs_unlink_at`) with `-ENOTEMPTY` validation on directories.
- Permission management (`xscore_vfs_chmod_at`) and ownership assignment (`xscore_vfs_chown_at`).
- Effective UID/GID checks for path traversal, file/directory read and write,
  creation, deletion, `chdir`, `chmod`, and root-only `chown`.
- Dynamic file read/write buffers up to 128 KiB with `O_TRUNC` support.
- Write protection (`-EROFS`) on read-only initramfs nodes.
- Directory listing (`getdents`) returning `.` and `..` entries followed by child nodes.

Dynamic VFS state is synchronized to the akaFS overlay via the in-kernel LRU sector buffer cache using dual alternating superblocks (Sector 8 & Sector 9, Payload at Sector 10) with FNV-1a checksums, committed transaction states, and generation counters for power-loss recovery. The kernel includes a 64-sector LRU buffer cache with dirty writeback and explicit synchronization via the `sync` syscall and `/bin/sync` utility.

## Native Runtime and Applications

Status: Freestanding C runtime/libc with CWD, dynamic heap allocator (`malloc`/`free`), formatted I/O (`printf`/`snprintf`), process spawning, IPC pipes, buffer cache synchronization (`sync`), system reset & poweroff (`reboot`), identities/accounts, file permissions, full-screen text editor (`ted`), and dynamic filesystem support; 26 native programs including PID 1, login, shell, and account utilities.

`userspace/libc` provides:
- Startup: current x86-64 `_start`, argument parsing into `main(argc, argv)`, `_exit`.
- Heap: `malloc()`, `free()`, `calloc()`, `realloc()`, `brk()`, `sbrk()`.
- Standard I/O: `printf()`, `fprintf()`, `sprintf()`, `snprintf()`, `vsnprintf()`, `vfprintf()`, `puts()`, `putchar()`.
- Syscalls: `open`, `read`, `write`, `close`, `dup`, `dup2`, `stat`, `getdents`, `getcwd`, `chdir`, `mkdir`, `unlink`, `rmdir`, `spawn`, `fork`, `execve`, `execv`, `waitpid`, `brk`, `pipe`, `kill`, `sync`, `reboot`, `chmod`, `chown`.
- String & memory: `strlen`, `strcmp`, `strncmp`, `strncpy`, `strchr`, `strrchr`, `memcmp`, `memcpy`, `memset`, `atoi`.

Native utilities in `/bin`:
- `/bin/init`: Root PID 1 supervisor. Establishes home ownership, spawns
  `/bin/login`, waits/reaps it, and respawns a fresh login after logout.
- `/bin/login`: Authenticates `/etc/shadow`, sets account GID/UID and home, and
  starts the configured shell.
- `/bin/su`: Setuid-root authenticated identity/session switcher.
- `/bin/passwd`: Setuid-root password changer; non-root users must verify their
  current password and may change only their own account.
- `/bin/akash`: Native userspace interactive shell running in unprivileged Ring 3. Reads commands from `stdin`, handles terminal EOF and every byte in multi-line reads, clamps script positional arguments, supports shell-specific builtins (`cd`, `exit`, `help`, `about`, `version`, `id`, `hostname`, `shutdown`, `jobs`, `fg`, `bg`), external utilities, multi-stage pipeline chaining (`cmd1 | cmd2 | cmd3 | ...`), file redirections (`< in`, `> out`, `>> app`), unquoted wildcard expansion, environment variable expansion (`$USER`, `$HOME`, `$PWD`, `$ARCH`, `$?`), command history navigation (Up/Down arrow keys via TTY line discipline), Ctrl+C foreground job interruption, and external program execution through `fork()` + `execv()` + `waitpid()`.
- `/bin/ted`: Native full-screen interactive ANSI text editor with buffer management, line navigation, status bar, `Ctrl+O` save, and `Ctrl+X` exit.
- `/bin/chmod`: Changes file permission bits; supports octal (e.g. `755`, `0644`) and symbolic modes (`+x`, `-w`, `u+rw,go=r`).
- `/bin/chown`: Changes file ownership and group; supports `owner[:group]` (or `uid:gid`).
- `/bin/reboot`: calls `sync()` then triggers hardware reboot via `reboot(RB_AUTOBOOT)`.
- `/bin/poweroff`: calls `sync()` then triggers system poweroff via `reboot(RB_POWER_OFF)`.
- `/bin/sync`: calls `sync()` syscall to synchronize in-kernel dirty cached sectors and VFS persistence to storage.
- `/bin/clear`: sends ANSI/formfeed screen-clearing sequence to clear framebuffer terminal.
- `/bin/grep`: pattern matching across stdin or files; supports `-i` (case-insensitive) and `-n` (line numbers).
- `/bin/head`: outputs first $N$ lines/bytes (`-n`, `-c`) from stdin or files with multi-file headers.
- `/bin/tail`: outputs last $N$ lines (`-n`) from stdin or files using dynamic circular line buffering.
- `/bin/wc`: counts lines (`-l`), words (`-w`), and bytes (`-c`) from stdin or files with formatted total summaries.
- `/bin/uname`: prints system/kernel/architecture information (`-s`, `-n`, `-r`, `-v`, `-m`, `-o`, `-a`) derived from `sdk/include/akaos/architecture.h`.
- `/bin/whoami`: resolves the effective UID through `/etc/passwd`.
- `/bin/hello`: tests process isolation, descriptors, pipe IPC, heap allocation/realloc/calloc/free, formatted `printf`, and syscall errors.
- `/bin/ls`: lists CWD or target directory/file; supports `-a` (dots), `-l` (permissions, owner, size), `-la`, `-h`, multiple arguments, bounded path construction, real error messages to stderr (`ls: cannot access 'xxx': No such file or directory`), and non-zero exit code on missing paths.
- `/bin/cat`: streams files to stdout; supports multiple files, detects directories (`EISDIR`) and missing files (`cat: xxx: No such file or directory`) to stderr with exit status 1.
- `/bin/touch`: creates empty files via `O_WRONLY | O_CREAT`.
- `/bin/mkdir`: creates directories via `mkdir()`.
- `/bin/rm`: removes files and directories; supports `-r`, `-R`, `--recursive` (recursive tree removal), `-f`, `--force` (ignore missing), `-d`, `--dir` (empty directories), `-v`, `--verbose`, combined flags (`-rf`), and `--help`.
- `/bin/pwd`: prints working directory via `getcwd()`.
- `/bin/echo`: prints arguments to stdout, with `-n`, `--`, `--help`, and `--version` handling.

## Build Status

Verified on 2026-09-07 using `wsl.exe -d Ubuntu`. The UEFI FAT32 build cleanly
compiles 26 native programs, packages a 35-entry initramfs with account data,
builds XSCore and its loader, and outputs `build/akaos.img`.

The same build also emits `build/XSCORE-BIOS.ELF`, the BIOS-linked entry image,
and `build/XSCORE32.ELF` (the native 32-bit x86_32 kernel image). The native
ARM32 builder emits `build/XSCORE_ARM32.ELF` and its 35-entry initramfs.

`./tools/build-arch-smoke.sh --all` also passes for x86_64, x86_32, AArch64,
ARM32, RISC-V64, and RISC-V32 under Ubuntu WSL. The native AArch64 and ARM32
images are additionally rebuilt and boot-tested by `tools/test-aarch64.sh` and
`tools/test-arm32.sh`.

`./tools/test-bios.sh` passes under Ubuntu WSL with GRUB Multiboot2 tooling and
QEMU/SeaBIOS, including framebuffer-tag acceptance, PCI/virtio sector reads,
and the Ring 3 process lifecycle self-test.

## Test Status

All listed baseline suites plus the native AArch64, ARM32, RISC-V 64, and RISC-V 32 suites verified passing on 2026-09-08 after the TTY burst, shell-signal, and ARM32 logout fixes. The x86_64 2/4-core SMP regression also passes with AP-owned kernel workers and concurrent allocator stress:
1. `./build.sh` (Dual-firmware and multi-arch build success across all 6 architectures)
2. `./test.sh` (UEFI/OVMF automated process-lifecycle self-test)
3. `./tools/test-x86_32.sh` (i386 kernel/userspace parity, clean shell logout and PID 1 login respawn, no fatal kernel diagnostics)
4. `./tools/test-x86_32-iso.sh` (one x86_32 ISO booted to login through OVMF/UEFI and SeaBIOS)
5. `./tools/test-auth.sh` (Multi-user authentication, root/user login, `su`, `passwd`)
6. `./tools/test-sigint.sh` (SIGINT foreground handoff & TTY line discipline)
7. `./tools/test-reboot.sh` (In-session reboot keeping QEMU open)
8. `./tools/test-persistence.sh` (akaFS dual-superblock persistence)
9. `./tools/test-bios.sh` (Legacy BIOS / SeaBIOS boot)
10. `./tools/test-interactive-sequence.sh` (End-to-end interactive shell session)
11. `./tools/test-iso.sh` (x86_64 dual-firmware hybrid ISO validation)
12. `./tools/test-aarch64.sh` (Native AArch64 direct-kernel QEMU login and shell parity)
13. `./tools/test-arm32.sh` (Native ARM32 direct-kernel QEMU login and shell parity)
14. `./tools/test-riscv64.sh` (Native RISC-V 64 direct-kernel QEMU login and shell parity)
15. `./tools/test-riscv32.sh` (Native RISC-V 32 direct-kernel QEMU login and shell parity)
16. `SMP_QEMU_TIMEOUT=120s ./tools/test-smp.sh` (2-core and 4-core AP handoff,
    PMM/heap stress, AP Ring 3 scheduling, and remote process reclamation)
16. `./tools/test-nonx86-lifecycle.sh` (ARM/RISC-V reboot, poweroff, and shutdown behavior)
17. `./tools/test-nonx86-persistence.sh` (ARM/RISC-V echo redirection and two-boot akaFS restoration)
18. `./tools/build-arch-smoke.sh --all` (All 6 architecture smoke builds passing)
19. Exact `./run.sh --arch x86_32 --disk-type fat32 --headless` two-boot test
    (echo redirection survives logout and relaunch)

The 2026-08-31 fork-continuation change was specifically re-verified with
`./test.sh`, `./tools/test-x86_32.sh`, `./tools/test-aarch64.sh`,
`./tools/test-arm32.sh`, `./tools/test-riscv64.sh`, and
`./tools/test-riscv32.sh`. Every suite observed `fork()` return zero in the
child and the child PID in the parent while preserving COW isolation.

After the latest model-authored page-cache, signal, and version changes, the
same six runtime suites were run again sequentially on 2026-08-31 under
Ubuntu WSL and all passed. The x86 logs additionally verified clean executable
page sharing and cache accounting; all six logs verified user-defined signal
handling. `./tools/build-arch-smoke.sh --all` also passed for all targets. No
source correction was needed during this audit.

The subsequent POSIX exec milestone was verified with the same six suites.
Every port now requires the image-replacement, environment-aware userspace,
and fork-exec-wait markers. Focused SIGINT, authentication, interactive
pipeline/redirection, persistence, and architecture smoke tests also pass.

The private mmap milestone was then verified with `hello --mmap-test` in
`./test.sh`, `./tools/test-x86_32.sh`, `./tools/test-aarch64.sh`,
`./tools/test-arm32.sh`, `./tools/test-riscv64.sh`, and
`./tools/test-riscv32.sh`. All six tests passed, including anonymous zero-fill,
fork COW isolation, descriptor-independent private file mappings, and unmap
cleanup. `./tools/build-arch-smoke.sh --all` also passes after the final ABI
validation checks.

On 2026-09-01 the same six runtime suites were rerun with mandatory
`hello --shared-memory-test` coverage. Every port passed independent named
object opens, shared physical-page visibility, `mprotect` permission changes,
unlink lifetime, and cleanup. The all-architecture smoke build also passed.

## Pollable descriptors and input events

Syscall 45 and libc `<poll.h>` provide `poll()` for up to 16 descriptors.
Readiness covers canonical TTY input, regular/memory/directory descriptors,
pipes (including writable space, EOF hangup, and broken-reader errors), and
input-event descriptors; invalid descriptors report `POLLNVAL`. Zero,
positive platform-counter, and infinite waits are supported, and pending
signals interrupt a blocking wait with `EINTR`.

Root userspace can open `/dev/input/event0` read-only. Each open receives a
cursor into a bounded 128-entry type/code/value event ring, so consumers do
not steal events from one another. Keyboard scan codes, translated serial text,
relative mouse movement/buttons, and synchronization events share the stable
ABI in `sdk/include/akaos/input.h`. x86 uses the PS/2 keyboard and a polled
three-byte PS/2 mouse decoder. ARM and RISC-V QEMU runs attach both VirtIO
keyboard and mouse devices, and the MMIO driver now manages up to four input
devices instead of stopping after the first.

`hello --poll-test` verifies pipe read/write readiness, hangup, `POLLNVAL`, and
opening/polling the input descriptor. The test passes in every architecture's
booted userspace parity suite. A direct AArch64 QEMU check additionally found
and initialized two simultaneous VirtIO input devices.

The subsequent display milestone passed `./test.sh`,
`./tools/test-x86_32.sh`, `./tools/test-aarch64.sh`,
`./tools/test-arm32.sh`, `./tools/test-riscv64.sh`, and
`./tools/test-riscv32.sh`. Every port verified the Lumen cross-process shared
surface protocol. x86_64 verified `/dev/fb0` metadata, and all four ARM/RISC-V
suites completed `lumen --once` against QEMU RAMFB without leaking pixel bytes
to serial. `./tools/build-arch-smoke.sh --all` and shell syntax checks also
passed after the final framebuffer descriptor routing fix.

## Visible device filesystem and uname dispatch

`/dev` is now a real VFS-visible kernel device namespace rather than a set of
paths recognized only by `open()`. It contains typed character nodes for
`tty1`, `null`, `zero`, `fb0`, and `input/event0`. Device identity is selected
after normal CWD-aware path resolution. Null, zero, TTY, framebuffer, and input
descriptors have their own behavior and readiness; the namespace itself is
not stored in the persistent akaFS overlay.

`akash` no longer shadows `/bin/uname` with a fixed-output builtin. The native
utility owns option validation and output, including full `-a` formatting and
errors for the operands `a` and `-`. `hello --devfs-test` validates modes,
directory enumeration, null/zero I/O, tty polling, and relative opens. All six
QEMU parity suites passed with the new device and uname checks on 2026-09-01.

## Uniform Build/Run Combination Routing (2026-08-30)

`build.sh` and `run.sh` no longer reject any combination made from their
documented architecture, firmware, and disk-type values. Native ARM/RISC-V
ports accept all shared selectors. x86_32 FAT32 selections create and boot
`build/akaos32.img` under UEFI; BIOS + FAT32 also emits/selects the BIOS-
capable hybrid ISO fallback. x86_64 BIOS + FAT32 likewise creates the FAT32
image and selects the ISO fallback at run time. Invalid or missing CLI values
are still diagnosed as input errors.

Verified commands include:

- `./build.sh --arch x86_32 --disk-type fat32` (completed with `akaos32.img` and `akaos32.iso`)
- `./run.sh --arch x86_32 --firmware uefi --disk-type fat32 --headless` (booted the FAT32 disk to `akaOS tty1`)
- `./run.sh --arch x86_32 --firmware bios --disk-type fat32 --headless` (booted the ISO fallback to `akaOS tty1`)
- `./build.sh --arch x86_64 --firmware bios --disk-type fat32` (completed)
- `./run.sh --arch x86_64 --firmware bios --disk-type fat32 --headless` (entered XSCore through the BIOS ISO fallback)
## Memory mappings and shared memory

Syscalls 39 and 40 expose demand-paged private mappings on every supported
architecture. `MAP_PRIVATE | MAP_ANONYMOUS` mappings reserve a bounded virtual
range and receive zero-filled pages on first read/write/execute fault. A
private file mapping copies the requested file range into a kernel-owned
snapshot before returning, allowing the descriptor to be closed without
invalidating the mapping; file bytes are still committed lazily per page.

Fork shares committed mapping pages read-only and resolves writes with the same
physical-page COW machinery used by image, stack, and heap pages. `munmap`
handles page-aligned ranges, releases resident frames and file snapshots, and
is also used by process teardown.

`MAP_SHARED` is implemented for anonymous mappings and named shared-memory
objects. Syscalls 41-44 provide `mprotect`, `shm_open`, `shm_unlink`, and
`ftruncate`. A named object owns demand-allocated physical pages; independently
opened mappings reference the same frames and observe writes immediately.
Fork shares these frames without COW, while private mappings remain COW.
Unlink removes the name but existing descriptors and mappings remain valid.

The namespace is intentionally bounded to 16 objects, names are at most 31
characters, and objects are at most 256 KiB. Resizing currently requires no
active mappings. Regular VFS files still support private snapshots only rather
than write-through `MAP_SHARED`. `hello --shared-memory-test` validates
independent child opens, shared writes, `O_EXCL`, `PROT_NONE` restoration,
unlink lifetime, and cleanup on all six architecture ports.
The shell also expands unquoted `*` and `?` patterns against the VFS before
launching a command, including path prefixes such as `/root/*`. Quoted patterns
remain literal and unmatched patterns remain unchanged. Glob path buffers live
in static shell storage rather than on the userspace stack so 32-bit ports do
not overflow at login. `rm *` and invalid `pwd -` behavior are boot-tested on
x86_32, AArch64, ARM32, RISC-V64, and RISC-V32; the shared shell is also covered
by the x86_64 UEFI and interactive suites. Duplicate utility builtins were
removed so `/bin/pwd`, `/bin/echo`, `/bin/clear`, `/bin/whoami`, `/bin/reboot`,
and `/bin/poweroff` are the single implementations.

## Sessions, PTYs, and generated procfs (2026-09-02)

The shared process layer now tracks SID, PGID, and controlling-TTY ownership.
Syscalls and libc expose `setsid`, `getsid`, `setpgid`, `getpgid`,
`tcsetpgrp`, and `tcgetpgrp`. The TTY delivers terminal signals to its real
foreground process group. `akash` creates groups for external commands and
pipelines, transfers foreground terminal ownership while waiting, and restores
its own group afterward.

The device filesystem now includes `/dev/ptmx` and `/dev/pts`. Opening ptmx
allocates one of four kernel PTY pairs; `ptsname()` identifies the dynamically
visible `/dev/pts/N` slave. Master/slave queues support blocking bidirectional
I/O and poll state. This is a byte-stream PTY foundation, not full termios/ioctl
or canonical slave line discipline.

Generated `/proc/self/status` and `/proc/processes` files provide stable
per-open snapshots of real process state. The implementation does not yet have
per-PID directories or a general procfs registration model.

Final verification passed `test.sh`, `tools/test-bios.sh`,
`tools/test-interactive-sequence.sh`, and the full parity suites for x86_32,
AArch64, ARM32, RISC-V64, and RISC-V32. The new session, PTY, and proc tests
passed on all six architectures. The expanded ARM32 run exposed and fixed a
real context-switch fault: `xscore_vmm_activate()` now synchronizes growing
kernel mappings before installing an older process TTBR, preventing an active
kernel stack from becoming unmapped.

## Termios, ioctl, terminal tools, and per-PID proc views (2026-09-02)

Syscall 49 implements a bounded terminal ioctl ABI shared by every port.
`tcgetattr`/`tcsetattr` support `ICANON`, `ECHO`, `ECHOE`, `ISIG`, `ICRNL`,
`OPOST`, and `ONLCR`, plus `VMIN` and the interrupt/erase/EOF/suspend control
characters. TTY and PTY descriptors expose window sizes and terminal names.
PTY master input now passes through canonical/raw line discipline, echo, erase,
EOF, and terminal-signal processing rather than entering the slave byte queue
unconditionally.

The initramfs contains native `/bin/tty`, `/bin/stty`, and `/bin/ps`. Akash
assembles input lines even when raw mode returns one byte at a time and tracks
all members of foreground, stopped, and background pipelines as one process
group. Absolute `/proc/<pid>/status` opens generate a live status snapshot;
dynamic PID directories are not yet listed by `getdents`.

Final tests passed on x86_64 UEFI/BIOS/interactive, x86_32, AArch64, ARM32,
RISC-V64, and RISC-V32. Every architecture executed PTY canonical/raw and
window-size checks plus the real `tty`, `stty -a`, and `ps` utilities.

## Dynamic Procfs Directory Enumeration & Introspection (2026-09-02)

XSCore's virtual `/proc` filesystem dynamically enumerates active processes through
`getdents()`. Listing `/proc` returns virtual directory entries for every running PID
alongside standard virtual files (`self`, `processes`, `mounts`). Opening, stating,
and listing `/proc/<pid>` provides virtual subdirectory structures with per-process
`status` files reporting PID, PPID, PGID, SID, UID, GID, state, memory statistics,
and executable command names.

The libc and userspace test suite in `userspace/bin/hello/main.c` (`--proc-test`)
verifies dynamic `/proc` directory reading, stat calls on active PID folders, directory
traversals of `/proc/<pid>`, and per-PID status parsing across all 6 architectures.

## Compositor Client IPC, Window Management & Palette GUI Client (2026-09-02)

`/bin/lumen` is expanded from a single-surface test into a multi-surface window
compositor with client IPC channel management (`/lumen-ipc`, `struct akaos_compositor_channel`).
Clients register surface SHM paths (`/palette-surf-<pid>`), window titles, coordinates,
dimensions, z-ordering, and damage sequences in shared memory.

Lumen renders window decorations (styled titlebars, close buttons, drop shadows,
focus borders), clips overlapping surfaces, composites alpha-blended mouse pointers,
and supports interactive window dragging and focus switching.

`/bin/palette` is introduced as a standalone graphical application rendering vibrant
color themes, a gradient spectrum bar, and geometric shapes into shared-memory surface
buffers, registering with `/lumen-ipc`. Both `lumen` and `palette` are built and packaged
into initramfs archives across all 6 architectures (`x86_64`, `x86_32`, `aarch64`,
`arm32`, `riscv64`, `riscv32`). Automated `--self-test` protocol verification passes on
all targets.

## Next-Gen Subsystems: Graphical Terminal, Audio, Network Stack & Compositor Optimization (2026-09-02)

- **Lumen Damage Tracking**: Accumulated dirty regions (`akaos_damage_rect`) and selective hardware blitting.
- **Graphical Terminal Emulator (`/bin/terminal`)**: PTY master/slave allocation, child `/bin/akash` connection, 8x16 font rasterization, 64x20 character matrix, and `/lumen-ipc` window registration.
- **Audio Subsystem**: Kernel audio engine in `xscore/kernel/audio.c`, character devices `/dev/audio` and `/dev/dsp`, ring buffer sample queue, PC speaker modulation, and `/bin/beep` audio utility.
- **Network Subsystem**: Raw Ethernet character device `/dev/net`, kernel packet ring, ARP/ICMP Echo loopback responder, VirtIO-net driver skeleton, and `/bin/netstat` utility.
- **Interactive `/bin/ping` & `sys_poll` Signals**:
  - Upgraded `/bin/ping` with `SIGINT` signal handler, continuous ping mode (default until `Ctrl+C` or count reached with `-c`), 1-second cadence, and accurate packet transmission/loss statistics.
  - Added `xscore_tty_poll_keyboard()` and interrupt unmasking (`sti; pause` / `wfe` / `wfi`) in `sys_poll` wait loops in `xscore/kernel/syscall.c` so `Ctrl+C` interrupts and serial/keyboard scancodes are handled instantaneously during polling.
- **Multi-Architecture Verification**: 100% test parity achieved across all 11 test suites and 6 CPU architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).

## Real Advanced Network Stack, BSD Sockets, Web Suite & System Configuration (2026-09-04)

Status: Complete and verified across all 6 CPU architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).

- **Hardware-Backed VirtIO-Net Driver**:
  - Direct PCI (`x86_64`, `x86_32`) and MMIO (`aarch64`, `arm32`, `riscv64`, `riscv32`) transport drivers.
  - Multi-virtqueue ring management (RX queue 0 with replenishable buffers; TX queue 1 with descriptor staging).
  - Autonomous ARP resolution and gratuitous ARP announcement pre-seeding gateway ARP tables.
  - Dynamic network interface stats ioctl (`AKAOS_SIOCGIFSTATS` via `/dev/net`).
- **BSD Sockets & In-Kernel TCP/UDP**:
  - Syscalls 50-56: `sys_socket`, `sys_connect`, `sys_bind`, `sys_listen`, `sys_accept`, `sys_sendto`, `sys_recvfrom`.
  - Full TCP state machine: `CLOSED`, `LISTEN`, `SYN_SENT`, `SYN_RCVD`, `ESTABLISHED`, `FIN_WAIT_1`, `FIN_WAIT_2`, `CLOSE_WAIT`, `LAST_ACK`, `TIME_WAIT`.
  - Socket integration into VFS file descriptor table (`FILE_SOCKET`) supporting unified `read`, `write`, `poll`, `close`.
  - Stack-safe static buffer architecture preventing kernel stack overflows; decoupled loopback packet queueing (`xscore_net_poll_loopback`).
- **DNS Resolver & Applications**:
  - Standard libc `gethostbyname()` over UDP querying `/etc/resolv.conf` (or default QEMU gateway `10.0.2.3:53`).
  - Native `/bin/nslookup` client and domain resolution in `/bin/ping`.
  - Native `/bin/sysmon` system and network telemetry monitor (`--once`, `--json`, interactive).
- **System Configuration Tool (`/bin/akaos-config`)**:
  - Raspi-config style interactive ANSI terminal menu and scriptable CLI options (`--status`, `--enable-httpd`, `--disable-httpd`, `--port`, `--hostname`).
  - Manages persistent settings in `/etc/akaos.conf`.
  - CLI mutations now support multiple options per invocation, validate ports (1-65535), boolean values, and RFC-style hostnames, and report persistence failures instead of silently claiming success.
  - Loaded configuration values are normalized back to safe defaults when the saved file contains malformed ports, toggles, hostname, or desktop identifiers.
- **Native desktop presentation hardening**:
  - Aurora remains resident for the full graphical session instead of exiting after a short demo interval.
  - Aurora wallpaper/panel use a restrained central glow, gradient glass-like panel, active task accents, and a live status marker; Lumen titlebars include macOS-style traffic lights, close control, shadows, and an active focus ring.
- **Micro Web Daemon (`/bin/httpd`) & HTTP Client (`/bin/curl`)**:
  - Web server daemon serving `/var/www/index.html`.
  - **Strictly disabled by default** (`HTTPD_ENABLED=0` in `/etc/akaos.conf`); only runs when explicitly enabled by the user via `akaos-config`.
  - HTTP client `/bin/curl` supporting URL parsing, `-i`, `-I`, `-s`, `-o`.
- **QEMU Port Forwarding**:
  - Integrated `hostfwd=tcp::8080-:80` in `run.sh` across all 6 architectures.
- **Verification**:
  - Automated network test harness `tools/test-advanced-network.sh` passed.
  - All 6 architecture parity suites verified and passing.

## Remote Shell, GUI Desktop Suite, VFS Mount Table, File Transfer & Linux `ip` Utility (2026-09-04)

Status: Complete and verified across all 6 CPU architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).

- **Linux-Compatible `ip` Command (`/bin/ip`)**:
  - Supports `ip link` / `ip link show`: displays device attributes, MAC address, MTU 1500, state UP, and flags (`<BROADCAST,MULTICAST,UP>`).
  - Supports `ip addr` / `ip addr show`: displays IPv4 addresses (`10.0.2.15/24` with broadcast `10.0.2.255`, loopback `127.0.0.1/8`).
  - Supports `ip route` / `ip route show`: displays default gateway route `default via 10.0.2.2 dev eth0` and kernel scope link routes.
  - Supports `ip neigh` / `ip neigh show`: queries ARP neighbor cache table showing gateway and DNS servers `REACHABLE`.
- **VFS Mount Table & Filesystem Management**:
  - Added Syscall 57 (`AKAOS_SYSCALL_MOUNT`) and Syscall 58 (`AKAOS_SYSCALL_UMOUNT`).
  - Implemented kernel mount table in `xscore/kernel/vfs.c` (up to 8 active mounts) with default mounts:
    `rootfs on / type akafs (rw)`
    `devfs on /dev type devfs (rw)`
    `procfs on /proc type procfs (ro)`
  - Implemented dynamic `/proc/mounts` file generation and directory entries in `/proc`.
  - Created standard userspace utilities `/bin/mount` (supports `mount`, `mount -a`, `mount -t ...`) and `/bin/umount`, backed by default `/etc/fstab`.
- **Remote Shell & Network Daemons (`/bin/telnetd`, `/bin/tftpd`, `/bin/tftp`)**:
  - Implemented Telnet daemon `/bin/telnetd` (RFC 854): listens on TCP port 23, performs Telnet option negotiations, allocates master/slave PTYs, and executes `/bin/login`.
  - Implemented TFTP daemon `/bin/tftpd` (RFC 1350): listens on UDP port 69, serves read and write file requests to `/tmp`.
  - Implemented TFTP client `/bin/tftp`: supports `tftp get <host> <file>` and `tftp put <host> <file>`.
  - Enhanced `/bin/httpd` with dynamic CGI script execution: requests to `/cgi-bin/*` fork and pipe execution of binaries under `/var/www/cgi-bin/` (including dynamic system status script `/var/www/cgi-bin/status`).
  - Integrated daemon configuration toggles in `/bin/akaos-config` (`--set-telnet`, `--set-tftp`, `--enable-telnetd`, `--enable-tftpd`) with persistent configuration in `/etc/akaos.conf`.
  - **All servers strictly disabled by default** (`HTTPD_ENABLED=0`, `TELNETD_ENABLED=0`, `TFTPD_ENABLED=0`).
  - QEMU host port forwarding configured in `run.sh` across all 6 architectures (`hostfwd=tcp::8080-:80,hostfwd=tcp::2323-:23,hostfwd=udp::6969-:69`).
- **Desktop GUI Application Suite**:
  - Implemented desktop launcher `/bin/desktop` (launches Lumen compositor, Aurora environment, and desktop apps).
  - Implemented `/bin/notepad`: graphical text editor on Lumen with line rendering, cursor display, and status bar.
  - Implemented `/bin/taskmgr`: graphical task manager with CPU and memory status meters and live `/proc/processes` process table.
  - Implemented `/bin/webviewer`: graphical web browser with BSD socket HTTP fetching, status bar, and HTML text renderer.
  - All GUI applications feature automated `--self-test` protocols for CI and headless testing.
- **Libc Runtime Enhancements**:
  - Added `stdin`, `stdout`, `stderr`, and standard `perror()` to `userspace/libc/include/stdio.h` and `stdio.c`.
  - Added `strcpy()`, `strstr()`, and `strerror()` with complete POSIX errno mappings to `userspace/libc/include/string.h` and `string.c`.
  - Created `userspace/libc/include/sys/mount.h` and libc wrappers `mount()`, `umount()`, and `umount2()`.
- **Automated Verification**:
  - Created `tools/test-suite-services.sh` verifying `ip`, `mount`, `akaos-config`, daemons, and GUI application self-tests.
  - Verified 100% full parity across all 6 architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).

## raspi-config Terminal UI, Dynamic Desktop Selection & Desktop Autostart (2026-09-04)

Status: Complete and verified across all 6 CPU architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).

- **raspi-config Styled Terminal UI (`/bin/akaos-config`)**:
  - Full-screen raspi-config styled ANSI terminal interface (`\033[44m` blue background, centered light-grey dialog window `\033[47;30m`, top title banner, bottom navigation bar with `<Select>` / `<Finish>` / `<Back>` buttons).
  - Main menu categories:
    - `1 System Options`: Hostname and audio bell configuration.
    - `2 Interface Options`: Micro Web Server (HTTPD), Telnet Daemon (telnetd), TFTP Daemon (tftpd).
    - `3 Desktop / Boot Options`: Autostart Desktop and Desktop Environment selection.
    - `4 Network Information`: Live interface telemetry and active IP assignments.
    - `5 About akaOS`: Distribution release, kernel architecture, and desktop environment details.
  - **Dynamic Installed Desktop Discovery**:
    - Queries the filesystem directly (`open(path, O_RDONLY)`). Only installed desktop environments (e.g. `/bin/aurora`) appear in the interactive selection menu. Uninstalled environments (such as KDE, GNOME, XFCE) are never shown if their binaries are missing.
    - CLI validation in `--desktop-env <env>` / `--env <env>`: validates binary existence before applying changes, rejecting uninstalled environments with: `akaos-config: error: desktop environment '<env>' is not installed`.
    - Shows installation status in `akaos-config --status`.
  - Scriptable CLI arguments:
    - `--autostart-desktop <0|1>`, `--enable-autostart-desktop`, `--disable-autostart-desktop`.
    - `--desktop-env <env>`.
  - Persistent state saved to `/etc/akaos.conf` (`AUTOSTART_DESKTOP` and `DESKTOP_ENV`).
- **Native Aurora Desktop Environment (`/bin/aurora`)**:
  - Standalone, native desktop environment for akaOS.
  - Deep midnight-slate vertical gradient wallpaper with center blue diamond emblem.
  - Charcoal taskbar panel with "akaOS" blue launcher button, task switcher badges, and system clock.
  - Removed mock/fake themes (KDE Plasma, GNOME Shell, Classic XFCE) and `--env` flag.
  - Includes `--once` headless self-test mode for automated headless test verification.
- **Desktop Launcher (`/bin/desktop`)**:
  - Dynamically resolves configured `desktop_env` to binary executable path (e.g. `aurora` -> `/bin/aurora`).
  - Verifies binary is installed before launching. Spawns desktop shell directly without mock flags.
- **Desktop Autostart on Login (`/bin/login`)**:
  - `/bin/login` inspects `AUTOSTART_DESKTOP` in `/etc/akaos.conf`.
  - When set to `1`, uses the kernel spawn path to launch `/bin/desktop --apps` in the background prior to starting the interactive shell.
  - Defaults to `0` (disabled) so automated console tests and headless workflows run unimpeded.
- **Multi-Architecture Verification**:
  - Verified and passed on all 6 architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).

## Universal Tab Key & Non-x86 Up/Down Arrow History Navigation (2026-09-04)

Status: Complete and verified across all 6 CPU architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).

- **Up & Down Arrow History Navigation on Non-x86 Architectures**:
  - **VirtIO Keyboard Scancodes**: Added support for Linux EV_KEY scancodes `KEY_UP` (103, `0x67`) and `KEY_DOWN` (108, `0x6C`) in `xscore_tty_input_scancode()` in `xscore/kernel/tty.c`.
  - **Serial Terminal ANSI Escape Parser**: Implemented a lightweight ANSI escape sequence state machine (`\033[A` / `\033OA` for Up Arrow, `\033[B` / `\033OB` for Down Arrow) in `xscore_tty_input_char()`, enabling command history navigation across serial UART terminals (`PL011` on ARM, `NS16550A` on RISC-V, `16550A` COM1 on x86) without echoing raw escape codes.
  - **Clean Line Redrawing**: Refactored history navigation into `tty_handle_up_arrow()` and `tty_handle_down_arrow()` with helper `tty_refresh_line()`. Properly clears visual columns with backspaces and spaces on both framebuffer and serial consoles without leaving ghost characters.
  - Preserved existing PS/2 extended scancodes `0xE0 0x48` and `0xE0 0x50` on `x86_64` and `x86_32`.
- **Universal Tab Key Across All 6 Architectures**:
  - **PS/2 and VirtIO Keyboard Mapping**: Mapped scancode `0x0F` (PS/2 Set 1 Tab and Linux `KEY_TAB = 15`) to `'\t'` in `tty_keymap` and `tty_shift_keymap`, and added an explicit check in `xscore_tty_input_scancode()`.
  - **Canonical Line Discipline**: Properly buffers `'\t'` in canonical mode and echoes it to standard output.
  - **Visual Tab-Stop Aware Backspacing**: Added `tty_visual_len()` to compute column position accounting for 4-space tab stops, erasing the exact number of spaces when backspacing over a tab character.
  - **Graphical Terminal Support**: Updated `term_write_char()` in `userspace/bin/terminal/main.c` to advance cursor to the next 4-space tab stop (`4 - (cursor_x % 4)` spaces).
  - **Shell Argument Splitting**: `akash` parses tabs as argument separators seamlessly in `parse_stage()`.
- **Automated Verification**:
  - Multi-architecture automated test harnesses (`tools/test-aarch64.sh`, `tools/test-arm32.sh`, `tools/test-riscv64.sh`, `tools/test-riscv32.sh`, `tools/test-x86_32.sh`, `test.sh`) verify Tab key input and Up/Down arrow history recall and execution.
  - 100% full feature parity verified across all 6 architectures.

## Linux-Style Shell Tab Autocompletion Across All 6 Architectures (2026-09-04)

Status: Complete and verified across all 6 CPU architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).

- **Linux-Style Bash Autocompletion in Kernel TTY**:
  - Replaced literal Tab whitespace insertion at shell prompt with full Linux `bash`-style Tab autocompletion at the TTY line discipline level (`xscore/kernel/tty.c`), providing seamless autocompletion across serial UART, PS/2 keyboards, and VirtIO input devices on all 6 architectures.
  - **Command Autocompletion**: When Tab is pressed on the initial command token, checks internal shell builtins (`cd`, `pwd`, `echo`, `exit`, `clear`, `export`, `unset`, `history`, `help`) and binary executables in `/bin` (`xscore/kernel/vfs.c`).
  - **Path and File Autocompletion**: When Tab is pressed on an argument containing `/` or a relative path, autocompletes directory and file entries relative to CWD or root.
  - **Directory vs File Distinctions**: Automatically appends a trailing slash (`/`) when completing directory paths, and a trailing space (` `) when completing commands or regular files.
  - **Longest Common Prefix Completion**: In the presence of multiple candidates, autocompletes up to their longest common prefix.
  - **Double-Tab Candidate Listing & Line Restoration**: If candidates share a common prefix or multiple options exist, double-pressing Tab (`<TAB><TAB>`) displays all matching candidates on a new line and seamlessly restores the active prompt and current line buffer without modifying input, exactly matching Linux bash behavior.
  - **Zero Unwanted Whitespace**: If no match exists or on initial single-tab ambiguous matches, rings the bell (`\a`) without adding extra spaces or tabs.
  - **Active Prompt Tracking**: Added `xscore_tty_record_output()` in `xscore/kernel/tty.c` hooked into `xscore_fd_write()` on stdout, automatically tracking the shell prompt string (`root@akaOS:...# `) to ensure clean redrawing.
- **Automated Multi-Architecture Verification**:
  - Validated command autocompletion (`whoa<TAB>` -> `whoami` -> `root`) and file path autocompletion (`cat /etc/mo<TAB>` -> `cat /etc/motd` -> `akaOS XSCore initramfs`) in automated test suites:
    - `x86_64`: `./build.sh` & `./test.sh`
    - `aarch64`: `./tools/test-aarch64.sh`
    - `arm32`: `./tools/test-arm32.sh`
    - `riscv64`: `./tools/test-riscv64.sh`
    - `riscv32`: `./tools/test-riscv32.sh`
    - `x86_32`: `./tools/test-x86_32.sh`
  - 100% full feature parity verified across all 6 architectures.

## Sudo Privilege Delegation & /etc/sudoers (2026-09-05)

Status: Complete and verified across all 6 CPU architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).

- **`sudo` Privilege Delegation Utility (`/bin/sudo`)**:
  - Implemented setuid-root binary (`mode 4755`) in `userspace/bin/sudo/main.c`, enabling authorized users to execute administrative commands with elevated privileges (root or target user).
  - **User Authentication**: Prompts for caller's own password (`[sudo] password for <user>: `) via `akaos_read_secret()` and verifies against `/etc/shadow` using `akaos_account_verify()`. Direct execution without password prompt when invoked by root (`UID 0`) or when `NOPASSWD:` tag is configured. Securely wipes password memory immediately upon verification.
  - **`/etc/sudoers` Rule Parser**: Implemented parser reading `/etc/sudoers` (mode `0440`), supporting username rules (`root`, `user`), group rules (`%wheel`), wildcard rules (`ALL`), host rules (`ALL`), target user specifications (`=(ALL)`, `=(ALL:ALL)`, `=(root)`), `NOPASSWD:` tag, and command specifications (`ALL` or specific commands).
  - **Group Membership Discovery**: Parses `/etc/group` to verify user group membership for group-based sudoers rules (`%wheel`). Added `wheel:x:10:root,user` to `/etc/group`.
  - **Command Path Resolution & Process Replacement**: Automatically resolves non-path commands to `/bin/<cmd>`, switches process credentials with `setgid()` and `setuid()`, and replaces process image via `execv()`, seamlessly preserving exit status `$?`, file descriptors, and signals.
  - **Options Supported**: `-h`/`--help` (usage), `-u <user>` (run as specified user), `-i`/`-s` (interactive login shell in target home), and `--` (end of options).
  - **Process BSS Buffers**: Avoids user stack overflow by placing large file-sized buffers (`group_buf`, `sudoers_buf`) into BSS.
- **Multi-Architecture Integration & Verification**:
  - Integrated into `build.sh`, `tools/make_userspace_builders.py`, and all 5 architecture userspace builders (`tools/build-user-32.sh`, `tools/build-user-aarch64.sh`, `tools/build-user-arm32.sh`, `tools/build-user-riscv64.sh`, `tools/build-user-riscv32.sh`).
  - Added automated negative and positive test assertions in `tools/test-auth.sh`: verified unprivileged `user` running `sudo -h`, `sudo whoami` (password verification -> root), and `sudo cat /etc/shadow` (protected mode 0600 file read).
  - Validated across all 6 architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).

## Core File Utilities (`cp`, `mv`) & Kernel VFS `rename` Syscall (2026-09-05)

Status: Complete and verified across all 6 CPU architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).

- **VFS Atomic Rename Syscall (`xscore_vfs_rename_at` & `AKAOS_SYSCALL_RENAME = 59`)**:
  - Implemented kernel-level `xscore_vfs_rename_at(old_cwd, old_path, new_cwd, new_path)` in `xscore/kernel/vfs.c`, enabling atomic in-place file and directory moves/renames across directory hierarchies.
  - **Directory Loop Protection**: When moving a directory, traverses ancestor pointers to guarantee the destination parent is not the source directory itself or any of its descendants, returning `-AKAOS_EINVAL`.
  - **Collision Handling**: If target entry already exists:
    - Same file/directory: succeeds immediately with 0 (no-op).
    - Directory over non-directory: returns `-AKAOS_ENOTDIR`.
    - Non-directory over directory: returns `-AKAOS_EISDIR`.
    - Directory over non-empty directory: returns `-AKAOS_ENOTEMPTY`.
    - Valid collision replaces target: frees existing target vnode blocks and updates source vnode in place.
  - **Dynamic Conversion & Persistence**: Automatically converts static initramfs files to dynamic vnode blocks (`convert_to_dynamic`) upon moving, and invokes dual-superblock journal synchronization (`xscore_vfs_persistence_sync`).
  - **Syscall Dispatch**: Dispatched in `xscore/kernel/syscall.c` using `xscore_copy_string_from_user` for safe userspace pointer translation.
- **Libc Standard `rename()` Function**:
  - Declared `int rename(const char *oldpath, const char *newpath);` in `userspace/libc/include/stdio.h` and `userspace/libc/include/unistd.h`.
  - Implemented in `userspace/libc/src/syscall.c` via `syscall3(AKAOS_SYSCALL_RENAME, ...)` with standard `errno` reporting.
- **File Copy Utility (`/bin/cp`)**:
  - Implemented in `userspace/bin/cp/main.c` supporting multi-operand copying (`cp [OPTIONS] SOURCE... DEST`).
  - Flags: `-r`/`-R` (recursive copy of directory trees), `-f` (force removal of existing destinations), `-v` (verbose output `'SRC' -> 'DEST'`), `-p` (preserve file permissions via `chmod` and ownership via `chown`), and `-h`/`--help` (usage).
  - Uses 4096-byte chunk copying with static BSS buffer and dynamic heap path buffers, safely adhering to the 4 KiB user stack constraint.
  - Detects and prevents copying a file to itself (`src_st.inode == dst_st.inode`) and copying a directory into its own subtree.
- **File Move/Rename Utility (`/bin/mv`)**:
  - Implemented in `userspace/bin/mv/main.c` supporting multi-operand moving (`mv [OPTIONS] SOURCE... DEST`).
  - Flags: `-f` (force overwrite), `-v` (verbose output `'SRC' -> 'DEST'`), and `-h`/`--help` (usage).
  - Uses `rename()` syscall for O(1) atomic moves; if target exists and `-f` is set, unlinks existing non-directory target and retries rename.
- **Build System & Multi-Architecture Parity**:
  - Integrated into `build.sh` and `tools/make_userspace_builders.py`.
  - Re-generated all 5 architecture userspace builders (`build-user-32.sh`, `build-user-aarch64.sh`, `build-user-arm32.sh`, `build-user-riscv64.sh`, `build-user-riscv32.sh`).
  - Added automated kernel self-tests in `xscore/kernel/user.c` and assertions in `test.sh`.
  - Verified 100% build and runtime test pass across `x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, and `riscv32`.

## Core Utilities, Stream Filters, Shell Scripting Engine & SSH Suite (2026-09-05)

Status: Complete and verified across all 6 CPU architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).

- **Interactive Text Pagers (`/bin/less`, `/bin/more`)**:
  - `/bin/less`: Full terminal raw mode pager with terminal window size discovery (`TIOCGWINSZ`), smooth vertical scrolling (j/k/Down/Up, Space/PageDown, b/PageUp, g/G for top/bottom), forward string search (`/pattern`, `n` next match), line truncation, status bar line, and graceful exit (`q`).
  - `/bin/more`: Traditional Unix `--More-- (percentage)` pager supporting space/enter pagination, file and standard input reading, line wrapping, and `q` quit.
- **Standard Stream Filters (`/bin/find`, `/bin/sort`, `/bin/uniq`, `/bin/xargs`, `/bin/test`, `/bin/[`)**:
  - `/bin/find`: Recursive directory traversal via `getdents` with criteria filters: `-name <pattern>` (wildcard matching `*` and `?`), `-type <d|f>`, `-maxdepth <n>`, and action `-exec <cmd> {} + \;`.
  - `/bin/sort`: Autonomous in-memory quicksort (`quick_sort`) with support for numeric sorting (`-n`, `simple_atoll`), reverse sorting (`-r`), unique filtering (`-u`), key column sorting (`-k <col>`), and output redirection (`-o <file>`).
  - `/bin/uniq`: Line deduplication utility with occurrence counting (`-c`), duplicate-only filtering (`-d`), unique-only filtering (`-u`), and case-insensitive comparison (`-i`).
  - `/bin/xargs`: Argument-batching command executor reading tokens from stdin, supporting `-n <max-args>`, string placeholder replacement (`-I <replace-str>`), null-delimited input (`-0`), and verbose execution logging (`-t`).
  - `/bin/test` & `/bin/[`: Standalone expression evaluation binaries with POSIX compatibility:
    - Unary file checks: `-e` (exists), `-f` (regular file), `-d` (directory), `-s` (size > 0).
    - Unary string checks: `-z` (empty), `-n` (non-empty).
    - Binary string comparisons: `=`, `!=`.
    - Binary integer comparisons: `-eq`, `-ne`, `-lt`, `-gt`, `-le`, `-ge` using 64-bit integer parsing.
    - Negation: `!` prefix.
    - Closing bracket verification: enforces trailing `]` when invoked as `[`.
- **Shell Scripting Engine in `akash`**:
  - **Script Execution**: Added support for executing `.sh` shell scripts directly (`akash script.sh [args...]`) or via `akash -c "commands"`.
  - **Positional Parameters**: Full support for `$0` through `$9`, `$#` (argument count), `$*` and `$@` (all arguments combined).
  - **Control Flow Structures**:
    - Conditionals: `if <condition>; then <statements>; elif <condition>; then <statements>; else <statements>; fi`.
    - Iteration: `for <var> in <list>; do <statements>; done`.
    - While loops: `while <condition>; do <statements>; done`.
    - Connectors: Command sequencing `;`, short-circuit logical AND `&&`, and logical OR `||`.
  - **Shell Builtins**: Integrated builtins for `test`, `[`, `echo`, `true`, `false`, `export`, and variable assignment (`VAR=value`).
  - **Comment Handling**: Strips comments starting with `#`.
  - **Stack Safety**: Static BSS command staging structures to ensure strict execution safety under the 4 KiB user stack budget.
- **SSH Suite & `akaos-config` Integration**:
  - **SSH Server (`/bin/sshd`)**:
    - Listens on TCP port 22 (configurable via `/etc/akaos.conf`). Disabled by default (`SSHD_ENABLED=0`) as akaOS defaults to a personal OS profile.
    - Master/slave pseudo-terminal allocation via `/dev/ptmx` and `TIOCSCTTY`.
    - Secure user authentication verifying credentials against `/etc/shadow` via `akaos_account_verify()`.
    - Spawns target user's login shell and forwards bidirectional PTY I/O via `poll()`.
    - Supports `-p <port>`, `-h`/`--help`, and `--status` inspection.
  - **SSH Client (`/bin/ssh`)**:
    - OpenSSH-compatible command syntax: `ssh [user@]host [-p port] [cmd]`.
    - Secure password prompt without echo (`Password: `) using raw terminal mode.
    - Full bidirectional raw terminal pumping for interactive sessions or remote single-command execution.
  - **Configuration Integration (`/etc/akaos.conf` & `/bin/akaos-config`)**:
    - Added `SSHD_ENABLED=0` and `SSHD_PORT=22` to `/etc/akaos.conf`.
    - `akaos-config`: Added CLI flags `--enable-sshd`, `--disable-sshd`, `--set-sshd <0|1>`, `--sshd-port <port>`, and updated `--status` report.
    - Integrated interactive toggle into `akaos-config` raspi-config TUI under "Interface Options -> SSH Server".
- **Kernel Stack Fault Growth Enhancement (`xscore/kernel/process.c`)**:
  - Enhanced page fault stack growth handler to support contiguous multi-page stack growth up to `USER_MAX_STACK_PAGES`, ensuring deep function calls and larger stack allocations execute without spurious crashes while maintaining safety against unmapped memory skips.
- **Build System & 100% Architecture Parity**:
  - Registered all new utilities in `build.sh` and `tools/make_userspace_builders.py`.
  - Regenerated all 5 architecture userspace builders (`tools/build-user-32.sh`, `tools/build-user-aarch64.sh`, `tools/build-user-arm32.sh`, `tools/build-user-riscv64.sh`, `tools/build-user-riscv32.sh`).
  - 84 initramfs entries built identically across all 6 architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).
  - Automated tests passing on x86_64 UEFI (`./test.sh`), BIOS (`./tools/test-bios.sh`), and AArch64 (`./tools/test-aarch64.sh`).

## Essential Scripting & System Admin Utilities Suite & Kernel Thread Stack Allocator Fix (2026-09-05)

Status: Complete and verified across all 6 CPU architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).

- **New Userspace Utilities (`/bin/sleep`, `/bin/which`, `/bin/env`, `/bin/cut`, `/bin/tr`, `/bin/df`, `/bin/du`)**:
  - `/bin/sleep`: Sub-second and integer second sleeping (`sleep 1`, `sleep 0.5s`, `sleep 100ms`, supports suffixes `s`, `ms`, `m`, `h`, `d`). Multiple argument cumulative delay.
  - `/bin/which`: Executable resolution across `$PATH` (`/bin:/usr/bin`). Supports `-a` (show all matching occurrences) and direct file existence/execution checking.
  - `/bin/env`: Displays environment variables and launches child processes with modified environments. Supports `-i`/`--ignore-environment`, `-u`/`--unset <NAME>`, and inline `VAR=VAL` assignments.
  - `/bin/cut`: Field and character slicing with delimiter specification. Supports `-d <delim>`, `-f <fields>` (ranges `N`, `N-`, `-M`, `N-M`, comma-separated lists), `-c <chars>`, and `-s` (suppress lines not containing delimiter).
  - `/bin/tr`: Character translation, deletion, and squeezing stream filter. Supports set expansion (ranges `a-z`, escape characters `\n`, `\t`, `\r`, `\\`, etc.), `-d` (delete), `-s` (squeeze repeats), and `-c`/`-C` (complement).
  - `/bin/df`: File system disk space and mount point usage reporting. Inspects `/proc/mounts`, formats blocks in 1K/1M/human-readable (`-h`), and calculates percentage utilization.
  - `/bin/du`: Directory and file space utilization estimation. Supports recursive directory tree traversal, `-s`/`--summarize`, `-h`/`--human-readable`, `-a`/`--all`, `-c`/`--total`, and `-d`/`--max-depth=N`. Designed with BSS buffers adhering strictly to the 4 KiB user stack budget.
- **C Standard Library Additions (`userspace/libc`)**:
  - `include/sys/types.h`: Added `typedef uint32_t useconds_t;`.
  - `include/unistd.h`: Declared `unsigned int sleep(unsigned int seconds);` and `int usleep(useconds_t usec);`.
  - `include/stdlib.h`: Declared `char *getenv(const char *name);`.
  - `src/syscall.c`: Implemented `getenv()` parsing `environ`, `usleep()` via `poll(NULL, 0, ms)`, and `sleep()`.
- **Kernel Architecture Fix — Slot-Based Thread Stack Virtual Address Allocation (`xscore/kernel/thread.c`)**:
  - **Root Cause**: Previously, `allocate_thread_stack` monotonically incremented `next_stack_address += (stack_pages + 1) * PAGE_SIZE` without recycling virtual addresses when threads were reaped. After 64 thread creations ($64 \times 32\text{ KiB} = 2048\text{ KiB} = 2\text{ MiB}$), `next_stack_address` crossed the 2 MiB boundary, allocating an additional page table (PT) in the permanent kernel root page table and causing an apparent 1-page leak in free physical page invariant checks.
  - **Resolution**: Replaced monotonic `next_stack_address` with deterministic slot-based virtual address allocation derived directly from `(thread - threads)`. All 16 thread slots fit within 512 KiB of virtual space inside the first 2 MiB page table, ensuring zero virtual address drift and zero kernel page table page leaks regardless of how many threads are created and destroyed.
- **Build System & Multi-Architecture Verification**:
  - Registered all 7 new utilities into `build.sh` and `tools/make_userspace_builders.py`.
  - Regenerated all 5 architecture builders (`build-user-32.sh`, `build-user-aarch64.sh`, `build-user-arm32.sh`, `build-user-riscv64.sh`, `build-user-riscv32.sh`).
  - Total initramfs entries increased from 84 to 91 across all 6 architectures.
  - Automated tests passing 100%: UEFI (`./test.sh`), BIOS (`./tools/test-bios.sh`), and AArch64 (`./tools/test-aarch64.sh`).

## System & Process Control Suite (`kill`, `pkill`, `uptime`, `free`, `watch`), ProcFS Virtual Files & Multi-Arch Timer Parity (2026-09-05)

Status: Complete and verified across all 6 CPU architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).

- **New Userspace Utilities (`/bin/kill`, `/bin/pkill`, `/bin/uptime`, `/bin/free`, `/bin/watch`)**:
  - `/bin/kill`: Sends signals to processes by PID. Supports numeric signals (`-9`, `-15`, `-2`), symbolic names (`-KILL`, `-TERM`, `-INT`, `-HUP`, `-s <SIG>`), and `kill -l` to print signal table in standard multi-column format. Implemented using position-independent fixed-size char array mappings to ensure relocation invariance.
  - `/bin/pkill`: Signals processes matched by name pattern against `/proc/processes`. Automatically excludes own PID to prevent suicide, supports `-e` (echo killed process info), numeric and symbolic signals, and exits 0 on match, 1 on no match.
  - `/bin/uptime`: Reports uptime, active process count, and synthetic load averages. Reads dynamic `/proc/uptime` (system uptime in seconds) and `/proc/processes` (enumerating active processes). Supports `-p`/`--pretty` formatting (`up X days, Y hours, Z minutes, W seconds`) and `-s`/`--since`.
  - `/bin/free`: Displays total, used, free, shared, buffer/cache, and available physical memory. Reads dynamic `/proc/meminfo` and renders tabular output in bytes (`-b`), kibibytes (`-k`, default), mebibytes (`-m`), gibibytes (`-g`), or human-readable (`-h`).
  - `/bin/watch`: Periodically runs commands fullscreen. Clears screen using ANSI escapes (`\033[2J\033[H`), displays a status header with interval, command string, and timestamp, executes commands via `/bin/akash -c "<cmd>"`, sleeps using libc `usleep()`, and cleanly terminates on `SIGINT` (Ctrl+C).
- **Kernel ProcFS Virtual Telemetry Files (`/proc/uptime`, `/proc/meminfo`) & Process Names**:
  - Dynamic virtual files `/proc/uptime` and `/proc/meminfo` registered in ProcFS with custom read handlers in `xscore/kernel/vfs.c`.
  - `/proc/uptime`: Renders exact elapsed uptime calculated from architecture timer ticks and frequency: `<seconds>.<hundredths> <idle_seconds>.<hundredths>\n`.
  - `/proc/meminfo`: Queries physical memory manager for `MemTotal`, `MemFree`, and `MemAvailable` in kB.
  - Process Name Tracking: Added `char name[32]` to `struct xscore_process`. Propagates names on `execve`, `create_elf_args`, `create_path_args_at`, and `fork`.
  - Updated `/proc/processes` with standard `NAME` column (`PID PPID PGID SID STATE NAME\n`) and `/proc/<pid>/status` with `Name:\t<name>\n`.
- **Freestanding 64-Bit Arithmetic & Architecture Timer Hooks**:
  - Implemented portable bitwise `u64_div_mod` in `xscore/kernel/vfs.c` to guarantee freestanding division and modulo on 32-bit platforms without runtime compiler helper library dependencies (`__aeabi_uldivmod`, `__udivdi3`).
  - Implemented hardware timer ticks and frequency readers across all architectures:
    - `x86_64` / `x86_32`: Programmable Interval Timer (PIT) tick counter.
    - `aarch64`: ARM Generic Timer registers `cntvct_el0` and `cntfrq_el0`.
    - `arm32`: ARM Generic Timer `mrrc p15, 1` (`CNTVCT`) and `mrc p15, 0` (`CNTFRQ`).
    - `riscv64`: RISC-V 64-bit `rdtime` instruction and 10 MHz reference clock.
    - `riscv32`: Atomic loop reading `rdtimeh` and `rdtime` registers with 10 MHz reference clock.
- **Build System, Generator & 100% Architecture Parity**:
  - Added `-mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none` to ARM32 userspace builder in `tools/make_userspace_builders.py`.
  - Registered all 5 utilities in `build.sh` and regenerated all userspace builder scripts.
  - Initramfs entries increased from 91 to 96 entries across all 6 architectures.
  - Automated tests passing 100%:
    - x86_64 UEFI (`./build.sh && ./test.sh`)
    - x86_64 BIOS (`./tools/test-bios.sh`)
    - AArch64 (`./tools/test-aarch64.sh`)
    - ARM32 (`./tools/test-arm32.sh`)
    - RISC-V 64 (`./tools/test-riscv64.sh`)
    - RISC-V 32 (`./tools/test-riscv32.sh`)
    - x86_32 (`./tools/test-x86_32.sh`)

## Unix Text Transformation & Diff Suite, Archiving & Compression Suite, and Advanced Shell Job Control (2026-09-05)

Status: Complete and verified across all 6 CPU architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).

- **Option 1: Unix Text Transformation & Diff Suite (`/bin/sed`, `/bin/diff`, `/bin/cmp`, `/bin/patch`)**:
  - `/bin/sed`: Stream editor supporting address ranges (numeric line numbers and string patterns), substitute operations `s/pattern/replacement/[g][p][i]`, deletion `d`, print `p`, quit `q`, quiet mode `-n`, script expression `-e <expr>`, in-place editing `-i`, and standard file / pipe stream processing.
  - `/bin/diff`: Line comparison utility implementing dynamic programming Longest Common Subsequence (LCS). Generates POSIX unified diffs (`-u`, `-U <N>`), brief difference reporting (`-q`), case-insensitive comparison (`-i`), whitespace ignoring (`-w`, `-b`), and line-level hunk generation. Refactored with dynamic heap line buffers to maintain an ultra-compact ELF size (~45 KiB).
  - `/bin/cmp`: Byte-by-byte file comparison supporting differing byte output (`-l`), quiet mode (`-s`/`--silent`), skip offsets (`skip1`, `skip2`), and standard exit codes (0 = identical, 1 = differ, 2 = error).
  - `/bin/patch`: Unified diff patch utility supporting strip count (`-p <num>`), reverse patching (`-R`/`--reverse`), patch input file (`-i <patch>`), custom output destination (`-o <out>`), silent operation (`-s`), and atomic multi-hunk application using dynamically allocated heap buffers.
- **Option 2: Advanced Shell Job Control, Background Tasks & Pipelines (`/bin/akash`)**:
  - Background Job Execution: Single `&` command separator support (`cmd &`, `cmd1 & cmd2`). Spawns asynchronous child processes with distinct process groups and reports job ID and PID (`[<id>] <pid>`).
  - Asynchronous Job Reaper: Non-blocking process reaping via kernel `waitpid(..., WNOHANG)` integrated into `check_completed_jobs()` prior to prompt rendering. Displays `[<id>]+  Done  <cmd>`.
  - Builtin Job Control Commands: `jobs` (supporting `-l` for PID display and `-p` for PID-only), `fg` (foreground transfer with terminal process group handoff and `SIGCONT`), `bg` (background continuation), and `wait` (blocking synchronization for specific PIDs or all background jobs).
  - Multi-Stage Pipeline Cleanup: Updated `execute_pipeline()` to close all inherited intermediate pipe descriptors inside child processes after `dup2()`, ensuring immediate EOF propagation downstream without descriptor leaks.
- **Option 3: Archiving & Compression Suite (`/bin/tar`, `/bin/gzip`, `/bin/gunzip`)**:
  - `/bin/tar`: POSIX `ustar` tar archiver supporting archive creation (`-c`), extraction (`-x`), listing (`-t`), verbose progress (`-v`), file specification (`-f`), directory change (`-C`), and recursive directory tree traversal. Interacts natively with `struct akaos_dirent`.
  - `/bin/gzip`: RFC 1952 / RFC 1951 Deflate compressor using an LZ77 32 KiB sliding window and fixed Huffman coding (Type 01). Emits standard 10-byte gzip headers, Deflate bitstreams, and 8-byte trailers containing CRC-32 (0xEDB88320) and original uncompressed byte count (`ISIZE`). Supports in-place compression, stdout streaming (`-c`), keep original files (`-k`), and force overwrite (`-f`).
  - `/bin/gunzip`: RFC 1952 / RFC 1951 Deflate decompressor supporting stored uncompressed blocks (Type 00), fixed Huffman blocks (Type 01), and dynamic Huffman code trees (Type 02) with canonical code length decoding. Performs full CRC-32 and length validation on decompression.
- **Kernel Memory Architecture & Footprint Optimization**:
  - Kernel Non-Blocking `waitpid`: Added `xscore_process_waitpid_options(pid, out_status, options)` supporting `WNOHANG` (option 1) returning 0 non-blockingly when child processes are still running.
  - Physical Memory Bitmap Optimization: Scaled `MAX_PHYSICAL_MEMORY` in `xscore/memory/physical.c` to 4 GiB, reducing the static `allocation_bitmap` from 2.0 MiB down to 128 KiB and reclaiming 1.87 MiB of kernel BSS.
  - UEFI Buffer & Conventional Memory Compliance: Expanded `KERNEL_BUFFER_CAPACITY` in `boot/uefi/loader.c` to 64 MiB. By trimming userspace BSS bloat in `diff` and `patch`, the combined kernel image and initramfs memory footprint (`0x14a000` to `0x68c010`, 6.54 MiB) now comfortably fits within OVMF's first 8.00 MiB conventional memory allocation block, preventing collisions with ACPI reclaim memory.
  - Process Image Page Tracking: Expanded `USER_IMAGE_MAX_PAGES` to 128 pages (512 KiB) in `xscore/kernel/process.c` to support larger userspace binaries.
- **Build System & Multi-Architecture Verification**:
  - Added `sed`, `diff`, `cmp`, `patch`, `tar`, `gzip`, `gunzip` to `build.sh` and `tools/make_userspace_builders.py`.
  - Regenerated all 5 non-x86_64 architecture scripts (`build-user-32.sh`, `build-user-aarch64.sh`, `build-user-arm32.sh`, `build-user-riscv64.sh`, `build-user-riscv32.sh`).
  - Total initramfs entries increased from 96 to **103 entries** across all 6 architectures.
  - 100% test pass rate achieved across all test suites:
    - x86_64 UEFI (`./build.sh && ./test.sh`)
    - x86_64 BIOS (`./tools/test-bios.sh`)
    - x86_32 (`./tools/test-x86_32.sh`)
    - AArch64 (`./tools/test-aarch64.sh`)
    - ARM32 (`./tools/test-arm32.sh`)
    - RISC-V 64 (`./tools/test-riscv64.sh`)
    - RISC-V 32 (`./tools/test-riscv32.sh`)

## Network Diagnostics & Management Suite, Kernel `/proc/net/` Telemetry & RFC 792 ICMP Generation (2026-09-06)

Status: Complete and verified across all 6 CPU architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).

- **Network Diagnostics & Management Suite (`/bin/netstat`, `/bin/route`, `/bin/traceroute`, `/bin/dig`, `/bin/arp`)**:
  - `/bin/netstat`: Fully upgraded from 42-line stub into standard network analysis utility. Inspects active TCP/UDP sockets via `/proc/net/tcp` and `/proc/net/udp`, displays routing tables via `-r` (`/proc/net/route`), reports interface byte/packet metrics via `-i` and `-s`, and supports numeric address formatting `-n` and listening filters `-l`/`-a`.
  - `/bin/route`: Displays and manipulates the kernel IPv4 routing table by parsing `/proc/net/route` (Destination, Gateway, Genmask, Flags `U`/`UG`, Metric, Ref, Use, Iface). Supports numeric mode `-n`, standard mode, and route table modification syntax (`add`, `del`).
  - `/bin/traceroute`: Probes packet routing paths with incremental TTLs from 1 up to `max_ttl` via `/dev/net`. Sends 3 probes per hop, records round-trip latency (RTT in ms), and decodes incoming ICMP Time Exceeded (Type 11, Code 0) responses from intermediate routers and ICMP Echo Reply (Type 0) responses from the destination target.
  - `/bin/dig`: RFC 1035 Domain Information Groper DNS client sending direct UDP queries on port 53. Fully supports recursive queries (`RD=1`), Answer/Authority/Additional section decoding, DNS compressed name pointer traversal (0xC0), record types (`A`, `AAAA`, `CNAME`, `MX`, `NS`, `PTR`, `SOA`, `TXT`, `ANY`), custom DNS server specification (`@server`), and `+short` terse output mode.
  - `/bin/arp`: Linux-compatible ARP cache utility parsing `/proc/net/arp`. Outputs IP Address, HW type (`ether`), HW address (MAC), Flags (`C`, `M`), Mask, and Interface (`eth0`).
- **Kernel `/proc/net/` Telemetry Subsystem**:
  - Implemented `/proc/net/tcp`, `/proc/net/udp`, `/proc/net/route`, `/proc/net/arp`, and `/proc/net/dev` virtual files in `xscore/kernel/vfs.c`, `xscore/kernel/socket.c`, and `xscore/kernel/net.c`.
  - Registered `/proc/net` directory in procfs, wired dynamic inode generation via `xscore_fd_open_generated()`, and implemented directory entry streaming in `xscore_vfs_getdents_node()`.
- **RFC 792 ICMP Time Exceeded (Type 11) Generation**:
  - Implemented automatic ICMP Time Exceeded generation in `xscore/kernel/net.c` when non-destination packets arrive with TTL = 1.
  - Appends original IPv4 header and 64 bits of datagram payload per RFC 792 to ensure traceroute compliance across virtual networks.
- **Build System & Multi-Architecture Verification**:
  - Added `route`, `traceroute`, `dig`, `arp` to `build.sh` and `tools/make_userspace_builders.py`.
  - Regenerated all builder scripts for `x86_32`, `aarch64`, `arm32`, `riscv64`, and `riscv32`.
  - Total initramfs entries increased from 103 to **107 entries** (3,788,845 bytes).
  - 100% test pass rate achieved across all test suites:
    - x86_64 UEFI (`./build.sh && ./test.sh`)
    - x86_64 BIOS (`./tools/test-bios.sh`)
    - x86_32 (`./tools/test-x86_32.sh`)
    - AArch64 (`./tools/test-aarch64.sh`)
    - ARM32 (`./tools/test-arm32.sh`)
    - RISC-V 64 (`./tools/test-riscv64.sh`)
    - RISC-V 32 (`./tools/test-riscv32.sh`)
    - VirtIO-Net hardware stack (`./tools/test-network.sh`)
## Hardware Drivers & Diagnostics Suite, Multi-Arch Telemetry, /dev/gpu0 & ProcFS Integration (2026-09-06)

Status: Complete and verified across all 6 CPU architectures (`x86_64`, `x86_32`, `aarch64`, `arm32`, `riscv64`, `riscv32`).

- **PCI / PCIe Bus Driver & Hardware Database (`xscore/include/xscore/pci.h`, `xscore/kernel/pci.c`)**:
  - Enhanced PCI bus enumerator: Extended scanning with complete BAR sizing (BAR0–BAR5), distinguishing MMIO vs I/O space and prefetchable flags.
  - Added PCIe capability linked-list parsing: Identifies PCI Express (Cap ID 0x10) link capabilities (Gen 1 at 2.5 GT/s up to Gen 5 at 32.0 GT/s, link width x1–x16), MSI (Cap ID 0x05) and MSI-X (Cap ID 0x11) interrupt vectors, and Power Management (Cap ID 0x01).
  - Extensive hardware database: Vendor and device ID lookup covering NVIDIA, AMD, Intel, Bochs, VirtIO, VMware, Realtek, Qualcomm, and Broadcom.
  - ProcFS `/proc/pci`: Emits comprehensive hardware listing with device names, vendor/device IDs, revisions, IRQ, PCIe generation/width, capabilities, and decoded BARs.

- **CPU Driver & Architectural Feature Discovery (`xscore/include/xscore/cpu.h`, `xscore/kernel/cpu.c`)**:
  - Multi-architecture CPU feature decoder:
    - x86_64 / x86_32: Discovers vendor string (GenuineIntel, AuthenticAMD), full 48-byte CPU brand string via CPUID leaves 0x80000002–0x80000004, family/model/stepping, address bits (physical/virtual), cache line size & topology, CPU MHz & calibrated BogoMIPS, and complete feature flags.
    - AArch64 / ARM32: Discovers Implementer, Architecture, Variant, PartNum, and Revision via `MIDR_EL1` / `MIDR`, cache topology via `CTR_EL0` / `CTR`, and timer frequency via `CNTFRQ_EL0` / `CNTFRQ`.
    - RISC-V 64 / RISC-V 32: Discovers ISA string (rv64imafdc / rv32imafdc) via `misa`, Vendor ID via `mvendorid`, Architecture ID via `marchid`, and Implementation ID via `mimpid`.
  - ProcFS `/proc/cpuinfo`: Formats standard Linux-compatible processor information blocks for all architectures.

- **GPU Driver Subsystem & Hardware Acceleration (`xscore/include/xscore/gpu.h`, `xscore/kernel/gpu.c`)**:
  - Modern & legacy GPU detection database:
    - NVIDIA: GeForce RTX 5090 / 5080 (Blackwell), RTX 4090 / 4080 (Ada Lovelace), RTX 3090 / 3080 (Ampere), GTX 1080 / 1070 (Pascal), Legacy Riva TNT / GeForce 256.
    - AMD Radeon: RX 9070 XT / 9070 (RDNA 4), RX 7900 XTX / 7900 XT (RDNA 3), RX 6900 XT (RDNA 2), Legacy Radeon 7000 / Rage 128.
    - Intel: Arc A770 / A750 / B580 (Alchemist / Battlemage), Iris Xe, UHD Graphics.
    - Virtualized / Emulated: Bochs BGA (0x1234:0x1111), VirtIO-GPU (0x1af4:0x1050), VMware SVGA II (0x15ad:0x0405).
    - Universal fallback: UEFI GOP / BIOS VBE / RAMFB Display driver with active resolution and framebuffer discovery.
  - Modesetting engine: Implemented hardware modesetting for Bochs BGA / QEMU Standard VGA via index/data port I/O registers (`0x01ce`/`0x01cf`).
  - ProcFS `/proc/gpuinfo` & Device Node `/dev/gpu0`: Emits graphics card reports including driver name, chipset, PCI BDF, VRAM capacity, current resolution/stride, pixel format, physical framebuffer base, and 2D/3D acceleration flags. Registered `/dev/gpu0` character device in devfs.

- **Audio Driver Subsystem & Sound Synthesis (`xscore/include/xscore/sound.h`, `xscore/kernel/audio_drivers.c`)**:
  - Multi-card sound driver architecture:
    - Intel High Definition Audio (Azalia): Probes PCI Class 0x0403 (e.g. 0x8086 ICH9/ICH10/PCH HDA), binds MMIO BARs, supports 192 kHz 32-bit 8-channel audio.
    - Ensoniq AudioPCI ES1371 / Creative Sound Blaster PCI (0x1274:0x1371): Binds I/O port BARs, supports 48 kHz 16-bit stereo.
    - Intel AC'97 Audio Controller: Probes PCI Class 0x0401 (e.g. 0x8086:0x2415), binds NAMBAR and NABMBAR.
    - VirtIO Sound (0x1af4:0x1059): Binds VirtIO audio device queues.
    - PC Speaker (Intel 8254 PIT Tone Synthesizer): Universal audio fallback providing frequency-based tone generation.
  - ProcFS `/proc/asound/cards`: Formats ALSA-compatible sound card directory and device table.

- **Userspace Diagnostics Utilities (`/bin/lscpu`, `/bin/lspci`, `/bin/gpuinfo`, `/bin/sndinfo`)**:
  - `/bin/lscpu` (`userspace/bin/lscpu/main.c`): Formats `/proc/cpuinfo` into standard `lscpu` tabular diagnostics.
  - `/bin/lspci` (`userspace/bin/lspci/main.c`): Scans and formats `/proc/pci`. Supports concise single-line format and verbose mode (`-v`).
  - `/bin/gpuinfo` (`userspace/bin/gpuinfo/main.c`): Reads `/proc/gpuinfo` and displays complete GPU hardware telemetry.
  - `/bin/sndinfo` (`userspace/bin/sndinfo/main.c`): Reads `/proc/asound/cards` and outputs audio hardware diagnostics.

- **Multi-Architecture Integration & Parity Verification**:
  - Freestanding kernel header hygiene: Replaced host `#include <string.h>` with freestanding `memset` declarations in `cpu.c`, `gpu.c`, and `audio_drivers.c`.
  - Kernel Driver Initialization: Wired `xscore_cpu_initialize()`, `xscore_gpu_initialize()`, and `xscore_sound_initialize()` into all architecture boot paths.
  - Dynamic VFS & ProcFS Expansion: Registered `/proc/cpuinfo`, `/proc/gpuinfo`, `/proc/pci`, `/proc/asound/`, and `/proc/asound/cards` in `vfs.c`, scaling `GENERATED_FILE_MAX_SIZE` to 4096 bytes.
- **ACPI Subsystem & Hardware Power Management (`xscore/include/xscore/acpi.h`, `xscore/kernel/acpi.c`)**:
  - Boot Interface Discovery: UEFI configuration table scanner matches ACPI 2.0 and 1.0 GUIDs, exporting physical address to `boot_info->acpi_rsdp`.
  - BIOS Physical Memory Scanner: Traverses EBDA (`0x80000`–`0x9FFFF`) and main BIOS ROM (`0xE0000`–`0xFFFFF`) on 16-byte boundaries with signature and checksum verification, safely bypassing guarded Page 0.
  - Table Parsing & Topology Extraction: Traverses XSDT (64-bit) / RSDT (32-bit), parsing FADT (PM1a/b blocks, PM timer, SCI IRQ, SMI port, DSDT address, reset register GAS), DSDT AML byte stream (`_S5_` sleep types `slp_typa`/`slp_typb`), MADT (Local APIC address, enabled CPU cores and APIC IDs, I/O APICs and GSI bases, ISOs), HPET MMIO base, and PCIe ECAM base (MCFG).
  - Native Power Management: Implements `xscore_acpi_shutdown()` via PM1a/PM1b sleep control and `xscore_acpi_reboot()` via ACPI reset register, directly integrated into `AKAOS_SYSCALL_REBOOT`.
  - ProcFS Telemetry: `/proc/acpi/info` (subsystem status, tables, MMIOs), `/proc/acpi/fadt` (power management registers), and `/proc/acpi/madt` (multiprocessor and interrupt topology).
  - Userspace Diagnostic Utility: `/bin/acpi` supporting default overview, `-c` / `--cpu`, `-p` / `--power`, and `-v` / `--verbose`.
  - Total initramfs entries increased from 111 to **112 entries** (3,994,357 bytes).
  - 100% automated test pass rate verified across all 10 test suites:
    - x86_64 UEFI (`./test.sh`)
    - x86_64 BIOS (`./tools/test-bios.sh`)
    - x86_32 (`./tools/test-x86_32.sh`)
    - AArch64 (`./tools/test-aarch64.sh`)
    - ARM32 (`./tools/test-arm32.sh`)
    - RISC-V 64 (`./tools/test-riscv64.sh`)
    - RISC-V 32 (`./tools/test-riscv32.sh`)
    - VirtIO-Net stack (`./tools/test-network.sh`)
    - Advanced network & services (`./tools/test-advanced-network.sh`)
    - Suite services & GUI (`./tools/test-suite-services.sh`)

## Six-Architecture Audit Matrix (2026-09-06)

Statuses come only from the 2026-09-06 live runs (test.sh, test-bios.sh,
test-smp.sh, test-{aarch64,arm32,riscv64,riscv32,x86_32}.sh,
build-arch-smoke.sh --all) plus serial logs; anything unproven is marked
accordingly. Key: W=WORKING (tested evidence), P=PARTIAL, N=NOT IMPLEMENTED,
NT=NOT TESTED.

| Feature      | x86_64 | x86_32 | ARM64 | ARM32 | RV64 | RV32 |
|--------------|--------|--------|-------|-------|------|------|
| Boot                | W (UEFI+BIOS) | W (UEFI+BIOS ISO) | W (virt) | W (virt) | W (virt) | W (virt) |
| Firmware handoff    | W (OVMF/GRUB mboot2) | W (GRUB mboot1/2) | W (QEMU -kernel) | W | W (OpenSBI) | W (OpenSBI) |
| Kernel entry/tables | W | W | W (VBAR_EL1) | W (VBAR/SVC) | W (stvec) | W |
| Memory PMM/heap/VMM | W | W (2-level) | W (4-level) | W (short-desc) | W (Sv39) | W (Sv32) |
| Page permissions    | W (W^X sections) | W | W | W | W | W |
| Kernel stacks/guards| W | W | W | W | W | W |
| Interrupts          | W (IOAPIC + MSI-X) | W (PIC IRQs) | W (GICv2) | W (GICv2) | W (PLIC) | W (PLIC) |
| Timer               | W (APIC 100Hz) | W (PIT/APIC) | W (gen timer) | W | W (SBI stime) | W |
| SMP                 | W (APs, per-CPU scheduling, private-root TLB shootdown, bounded Ring 3 migration; future subsystem migration remains) | N (single core) | P (secondary idle/timer + READY admission; user SMP future) | P (secondary idle/timer + READY admission; user SMP future) | P (secondary idle/timer + READY admission; user SMP future) | P (secondary idle/timer + READY admission; user SMP future) |
| Scheduler/preempt   | W (Ring3 IRQ-return preemption) | W | P (timer accounting; BSP-only user scheduling) | P (timer accounting; BSP-only user scheduling) | P (timer accounting; BSP-only user scheduling) | P (timer accounting; BSP-only user scheduling) |
| Processes/threads   | W (fork/execve/waitpid) | W | W | W | W | W |
| Syscalls            | W (int 0x80 / ecall / svc parity) | W | W | W | W | W |
| VFS + akaFS persist | W (2-boot proven) | W (ATA fallback, 2-boot) | W (2-boot) | W (2-boot) | W (2-boot) | W (2-boot) |
| Storage             | W (VirtIO MSI-X) | W (VirtIO+ATA sync) | W (MMIO IRQ-aware) | W (MMIO IRQ-aware) | W (MMIO IRQ-aware) | W (MMIO IRQ-aware) |
| Input               | W (PS/2 kbd+mouse IRQ) | W (PIC IRQ) | W (GIC VirtIO/UART IRQ) | W (GIC VirtIO/UART IRQ) | W (PLIC VirtIO/UART IRQ) | W (PLIC VirtIO/UART IRQ) |
| Graphics console    | W (fb console+splash) | W (VESA fb) | W (RAMFB) | W (RAMFB) | W (RAMFB) | W (RAMFB) |
| Terminal/TTY        | W (PTY, termios, IRQ) | W | W | W | W | W |
| akash shell         | W (pipes, redir, jobs, tab-complete) | W | W | W | W | W |
| Pipes/redirection   | W | W | W | W | W | W |
| Networking          | W (PCI VirtIO + SLIRP) | P (PCI path present; no external capture) | W (MMIO VirtIO + SLIRP) | W (MMIO VirtIO + SLIRP) | W (MMIO VirtIO + SLIRP) | W (MMIO VirtIO + SLIRP) |
| Sockets TCP/UDP     | W (BSD-style, httpd+curl proven) | NT | W (host curl) | W (host curl) | W (host curl) | W (host curl) |
| DNS                 | W (dig/nslookup vs resolv.conf) | W | W | W | W | W |
| GUI compositor      | W (queue + presented) | W (queue + presented) | W (queue + presented) | W (queue + presented) | W (queue + presented) | W (queue + presented) |
| Desktop             | W (aurora+terminal+palette) | W | W | W | W | W |
| Reboot/poweroff     | W (ACPI S5/reset; test-reboot PASS) | W (ACPI/keyboard) | W (PSCI) | W (PSCI) | W (SRST) | W (SRST) |
| Persistence         | W (test-persistence) | W (2-boot) | W (2-boot) | W (2-boot) | W (2-boot) | W (2-boot) |
| Audio               | W (PC speaker /dev/audio) | W | P (device nodes, no HDA IRQ) | P | P | P |
| ACPI                | W (RSDP/FADT/MADT/DSDT) | P (BIOS scan) | N | N | N | N |
| SSH suite           | P (TCP login conversation, no crypto) | P | P | P | P | P |

Evidence notes:

- Native networking is now W: `tools/test-nonx86-network.sh` captures
  external QEMU SLIRP traffic through a real MMIO VirtIO-Net device on all
  four native ports. Each log records the MMIO initialization marker,
  `2 packets transmitted, 2 received, 0% packet loss`, live `eth0` counters,
  and a host-served HTTP response returned by `/bin/curl`. The test also rejects
  NUL bytes caused by Ethernet padding.
- SMP W (bounded) on x86_64: test-smp.sh proves APs execute concurrent
  spinlocked work, each AP owns an idle scheduler TCB, explicitly queued kernel
  workers and Ring 3 workloads run and exit on their owner CPU, and PMM/heap
  allocation plus private-root page faults are safe under that load. The same
  bounded AP workload now exercises per-CPU descriptor tables, VFS create/write/
  read/rename/unlink, `/dev/null` and `/dev/zero`, and UDP socket lifecycle
  allocation/bind/poll/close. Normal non-test descendants may leave the BSP
  queue after boot self-tests. This is tested bounded multicore support, not a
  claim that every future subsystem is lock-free or migration-ready.
- x86_32 remains P/NT for external network capture because its dedicated
  network transcript has not yet been added; the native ARM/RISC-V socket
  cells are W based on the host HTTP `curl` transactions above.
- USB, generic MSI allocation, and hardware SMP user scheduling on ARM/RISC-V
  remain NOT IMPLEMENTED or NOT TESTED as detailed in prior sections.

## x86_64 multicore address-space hardening (2026-09-07)

The x86_64 VMM now serializes page-table reads and mutations with a global
page-table lock.  Mapping, unmapping, page-flag updates, and large-page
splitting perform synchronous TLB invalidation after releasing that lock so a
faulting CPU cannot deadlock the sender while waiting for a remote IPI.
The lock and the fixed-vector TLB mailbox both keep interrupts serviceable
while contended, then disable them for the atomic acquisition.  A root-aware
TLB shootdown IPI (vector 253) invalidates CPUs currently running the affected
address space; shared kernel-root changes are broadcast to every online CPU.
Per-CPU address-space tracking is updated on AP startup and every context
switch.  Reschedule IPIs may target the BSP as well as APs, allowing remote
waiters to wake safely.

Verification after this change:

- `tools/test-smp.sh` passed with both 2 and 4 x86_64 CPUs, including AP Ring 3
  stack-growth workloads and concurrent PMM/heap stress.
- `./test.sh` and `tools/test-bios.sh` passed.
- `tools/test-x86_32.sh`, `tools/test-aarch64.sh`, `tools/test-arm32.sh`,
  `tools/test-riscv64.sh`, and `tools/test-riscv32.sh` passed.
- `tools/build-arch-smoke.sh --all`, `tools/test-nonx86-lifecycle.sh`, and
  `tools/test-persistence.sh` passed.

This establishes tested multicore address-space behavior for the bounded
x86_64 workloads in the suite. ARM and RISC-V now have tested firmware
secondary-entry heartbeats, while their VMM and process scheduler remain
intentionally single-core.

## Aurora desktop state (2026-09-09)

Aurora and Lumen now present a modern native desktop rather than the earlier
classic demo. Lumen procedurally renders the full-screen wallpaper and modern
window chrome; Aurora supplies the resident centered dock and live task state.
Notepad and Task Manager render readable shared-font UI, and the dock/window
interaction model is aligned. The live reference image is
`build/aurora-desktop.png`. The six-port compositor IPC lifecycle suite passes.
Desktop typography is no longer the 5x7 pixel face: titlebars and the dock use
anti-aliased proportional DejaVu Sans, while data/editor surfaces use
anti-aliased DejaVu Sans Mono. The raster atlases are compiled into akaOS, so
no host font service or Linux runtime component is involved.
The bottom panel is now icon-first: running applications appear as distinct
compact icons with a focus underline. Its right-side tray contains Wi-Fi and
battery/power glyphs plus a live clock and date; the old `ONLINE` text is gone.

## x86_64 bounded VFS/device/socket stress (2026-09-07)

The AP scheduler self-test now runs eight rounds per AP using independent
descriptor tables against shared kernel pools. Each round creates a unique
regular file, writes and verifies data, renames and unlinks it, duplicates and
polls descriptors, exercises `/dev/null` and `/dev/zero`, and allocates/binds/
polls/closes a UDP socket. File-pool lifetime locking is enabled immediately
after initramfs setup, and socket lifecycle operations use an IRQ-safe pool
lock. The 2/4-CPU SMP suite passes this bounded stress workload. External
network transmit and persistent commits are covered by the extension below;
full external responses, unrestricted transactions, and broad VFS/device
migration remain separate work.

## x86_64 persistent/network stress extension (2026-09-07)

The bounded AP workload now runs after PCI, VirtIO block/network, and writable
akaFS initialization. Each AP commits a unique `/root/xsmpN/persist` record and
the host harness verifies all records in the writable disk image after the
4-core guest exits. Snapshot construction takes the VFS tree lock while copying
names, metadata, and file blocks, then releases it before block I/O; the
persistence lock still serializes the dual-superblock commit. Each AP also
sends four small UDP datagrams to QEMU's user-mode gateway through the real
VirtIO-Net TX queue. The single shared TX descriptor/buffer is IRQ-safely
serialized. `tools/test-smp.sh` passes with both 2 and 4 CPUs, including the
new persistent-transaction and external-network transmit markers. Full
external responses, unrestricted disk transactions, and broad socket I/O are
still separate stress targets.

## Native pthread/shared-address-space threads (2026-09-11)

XSCore now exposes a real, bounded pthread-style user-thread ABI on all six
ports. `AKAOS_SYSCALL_THREAD_CREATE` creates a scheduler TCB sharing the
calling process address-space root and descriptor table; `THREAD_EXIT`,
`THREAD_JOIN`, and `THREAD_DETACH` provide lifecycle and return-value
semantics. Architecture entry paths deliver the initial argument in the
native ABI register (or the i386 cdecl stack slot), and libc maps a private
64 KiB stack with a freestanding start thunk. `hello --pthread-test` creates a
worker, executes its body, returns `42`, and joins it. The compositor client
harness verifies this on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32
live QEMU guests. This is a pthread foundation, not a claim that KDE/GNOME/
LXDE/LXQt are ported: the shared-object namespace now covers bounded
DT_NEEDED/SysV-GNU-hash/PLT binding and binary D-Bus; complete toolkit
implementations and full X11/Wayland coverage remain TODO, while the bounded
AF_UNIX display handshake bridge is verified below.

Final verification for this milestone (2026-09-11): `tools/build-arch-smoke.sh
--all` passes x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32;
`tools/test-compositor-clients.sh` passes all six live QEMU guests;
`tools/test-desktop-readiness.sh` passes in the x86_64 guest;
`tools/test-nonx86-lifecycle.sh` passes reboot/poweroff/shutdown on all four
non-x86 ports; and `tools/test-iso.sh` boots the dual BIOS/UEFI ISO through
both firmware paths. The RISC-V images reserve memory through `__kernel_end`
to avoid allocator/page-table overlap as the kernel BSS grows.

## Static TLS and user thread-pointer ABI (2026-09-21)

XSCore now carries a per-thread user thread pointer in the user-transition
record (offset 48, transition size 56) and exposes `SET_TP`/`GET_TP` syscalls
69/70. Every architecture restores that value at user-entry, syscall, IRQ, and
resume boundaries: x86_64 uses FS base, x86_32 uses its private TLS GDT slot,
AArch64 uses TPIDR_EL0, and RISC-V uses `tp`. ARM32 keeps the authoritative
value in the transition record and mirrors it through the privileged return
path; userspace is built with `-mtp=soft` and its `__aeabi_read_tp` helper is a
minimal register-preserving GET_TP stub so compiler TLS offsets survive calls.

The native loader assigns bounded static offsets for every PT_TLS image,
creates a 64 KiB per-thread area, copies each module's template on first use,
supports static TP relocations on all six ports, and routes general-dynamic
`__tls_get_addr` through the same per-thread instance. DSO close/reload creates
fresh template state. `dltest` now verifies initial-exec/static TLS, worker
isolation, and reload behavior in addition to dynamic TLS.

Final clean rebuild and live `tools/test-dynamic-loader.sh` verification on
2026-09-21 passed x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32,
including constructors, dependencies, symbol versions, dynamic/static TLS,
and TLS reclamation. This is a real loader/pthread prerequisite, not a claim
that unmodified KDE, GNOME, LXDE, or LXQt binaries are now runnable; complete
toolkits, broad POSIX coverage, and full X11/Wayland stacks remain open. The
existing x86_64 `tools/test-desktop-readiness.sh` suite also passes against
the rebuilt image, so the bounded native profile launchers and D-Bus/display
handshake checks remain intact.

The GLib-shaped runtime now returns a stable non-null default main context;
`g_main_loop_new()` associates loops with that context and the public header
exports context ref/unref helpers. `hello --desktop-runtime-test` exercises
the default-context handle through the same dlsym boundary used by clients.
The readiness probe's boot wait is configurable and defaults to 90 seconds,
matching the measured UEFI boot time on a loaded WSL host; the live x86_64
desktop matrix passed after this change. All five native non-x86 build scripts
and the x86_32 build also compile the updated runtime successfully.
The final x86_64 rebuild additionally exercises context reference retention and
release through `g_main_context_ref()`/`g_main_context_unref()`.

The desktop main loop now has real descriptor sources: `g_unix_fd_add()` and
`g_unix_fd_add_full()` register bounded watches backed by the kernel POLL
syscall through `akaos_desktop_poll()`. `G_IO_IN/OUT/ERR/HUP/NVAL` conditions
are delivered to callbacks, timers and descriptors share one dispatch pass,
and descriptor-only loops use a 100 ms bounded poll slice so quit remains
responsive. The live x86_64 readiness matrix reports
`GLib descriptor source readiness verified`; the runtime and I/O bridge pass
strict compilation for all six architectures.

The same layer now exports bounded GIOChannel wrappers: Unix-channel creation,
reference/unreference, descriptor lookup, `g_io_add_watch()`, and
`g_io_add_watch_full()`. Watches retain their channel until source teardown,
forward G_IO conditions through the existing poll-backed dispatcher, and run
user destroy-notify callbacks. The final x86_64 live matrix reports
`GIOChannel descriptor watch verified` as well.

After rebuilding every port with this runtime, the full compositor/client
matrix passed on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32. The
live transcripts include D-Bus service routing, X11/Wayland handshakes, GTK/
Qt runtime probes, and the Lumen/palette compositor lifecycle on each guest.
 
## Asynchronous native D-Bus transport (2026-09-21)

`sdk/include/akaos/dbus.h` and `userspace/libc/src/dbus.c` now expose a
caller-owned `akaos_dbus_async_ping` state machine. It sends the native
five-byte PING request during initialization, then performs bounded,
poll-backed reads until PONG arrives; EAGAIN, partial reads, HUP/ERR, EOF, and
protocol mismatch are handled explicitly. The state machine is intentionally
small and can be driven by `g_unix_fd_add()`/GIOChannel without a hidden
thread or heap-owned event loop.

`hello --desktop-runtime-test` opens the live session bus, registers the
descriptor with the GLib-shaped runtime, and verifies asynchronous PONG
completion through the callback. `tools/test-desktop-readiness.sh` now
requires `D-Bus asynchronous descriptor watch verified`. A fresh x86_64 UEFI
image passed the live desktop readiness matrix, and the rebuilt compositor
client matrix passed on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32.

This is a real native asynchronous transport foundation, not the complete
upstream libdbus message API: binary message queues, cancellation, reconnect,
per-context source ownership, and full D-Bus authentication/address parsing
remain open before unmodified desktop stacks can run.

## Asynchronous standard D-Bus frame (2026-09-21)

The async boundary now also covers a standard little-endian D-Bus method
return. `akaos_dbus_binary_connect()` completes the existing bounded SASL
EXTERNAL/BEGIN bootstrap, `akaos_dbus_async_binary_ping_init()` sends the
org.freedesktop.DBus.Ping method call, and its pump parses the 16-byte header,
validates a METHOD_RETURN/protocol version, computes aligned field/body tail
lengths with overflow bounds, and consumes partial reads until the frame is
complete. No hidden thread or nonblocking-fcntl dependency is required; the
caller drives it from the existing POLL/GIO watch.

`hello --desktop-runtime-test` reports
`D-Bus asynchronous binary descriptor watch verified`. The x86_64 readiness
matrix passes, all five non-x86/x86_32 images rebuild successfully, and the
compositor/client probe verifies the binary marker on x86_64, x86_32, AArch64,
ARM32, RISC-V64, and RISC-V32 (RISC-V targets were also rerun individually
after a host-side combined-run interruption).

## Reusable asynchronous D-Bus request queue (2026-09-21)

The public D-Bus ABI now includes a bounded caller-owned asynchronous client
queue. `akaos_dbus_async_client` accepts up to four small standard binary
METHOD_CALL requests, assigns serials, writes bounded frames, incrementally
parses little-endian METHOD_RETURN/ERROR replies, matches replies by serial,
and supports cancellation and bounded reply extraction. Signals and malformed
or oversized frames are rejected without an allocator or hidden worker.

`hello --desktop-runtime-test` queues concurrent `Ping` and `GetId` calls,
drives the authenticated descriptor through the existing GLib-shaped
`g_unix_fd_add()` source, and takes both serial-matched replies. The live
`test-compositor-clients.sh` matrix reports `D-Bus asynchronous request queue
verified` on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32. This is a
real reusable bounded transport layer, but reconnect, per-context source
ownership, and the complete upstream libdbus API remain future work.

## Per-context GLib source ownership (2026-09-21)

The desktop runtime now attaches every timer, descriptor, and GIOChannel watch
to a retained `runtime_context`. `g_main_context_new()` allocates a bounded
explicit context, `g_main_context_push_thread_default()`/`pop_thread_default()`
select the context used by source-registration APIs, and loop/iteration/
pending/poll dispatch filters by the owning context. Loop and source teardown
release their context references without leaving dangling source handles.

`hello --desktop-runtime-test` creates a second context, registers a pipe watch
while it is pushed, verifies the default context cannot consume the event, and
then dispatches it from the owner. The x86_64 readiness run and the rebuilt
six-architecture compositor/client matrix pass
`GLib per-context source ownership verified`. D-Bus reconnect and the full
upstream GLib source API remain open.

## Asynchronous D-Bus reconnect lifecycle (2026-09-21)

`akaos_dbus_async_client_reconnect()` now closes the old authenticated
connection, invalidates all outstanding serial slots and partial-frame state,
performs a fresh bounded standard D-Bus authentication, and returns the client
to the active state with a new serial epoch. Callers remove the old descriptor
watch and attach the replacement descriptor before queueing new work; old
serials are intentionally no longer valid after reconnect.

The desktop-runtime probe verifies this lifecycle by reconnecting after the
concurrent Ping/GetId queue, attaching a new `g_unix_fd_add()` watch, and
completing a fresh Ping. `D-Bus asynchronous reconnect verified` is present in
the x86_64 readiness log and in all six compositor/client guest transcripts.
Explicit cancelled-reply/close ownership is covered below; richer retry
backoff/address parsing and the complete upstream libdbus lifecycle remain
open.

## Asynchronous D-Bus cancellation and close lifecycle (2026-09-21)

The bounded client now retains a cancelled request slot until the caller
acknowledges it with `akaos_dbus_async_client_take_reply()`. The acknowledgement
returns `-1` with `ECANCELED`, clears the slot exactly once, and distinguishes a
cancelled serial from an unknown serial. `akaos_dbus_async_client_close()` now
owns descriptor teardown, clears queued/reply/partial-frame state, and leaves
the object in a closed error state instead of requiring callers to close the
raw descriptor directly.

The live desktop-runtime probe verifies both one-shot cancellation and close
cleanup after the reconnect Ping. The x86_64 readiness run and all six
compositor/client transcripts contain `D-Bus asynchronous cancellation
lifecycle verified` and `D-Bus asynchronous cancellation and close lifecycle
verified`.

## Bounded asynchronous D-Bus retry policy (2026-09-21)

`akaos_dbus_async_client_retry()` now provides an explicit caller-driven
recovery boundary for a closed or errored async client. The public budget is
capped at `AKAOS_DBUS_ASYNC_CLIENT_MAX_RETRIES` (three attempts); every failed
attempt clears the descriptor, pending slots, cancelled state, partial frame,
and stale serial epoch before trying a fresh authenticated connection. A
successful retry starts a new serial epoch and leaves descriptor-watch
reattachment to the caller, so no hidden worker or implicit event-loop source
is introduced.

The desktop-runtime probe closes the authenticated client, retries with a
two-attempt budget, attaches a new `g_unix_fd_add()` watch, and completes a
fresh Ping. `D-Bus bounded retry policy verified` appears in x86_64 readiness
and in all six compositor/client guest transcripts. Richer backoff/address
parsing and the complete upstream D-Bus serial/message lifecycle remain open.

## Pthread cleanup and exit lifecycle (2026-09-21)

The shared libc pthread layer now exposes the POSIX cleanup-stack shape used by
GLib/Qt worker code: `pthread_cleanup_push()`/`pthread_cleanup_pop()` macros,
`pthread_exit()`, and bounded per-thread LIFO handler storage. Cancellation
and explicit thread exit drain handlers before key destructors and static-TLS
release; normal `pop(1)` execution remains one-shot. The initial thread is
tracked separately from worker records so its cleanup handlers are not lost
when its TID is nonzero.

The hello pthread probe verifies both normal pop execution and a cancelled
worker's cleanup callback. The x86_64 readiness run and the full rebuilt
x86_64/x86_32/AArch64/ARM32/RISC-V64/RISC-V32 compositor/client matrix pass
with the cleanup marker. Clone flags, asynchronous scheduler interruption,
and complete POSIX process-teardown accounting remain open.

### Typed desktop D-Bus properties (2026-09-22)

The native desktop compatibility service now exposes a bounded, relocation-safe
`org.freedesktop.DBus.Properties` interface. `Get` returns real string/boolean
variants, `GetAll` returns an aligned `a{sv}` dictionary, and `Set` reports the
properties as read-only. The published properties are `Status`, `Ready`,
`Capabilities`, `DisplayServer`, `Toolkit`, and `SessionType`. The service
dispatcher now reads object path, interface, and signature headers, while the
desktop regression uses `/org/akaos/Desktop` and the Properties interface
instead of relying on broker-default headers. The x86_64 live readiness matrix
passes the new `Get` and `GetAll` calls. Property-change signals, richer
signatures, and full upstream D-Bus semantics remain open work.

Desktop roles now also emit a bounded `PropertiesChanged` readiness signal
with the standard `sa{sv}as` signature. The hello regression subscribes with a
real match rule, validates the object path/interface/member/signature, and
decodes the complete mixed typed dictionary: `Status`, `Ready`,
`Capabilities`, `DisplayServer`, `Toolkit`, and `SessionType`. The signal
probe passes on all six rebuilt guests, independent of dictionary order; empty
invalidation remains explicit, while general-purpose marshalling remains open.

### Six-port compositor verification (2026-09-22)

After the typed-properties fixes, all six images were rebuilt successfully and
`tools/test-compositor-clients.sh` completed with exit code 0 for x86_64,
x86_32, AArch64, ARM32, RISC-V64, and RISC-V32. The run verified the full
bounded client path on each guest: Lumen mouse input, Openbox component
activation/name ownership, typed desktop D-Bus routing, mixed typed
`PropertiesChanged` delivery, XSDisplay readiness, toolkit display bridging,
and palette surface resize/damage/destroy lifecycle. The compatibility layer
is still bounded; richer signatures, complete D-Bus marshalling, and
unmodified upstream desktop packages remain future work.

### Standard broker and large-message transport (2026-09-22)

The native broker now covers additional standard methods used during desktop
session discovery: `GetConnectionUnixProcessID`, `GetConnectionCredentials`,
`GetAllMatchRules`, `ListQueuedOwners`, `UpdateActivationEnvironment`, and
`ReloadConfig`. Replies use bounded standard signatures (`u`, `a{sv}`, `as`,
and empty returns), are advertised through introspection, and are exercised by
the live standard-broker probe.

The kernel IPv4/TCP stream path now segments writes into ordered packets below
the Ethernet buffer limit. This prevents a large introspection frame from
being silently discarded and applies equally to D-Bus, X11, and toolkit
traffic. The x86_64 readiness gate and the full six-architecture compositor /
input / palette matrix pass after the fix. General-purpose D-Bus marshalling
and unmodified KDE/GNOME/LXDE/LXQt packages remain open work.

### Shared libc and PT_INTERP validation (2026-09-22)

The userspace builders now produce a genuine shared `libc.so.6` ET_DYN from
the akaOS libc sources and package it in every architecture's initramfs. The
dynamic hello program has no statically linked libc objects; it resolves libc
through the native PT_INTERP loader alongside the demo DSO. i386 has a
dedicated dynamic GOT/PLT CRT, while the RISC-V loader accepts the RV32 data
relocation emitted by the shared libc.

The dynamic-loader matrix and the compositor/client lifecycle matrix both pass
on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32. The desktop work is
still akaOS-native compatibility coverage; complete arbitrary D-Bus
marshalling, full toolkit ABI coverage, and unmodified upstream KDE/GNOME/
LXDE/LXQt packages remain future work.

### Generic D-Bus codec and current image validation (2026-09-22)

`userspace/libc/src/dbus_codec.c` now supplies a reusable D-Bus signature
parser, wire-body validator, and caller-owned value-tree marshaller. It handles
little-endian alignment and length fields for primitive values, arrays,
structs, dictionary entries, and variants, with bounded signature depth and
malformed-body rejection. `hello --dbus-marshaller-test` exercises primitive,
wide, nested `a{sv}`, tuple, variant, and invalid-input cases.

The source is included by the common and all five native cross-architecture
userspace builders. Fresh x86_64 and x86_32 dual-firmware ISOs plus the four
native kernel images were tested: the compositor/client lifecycle matrix passed
on all six ports, and the x86_64 UEFI desktop-readiness matrix passed through
all Aurora/LXDE/LXQt/GNOME/KDE compatibility sessions. This remains a bounded
akaOS compatibility layer, not unmodified upstream desktop binaries; generic
decode APIs, strict object-path validation, arbitrary broker dispatch, and
full toolkit/protocol ABIs are still open.

### Codec boundary hardening (2026-09-22)

The codec now exposes `akaos_dbus_object_path_validate()`, rejects malformed
object paths and signature-valued fields, and accepts the valid empty-signature
zero-body case without requiring a non-null buffer. Array marshalling and
validation were corrected to exclude the alignment padding after an array
length word, matching the standard D-Bus `a{ss}` layout. Generic checks now
run before outbound async/synchronous calls and signals, and before resident
desktop-service dispatch.

The resulting six rebuilt ports pass `tools/test-compositor-clients.sh` and the
x86_64 `tools/test-desktop-readiness.sh` gate. The codec now has generic
decoding/value-tree output and strict object-path/signature validation;
arbitrary broker dispatch and full toolkit/protocol ABIs remain open.

## Futex-backed pthread blocking (2026-09-23)

XSCore syscall 71 implements aligned 32-bit futex WAIT and WAKE operations.
Wait queues use the mapped physical word so shared mappings can synchronize
across process address spaces; `AKAOS_FUTEX_PRIVATE_FLAG` selects an
address-space/virtual-address key. The scheduler records a pending wake when a
waker races a waiter before it blocks, and `xscore_thread_block()` consumes
that event instead of sleeping indefinitely.

Libc now uses the blocking path for mutex contention, condition-variable
signal/broadcast, rwlock contention/unlock, `pthread_once`, barrier generation
waits, and `sem_wait`. At that point timed condition/semaphore waits still used
bounded `nanosleep` polling; the newer interrupt-driven wait entry at the top
supersedes this. The refreshed six-port compositor/client matrix passes,
including `hello --pthread-test` on every guest, and the live x86_64 desktop
readiness gate passes. The shared-memory futex regression maps the same object
separately in parent and child, then verifies WAIT/WAKE through its physical
page on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32. Process-shared
pthread mutex, condition, rwlock, barrier, and semaphore paths also pass using
independently mapped process views on all six ports. The live x86_64
desktop-readiness gate passes with the rebuilt libc. Timed condition and
semaphore waits still poll, and robust/priority-inheritance futex semantics
remain unimplemented.

Before the scheduler scaling section below, XSCore supported 16 threads.
Full POSIX lifecycle/cancellation/TLS behavior plus upstream GLib/GTK/Qt ABIs
remain incomplete. Do not claim KDE, GNOME, LXDE, or LXQt run unmodified on
akaOS.

## Scheduler and pthread scaling (2026-09-23)

XSCore's fixed thread table now supports 64 TCBs. The per-CPU ready set is a
64-bit bitmap, and libc's bounded pthread bookkeeping pool is also 64 records.
`hello --pthread-test` holds 24 workers at a start gate, validates each
worker's TLS and join result, and passes on x86_64, x86_32, AArch64, ARM32,
RISC-V64, and RISC-V32. The complete `tools/test-compositor-clients.sh`
matrix passed on all six rebuilt guests; `tools/test-desktop-readiness.sh`
passed on the x86_64 guest. The rebuilt x86_32 dual-firmware ISO also reached
login under both UEFI and BIOS using `tools/test-x86_32-iso.sh --no-build`.
The test harness gives the UEFI guest 256 MiB; its previous 128 MiB setting
timed out before GRUB handoff, while BIOS and the six-port runtime matrix use
their existing configurations.

The x86_32 per-process mmap metadata budget remains 128 pages (512 KiB).
Increasing the cap above 128 reproducibly caused a boot-time supervisor page
fault, so 24-worker stress uses 16 KiB stacks on every port and stays within
the verified limit. Replacing the fixed x86_32 page-record storage safely is
still open. This capacity test does not establish full POSIX conformance or
that unmodified KDE/GNOME/LXDE/LXQt packages run.
