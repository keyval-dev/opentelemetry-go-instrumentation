#include "bpf_helpers.h"

#define MAX_ENTRIES 50

// Injected in init
volatile const u32 total_cpus;
volatile const u64 start_addr;
volatile const u64 end_addr;

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__type(key, s32);
	__type(value, u64);
	__uint(max_entries, MAX_ENTRIES);
	__uint(pinning, LIBBPF_PIN_BY_NAME);
} alloc_map SEC(".maps");

u64 get_area_start() {
    s64 partition_size = (end_addr - start_addr) / total_cpus;
    if (partition_size > 3 * 1024) {
        partition_size = 3 * 1024;
    }
    u32 current_cpu = bpf_get_smp_processor_id();
    s32 start_index = 0;
    u64* start = (u64*) bpf_map_lookup_elem(&alloc_map, &start_index);
    if (start == NULL || *start == 0) {
        u64 current_start_addr = start_addr + (partition_size * current_cpu);
        bpf_map_update_elem(&alloc_map, &start_index, &current_start_addr, BPF_ANY);
        return current_start_addr;
    } else {
        return *start;
    }
}

u64 get_area_end(u64 start) {
    s64 partition_size = (end_addr - start_addr) / total_cpus;
    if (partition_size > 3 * 1024) {
        partition_size = 3 * 1024;
    } else if (partition_size < 0) {
        partition_size = 0;
    }
    s32 end_index = 1;
    u64* end = (u64*)bpf_map_lookup_elem(&alloc_map, &end_index);
    if (end == NULL || *end == 0) {
        u64 current_end_addr = start + partition_size;
        bpf_map_update_elem(&alloc_map, &end_index, &current_end_addr, BPF_ANY);
        return current_end_addr;
    } else {
        return *end;
    }
}

static __always_inline void* write_target_data(void* data, s32 size) {
    u64 start = get_area_start();
    u64 end = get_area_end(start);
    void* target = (void*)start;
    long success = bpf_probe_write_user(target, data, size);
    if (success == 0) {
        s32 start_index = 0;
        u64 updated_start = start + size;
        bpf_map_update_elem(&alloc_map, &start_index, &updated_start, BPF_ANY);
        return target;
    } else {
        return NULL;
    }
}