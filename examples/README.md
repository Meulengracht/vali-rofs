# VaFS API Examples

These examples are small, standalone C snippets that demonstrate the current opaque builder and reader APIs.

They are not part of the normal CMake build. They are meant to be read, copied, or compiled manually against a local VaFS build.

## Builder Example

`builder_example.c` creates a tiny image with:

- a `/docs` directory
- a regular file with payload bytes
- a hardlink alias to that file
- a symlink
- a FIFO special entry
- a couple of object extended attributes

Example compile command from the repository root:

```bash
cc examples/builder_example.c -Ilibvafs/include -Lbuild/lib -lvafs -lvafs-blockcache -o /tmp/vafs-builder-example
```

Example run:

```bash
/tmp/vafs-builder-example /tmp/example.vafs
```

## Reader Example

`reader_example.c` opens an image, lists the root directory, stats and reads `/docs/hello.txt`, reads xattrs, and opens `/docs/latest` with `VaFsLookup_NoFollow` to show symlink-object access.

If you run the builder example first, the reader example can inspect that generated image:

```bash
cc examples/reader_example.c -Ilibvafs/include -Lbuild/lib -lvafs -lvafs-blockcache -o /tmp/vafs-reader-example
/tmp/vafs-reader-example /tmp/example.vafs
```