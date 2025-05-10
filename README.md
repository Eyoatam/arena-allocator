# Arena Allocator

**Note**: _This implementation would only work on macOS and Linux. It has barely been tested; so, use at your own risk!_

This repo contains code for a growing pool arena allocator. Arenas are regions in memory used to allocate objects that share the same lifetime. This implementation operates by initially reserving a fixed linear virtual address range and committing pages as needed. It can optionally grow its capacity by chaining additional arenas. More about region-based allocation can be found on [Wikipedia](https://wikipedia.org/wiki/Region-based_memory_management).
