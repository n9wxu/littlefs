## Handling errors in littlefs

littlefs usually runs where no one is around to read an error. When a call
fails, the firmware has to decide on its own what to do next, and that
decision is only safe if it knows what the error means and what the call left
behind.

This document gives each error code one meaning and one recommended action,
and says, for each kind of call, what is on disk and in RAM after an error.
The codes each function can return are listed with the function in
[lfs3.h](lfs3.h).

```
   | | |     .---._____
  .-----.   |          |
--|o    |---| littlefs |
--|     |---|    v3    |
  '-----'   '----------'
   | | |
```

## Actions

Every error code maps to one of three actions:

- **Retry.** The same call can succeed later, unchanged. Bound the number of
  retries and wait between them.

- **Rebuild.** Something has to be repaired or reclaimed first, then the call
  can be retried.

- **Fail.** Stop using this path and take the application's contingency, see
  [Contingencies](#contingencies).

## Error codes

| Code | Value | Meaning | Action |
|---|---|---|---|
| `LFS3_ERR_IO` | -5 | A block device callback failed, in a way that may not last: a bus error, a low supply | Retry |
| `LFS3_ERR_CORRUPT` | -84 | Data on disk failed a check, or the read callback reported it unreadable | Rebuild |
| `LFS3_ERR_NOSPC` | -28 | No free block, or no room left in a file's metadata block | Rebuild |
| `LFS3_ERR_NOMEM` | -12 | `lfs3_malloc` returned NULL | Rebuild |
| `LFS3_ERR_BADFD` | -77 | A file handle is torn: an earlier error left it matching no version of its file | Rebuild |
| `LFS3_ERR_BUSY` | -16 | The target is in use | Rebuild |
| `LFS3_ERR_NOTSUP` | -95 | The disk or the request needs something this build or this filesystem doesn't have | Fail |
| `LFS3_ERR_FBIG` | -27 | A file would grow past `file_limit` | Fail |
| `LFS3_ERR_INVAL` | -22 | An invalid argument, a bug in the caller | Fail |
| `LFS3_ERR_NOENT`, `LFS3_ERR_EXIST`, `LFS3_ERR_NOTDIR`, `LFS3_ERR_ISDIR`, `LFS3_ERR_NOTEMPTY`, `LFS3_ERR_NAMETOOLONG`, `LFS3_ERR_NOATTR` | -2, -17, -20, -21, -39, -36, -61 | The namespace isn't what the call expects | Fail |
| `LFS3_ERR_RANGE`, `LFS3_ERR_UNKNOWN` | -34, -1 | Internal, never returned | Fail |
| Any other negative value | | Returned by the block device, passed through unchanged | The block device's |

### `LFS3_ERR_IO`

A read, prog, erase or sync callback returned `LFS3_ERR_IO`. Flash read at
low supply voltage, a noisy bus, or a busy device can all fail once and work
on the next try, so littlefs treats IO as transient: it returns it from the
call it happened in, and doesn't read another copy, move data, or mark
anything bad or suspect because of it. The one exception is the cleanup
after `lfs3_remove` and `lfs3_rename`, see
[Metadata operations](#metadata-operations).

**Retry** the call, a bounded number of times, after a delay or once the
supply has recovered. [The state after an error](#the-state-after-an-error)
says what each call left behind, which is what makes retrying safe. If the
error persists, **Fail**.

A block device should return `LFS3_ERR_IO` for failures that may pass, and
`LFS3_ERR_CORRUPT` only for data it knows to be bad, such as an
uncorrectable ECC error.

### `LFS3_ERR_CORRUPT`

Data read from disk failed its checksum or parity check, or the read
callback returned `LFS3_ERR_CORRUPT`. A failed check doesn't prove the data
is lost: a read at low supply, or a bit left metastable by a power loss, can
fail once and pass later. Reads that fail are listed by
`lfs3_fs_nextsuspect` (with `LFS3_GBMAP`).

**Rebuild**, with the smallest repair that works:

1. Run `lfs3_fs_ck(lfs3, LFS3_CK_CKMETA | LFS3_CK_CKDATA)` with `ck_retries`
   set (we suggest 3). It reads failing blocks again, and on a writable
   filesystem moves the data of any block that needed a retry to a new
   block. If it returns 0, retry the call.
2. If it still returns `LFS3_ERR_CORRUPT`, find the damaged files with
   `lfs3_file_ck` and remove or rewrite them. A damaged gbmap can be rebuilt
   with `lfs3_fs_rmgbmap` and `lfs3_fs_mkgbmap`.
3. Only damaged metadata leaves no repair but a reformat, which loses
   everything, see [Contingencies](#contingencies).

### `LFS3_ERR_NOSPC`

No free block was left, or an entry or attribute doesn't fit in its
metadata block, together with the file's name and its other attributes.

**Rebuild:** free space, by removing or truncating files (rotating logs,
for example), or, for an attribute, by removing other attributes of the same
file, then retry. If nothing can be freed, **Fail**. From `lfs3_format` with
`LFS3_F_GBMAP`, NOSPC means no block was left that erases and programs for
the gbmap's root: the device is worn out.

### `LFS3_ERR_NOMEM`

`lfs3_malloc` returned NULL, in `lfs3_format`, `lfs3_mount`,
`lfs3_file_open` or `lfs3_file_opencfg`.

**Rebuild:** free memory (close files, unmount another filesystem), or give
littlefs its buffers (`rcache_buffer`, `pcache_buffer`, `lookahead_buffer`,
and `fcache_buffer` in `struct lfs3_file_cfg`), then retry. Otherwise
**Fail**.

### `LFS3_ERR_BADFD`

A write, truncate or fruncate that spans several entries commits them one
at a time. An error between those commits leaves the handle torn, matching
neither the file's old contents nor its new ones. The call that failed
returns its own error; after that, every call that would read, write,
flush, sync, check or size the handle returns `LFS3_ERR_BADFD`, and writes
nothing. The file on disk is untouched.

**Rebuild:** call `lfs3_file_resync`, which drops the handle's unsynchronized
changes, or close the file and open it again, then redo the writes made
since the last successful `lfs3_file_sync`.

### `LFS3_ERR_BUSY`

The target of the call is in use: the root directory, which can't be
removed or renamed; a block that holds live data, which `lfs3_fs_mkbad`
can't mark bad; or a traversal opened with `LFS3_T_EXCL`, under which the
filesystem changed.

**Rebuild:** free the target, then retry: rewind the traversal with
`lfs3_trv_rewind`, or move the data off the block (rewriting or removing
the file that holds it moves it). If the target can't be freed, as the root
never can, **Fail**. The root returns BUSY rather than INVAL because it is
always in use, as on Linux.

### `LFS3_ERR_NOTSUP`

The disk, or the request, needs something this build or this filesystem
doesn't have: an on-disk version or compat flag this build doesn't know, a
block size other than the configuration's or more blocks than it, a file of
an unknown type, or a bad-block call on a filesystem without a gbmap.

**Fail.** At mount, reformatting is a contingency only if losing the
contents is acceptable; usually the firmware or its configuration is wrong.

### `LFS3_ERR_FBIG`

A write, truncate or fruncate would make a file larger than `file_limit`, or
`lfs3_set` was given a value larger than it.

**Fail** the call; rotate to a new file. Like any failed write it
desynchronizes the handle, see [File writes](#file-writes).

### `LFS3_ERR_INVAL`

The caller passed an invalid argument: an empty path, or one that climbs
above the root with `..`; a directory renamed into itself; a seek past
`file_limit`; a block count smaller than the filesystem or larger than the
device for `lfs3_fs_grow`; a block out of range, or 0 or 1, for
`lfs3_fs_mkbad` and `lfs3_fs_mkgood`.

**Fail.** This is a bug in the caller; never retry unchanged.

### The namespace codes

`LFS3_ERR_NOENT` (no such entry), `LFS3_ERR_EXIST` (it already exists),
`LFS3_ERR_NOTDIR` (not a directory), `LFS3_ERR_ISDIR` (is a directory),
`LFS3_ERR_NOTEMPTY` (directory not empty), `LFS3_ERR_NAMETOOLONG` (name
longer than `name_limit`) and `LFS3_ERR_NOATTR` (no such attribute) describe
the filesystem's contents, not a fault.

**Fail** the request, which is for the application's logic to handle; never
retry unchanged. Two cases are not failures at all:

- `LFS3_ERR_NOENT` from `lfs3_dir_read`, `lfs3_trv_read`, `lfs3_fs_nextbad`
  and `lfs3_fs_nextsuspect` marks the end of the iteration.
- After a retry, `LFS3_ERR_EXIST` from `lfs3_mkdir`, `LFS3_ERR_NOENT` from
  `lfs3_remove` and `lfs3_rename`, and `LFS3_ERR_NOATTR` from
  `lfs3_removeattr` mean the first attempt took effect, see
  [Metadata operations](#metadata-operations). Treat them as success.

### `LFS3_ERR_RANGE` and `LFS3_ERR_UNKNOWN`

These are used inside littlefs and are never returned. If one is,
**Fail** and report a littlefs bug.

### Codes from the block device

The block device callbacks may return their own negative codes, which
littlefs passes through unchanged from the call that ran the callback, as it
does errors from the `lock` and `unlock` callbacks with `LFS3_THREADSAFE`.
Their meaning, and the action, is the block device's.

## The state after an error

### Reads

`lfs3_stat`, `lfs3_get`, `lfs3_size`, `lfs3_getattr`, `lfs3_sizeattr`,
`lfs3_file_read`, `lfs3_dir_read`, `lfs3_dir_seek`, `lfs3_dir_rewind`,
`lfs3_trv_read`, `lfs3_fs_usage`, `lfs3_fs_nextbad`:

- Nothing on disk changes.
- A failed `lfs3_file_read` leaves the file position where it was, though
  the buffer may hold part of the data, and a failed `lfs3_dir_read` leaves
  the directory position where it was. Calling again returns what an
  undisturbed call returns.
- After a failed `lfs3_trv_read`, rewind the traversal before reading again.
- `lfs3_file_read` of a handle with unflushed writes flushes them first, and
  a failed flush leaves the handle as a failed write does.

### Metadata operations

`lfs3_mkdir`, `lfs3_remove`, `lfs3_rename`, `lfs3_setattr`,
`lfs3_removeattr`, `lfs3_set`, and the first `lfs3_file_sync` or
`lfs3_file_close` of a file opened with `LFS3_O_CREAT`:

- On an error the operation did not happen, in RAM or on disk.
- Except after an error from the sync callback: then the operation may have
  happened, alike in RAM and on disk. It has if the failed sync followed the
  commit that made it, and the error is still returned, once. A retry then
  returns `LFS3_ERR_EXIST`, `LFS3_ERR_NOENT` or `LFS3_ERR_NOATTR` (see
  [The namespace codes](#the-namespace-codes)), or succeeds again.
- `lfs3_remove` of a directory and `lfs3_rename` return 0 when only the
  cleanup after their commit fails. The operation is complete;
  `lfs3_fs_stat` reports `LFS3_I_MKCONSISTENT`, and the next write, or
  `lfs3_fs_mkconsistent`, finishes the cleanup and returns its error if it
  fails again.

### File writes

`lfs3_file_write`, `lfs3_file_flush`, `lfs3_file_sync`,
`lfs3_file_truncate`, `lfs3_file_fruncate`:

- On disk, the file is as of its last successful `lfs3_file_sync`. After an
  error from the sync callback in `lfs3_file_sync` it may instead be as of
  that sync, and other handles of the file then see it.
- The handle is desynchronized, as by `lfs3_file_desync`. It keeps the
  changes made since the last sync, including whatever part of the failed
  call's data reached it, and its position may or may not have moved.
  `lfs3_file_close` then writes nothing and returns 0, so check
  `lfs3_file_sync` to know whether data reached storage.
- To retry, call `lfs3_file_sync`, which commits what the handle holds. To
  get back to a known state first, call `lfs3_file_resync` and redo the
  writes made since the last sync, or seek back and write the failed range
  again.
- If the failed call spanned several commits, the handle may be torn, see
  `LFS3_ERR_BADFD`.

### Opening and closing

- After an error from `lfs3_file_open`, `lfs3_file_opencfg`,
  `lfs3_dir_open` or `lfs3_trv_open`, the handle isn't open, and nothing
  stays allocated.
- `lfs3_file_close` releases the handle, even when it returns an error.
  `lfs3_dir_close` and `lfs3_trv_close` never fail.

### Mount and format

- After an error from `lfs3_mount`, the filesystem isn't mounted and nothing
  stays allocated. A mount without mount-time work writes nothing. Mount-time
  work (`LFS3_M_MKCONSISTENT`, `LFS3_M_LOOKAHEAD`, `LFS3_M_COMPACT`, and the
  repairs of `LFS3_M_CKMETA` and `LFS3_M_CKDATA` with `ck_retries`) leaves
  what it wrote as the janitorial calls do.
- `lfs3_unmount` never fails.
- After an error from `lfs3_format`, the device may no longer hold the
  previous filesystem. Format again.

### Janitorial calls

`lfs3_fs_mkconsistent`, `lfs3_fs_ck`, `lfs3_fs_gc`, `lfs3_fs_grow`,
`lfs3_fs_mkgbmap`, `lfs3_fs_rmgbmap`, `lfs3_fs_mkbad`, `lfs3_fs_mkgood`, and
`lfs3_trv_read` of a traversal with work flags:

- The filesystem stays consistent and its contents don't change. Work done
  before the error stays done, the rest stays pending, its `LFS3_I_*` flag
  set, and calling again finishes it.
- As with the metadata operations, the block count of `lfs3_fs_grow` and
  the gbmap of `lfs3_fs_mkgbmap` and `lfs3_fs_rmgbmap` change only if the
  error came from the sync callback after their commit.
- `lfs3_fs_mkbad` and `lfs3_fs_mkgood` may leave the new mark in RAM, where
  `lfs3_fs_nextbad` reports it, before it reaches disk. The next commit of
  any write takes it to disk, as does calling again; a remount before that
  loses it.

### After a power loss

A power loss isn't an error a call returns. After one, the next mount finds
every metadata operation either complete or not started, and every file as
it was at its last completed sync, with the caveat in `lfs3.h`'s note on the
prog callback about bits a power loss leaves metastable.

## Contingencies

When the action is **Fail**, the application decides how to keep going. In
order of how much they cost:

1. **Report.** Log the code and the call. For health, `lfs3_fs_nextbad` and
   `lfs3_fs_nextsuspect` list the blocks marked bad and the blocks whose
   reads failed, `lfs3_fs_usage` the blocks in use, and `lfs3_fs_stat`'s
   flags the pending work.
2. **Give up on the file, not the filesystem.** Remove a damaged file, or
   leave it and write to a new one; the other files stay usable.
3. **Remount read-only** (`LFS3_M_RDONLY`) and keep serving what can be
   read, while writes are dropped and counted, or kept in RAM.
4. **Run without storage**, if the product can.
5. **Reformat**, best on the next boot after copying out what still reads.
   This loses everything on the filesystem, so it is the last resort.

## Checking this document

The test suite checks what this document says. Every public call a test
makes goes through `runners/test_errs.h`, which with `TEST_ERRS=<file>`
records each error code a function returns, and `scripts/ckerrs.py` fails
if any of them isn't listed with its function in `lfs3.h` (`make
test-errs`). `test_errs_ioerror` fails every read, prog, erase and sync of
32 calls in turn and checks the state each leaves behind. The requirements
behind this are LFS3-ERR-01 to LFS3-ERR-07 in
[REQUIREMENTS.md](REQUIREMENTS.md).
