# Memory-Efficient Versioned File Indexer

A command-line tool that builds a word-frequency index over large text files without loading them fully into memory. Files are read chunk by chunk using a fixed-size buffer, and each indexed file is associated with a version label so you can query and compare across versions.

---

## Building

```bash
g++ -O2 230437_Hardik.cpp -o analyzer
```

---

## How it works

The buffer size is fixed at construction time (anywhere between 256 KB and 1024 KB). Each call to `readChunk()` fills the buffer with the next block of bytes from the file. Words that happen to fall exactly on a buffer boundary are handled by carrying the incomplete token (`leftover`) into the next chunk, so nothing gets dropped.

All tokens are lowercased before being stored, so queries are case-insensitive by default.

Internally, each version maps to a `KeyValueStore<string, int>` — a thin wrapper around `unordered_map` that handles the increment-or-insert pattern. Memory grows with the number of unique words, not total words, which keeps it manageable even on repetitive log files.

---

## Usage

```
./analyzer --file <path> --version <name> --buffer <kb> --query <type> [query options]
```

### Flags

```
--file <path>       file to index (word and top queries)
--file1 <path>      first file  (diff query)
--file2 <path>      second file (diff query)
--version <name>    version label (word and top queries)
--version1 <name>   version label for file1 (diff query)
--version2 <name>   version label for file2 (diff query)
--buffer <kb>       buffer size in KB, must be 256–1024 (default: 512)
--query <type>      word | diff | top
--word <token>      the word to look up (word and diff queries)
--top <k>           number of results (top query, default: 10)
```

---

## Query types

### word — look up a single word

```bash
./analyzer --file logs.txt --version v1 --buffer 512 --query word --word error
```

```
Version: v1
Count: 605079
Buffer Size (KB): 512
Execution Time (s): 1.08
```

### top — most frequent words

```bash
./analyzer --file logs.txt --version v1 --buffer 512 --query top --top 5
```

```
Top-5 words in version v1:
devops 1209558
debug 605150
error 605079
info 604266
warning 604149
Buffer Size (KB): 512
Execution Time (s): 1.09
```

Words with equal frequency are ordered alphabetically. If `--top` is larger than the vocabulary size, all words are returned.

### diff — compare word frequency between two versions

```bash
./analyzer --file1 v1.txt --version1 v1 \
          --file2 v2.txt --version2 v2 \
          --buffer 512 --query diff --word error
```

```
Difference (v2 - v1): -122769
Buffer Size (KB): 512
Execution Time (s): 2.25
```

The difference is always `count(version2) - count(version1)`, so a negative result means the word appears more in version1. If the word is absent from one version, its count is treated as 0.

---

## Classes

**`KeyValueStore<K, V>`** — template class wrapping an `unordered_map`. Provides `increment()`, `get()`, and `all()`. Used as the per-version word counter.

**`BufferedFileReader`** — opens a file in binary mode and reads it in fixed-size blocks. Tracks whether EOF has been reached via `reachedEOF` flag. Throws `runtime_error` if the file cannot be opened.

**`Tokenizer`** — scans each buffer byte by byte, building lowercase alphanumeric tokens. Stores an incomplete token in `leftover` when a chunk ends mid-word.

**`VersionedIndex`** — owns a map from version name to `KeyValueStore`. Exposes two overloads of `getCount`: one that throws on a missing version, and one that returns a caller-supplied default. Also provides `getTopK` with a custom sort comparator.

**`Query` / `WordQuery` / `DiffQuery` / `TopKQuery`** — `Query` is an abstract base with pure virtual `execute()` and `typeName()`. The three derived classes implement each query type. `QueryProcessor` stores a `unique_ptr<Query>` and calls through it, so the dispatch is fully runtime.

**`QueryProcessor`** — parses `argv`, validates arguments, indexes the file(s), constructs the right query object, and prints results with timing.

---

## Error handling

The program throws `std::invalid_argument` or `std::runtime_error` for bad arguments, out-of-range buffer sizes, missing required flags, and files that cannot be opened. All exceptions are caught in `main` and printed to `stderr`, and the process exits with code 1.

---

## Notes

- Tokenization only keeps alphanumeric characters. Hyphens, apostrophes, underscores etc. act as delimiters, so `don't` indexes as `don` and `t` separately.
- Version labels are arbitrary strings — they don't have to follow any particular format.
- The buffer size affects peak I/O throughput but not correctness. 512 KB is a reasonable default for most systems.
