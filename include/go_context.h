// Copyright The OpenTelemetry Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "bpf_helpers.h"

#define MAX_DISTANCE 10

struct
{
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, void *);
    __type(value, struct span_context);
    __uint(max_entries, MAX_CONCURRENT_SPANS);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
} tracked_spans SEC(".maps");

struct
{
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, struct span_context);
    __type(value, void *);
    __uint(max_entries, MAX_CONCURRENT_SPANS);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
} tracked_spans_by_sc SEC(".maps");

static __always_inline void *get_parent_go_context(void *ctx, void *map) {
    void *data = ctx;
    for (int i = 0; i < MAX_DISTANCE; i++)
    {
        if (data == NULL)
        {
            break;
        }

        void *found_in_map = bpf_map_lookup_elem(map, &data);
        if (found_in_map != NULL)
        {
            return data;
        }

        // We assume context.Context implementation contains Parent context.Context member
        // Since the parent is also an interface, we need to read the data part of it
        bpf_probe_read(&data, sizeof(data), data + 8);
    }

    return NULL;
}

static __always_inline struct span_context *get_parent_span_context(void *ctx) {
    void *parent_ctx = get_parent_go_context(ctx, &tracked_spans);
    if (parent_ctx == NULL)
    {
        return NULL;
    }

    struct span_context *parent_sc = bpf_map_lookup_elem(&tracked_spans, &parent_ctx);
    if (parent_sc == NULL)
    {
        return NULL;
    }

    return parent_sc;
}

static __always_inline void track_running_span(void *contextContext, struct span_context *sc) {
    long err = 0;
    err = bpf_map_update_elem(&tracked_spans, &contextContext, sc, BPF_ANY);
    if (err != 0)
    {
        bpf_printk("Failed to update tracked_spans map: %ld", err);
        return;
    }

    err = bpf_map_update_elem(&tracked_spans_by_sc, sc, &contextContext, BPF_ANY);
    if (err != 0)
    {
        bpf_printk("Failed to update tracked_spans_by_sc map: %ld", err);
        return;
    }
}

static __always_inline void stop_tracking_span(struct span_context *sc, bool isRoot) {
    if (isRoot)
    {
        void *ctx = bpf_map_lookup_elem(&tracked_spans_by_sc, sc);
        if (ctx == NULL)
        {
            return;
        }

        bpf_map_delete_elem(&tracked_spans, ctx);
    }
    bpf_map_delete_elem(&tracked_spans_by_sc, sc);
}