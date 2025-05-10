# Arena Allocator

> **Note**: _This implementation would only work on macOS and Linux. It has barely been tested; so, use at your own risk!_

This repo contains code for a growing pool arena allocator. Arenas are regions memory that can be used for allocating objects that share the same lifetime. This specific implementation works by initially reserving a large, fixed linear virtual address range. It then commits smaller pages from this reserved range as memory is needed. The allocator can optionally grow its total capacity by chaining together multiple such regions (or "arenas"). More about region-based allocation can be found on [Wikipedia](https://wikipedia.org/wiki/Region-based_memory_management).
