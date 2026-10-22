/* goal_scalar_call.c
 * Private arity switch for uint64 scalar GOAL entries. (AI-assisted)
 *
 * ISC License
 * Copyright (c) 2026
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "goal_scalar_call.h"

int dc_goal_scalar_call(uintptr_t entry, unsigned argc, const uint64_t *args,
                        uint64_t *out)
{
    uint64_t result;

    if (entry == 0 || argc > 8u || out == 0 || (argc > 0u && args == 0)) {
        return 0;
    }

    switch (argc) {
    case 0: {
        uint64_t (*fn)(void) = (uint64_t (*)(void))entry;
        result = fn();
        break;
    }
    case 1: {
        uint64_t (*fn)(uint64_t) = (uint64_t (*)(uint64_t))entry;
        result = fn(args[0]);
        break;
    }
    case 2: {
        uint64_t (*fn)(uint64_t, uint64_t) =
            (uint64_t (*)(uint64_t, uint64_t))entry;
        result = fn(args[0], args[1]);
        break;
    }
    case 3: {
        uint64_t (*fn)(uint64_t, uint64_t, uint64_t) =
            (uint64_t (*)(uint64_t, uint64_t, uint64_t))entry;
        result = fn(args[0], args[1], args[2]);
        break;
    }
    case 4: {
        uint64_t (*fn)(uint64_t, uint64_t, uint64_t, uint64_t) =
            (uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t))entry;
        result = fn(args[0], args[1], args[2], args[3]);
        break;
    }
    case 5: {
        uint64_t (*fn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) =
            (uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t))entry;
        result = fn(args[0], args[1], args[2], args[3], args[4]);
        break;
    }
    case 6: {
        uint64_t (*fn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) =
            (uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                          uint64_t))entry;
        result = fn(args[0], args[1], args[2], args[3], args[4], args[5]);
        break;
    }
    case 7: {
        uint64_t (*fn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                       uint64_t) =
            (uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t))entry;
        result = fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
        break;
    }
    case 8: {
        uint64_t (*fn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                       uint64_t, uint64_t) =
            (uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t, uint64_t))entry;
        result = fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                    args[7]);
        break;
    }
    default:
        return 0;
    }

    *out = result;
    return 1;
}
