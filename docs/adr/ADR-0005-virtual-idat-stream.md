# ADR-0005: Use A Virtual IDAT Stream Without Full Concatenation

- Status: Accepted
- Date: 2026-08-20

## Context

PNG permits the zlib stream to cross arbitrary IDAT boundaries. A Deflate block, token or checksum can span chunks, while physical file offsets must remain available for Hex and provenance views. Concatenating all IDAT payloads duplicates potentially large input and loses direct physical segmentation.

## Decision

Represent IDAT payloads as a segment table over borrowed `ByteSource` ranges. `VirtualIDATStream` provides bounded logical reads across segments and mappings between logical ranges and one or more physical source spans. Chunk headers and CRC fields are excluded from the logical stream.

All range arithmetic is checked, and views retain a valid backing-source lifetime. Constructing the virtual stream must not allocate storage proportional to the total IDAT payload size.

## Consequences

- Deflate code consumes one logical stream without assuming contiguous file storage.
- Logical-to-physical provenance remains available across arbitrary IDAT boundaries.
- Reads that cross segments may require scatter/gather handling or a bounded caller buffer.
- Any cache or checkpoint stores logical coordinates plus source identity, not unstable pointers.
